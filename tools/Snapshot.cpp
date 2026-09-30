// Headless end-to-end check of the plugin: renders the editor to PNG and
// drives the whole chain (load -> analysis -> loops/shots -> KILL -> preview
// -> MIDI -> export -> state restore).
//
// Usage: DiggaKillaSnapshot <outDir> [sample]
#include "PluginProcessor.h"
#include "export/Exporter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <thread>

using namespace digga;

namespace
{
    void onMessageThread (std::function<void()> fn)
    {
        juce::MessageManager::getInstance()->callFunctionOnMessageThread (
            [] (void* f) -> void* { (*static_cast<std::function<void()>*> (f))(); return nullptr; }, &fn);
    }

    template <typename T>
    T query (std::function<T()> fn)
    {
        T value {};
        onMessageThread ([&] { value = fn(); });
        return value;
    }

    void save (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("  wrote %s\n", file.getFileName().toRawUTF8());
    }

    void wait (int ms) { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }

    bool waitUntil (std::function<bool()> condition, int timeoutMs)
    {
        for (int t = 0; t < timeoutMs; t += 50)
        {
            if (query<bool> (condition))
                return true;
            wait (50);
        }
        return false;
    }

    int idOf (DiggaKillaProcessor& p, const juce::String& key)
    {
        return query<int> ([&] { const auto* n = p.getEngine().getTree().findByKey (key); return n != nullptr ? n->id : -1; });
    }

    bool idle (DiggaKillaProcessor& p)
    {
        auto& e = p.getEngine();
        if (e.getPhase() != Engine::Phase::ready)
            return false;
        for (const auto& n : e.getTree().all())
            if (e.isBusy (n.id))
                return false;
        return true;
    }

    float runBlocks (juce::AudioProcessor& processor, int blocks, juce::MidiBuffer firstMidi = {})
    {
        juce::AudioBuffer<float> buffer (2, 512);
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi = firstMidi;
            processor.processBlock (buffer, midi);
            peak = juce::jmax (peak, buffer.getMagnitude (0, buffer.getNumSamples()));
        }
        return peak;
    }

    int failures = 0;
    void check (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what);
        if (! ok)
            ++failures;
    }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto outDir = argc > 1 ? cwd.getChildFile (argv[1]) : cwd;
    const auto sample = argc > 2 ? cwd.getChildFile (argv[2]) : juce::File();
    outDir.createDirectory();

    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& processor = static_cast<DiggaKillaProcessor&> (*base);
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorIfNeeded());

    std::thread script ([&]
    {
        onMessageThread ([&] { editor->setSize (1344, 896); });
        wait (300);
        onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_empty.png")); });

        if (sample != juce::File())
        {
            auto& engine = processor.getEngine();
            auto start = juce::Time::getMillisecondCounterHiRes();

            std::printf ("\nLOAD + ANALYSE + GENERATE\n");
            onMessageThread ([&] { engine.newSampleChosen(); processor.getSampleStore().loadFile (sample); });
            check (waitUntil ([&] { return idle (processor) && ! engine.getTree().isEmpty(); }, 180000), "results generated");
            std::printf ("  took %.1f s\n", (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0);

            onMessageThread ([&]
            {
                if (const auto* a = engine.getAnalysis())
                    std::printf ("  tempo %.2f BPM (detected %.2f), key %s, first bar at %.3f s\n",
                                 a->bpm, a->detectedBpm, a->keyName.toRawUTF8(), a->downbeatSeconds);
                for (const auto& n : engine.getTree().all())
                    std::printf ("  %-22s %6.2f s  peak %.2f\n", n.displayName().toRawUTF8(),
                                 n.lengthSeconds (engine.getSampleRate()), audio::peak (*n.audio));
            });
            wait (400);
            onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_loaded.png")); });

            if (argc > 3 && juce::String (argv[3]) == "analyse")
            {
                onMessageThread ([&] { editor = nullptr; juce::MessageManager::getInstance()->stopDispatchLoop(); });
                return;
            }

            std::printf ("\nKILL\n");
            const float strengths[] = { 0.6f, 1.0f, 0.3f };
            const char* keys[] = { "A2:", "A2:3", "S1:" };
            for (int i = 0; i < 3; ++i)
            {
                const int id = idOf (processor, keys[i]);
                start = juce::Time::getMillisecondCounterHiRes();
                onMessageThread ([&] { engine.kill (id, strengths[i]); });
                wait (100);
                const bool done = waitUntil ([&] { return idle (processor) && engine.getTree().childrenOf (id).size() == 6; }, 120000);
                std::printf ("  KILL %s (strength %.1f): %.1f s\n", keys[i], strengths[i],
                             (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0);
                check (done, "6 variations created");
            }

            check (query<bool> ([&]
            {
                const auto* original = engine.getTree().findByKey ("A2:");
                const auto* variation = engine.getTree().findByKey ("A2:3.2");
                return original != nullptr && variation != nullptr
                    && original->audio->getNumSamples() == variation->audio->getNumSamples();
            }), "loop variations keep the exact loop length");

            wait (300);
            onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_killed.png")); });

            std::printf ("\nPLAYBACK\n");
            const int a1 = idOf (processor, "A1:");
            onMessageThread ([&] { processor.getClipPlayer().preview (a1); });
            check (runBlocks (processor, 40) > 0.05f, "row preview plays");
            onMessageThread ([&] { processor.getClipPlayer().stopPreview(); });
            runBlocks (processor, 10);

            const int s1 = idOf (processor, "S1:");
            onMessageThread ([&] { engine.select (s1); });
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 110), 0);
            check (runBlocks (processor, 20, midi) > 0.05f, "MIDI note plays selected one-shot (chromatic)");

            onMessageThread ([&] { engine.select (a1); });
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            check (runBlocks (processor, 20, midi) > 0.05f, "MIDI note starts selected loop");
            midi.clear();
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            runBlocks (processor, 10, midi);

            std::printf ("\nEXPORT\n");
            onMessageThread ([&]
            {
                const auto* node = engine.getTree().findByKey ("A2:3.2");
                exporter::Request request;
                request.audio = node->audio;
                request.isLoop = true;
                request.fx.reverb = 0.35f;
                request.fx.delay = 0.2f;
                request.fx.pitch = 0.0f;
                request.fx.bpm = engine.getProjectBpm();
                request.sampleRate = engine.getSampleRate();
                request.fileName = exporter::makeFileName (node->fileTag(), engine.getProjectBpm(), engine.getAnalysis()->keyName);
                const auto file = exporter::renderToTempFile (request);
                std::printf ("  %s (%lld bytes)\n", file.getFullPathName().toRawUTF8(), (long long) file.getSize());
                check (file.existsAsFile() && file.getFileName().startsWith ("DiggaKilla_A2_KillMix3-2_"), "WAV rendered with tempo + key in the name");

                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
                check (reader != nullptr && reader->lengthInSamples == node->audio->getNumSamples(), "exported loop is exactly loop-long");

                request.fx.pitch = 5.0f;
                request.isLoop = false;
                check (exporter::render (request).getNumSamples() > 0, "pitched export renders");
            });

            std::printf ("\nSTATE SAVE / RESTORE\n");
            juce::MemoryBlock state;
            processor.getStateInformation (state);
            std::printf ("  state %d bytes\n", (int) state.getSize());

            std::unique_ptr<juce::AudioProcessor> base2 (createPluginFilter());
            auto& restored = static_cast<DiggaKillaProcessor&> (*base2);
            restored.setPlayConfigDetails (0, 2, 48000.0, 512);
            restored.prepareToPlay (48000.0, 512);
            restored.setStateInformation (state.getData(), (int) state.getSize());

            const auto expected = query<int> ([&] { return (int) engine.getTree().all().size(); });
            const bool same = waitUntil ([&]
            {
                return idle (restored) && (int) restored.getEngine().getTree().all().size() == expected;
            }, 240000);
            check (same, "restored tree has every variation");

            check (query<bool> ([&]
            {
                const auto* x = engine.getTree().findByKey ("A2:3.2");
                const auto* y = restored.getEngine().getTree().findByKey ("A2:3.2");
                if (x == nullptr || y == nullptr || x->audio->getNumSamples() != y->audio->getNumSamples())
                    return false;
                float diff = 0.0f;
                for (int ch = 0; ch < x->audio->getNumChannels(); ++ch)
                    for (int i = 0; i < x->audio->getNumSamples(); ++i)
                        diff = juce::jmax (diff, std::abs (x->audio->getSample (ch, i) - y->audio->getSample (ch, i)));
                std::printf ("  max sample difference A2:3.2 = %g\n", (double) diff);
                return diff < 1.0e-4f;
            }), "seeds reproduce the same audio");

            onMessageThread ([&] { base2 = nullptr; });

            std::printf ("\nUNDO / TEMPO\n");
            onMessageThread ([&] { engine.undo(); });
            check (query<bool> ([&] { return engine.getTree().childrenOf (idOf (processor, "S1:")).empty(); }), "undo removes the last KILL");

            onMessageThread ([&] { engine.setSampleBpm (engine.getAnalysis()->bpm * 2.0); });
            wait (100);
            check (waitUntil ([&] { return idle (processor) && engine.getTree().findByKey ("A2:3.2") != nullptr; }, 240000),
                   "x2 regenerates and replays the KILL tree");
            onMessageThread ([&] { std::printf ("  tempo now %.1f BPM\n", engine.getAnalysis()->bpm); });
            onMessageThread ([&] { engine.setSampleBpm (engine.getAnalysis()->bpm * 0.5); });
            wait (100);
            waitUntil ([&] { return idle (processor); }, 240000);
        }

        onMessageThread ([&] { editor->setSize (672, 448); });
        wait (200);
        onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_small.png")); });

        onMessageThread ([&] { editor = nullptr; juce::MessageManager::getInstance()->stopDispatchLoop(); });
    });

    juce::MessageManager::getInstance()->runDispatchLoop();
    script.join();
    base = nullptr;

    std::printf ("\n%s (%d failed)\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
