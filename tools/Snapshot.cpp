// Renders the plugin editor to PNG files without a host — used to check the
// UI against docs/design.png.   Usage: DiggaKillaSnapshot <outDir> [sample]
#include "PluginProcessor.h"

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
