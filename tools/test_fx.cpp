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

    juce::AudioBuffer<float> makeSine (int n, float amp = 0.5f, double freq = 330.0)
    {
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const auto v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr);
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

    /** Zero crossings per sample of channel 0 over [start, start + num). */
    double crossingRate (const juce::AudioBuffer<float>& b, int start, int num)
    {
        int c = 0;
        for (int i = start + 1; i < start + num; ++i)
            c += (b.getSample (0, i - 1) < 0.0f) != (b.getSample (0, i) < 0.0f) ? 1 : 0;
        return (double) c / juce::jmax (1, num);
    }

    /** Normalised correlation of a[aStart..] and b[bStart..] over num samples (channel 0). */
    double correlation (const juce::AudioBuffer<float>& a, int aStart, const juce::AudioBuffer<float>& b, int bStart, int num)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (int i = 0; i < num; ++i)
        {
            const double x = a.getSample (0, aStart + i), y = b.getSample (0, bStart + i);
            ab += x * y; aa += x * x; bb += y * y;
        }
        return aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : 0.0;
    }

    /** Largest sample-to-sample step over the whole buffer and across the loop wrap. */
    void steps (const juce::AudioBuffer<float>& b, float& maxStep, float& seam, double& meanStep)
    {
        const int n = b.getNumSamples();
        maxStep = seam = 0.0f;
        meanStep = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* d = b.getReadPointer (ch);
            for (int i = 1; i < n; ++i)
            {
                maxStep = juce::jmax (maxStep, std::abs (d[i] - d[i - 1]));
                meanStep += std::abs (d[i] - d[i - 1]);
            }
            seam = juce::jmax (seam, std::abs (d[0] - d[n - 1]));
        }
        meanStep /= (double) b.getNumChannels() * (n - 1);
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
        return (int) (s.timeMode != 0) + (int) (s.repeatRate != 0) + (int) s.filterOn + (int) s.driveOn + (int) s.crushOn + (int) s.gateOn
             + (int) s.tapeStopOn + (int) s.delayOn + (int) s.reverbOn + (int) s.widthOn;
    }

    // ==========================================================================
    void testSerialisation()
    {
        fx::FxSettings s;
        s.timeMode = 2; s.repeatRate = 4;
        s.filterOn = true; s.filterType = 2; s.filterFreq = 812.5f; s.filterReso = 3.25f;
        s.driveOn = true; s.driveType = 1; s.drive = 0.77f; s.driveMix = 0.5f;
        s.crushOn = true; s.crushBits = 5.0f; s.crushRate = 0.61f;
        s.gateOn = true; s.gateRate = 3; s.gatePattern = 0x5A3C; s.gateDepth = 0.65f;
        s.tapeStopOn = true; s.tapeStopBeats = 2.5f;
        s.delayOn = true; s.delayRate = 4; s.delayFeedback = 0.66f; s.delayMix = 0.41f; s.delayPingPong = false;
        s.reverbOn = true; s.reverbSize = 0.83f; s.reverbDamp = 0.21f; s.reverbMix = 0.37f;
        s.widthOn = true; s.width = 0.4f;

        const auto tree = s.toValueTree();
        check (tree.hasType ("Fx") && tree.getNumProperties() == 30, "toValueTree: type Fx, every field a property (" + juce::String (tree.getNumProperties()) + ")");
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
        bad.setProperty ("timeMode", 7, nullptr);
        bad.setProperty ("repeatRate", -2, nullptr);
        const auto c = fx::FxSettings::fromValueTree (bad);
        check (c.filterFreq <= 20000.0f && c.delayFeedback <= 0.95f && c.filterType == 2 && c.width >= 0.0f
                   && c.timeMode == 3 && c.repeatRate == 0, "fromValueTree clamps out-of-range values");

        fx::FxSettings t;
        t.timeMode = 1;
        fx::FxSettings u;
        u.repeatRate = 5;
        const auto t2 = fx::FxSettings::fromValueTree (t.toValueTree());
        const auto u2 = fx::FxSettings::fromValueTree (u.toValueTree());
        check (t.anyOn() && ! t.isDefault() && t != fx::FxSettings {} && t2.timeMode == 1 && t2 == t,
               "timeMode alone: anyOn, not default, == sees it, round trip");
        check (u.anyOn() && ! u.isDefault() && u != t && u2.repeatRate == 5 && u2 == u,
               "repeatRate alone: anyOn, not default, == sees it, round trip");

        juce::ValueTree old ("Fx");   // a project saved before time / repeat existed
        old.setProperty ("filterOn", true, nullptr);
        const auto o = fx::FxSettings::fromValueTree (old);
        check (o.filterOn && o.timeMode == 0 && o.repeatRate == 0, "older projects load with time and repeat off");

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
            { "half",      [] (fx::FxSettings& s) { s.timeMode = 1; } },
            { "quarter",   [] (fx::FxSettings& s) { s.timeMode = 2; } },
            { "double",    [] (fx::FxSettings& s) { s.timeMode = 3; } },
            { "repeat8",   [] (fx::FxSettings& s) { s.repeatRate = 3; } },
            { "repeat32",  [] (fx::FxSettings& s) { s.repeatRate = 5; } },
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
                const bool lengthOk = loop || s.timeMode == 3 ? out.getNumSamples() == (loop ? in.getNumSamples() : (in.getNumSamples() + 1) / 2)
                                                              : out.getNumSamples() >= in.getNumSamples();
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
        bool countsOk = true, valid = true, visibleOnly = true;
        int toys = 0, repeats = 0;
        fx::FxSettings previous;
        juce::String detail;
        for (int strength = 0; strength <= 2; ++strength)
            for (int i = 0; i < 60; ++i)
            {
                const auto s = fx::randomise (random, strength);
                const int c = countOn (s);
                if (c < 1 || c > 4)
                {
                    countsOk = false;
                    detail << " " << c;
                }
                if (s.driveOn || s.crushOn || s.gateOn || s.tapeStopOn || s.widthOn || (s.filterOn && s.filterType == 2)
                    || (s.delayOn && s.delayRate != 0 && s.delayRate != 1))
                    visibleOnly = false;
                toys += (s.timeMode != 0 || s.repeatRate != 0) ? 1 : 0;
                if (s == previous)
                    ++repeats;
                if (! (fx::FxSettings::fromValueTree (s.toValueTree()) == s))
                    valid = false;
                previous = s;
            }
        check (countsOk, "randomise: 1-4 modules on for every strength" + detail);
        check (visibleOnly, "randomise (KILL) only uses the modules the rack shows (time, repeat, LP/HP filter, 1/8 or 1/4 delay, reverb)");
        check (toys >= 130, "randomise favours time / repeat (" + juce::String (toys) + " of 180 racks)");
        check (repeats <= 9, "randomise: (almost) always differs from the previous call (" + juce::String (repeats) + " repeats in 180)");
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
        s.timeMode = 1; s.repeatRate = 4;
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

    void testTime (const juce::File& dir)
    {
        // a 4-beat loop: 440 Hz in the first quarter, 880 Hz after
        const int n = (int) std::round (beat * 4) & ~3;
        juce::AudioBuffer<float> in (2, n);
        double phase = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.5f * (float) std::sin (phase);
            phase += 2.0 * juce::MathConstants<double>::pi * (i < n / 4 ? 440.0 : 880.0) / sr;
            in.setSample (0, i, v);
            in.setSample (1, i, v);
        }

        fx::FxSettings half;
        half.timeMode = 1;
        auto out = in;
        fx::process (out, half, sr, bpm, true);
        writeWav (out, dir.getChildFile ("fx_time_half_sine.wav"));
        check (out.getNumSamples() == n, "HALF on a loop keeps the length");

        const int edge = (int) (sr * 0.01);
        const double zIn = crossingRate (in, 0, n / 4), zOut = crossingRate (out, edge, n / 2 - edge);
        check (std::abs (zOut / zIn - 0.5) < 0.03, "HALF: first half of the output = first quarter of the input slowed (crossing rate x"
                                                       + juce::String (zOut / zIn, 3) + ")");
        float exact = 0.0f;
        for (int k = edge; k < n / 4; ++k)
            exact = juce::jmax (exact, std::abs (out.getSample (0, 2 * k) - in.getSample (0, k)));
        check (exact < 1.0e-4f, "HALF reads the input at half speed (out[2k] == in[k], max diff " + juce::String (exact, 6) + ")");

        // seam on a real loop
        const auto loop = makeLoop (8);
        for (int mode : { 1, 2, 3 })
        {
            fx::FxSettings s;
            s.timeMode = mode;
            auto o = loop;
            fx::process (o, s, sr, bpm, true);
            float maxStep, seam;
            double meanStep;
            steps (o, maxStep, seam, meanStep);
            const juce::String name = mode == 1 ? "HALF" : (mode == 2 ? "QUARTER" : "DOUBLE");
            check (o.getNumSamples() == loop.getNumSamples() && finiteAndSafe (o)
                       && seam <= juce::jmax (0.02f, (float) meanStep * 6.0f) && seam <= maxStep,
                   name + " loop: same length, seamless wrap (jump " + juce::String (seam, 5) + ", mean step " + juce::String (meanStep, 5) + ")");
            writeWav (o, dir.getChildFile ("fx_time_" + name.toLowerCase() + "_loop.wav"));

            if (mode == 3)
            {
                const int h = o.getNumSamples() / 2;
                const double twice = correlation (o, 0, o, h, h);
                // the loop at double speed: out[i] ~ in[2i] (anti-alias filtered)
                juce::AudioBuffer<float> dec (2, h);
                for (int i = 0; i < h; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        dec.setSample (ch, i, loop.getSample (ch, 2 * i));
                const double same = correlation (o, 0, dec, 0, h);
                check (twice > 0.99 && same > 0.9, "DOUBLE loop contains the loop twice (halves corr " + juce::String (twice, 4)
                                                       + ", vs in[2i] " + juce::String (same, 3) + ")");
            }
        }

        // DOUBLE is anti-aliased: a 15 kHz tone (-> 30 kHz) must not fold back down
        auto high = makeSine ((int) (sr * 0.5), 0.5f, 15000.0);
        fx::FxSettings dbl;
        dbl.timeMode = 3;
        fx::process (high, dbl, sr, bpm, true);
        const float alias = rms (high, 0, (int) (sr * 0.05), high.getNumSamples() - (int) (sr * 0.1)) / (0.5f / std::sqrt (2.0f));
        check (alias < 0.03f, "DOUBLE anti-aliasing: a 15 kHz tone leaves " + juce::String (juce::Decibels::gainToDecibels (alias), 1) + " dB");

        // one-shots change length (and pitch)
        const auto hit = makeSine ((int) (sr * 0.3));
        for (int mode : { 1, 2, 3 })
        {
            fx::FxSettings s;
            s.timeMode = mode;
            auto o = hit;
            fx::process (o, s, sr, bpm, false);
            const int len = hit.getNumSamples();
            const int expected = mode == 1 ? len * 2 : (mode == 2 ? len * 4 : (len + 1) / 2);
            const double z = crossingRate (o, 0, o.getNumSamples() - 8) / crossingRate (hit, 0, len - 8);
            const double zWant = mode == 1 ? 0.5 : (mode == 2 ? 0.25 : 2.0);
            check (o.getNumSamples() == expected && std::abs (z / zWant - 1.0) < 0.03 && finiteAndSafe (o),
                   "one-shot " + juce::String (mode == 1 ? "HALF" : (mode == 2 ? "QUARTER" : "DOUBLE")) + ": length "
                       + juce::String (len) + " -> " + juce::String (o.getNumSamples()) + ", pitch x" + juce::String (z, 3));
            if (mode == 1)
                writeWav (o, dir.getChildFile ("fx_time_half_oneshot.wav"));
        }
    }

    void testRepeat (const juce::File& dir)
    {
        const auto in = makeLoop (8);
        const int n = in.getNumSamples();
        fx::FxSettings s;
        s.repeatRate = 4;   // 1/16
        auto out = in;
        fx::process (out, s, sr, bpm, true);
        writeWav (out, dir.getChildFile ("fx_repeat16.wav"));
        check (out.getNumSamples() == n && finiteAndSafe (out), "REPEAT keeps the length");

        const double grid = beat / 4.0;
        const int skip = (int) (sr * 0.004);
        const int len = (int) grid - skip - 4;
        double worst = 1.0, firstSlice = 1.0;
        for (int k = 0; k < 8; ++k)
        {
            const auto segStart = (int) std::llround (k * beat);
            for (int m = 1; m < 4; ++m)
            {
                const auto start = (int) std::llround (k * beat + m * grid);
                worst = juce::jmin (worst, correlation (out, start + skip, in, segStart + skip, len));
            }
            firstSlice = juce::jmin (firstSlice, correlation (out, segStart + skip, in, segStart + skip, len));
        }
        check (worst > 0.999 && firstSlice > 0.999, "REPEAT 1/16 at 140 bpm: every 16th of a beat = the beat's first 16th (min corr "
                                                        + juce::String (worst, 5) + ")");

        // a later 16th is not the input there (it really repeats)
        const int third = (int) std::llround (2 * grid);
        check (correlation (out, third + skip, in, third + skip, len) < 0.9, "REPEAT replaces the rest of the beat");

        // click-free on a sine and across the loop wrap
        auto sine = makeSine ((int) std::round (beat * 4));
        fx::process (sine, s, sr, bpm, true);
        float maxStep, seam;
        double meanStep;
        steps (sine, maxStep, seam, meanStep);
        const float sineStep = 0.5f * 2.0f * juce::MathConstants<float>::pi * 330.0f / (float) sr;
        check (maxStep < sineStep * 1.6f && seam < sineStep * 1.6f, "REPEAT slice edges are click-free (max step " + juce::String (maxStep, 4)
                                                                         + ", wrap " + juce::String (seam, 4) + ", sine step " + juce::String (sineStep, 4) + ")");
        writeWav (sine, dir.getChildFile ("fx_repeat16_sine.wav"));

        // every grid keeps the length and renders safely, loop and one-shot
        bool ok = true;
        for (int rate = 1; rate <= 5; ++rate)
            for (bool loop : { true, false })
            {
                fx::FxSettings r;
                r.repeatRate = rate;
                auto o = in;
                fx::process (o, r, sr, bpm, loop);
                ok = ok && o.getNumSamples() == n && finiteAndSafe (o) && maxDiff (o, in) > 1.0e-3f;
            }
        check (ok, "REPEAT 1/2 .. 1/32: same length, safe, changes the signal (loop and one-shot)");

        // time then repeat: HALF + 1/8 keeps the loop's length and stays seamless
        fx::FxSettings both;
        both.timeMode = 1; both.repeatRate = 3;
        auto o = in;
        fx::process (o, both, sr, bpm, true);
        steps (o, maxStep, seam, meanStep);
        check (o.getNumSamples() == n && seam <= juce::jmax (0.02f, (float) meanStep * 6.0f), "HALF + REPEAT 1/8: same length, seamless");
        writeWav (o, dir.getChildFile ("fx_half_repeat8.wav"));
    }

    // ==========================================================================
    template <typename T>
    T* findChild (juce::Component& parent, std::function<bool (T&)> match)
    {
        for (auto* c : parent.getChildren())
            if (auto* t = dynamic_cast<T*> (c); t != nullptr && match (*t))
                return t;
        return nullptr;
    }

    void testUi (const juce::File& dir)
    {
        FxRack rack;
        int calls = 0;
        fx::FxSettings last;
        rack.onChange = [&] (const fx::FxSettings& s) { ++calls; last = s; };

        fx::FxSettings s;
        s.timeMode = 1;
        s.repeatRate = 4;
        s.filterOn = true; s.filterType = 0; s.filterFreq = 800.0f;
        s.delayOn = true; s.delayRate = 1; s.delayMix = 0.28f;
        rack.setSettings (s);
        rack.setEnabled (true);
        check (calls == 0 && rack.getSettings() == s, "setSettings does not call onChange");

        rack.setSize (1100, 190);
        savePng (rack, dir.getChildFile ("fx_rack.png"));
        savePng (rack, dir.getChildFile ("fx_rack@2x.png"), 2.0f);
        savePng (rack, dir.getChildFile ("fx_rack_scaled08.png"), 0.8f);
        rack.setSize (900, 170);
        savePng (rack, dir.getChildFile ("fx_rack_small.png"));
        savePng (rack, dir.getChildFile ("fx_rack_small_scaled08.png"), 0.8f);
        rack.setSettings ({});
        savePng (rack, dir.getChildFile ("fx_rack_off.png"));
        rack.setSize (1100, 190);

        // the first knob is the bipolar FILTER: drag up -> high-pass, reported once at mouse up
        auto* knob = findChild<juce::Component> (rack, [] (juce::Component& c)
        {
            return c.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::UpDownResizeCursor);
        });
        if (knob != nullptr)
        {
            const auto centre = knob->getLocalBounds().getCentre().toFloat();
            knob->mouseDown (mouse (*knob, centre, centre, false));
            for (int dy = 1; dy <= 100; dy += 3)
                knob->mouseDrag (mouse (*knob, centre.translated (0.0f, (float) -dy), centre, true));
            const int during = calls;
            knob->mouseUp (mouse (*knob, centre.translated (0.0f, -100.0f), centre, true));
            check (during == 0 && calls == 1 && last.filterOn && last.filterType == 1 && last.filterFreq > 30.0f,
                   "FILTER knob right: high-pass at " + juce::String (last.filterFreq, 0) + " Hz, onChange only at mouse up");

            knob->mouseDown (mouse (*knob, centre, centre, false));
            for (int dy = 1; dy <= 300; dy += 5)
                knob->mouseDrag (mouse (*knob, centre.translated (0.0f, (float) dy), centre, true));
            knob->mouseUp (mouse (*knob, centre.translated (0.0f, 300.0f), centre, true));
            check (calls == 2 && last.filterOn && last.filterType == 0 && last.filterFreq < 1000.0f,
                   "FILTER knob left: low-pass at " + juce::String (last.filterFreq, 0) + " Hz");

            knob->mouseDoubleClick (mouse (*knob, centre, centre, false));
            check (calls == 3 && ! last.filterOn && last.isDefault(), "FILTER double-click: centre = off, settings back to default");
        }
        else
        {
            check (false, "found the filter knob");
        }

        // the second knob is DELAY MIX: up from 0 turns the delay on at a rate the strip shows
        juce::Component* delayKnob = nullptr;
        int knobs = 0;
        for (auto* c : rack.getChildren())
            if (c->getMouseCursor() == juce::MouseCursor (juce::MouseCursor::UpDownResizeCursor) && ++knobs == 2)
                delayKnob = c;
        if (delayKnob != nullptr)
        {
            const auto centre = delayKnob->getLocalBounds().getCentre().toFloat();
            delayKnob->mouseDown (mouse (*delayKnob, centre, centre, false));
            for (int dy = 1; dy <= 60; dy += 3)
                delayKnob->mouseDrag (mouse (*delayKnob, centre.translated (0.0f, (float) -dy), centre, true));
            delayKnob->mouseUp (mouse (*delayKnob, centre.translated (0.0f, -60.0f), centre, true));
            check (calls == 4 && last.delayOn && last.delayMix > 0.1f && last.delayRate == 1,
                   "DELAY mix up from 0: delay on, mix " + juce::String (last.delayMix, 2) + ", rate 1/8");
            delayKnob->mouseDoubleClick (mouse (*delayKnob, centre, centre, false));
            check (calls == 5 && ! last.delayOn && last.isDefault(), "DELAY mix double-click: off, settings back to default");
        }
        else
        {
            check (false, "found the delay knob");
        }

        // HALFTIME pills (the first pill row): click the second segment, half speed
        auto* pills = findChild<juce::Component> (rack, [] (juce::Component& c)
        {
            return c.getMouseCursor() == juce::MouseCursor (juce::MouseCursor::PointingHandCursor) && dynamic_cast<juce::Button*> (&c) == nullptr;
        });
        if (pills != nullptr)
        {
            const auto b = pills->getLocalBounds().toFloat();
            const auto at = juce::Point<float> (b.getWidth() * 0.375f, b.getCentreY());
            pills->mouseDown (mouse (*pills, at, at, false));
            check (calls == 6 && last.timeMode == 1, "HALFTIME pill sets timeMode (" + juce::String (last.timeMode) + ")");
        }
        else
        {
            check (false, "found the halftime pills");
        }

        // KILL and RESET
        if (auto* kill = findChild<juce::Button> (rack, [] (juce::Button& b) { return b.getName() == "KILL"; }))
        {
            bool visibleOnly = true;
            for (int i = 0; i < 40; ++i)
            {
                const int before = calls;
                const auto previous = rack.getSettings();
                kill->onClick();
                const auto& k = rack.getSettings();
                visibleOnly = visibleOnly && calls == before + 1 && last == k && k != previous && countOn (k) >= 1
                           && ! (k.driveOn || k.crushOn || k.gateOn || k.tapeStopOn || k.widthOn);
            }
            check (visibleOnly, "KILL applies and reports a new random rack of visible modules only (40 presses)");
            savePng (rack, dir.getChildFile ("fx_rack_kill.png"));
        }
        else
        {
            check (false, "found KILL");
        }
        if (auto* reset = findChild<juce::Button> (rack, [] (juce::Button& b) { return b.getName() == "Reset"; }))
        {
            reset->onClick();
            check (rack.getSettings().isDefault() && last.isDefault(), "RESET restores the defaults");
        }
        else
        {
            check (false, "found RESET");
        }

        // an older project with modules the strip does not show: RESET is flagged
        fx::FxSettings legacy = s;
        legacy.gateOn = true;
        rack.setSettings (legacy);
        savePng (rack, dir.getChildFile ("fx_rack_legacy.png"));

        rack.setSettings (s);
        rack.setEnabled (false, "Pick a loop or one-shot");
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
    testTime (dir);
    testRepeat (dir);
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
