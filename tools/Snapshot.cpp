// Headless end-to-end check of TrapWin Sampler: renders the editor to PNG and
// drives the whole chain (load -> analysis -> section loops / one-shots ->
// editor -> preview -> MIDI -> drag export -> state restore -> tempo fix).
//
// Usage: TrapWinTest <outDir> [song] [analyse]
#include "PluginProcessor.h"
#include "export/Exporter.h"
#include "midi/BasicPitch.h"
#include "midi/MidiExport.h"
#include "separation/Stems.h"

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

    void save (juce::Component& c, const juce::File& file, float scale = 1.0f)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
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

    bool ready (TrapWinProcessor& p) { return p.getEngine().getPhase() == Engine::Phase::ready && ! p.getEngine().getItems().empty(); }
}

/** bpwindow <in.f32> <out.f32>: raw model on one 43844-sample window.
    bptranscribe <in.f32 @22050> <out.txt>: full transcription, one note per line. */
static int basicPitchTool (char** argv)
{
    const juce::String mode (argv[1]);
    juce::MemoryBlock data;
    juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]).loadFileAsData (data);
    const auto* x = static_cast<const float*> (data.getData());
    const int n = (int) (data.getSize() / sizeof (float));
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]);

    if (mode == "bpwindow")
    {
        std::vector<float> note (172 * 88), onset (172 * 88), contour (172 * 264);
        midi::runModelWindow (x, note.data(), onset.data(), contour.data());
        juce::MemoryBlock result;
        result.append (note.data(), note.size() * sizeof (float));
        result.append (onset.data(), onset.size() * sizeof (float));
        result.append (contour.data(), contour.size() * sizeof (float));
        out.replaceWithData (result.getData(), result.getSize());
        return 0;
    }

    juce::AudioBuffer<float> audio (1, n);
    audio.copyFrom (0, 0, x, n);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto notes = midi::transcribe (audio, 22050.0);
    std::printf ("transcribed %.1f s of audio in %.2f s: %d notes\n", n / 22050.0,
                 (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0, (int) notes.size());
    juce::String text;
    for (const auto& note : notes)
        text << juce::String (note.startSeconds, 4) << " " << juce::String (note.endSeconds, 4) << " "
             << note.pitch << " " << juce::String (note.amplitude, 4) << "\n";
    out.replaceWithText (text);
    return 0;
}

/** stems <model.bin> <refDir> <song.wav> <outDir>: U-Net parity against the TensorFlow
    reference (refDir/input.f32, refDir/<stem>.f32 from tools/export_spleeter.py) and a full
    separation of the song (timing, stems sum back to the mix, stem WAVs). */
static int stemsTool (char** argv)
{
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    stems::Separator separator;
    const auto loadStart = juce::Time::getMillisecondCounterHiRes();
    if (! separator.load (cwd.getChildFile (argv[2]))) { std::printf ("model load FAILED\n"); return 1; }
    std::printf ("  model loaded in %.0f ms\n", juce::Time::getMillisecondCounterHiRes() - loadStart);

    const auto refDir = cwd.getChildFile (argv[3]);
    juce::MemoryBlock input;
    refDir.getChildFile ("input.f32").loadFileAsData (input);
    const size_t n = (size_t) 2 * stems::Separator::segmentFrames * stems::Separator::modelBins;
    int failed = 0;
    std::vector<float> out (n);
    for (int s = 0; s < stems::numStems; ++s)
    {
        juce::MemoryBlock ref;
        refDir.getChildFile (juce::String ("ref_") + juce::String (s) + ".f32").loadFileAsData (ref);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        separator.runNetwork (s, static_cast<const float*> (input.getData()), out.data());
        const auto* r = static_cast<const float*> (ref.getData());
        double maxErr = 0.0, maxRef = 0.0;
        for (size_t i = 0; i < n; ++i) { maxErr = std::max (maxErr, (double) std::abs (out[i] - r[i])); maxRef = std::max (maxRef, (double) std::abs (r[i])); }
        const bool ok = maxErr <= 1.0e-2 * std::max (1.0, maxRef);
        failed += ok ? 0 : 1;
        std::printf ("  %-6s max err %.3g (ref max %.3g) %.0f ms  %s\n", stems::stemName (s), maxErr, maxRef,
                     juce::Time::getMillisecondCounterHiRes() - start, ok ? "ok" : "FAILED");
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (cwd.getChildFile (argv[4])));
    if (reader == nullptr) { std::printf ("song load FAILED\n"); return 1; }
    juce::AudioBuffer<float> song ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&song, 0, song.getNumSamples(), 0, true, true);

    stems::StemBuffers result;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    separator.separate (song, reader->sampleRate, result);
    const double seconds = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    std::printf ("  separated %.1f s of audio in %.1f s\n", song.getNumSamples() / reader->sampleRate, seconds);

    double err = 0.0, ref = 0.0;
    for (int ch = 0; ch < song.getNumChannels(); ++ch)
        for (int i = 4096; i < song.getNumSamples() - 4096; ++i)
        {
            float sum = 0.0f;
            for (auto& b : result) sum += b.getSample (ch, i);
            err += (double) (sum - song.getSample (ch, i)) * (sum - song.getSample (ch, i));
            ref += (double) song.getSample (ch, i) * song.getSample (ch, i);
        }
    const double snr = 10.0 * std::log10 (ref / std::max (err, 1.0e-20));
    std::printf ("  stems sum back to the mix: %.1f dB SNR  %s\n", snr, snr > 40.0 ? "ok" : "FAILED");
    failed += snr > 40.0 ? 0 : 1;

    const auto outDir = cwd.getChildFile (argv[5]);
    outDir.createDirectory();
    for (int s = 0; s < stems::numStems; ++s)
    {
        const auto file = outDir.getChildFile (juce::String ("stem_") + stems::stemName (s) + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file),
            reader->sampleRate, (unsigned int) song.getNumChannels(), 16, {}, 0));
        writer->writeFromAudioSampleBuffer (result[(size_t) s], 0, song.getNumSamples());
    }
    std::printf (failed == 0 ? "STEMS OK\n" : "STEMS FAILED\n");
    return failed == 0 ? 0 : 1;
}

int main (int argc, char** argv)
{
    if (argc > 5 && juce::String (argv[1]) == "stems")
        return stemsTool (argv);
    if (argc > 3 && juce::String (argv[1]).startsWith ("bp"))
        return basicPitchTool (argv);

    const juce::ScopedJuceInitialiser_GUI gui;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto outDir = argc > 1 ? cwd.getChildFile (argv[1]) : cwd;
    const auto sample = argc > 2 ? cwd.getChildFile (argv[2]) : juce::File();
    outDir.createDirectory();

    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& processor = static_cast<TrapWinProcessor&> (*base);
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorIfNeeded());

    std::thread script ([&]
    {
        onMessageThread ([&] { editor->setSize (1440, 900); });
        wait (300);
        onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_empty.png")); });

        if (sample != juce::File())
        {
            auto& engine = processor.getEngine();
            auto start = juce::Time::getMillisecondCounterHiRes();

            std::printf ("\nLOAD + ANALYSE + GENERATE\n");
            onMessageThread ([&] { engine.newSampleChosen(); processor.getSampleStore().loadFile (sample); });
            wait (400);
            onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_busy.png")); });
            check (waitUntil ([&] { return ready (processor); }, 300000), "results generated");
            std::printf ("  took %.1f s\n", (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0);

            onMessageThread ([&]
            {
                if (const auto* a = engine.getAnalysis())
                    std::printf ("  tempo %.2f BPM, key %s, first bar at %.3f s\n", a->bpm, a->keyName.toRawUTF8(), a->downbeatSeconds);
                for (const auto& item : engine.getItems())
                    std::printf ("  %-5s %-14s %-30s %6.2f s\n", item.key.toRawUTF8(), item.name.toRawUTF8(),
                                 item.detail (engine.getSampleRate()).toRawUTF8(), item.lengthSeconds (engine.getSampleRate()));
            });
            if (argc > 3 && juce::String (argv[3]) == "analyse")
            {
                wait (300);
                onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_loaded.png")); save (*editor, outDir.getChildFile ("ui_cover.png"), 2.0f); editor = nullptr; juce::MessageManager::getInstance()->stopDispatchLoop(); });
                return;
            }

            const int firstLoop = query<int> ([&] { for (const auto& i : engine.getItems()) if (i.kind == ClipKind::loop) return i.id; return -1; });
            const int firstShot = query<int> ([&] { for (const auto& i : engine.getItems()) if (i.kind == ClipKind::shot) return i.id; return -1; });
            check (firstLoop >= 0 && firstShot >= 0, "found loops and one-shots");

            std::printf ("\nEDITOR\n");
            const int originalLength = query<int> ([&] { return engine.find (firstLoop)->original->getNumSamples(); });
            onMessageThread ([&]
            {
                EditSettings e;
                e.start = 0.25; e.end = 0.75; e.fadeOutMs = 200.0f; e.pitch = 2.0f; e.gainDb = -3.0f;
                engine.select (firstLoop);
                engine.setEdit (firstLoop, e);
            });
            check (waitUntil ([&] { return engine.find (firstLoop)->edited->getNumSamples() != originalLength; }, 60000),
                   "edit renders (crop + pitch + fade + gain)");
            check (query<bool> ([&] { return std::abs (engine.find (firstLoop)->edited->getNumSamples() - originalLength / 2) < 4; }),
                   "crop keeps exactly the chosen half");
            wait (300);
            onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_loaded.png")); save (*editor, outDir.getChildFile ("ui_cover.png"), 2.0f); });

            std::printf ("\nPLAYBACK\n");
            onMessageThread ([&] { processor.getClipPlayer().preview (firstLoop); });
            check (runBlocks (processor, 40) > 0.02f, "preview plays the edited loop");
            onMessageThread ([&] { processor.getClipPlayer().stopPreview(); });
            runBlocks (processor, 10);
            onMessageThread ([&] { engine.select (firstShot); });
            wait (300);
            onMessageThread ([&] { save (*editor, outDir.getChildFile ("ui_shots.png")); });
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 110), 0);
            check (runBlocks (processor, 20, midi) > 0.02f, "piano roll plays the selected one-shot");

            std::printf ("\nDRAG EXPORT\n");
            onMessageThread ([&]
            {
                const auto* item = engine.find (firstLoop);
                const auto name = exporter::makeFileName (item->fileTag(), engine.getProjectBpm(), engine.getAnalysis()->keyName);
                const auto wav = exporter::writeWav (*item->edited, engine.getSampleRate(), name);
                std::printf ("  %s (%lld bytes)\n", wav.getFileName().toRawUTF8(), (long long) wav.getSize());
                check (wav.existsAsFile() && wav.getFileName().startsWith ("TrapWin_"), "WAV written");

                const auto notes = midi::transcribe (*item->edited, engine.getSampleRate());
                midi::ExportOptions options;
                options.bpm = engine.getProjectBpm();
                options.lengthSeconds = item->lengthSeconds (engine.getSampleRate());
                const auto mid = midi::writeToTempFile (notes, options, name);
                std::printf ("  %s: %d notes\n", mid.getFileName().toRawUTF8(), (int) notes.size());
                check (mid.existsAsFile() && ! notes.empty(), "MIDI written");
            });

            std::printf ("\nSTATE\n");
            juce::MemoryBlock state;
            processor.getStateInformation (state);
            std::unique_ptr<juce::AudioProcessor> base2 (createPluginFilter());
            auto& restored = static_cast<TrapWinProcessor&> (*base2);
            restored.setPlayConfigDetails (0, 2, 48000.0, 512);
            restored.prepareToPlay (48000.0, 512);
            restored.setStateInformation (state.getData(), (int) state.getSize());
            const auto expected = query<int> ([&] { return (int) engine.getItems().size(); });
            check (waitUntil ([&] { return ready (restored) && (int) restored.getEngine().getItems().size() == expected; }, 300000),
                   "restored project has the same samples");
            check (waitUntil ([&]
            {
                const auto& items = restored.getEngine().getItems();
                for (const auto& i : items)
                    if (i.key == engine.find (firstLoop)->key)
                        return i.edited->getNumSamples() == engine.find (firstLoop)->edited->getNumSamples();
                return false;
            }, 60000), "editor settings restored");
            onMessageThread ([&] { base2 = nullptr; });

            std::printf ("\nTEMPO\n");
            onMessageThread ([&] { engine.setSampleBpm (engine.getAnalysis()->bpm * 0.5); });
            wait (100);
            check (waitUntil ([&] { return ready (processor); }, 300000), "half tempo regenerates");
            onMessageThread ([&] { std::printf ("  tempo now %.1f BPM, %d loops\n", engine.getAnalysis()->bpm, (int) engine.getItems().size()); });
            onMessageThread ([&] { engine.setSampleBpm (engine.getAnalysis()->bpm * 2.0); });
            wait (100);
            waitUntil ([&] { return ready (processor); }, 300000);
        }

        onMessageThread ([&] { editor->setSize (800, 500); });
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
