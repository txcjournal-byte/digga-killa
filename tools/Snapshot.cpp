// Renders the plugin editor to PNG files without a host — used to check the
// UI against docs/design.png.   Usage: DiggaKillaSnapshot <outDir> [sample]
#include "PluginProcessor.h"
#include "ui/TrackColumn.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <thread>

namespace
{
    void onMessageThread (std::function<void()> fn)
    {
        juce::MessageManager::getInstance()->callFunctionOnMessageThread (
            [] (void* f) -> void* { (*static_cast<std::function<void()>*> (f))(); return nullptr; }, &fn);
    }

    void save (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    }

    template <typename T>
    void findAll (juce::Component& root, std::vector<T*>& found)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (child))
                found.push_back (t);
            findAll (*child, found);
        }
    }

    std::vector<float> fakePeaks (int n, bool decay, int seed)
    {
        juce::Random r (seed);
        std::vector<float> peaks ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) n;
            const float env = decay ? std::exp (-t * 4.0f) : (0.75f - 0.25f * t) * (t > 0.85f ? (1.0f - t) / 0.15f : 1.0f);
            peaks[(size_t) i] = juce::jlimit (0.03f, 1.0f, env * (0.45f + 0.55f * r.nextFloat()));
        }
        return peaks;
    }

    /** Fills the tracklists with the mock content shown in docs/design.png. */
    void fillDemoRows (juce::Component& editor)
    {
        std::vector<digga::TrackColumn*> columns;
        findAll (editor, columns);
        if (columns.size() < 2)
            return;

        const juce::String dash (juce::CharPointer_UTF8 (" \xe2\x80\x93 "));
        std::vector<digga::TrackRowModel> loops;
        auto add = [&] (juce::String name, juce::String dur, int depth, bool last)
        {
            digga::TrackRowModel m;
            m.name = name; m.duration = dur; m.depth = depth; m.lastSibling = last; m.placeholder = false;
            m.peaks = fakePeaks (160, false, (int) loops.size() + 3);
            loops.push_back (m);
        };
        add ("A1" + dash + "Loop 8 bars", "0:16", 0, false);
        add ("A2" + dash + "Loop 16 bars", "0:32", 0, false);
        for (int i = 1; i <= 6; ++i)
            add ("A2" + dash + "Kill Mix " + juce::String (i), "0:32", 1, i == 6);
        add ("B1" + dash + "Loop 8 bars", "0:16", 0, false);
        add ("B2" + dash + "Loop 16 bars", "0:32", 0, false);
        columns[0]->setRows (loops);
        columns[0]->setSelectedRow (1);

        std::vector<digga::TrackRowModel> shots;
        for (int i = 1; i <= 8; ++i)
        {
            digga::TrackRowModel m;
            m.name = "Shot " + juce::String (i); m.duration = "0:01"; m.placeholder = false;
            m.peaks = fakePeaks (60, true, i);
            shots.push_back (m);
        }
        columns[1]->setRows (shots);
    }

    void wait (int ms) { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto outDir = argc > 1 ? cwd.getChildFile (argv[1]) : cwd;
    const auto sample = argc > 2 ? cwd.getChildFile (argv[2]) : juce::File();
    outDir.createDirectory();

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    processor->setPlayConfigDetails (0, 2, 48000.0, 512);
    processor->prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorIfNeeded());

    std::thread script ([&]
    {
        onMessageThread ([&] { editor->setSize (1344, 896); });
        wait (300);
        onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_empty.png")); });

        if (sample != juce::File())
        {
            auto& p = static_cast<digga::DiggaKillaProcessor&> (*processor);
            onMessageThread ([&] { p.getSampleStore().loadFile (sample); });

            for (int i = 0; i < 200 && p.getSampleStore().getStatus() == digga::SampleStore::Status::loading; ++i)
                wait (50);

            // audio: a note-on must start playback through the lock-free handoff
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            float peak = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                processor->processBlock (buffer, midi);
                midi.clear();
                peak = juce::jmax (peak, buffer.getMagnitude (0, buffer.getNumSamples()));
            }
            std::printf ("status=%d peak=%.3f playing=%d\n", (int) p.getSampleStore().getStatus(), peak,
                         (int) p.getPlayer().isPlaying());

            wait (300);
            onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_loaded.png")); });
        }

        onMessageThread ([&] { fillDemoRows (*editor); });
        wait (200);
        onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_demo.png")); });

        onMessageThread ([&] { editor->setSize (672, 448); });
        wait (200);
        onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_small.png")); });

        onMessageThread ([&] { editor = nullptr; juce::MessageManager::getInstance()->stopDispatchLoop(); });
    });

    juce::MessageManager::getInstance()->runDispatchLoop();
    script.join();
    processor = nullptr;
    return 0;
}
