// Component test for the FX rack: DSP (fx::process / randomise / serialisation)
// and the FxRack UI (snapshots + gesture reporting). Usage: TrapWinComponentTest fx <outDir>
#include "fx/FxChain.h"
#include "ui/FxRack.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>

using namespace digga;

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    constexpr double sr = 44100.0, bpm = 140.0;
    const double beat = 60.0 / bpm * sr;

    /** One bar-ish drum + tone loop: kick-like thumps on each beat, a hat on the
        off-beats and a soft chord; periodic in its own length. */
    juce::AudioBuffer<float> makeLoop (int beats)
    {
        const int n = (int) std::round (beat * beats);
        juce::AudioBuffer<float> b (2, n);
        juce::Random rnd (42);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double inBeat = std::fmod ((double) i, beat) / sr;
            const double inHalf = std::fmod ((double) i + beat * 0.5, beat) / sr;
            const double kick = std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 * inBeat + 60.0 * (1.0 - std::exp (-inBeat * 30.0)) / 30.0))
                              * std::exp (-inBeat * 9.0);
            const double hat = (rnd.nextFloat() * 2.0 - 1.0) * std::exp (-inHalf * 60.0) * 0.25;
            const double tone = 0.12 * (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                                        + std::sin (2.0 * juce::MathConstants<double>::pi * 277.18 * t));
            // fade the tone at the loop edges so the raw loop itself is seamless
            const double edge = juce::jmin (1.0, juce::jmin (i, n - 1 - i) / (sr * 0.005));
            b.setSample (0, i, (float) (0.6 * kick + hat + tone * edge));
            b.setSample (1, i, (float) (0.6 * kick - 0.7 * hat + tone * edge * 0.9));
        }
        return b;
    }

    juce::AudioBuffer<float> makeSine (int n, float amp = 0.5f)
    {
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const auto v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * i / sr);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        return b;
    }

    bool finiteAndSafe (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)) || std::abs (b.getSample (ch, i)) > 0.9901f)
                    return false;
        return true;
    }

    float maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float d = 0.0f;
        const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
        for (int ch = 0; ch < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++ch)
            for (int i = 0; i < n; ++i)
                d = juce::jmax (d, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        return d;
    }

    float rms (const juce::AudioBuffer<float>& b, int ch, int start, int num)
    {
        double e = 0.0;
        for (int i = start; i < start + num; ++i)
            e += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return (float) std::sqrt (e / juce::jmax (1, num));
    }

    void writeWav (const juce::AudioBuffer<float>& b, const juce::File& file)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions {}.withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, options))
            writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    void savePng (juce::Component& c, const juce::File& file, float scale = 1.0f)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("  wrote %s\n", file.getFileName().toRawUTF8());
    }

    juce::uint16 pattern (const char* p)
    {
        juce::uint16 bits = 0;
        for (int i = 0; i < 16; ++i)
            if (p[i] == 'x')
                bits = (juce::uint16) (bits | (1u << i));
        return bits;
    }

    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier,
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, 1, dragged);
    }

    int countOn (const fx::FxSettings& s)
    {
        return (int) s.filterOn + (int) s.driveOn + (int) s.crushOn + (int) s.gateOn
             + (int) s.tapeStopOn + (int) s.delayOn + (int) s.reverbOn + (int) s.widthOn;
    }

    // ==========================================================================
    void testSerialisation()
    {
        fx::FxSettings s;
        s.filterOn = true; s.filterType = 2; s.filterFreq = 812.5f; s.filterReso = 3.25f;
        s.driveOn = true; s.driveType = 1; s.drive = 0.77f; s.driveMix = 0.5f;
        s.crushOn = true; s.crushBits = 5.0f; s.crushRate = 0.61f;
        s.gateOn = true; s.gateRate = 3; s.gatePattern = 0x5A3C; s.gateDepth = 0.65f;
        s.tapeStopOn = true; s.tapeStopBeats = 2.5f;
        s.delayOn = true; s.delayRate = 4; s.delayFeedback = 0.66f; s.delayMix = 0.41f; s.delayPingPong = false;
        s.reverbOn = true; s.reverbSize = 0.83f; s.reverbDamp = 0.21f; s.reverbMix = 0.37f;
        s.widthOn = true; s.width = 0.4f;

        const auto tree = s.toValueTree();
        check (tree.hasType ("Fx") && tree.getNumProperties() == 28, "toValueTree: type Fx, every field a property (" + juce::String (tree.getNumProperties()) + ")");
        const auto back = fx::FxSettings::fromValueTree (juce::ValueTree::fromXml (tree.toXmlString()));
        check (back == s && ! (back != s), "round trip through XML keeps every (non-default) value");
        check (! s.isDefault() && s.anyOn(), "isDefault / anyOn on a full rack");
        check (fx::FxSettings::fromValueTree ({}).isDefault() && fx::FxSettings::fromValueTree (juce::ValueTree ("Fx")).isDefault(),
               "missing tree / missing properties give defaults");

        juce::ValueTree bad ("Fx");
        bad.setProperty ("filterFreq", 99999.0, nullptr);
        bad.setProperty ("delayFeedback", 3.0, nullptr);
        bad.setProperty ("filterType", 9, nullptr);
        bad.setProperty ("width", -5.0, nullptr);
        const auto c = fx::FxSettings::fromValueTree (bad);
        check (c.filterFreq <= 20000.0f && c.delayFeedback <= 0.95f && c.filterType == 2 && c.width >= 0.0f, "fromValueTree clamps out-of-range values");

        fx::FxSettings d;
        d.filterFreq += 1.0f;
        check (! (d == fx::FxSettings {}) && fx::FxSettings {}.isDefault() && ! fx::FxSettings {}.anyOn(), "operator== sees a single float change; defaults are default");
    }

    void testPassthrough()
    {
        auto in = makeLoop (4);
        auto out = in;
        fx::process (out, {}, sr, bpm, true);
        check (out.getNumSamples() == in.getNumSamples() && maxDiff (in, out) <= 0.0f, "default settings: bit-identical passthrough (loop)");
        fx::process (out, {}, sr, bpm, false);
        check (out.getNumSamples() == in.getNumSamples() && maxDiff (in, out) <= 0.0f, "default settings: bit-identical passthrough (one-shot)");
    }

    void testModules (const juce::File& dir)
    {
        const auto in = makeLoop (8);
        writeWav (in, dir.getChildFile ("fx_input.wav"));

        struct Case { const char* name; std::function<void (fx::FxSettings&)> set; };
        const Case cases[] {
            { "filter",    [] (fx::FxSettings& s) { s.filterOn = true; s.filterFreq = 600.0f; s.filterReso = 4.0f; } },
            { "filter_hp", [] (fx::FxSettings& s) { s.filterOn = true; s.filterType = 1; s.filterFreq = 900.0f; } },
            { "filter_bp", [] (fx::FxSettings& s) { s.filterOn = true; s.filterType = 2; s.filterFreq = 1500.0f; s.filterReso = 8.0f; } },
            { "drive",     [] (fx::FxSettings& s) { s.driveOn = true; s.drive = 0.9f; } },
            { "drive_tube",[] (fx::FxSettings& s) { s.driveOn = true; s.driveType = 1; s.drive = 0.7f; } },
            { "drive_hard",[] (fx::FxSettings& s) { s.driveOn = true; s.driveType = 2; s.drive = 1.0f; } },
            { "crush",     [] (fx::FxSettings& s) { s.crushOn = true; s.crushBits = 4.0f; s.crushRate = 0.5f; } },
            { "gate",      [] (fx::FxSettings& s) { s.gateOn = true; } },
            { "tapestop",  [] (fx::FxSettings& s) { s.tapeStopOn = true; s.tapeStopBeats = 2.0f; } },
            { "delay",     [] (fx::FxSettings& s) { s.delayOn = true; s.delayFeedback = 0.95f; s.delayMix = 1.0f; } },
            { "reverb",    [] (fx::FxSettings& s) { s.reverbOn = true; s.reverbSize = 1.0f; s.reverbMix = 1.0f; } },
            { "width",     [] (fx::FxSettings& s) { s.widthOn = true; s.width = 2.0f; } },
            { "width_mono",[] (fx::FxSettings& s) { s.widthOn = true; s.width = 0.0f; } },
        };

        for (auto& c : cases)
        {
            fx::FxSettings s;
            c.set (s);
            for (bool loop : { true, false })
            {
                auto out = in;
                fx::process (out, s, sr, bpm, loop);
                const bool lengthOk = loop ? out.getNumSamples() == in.getNumSamples() : out.getNumSamples() >= in.getNumSamples();
                check (lengthOk && maxDiff (in, out) > 1.0e-3f && finiteAndSafe (out),
                       juce::String (c.name) + (loop ? " (loop)" : " (one-shot)") + ": changes the signal, finite, peak <= 0.99, length "
                           + juce::String (out.getNumSamples()));
                if (loop)
                    writeWav (out, dir.getChildFile ("fx_" + juce::String (c.name) + ".wav"));
            }
        }

        // mono buffers work too
        juce::AudioBuffer<float> mono (1, in.getNumSamples());
        mono.copyFrom (0, 0, in, 0, 0, in.getNumSamples());
        juce::Random random (3);
        fx::FxSettings all = fx::randomise (random, 2);
        all.delayOn = all.reverbOn = all.widthOn = true;
        fx::process (mono, all, sr, bpm, true);
        check (mono.getNumChannels() == 1 && finiteAndSafe (mono), "mono buffer stays mono and safe");

        // hot input is scaled under 0 dBFS
        auto hot = in;
        hot.applyGain (4.0f);
        fx::FxSettings w;
        w.widthOn = true;
        fx::process (hot, w, sr, bpm, true);
        check (hot.getMagnitude (0, hot.getNumSamples()) <= 0.9901f, "a hot input is scaled under 0.99");
    }

    void testLoopSeam (const juce::File& dir)
    {
        const auto in = makeLoop (8);
        fx::FxSettings s;
        s.delayOn = true; s.delayRate = 2; s.delayFeedback = 0.6f; s.delayMix = 0.5f;
        s.reverbOn = true; s.reverbSize = 0.8f; s.reverbMix = 0.4f;

        auto loop = in;
        fx::process (loop, s, sr, bpm, true);
        writeWav (loop, dir.getChildFile ("fx_delay_loop.wav"));
        auto shot = in;
        fx::process (shot, s, sr, bpm, false);
        writeWav (shot, dir.getChildFile ("fx_delay_oneshot.wav"));

        const int n = in.getNumSamples();
        check (loop.getNumSamples() == n, "loop mode keeps the length (" + juce::String (n) + ")");

        // seam: the jump from the last to the first sample is like any other step
        float maxStep = 0.0f, seam = 0.0f;
        double meanStep = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* d = loop.getReadPointer (ch);
            for (int i = 1; i < n; ++i)
            {
                maxStep = juce::jmax (maxStep, std::abs (d[i] - d[i - 1]));
                meanStep += std::abs (d[i] - d[i - 1]);
            }
            seam = juce::jmax (seam, std::abs (d[0] - d[n - 1]));
        }
        meanStep /= 2.0 * (n - 1);
        check (seam <= juce::jmax (0.02f, (float) meanStep * 6.0f) && seam <= maxStep,
               "loop seam continuous: jump " + juce::String (seam, 5) + " (mean step " + juce::String (meanStep, 5) + ", max " + juce::String (maxStep, 4) + ")");

        // the start already holds the tail of the end (differs from the one-shot render)
        const int head = (int) (sr * 0.05);
        juce::AudioBuffer<float> a (2, head), b (2, head);
        for (int ch = 0; ch < 2; ++ch)
        {
            a.copyFrom (ch, 0, loop, ch, 0, head);
            b.copyFrom (ch, 0, shot, ch, 0, head);
        }
        check (maxDiff (a, b) > 1.0e-3f, "loop start contains the wrapped delay / reverb tail (diff " + juce::String (maxDiff (a, b), 4) + ")");

        // play the loop twice: the steady state of pass 2 equals pass 1 (seamless repeat)
        check (shot.getNumSamples() > n + (int) (sr * 0.2), "one-shot mode appends the tail (+" + juce::String ((shot.getNumSamples() - n) / sr, 2) + " s)");
        check (shot.getNumSamples() <= n + (int) (sr * 3.05), "one-shot tail at most ~3 s");

        // one-shot: short hit + reverb gets a tail that ends quietly
        auto hit = makeSine ((int) (sr * 0.2));
        hit.applyGainRamp (0, hit.getNumSamples(), 1.0f, 0.0f);
        fx::FxSettings r;
        r.reverbOn = true; r.reverbSize = 0.5f; r.reverbMix = 0.5f;
        fx::process (hit, r, sr, bpm, false);
        writeWav (hit, dir.getChildFile ("fx_reverb_oneshot.wav"));
        const int m = hit.getNumSamples();
        check (m > (int) (sr * 0.5) && rms (hit, 0, m - 64, 64) < 1.0e-3f && finiteAndSafe (hit),
               "one-shot reverb: tail appended (" + juce::String (m / sr, 2) + " s) and fades out");
    }

    void testGate (const juce::File& dir)
    {
        const double stepLen = beat / 4.0;   // 1/16 at 140 bpm
        const int n = (int) std::round (stepLen * 32);
        auto in = makeSine (n);
        fx::FxSettings s;
        s.gateOn = true; s.gateRate = 2; s.gateDepth = 1.0f;
        s.gatePattern = pattern ("xx.x..x.xxx.x..x");
        auto out = in;
        fx::process (out, s, sr, bpm, true);
        writeWav (out, dir.getChildFile ("fx_gate.wav"));

        bool ok = true;
        juce::String detail;
        for (int step = 0; step < 32; ++step)
        {
            const int a = (int) std::round (step * stepLen + stepLen * 0.2);
            const int len = (int) (stepLen * 0.6);
            const float level = rms (out, 0, a, len) / rms (in, 0, a, len);
            const bool on = ((s.gatePattern >> (step % 16)) & 1) != 0;
            if (on ? level < 0.98f : level > 1.0e-4f)
            {
                ok = false;
                detail << " step " << step << "=" << juce::String (level, 4);
            }
        }
        check (ok, "gate 1/16 at 140 bpm: off-steps silent, on-steps open" + detail);

        // smooth ramps: no step larger than a 2.5 ms ramp would make
        float maxJump = 0.0f;
        for (int i = 1; i < n; ++i)
            maxJump = juce::jmax (maxJump, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        const float sineStep = 0.5f * 2.0f * juce::MathConstants<float>::pi * 330.0f / (float) sr;
        check (maxJump < sineStep * 1.6f, "gate ramps are click-free (max step " + juce::String (maxJump, 4) + ")");

        s.gateDepth = 0.5f;
        auto half = in;
        fx::process (half, s, sr, bpm, true);
        const int off = (int) std::round (2 * stepLen + stepLen * 0.3);
        const float level = rms (half, 0, off, (int) (stepLen * 0.4)) / rms (in, 0, off, (int) (stepLen * 0.4));
        check (std::abs (level - 0.5f) < 0.02f, "gate depth 50% halves the off-steps (" + juce::String (level, 3) + ")");
    }

    void testTapeStop (const juce::File& dir)
    {
        auto out = makeSine ((int) (beat * 4));
        auto in = out;
        fx::FxSettings s;
        s.tapeStopOn = true; s.tapeStopBeats = 1.0f;
        fx::process (out, s, sr, bpm, false);
        writeWav (out, dir.getChildFile ("fx_tapestop.wav"));
        const int n = out.getNumSamples();
        const int stopStart = n - (int) std::round (beat);
        check (n == in.getNumSamples() && maxDiff (in, out) > 0.01f, "tape stop: same length (no time fx), signal changed");
        juce::AudioBuffer<float> pre (2, stopStart);
        for (int ch = 0; ch < 2; ++ch) pre.copyFrom (ch, 0, out, ch, 0, stopStart);
        juce::AudioBuffer<float> preIn (2, stopStart);
        for (int ch = 0; ch < 2; ++ch) preIn.copyFrom (ch, 0, in, ch, 0, stopStart);
        check (maxDiff (pre, preIn) <= 0.0f, "tape stop leaves everything before the last beat untouched");
        check (std::abs (out.getSample (0, n - 1)) < 1.0e-6f && rms (out, 0, n - (int) (sr * 0.02), (int) (sr * 0.02)) < 2.0e-3f,
               "tape stop ends in silence (last 20 ms rms " + juce::String (rms (out, 0, n - (int) (sr * 0.02), (int) (sr * 0.02)), 5) + ")");

        // pitch drops: zero crossings get rarer towards the end
        auto crossings = [&] (int a, int len)
        {
            int c = 0;
            for (int i = a + 1; i < a + len; ++i)
                c += (out.getSample (0, i - 1) < 0.0f) != (out.getSample (0, i) < 0.0f) ? 1 : 0;
            return c;
        };
        const int q = (int) (beat / 4);
        check (crossings (stopStart, q) > crossings (stopStart + 2 * q, q) * 1.5, "tape stop slows down (pitch falls)");
    }

    void testRandomise (const juce::File& dir)
    {
        juce::Random random (1234);
        bool countsOk = true, differ = true, valid = true;
        fx::FxSettings previous;
        juce::String detail;
        for (int strength = 0; strength <= 2; ++strength)
            for (int i = 0; i < 60; ++i)
            {
                const auto s = fx::randomise (random, strength);
                const int c = countOn (s);
                if (c < 2 || c > 4)
                {
                    countsOk = false;
                    detail << " " << c;
                }
                if (s == previous)
                    differ = false;
                if (! (fx::FxSettings::fromValueTree (s.toValueTree()) == s))
                    valid = false;
                previous = s;
            }
        check (countsOk, "randomise: 2-4 modules on for every strength" + detail);
        check (differ, "randomise: differs per call");
        check (valid, "randomise: values inside the legal ranges");

        juce::Random r2 (99);
        const auto in = makeLoop (8);
        bool safe = true;
        for (int i = 0; i < 12; ++i)
        {
            auto out = in;
            const auto s = fx::randomise (r2, i % 3);
            fx::process (out, s, sr, bpm, true);
            safe = safe && finiteAndSafe (out) && out.getNumSamples() == in.getNumSamples();
            if (i < 3)
                writeWav (out, dir.getChildFile ("fx_kill_" + juce::String (i) + ".wav"));
        }
        check (safe, "12 random racks render finite, safe, same length (loop)");
    }

    void testPerformance (const juce::File& dir)
    {
        auto in = makeLoop (64);   // 16 bars at 140 bpm
        fx::FxSettings s;
        s.filterOn = s.driveOn = s.crushOn = s.gateOn = s.tapeStopOn = s.delayOn = s.reverbOn = s.widthOn = true;
        s.crushBits = 10.0f; s.crushRate = 0.1f; s.reverbSize = 0.9f;

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        fx::process (in, s, sr, bpm, true);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        writeWav (in, dir.getChildFile ("fx_all_16bars.wav"));
#if JUCE_DEBUG
        std::printf ("  16-bar loop, all modules: %.0f ms (Debug build; Release target < 1500 ms)\n", ms);
        check (finiteAndSafe (in), "performance render (Debug, " + juce::String (ms, 0) + " ms) is finite and safe");
#else
        check (ms < 1500.0 && finiteAndSafe (in), "16-bar loop with all modules renders in " + juce::String (ms, 0) + " ms (< 1500)");
#endif
    }

    // ==========================================================================
    void testUi (const juce::File& dir)
    {
        FxRack rack;
        int calls = 0;
        fx::FxSettings last;
        rack.onChange = [&] (const fx::FxSettings& s) { ++calls; last = s; };

        fx::FxSettings s;
        s.filterOn = true; s.filterFreq = 1800.0f; s.filterReso = 2.2f;
        s.driveOn = true; s.driveType = 1; s.drive = 0.55f; s.driveMix = 0.8f;
        s.gateOn = true; s.gatePattern = pattern ("xx.xxx.xxx.xxx.x");
        s.delayOn = true; s.delayRate = 2; s.delayFeedback = 0.45f; s.delayMix = 0.28f;
        s.reverbOn = true; s.reverbMix = 0.22f;
        rack.setSettings (s);
        rack.setEnabled (true);
        check (calls == 0 && rack.getSettings() == s, "setSettings does not call onChange");

        rack.setSize (1100, 230);
        savePng (rack, dir.getChildFile ("fx_rack.png"));
        savePng (rack, dir.getChildFile ("fx_rack@2x.png"), 2.0f);
        rack.setSize (900, 220);
        savePng (rack, dir.getChildFile ("fx_rack_900.png"));
        rack.setSize (1100, 230);

        // a knob drag reports once, at mouse up
        juce::Component* knob = nullptr;
        for (auto* c : rack.getChildren())
            if (c->getMouseCursor() == juce::MouseCursor (juce::MouseCursor::UpDownResizeCursor))
            {
                knob = c;   // first knob: filter FREQ
                break;
            }
        if (knob != nullptr)
        {
            const auto centre = knob->getLocalBounds().getCentre().toFloat();
            knob->mouseDown (mouse (*knob, centre, centre, false));
            for (int dy = 1; dy <= 40; dy += 3)
                knob->mouseDrag (mouse (*knob, centre.translated (0.0f, (float) -dy), centre, true));
            const int during = calls;
            knob->mouseUp (mouse (*knob, centre.translated (0.0f, -40.0f), centre, true));
            check (during == 0 && calls == 1 && last.filterFreq > s.filterFreq, "knob drag: onChange only at mouse up (freq " + juce::String (last.filterFreq, 0) + " Hz)");
            knob->mouseDoubleClick (mouse (*knob, centre, centre, false));
            check (calls == 2 && juce::approximatelyEqual (last.filterFreq, fx::FxSettings {}.filterFreq), "knob double-click resets to default");
        }
        else
        {
            check (false, "found a knob");
        }

        // KILL and RESET
        for (auto* c : rack.getChildren())
            if (auto* b = dynamic_cast<juce::Button*> (c); b != nullptr && b->getName() == "KILL")
            {
                const int before = calls;
                b->onClick();
                const int on = countOn (rack.getSettings());
                check (calls == before + 1 && on >= 2 && on <= 4 && last == rack.getSettings(), "KILL applies a random rack and reports it (" + juce::String (on) + " modules)");
                savePng (rack, dir.getChildFile ("fx_rack_kill.png"));
            }
        for (auto* c : rack.getChildren())
            if (auto* b = dynamic_cast<juce::Button*> (c); b != nullptr && b->getName() == "Reset FX")
            {
                b->onClick();
                check (rack.getSettings().isDefault() && last.isDefault(), "RESET FX restores the defaults");
            }

        rack.setSettings (s);
        rack.setEnabled (false, "Select a sample to add effects");
        savePng (rack, dir.getChildFile ("fx_rack_disabled.png"));
    }
}

int runFxTest (int argc, char** argv)
{
    const juce::File dir (argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])
                                   : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fx_out"));
    dir.createDirectory();
    std::printf ("FX test -> %s\n", dir.getFullPathName().toRawUTF8());

    testSerialisation();
    testPassthrough();
    testModules (dir);
    testLoopSeam (dir);
    testGate (dir);
    testTapeStop (dir);
    testRandomise (dir);
    testPerformance (dir);
    testUi (dir);

    if (failures == 0)
        std::printf ("ALL CHECKS PASSED\n");
    else
        std::printf ("%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
