#include "fx/FxChain.h"

#include <cmath>
#include <optional>
#include <vector>

namespace digga::fx
{

// ==============================================================================
// Settings
// ==============================================================================
bool FxSettings::anyOn() const noexcept
{
    return filterOn || driveOn || crushOn || gateOn || tapeStopOn || delayOn || reverbOn || widthOn;
}

bool FxSettings::isDefault() const noexcept { return *this == FxSettings {}; }

namespace
{
    // every field once: name, member pointer, range (used for load clamping)
    struct BoolField  { const char* id; bool FxSettings::* member; };
    struct IntField   { const char* id; int FxSettings::* member; int lo, hi; };
    struct FloatField { const char* id; float FxSettings::* member; float lo, hi; };

    constexpr BoolField boolFields[] {
        { "filterOn", &FxSettings::filterOn }, { "driveOn", &FxSettings::driveOn },
        { "crushOn", &FxSettings::crushOn },   { "gateOn", &FxSettings::gateOn },
        { "tapeStopOn", &FxSettings::tapeStopOn }, { "delayOn", &FxSettings::delayOn },
        { "delayPingPong", &FxSettings::delayPingPong }, { "reverbOn", &FxSettings::reverbOn },
        { "widthOn", &FxSettings::widthOn } };

    constexpr IntField intFields[] {
        { "filterType", &FxSettings::filterType, 0, 2 }, { "driveType", &FxSettings::driveType, 0, 2 },
        { "gateRate", &FxSettings::gateRate, 0, 3 },     { "delayRate", &FxSettings::delayRate, 0, 4 } };

    constexpr FloatField floatFields[] {
        { "filterFreq", &FxSettings::filterFreq, 20.0f, 20000.0f }, { "filterReso", &FxSettings::filterReso, 0.5f, 10.0f },
        { "drive", &FxSettings::drive, 0.0f, 1.0f },                { "driveMix", &FxSettings::driveMix, 0.0f, 1.0f },
        { "crushBits", &FxSettings::crushBits, 2.0f, 16.0f },       { "crushRate", &FxSettings::crushRate, 0.0f, 1.0f },
        { "gateDepth", &FxSettings::gateDepth, 0.0f, 1.0f },        { "tapeStopBeats", &FxSettings::tapeStopBeats, 0.25f, 4.0f },
        { "delayFeedback", &FxSettings::delayFeedback, 0.0f, 0.95f }, { "delayMix", &FxSettings::delayMix, 0.0f, 1.0f },
        { "reverbSize", &FxSettings::reverbSize, 0.0f, 1.0f },      { "reverbDamp", &FxSettings::reverbDamp, 0.0f, 1.0f },
        { "reverbMix", &FxSettings::reverbMix, 0.0f, 1.0f },        { "width", &FxSettings::width, 0.0f, 2.0f } };
}

juce::ValueTree FxSettings::toValueTree() const
{
    juce::ValueTree tree ("Fx");
    for (auto& f : boolFields)  tree.setProperty (f.id, this->*f.member, nullptr);
    for (auto& f : intFields)   tree.setProperty (f.id, this->*f.member, nullptr);
    for (auto& f : floatFields) tree.setProperty (f.id, this->*f.member, nullptr);
    tree.setProperty ("gatePattern", (int) gatePattern, nullptr);
    return tree;
}

FxSettings FxSettings::fromValueTree (const juce::ValueTree& tree)
{
    FxSettings s;
    if (! tree.isValid())
        return s;

    for (auto& f : boolFields)
        if (tree.hasProperty (f.id))
            s.*f.member = (bool) tree[f.id];
    for (auto& f : intFields)
        if (tree.hasProperty (f.id))
            s.*f.member = juce::jlimit (f.lo, f.hi, (int) tree[f.id]);
    for (auto& f : floatFields)
        if (tree.hasProperty (f.id))
        {
            const auto v = (float) (double) tree[f.id];
            s.*f.member = std::isfinite (v) ? juce::jlimit (f.lo, f.hi, v) : FxSettings {}.*f.member;
        }
    if (tree.hasProperty ("gatePattern"))
        s.gatePattern = (juce::uint16) ((int) tree["gatePattern"] & 0xffff);
    return s;
}

bool FxSettings::operator== (const FxSettings& o) const noexcept
{
    for (auto& f : boolFields)
        if (this->*f.member != o.*f.member)
            return false;
    for (auto& f : intFields)
        if (this->*f.member != o.*f.member)
            return false;
    for (auto& f : floatFields)
        if (! juce::approximatelyEqual (this->*f.member, o.*f.member))
            return false;
    return gatePattern == o.gatePattern;
}

// ==============================================================================
// DSP helpers
// ==============================================================================
namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;

    float onePoleCoeff (double cutoff, double sampleRate)
    {
        return (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoff / sampleRate));
    }

    /** Balanced dry/wet: both at full level in the middle of the knob. */
    void mixGains (float mix, float& dry, float& wet)
    {
        dry = juce::jmin (1.0f, 2.0f * (1.0f - mix));
        wet = juce::jmin (1.0f, 2.0f * mix);
    }

    /** Runs a per-sample stage over all channels. In loop mode the stage is first
        warmed up on the loop's last `warm` samples so its state at the start
        matches the state at the end (no seam click). */
    template <typename Tick>
    void runStage (juce::AudioBuffer<float>& audio, int numSamples, bool loop, int warm, Tick&& tick)
    {
        warm = loop ? juce::jmin (warm, numSamples) : 0;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            auto* d = audio.getWritePointer (ch);
            for (int i = numSamples - warm; i < numSamples; ++i)
                (void) tick (ch, d[i]);
            for (int i = 0; i < numSamples; ++i)
                d[i] = tick (ch, d[i]);
        }
    }

    // ---- FILTER: Zavalishin TPT state-variable filter
    void processFilter (juce::AudioBuffer<float>& audio, int n, const FxSettings& s, double sr, bool loop)
    {
        const double fc = juce::jlimit (20.0, sr * 0.45, (double) s.filterFreq);
        const float g = (float) std::tan (juce::MathConstants<double>::pi * fc / sr);
        const float k = 1.0f / juce::jlimit (0.5f, 10.0f, s.filterReso);
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const int type = s.filterType;
        std::vector<float> ic1 ((size_t) audio.getNumChannels()), ic2 (ic1);

        runStage (audio, n, loop, (int) sr, [&] (int ch, float x)
        {
            const auto c = (size_t) ch;
            const float v3 = x - ic2[c];
            const float v1 = a1 * ic1[c] + a2 * v3;
            const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
            ic1[c] = 2.0f * v1 - ic1[c];
            ic2[c] = 2.0f * v2 - ic2[c];
            if (type == 1) return x - k * v1 - v2;     // high-pass
            if (type == 2) return v1 * juce::jmin (1.0f, 2.0f * k);   // band-pass, ~unity peak
            return v2;                                  // low-pass
        });
    }

    // ---- DRIVE
    float shape (int type, float u) noexcept
    {
        switch (type)
        {
            case 1:  { const float t = std::tanh (u); return std::tanh (u + 0.3f * t * t); }   // tube: monotonic, asymmetric -> even harmonics
            case 2:  return juce::jlimit (-1.0f, 1.0f, u);                               // hard clip
            default: return std::tanh (u + 0.08f) - 0.0798298f;                          // tape: soft, slightly asymmetric
        }
    }

    void processDrive (juce::AudioBuffer<float>& audio, int n, const FxSettings& s, double sr, bool loop)
    {
        const float pre = juce::Decibels::decibelsToGain (juce::jlimit (0.0f, 1.0f, s.drive) * 30.0f) * 1.2f;
        const int type = s.driveType;

        // gain compensation: match the shaped RMS to the input RMS
        double dryEnergy = 0.0, wetEnergy = 0.0;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const auto* d = audio.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float y = shape (type, d[i] * pre);
                dryEnergy += (double) d[i] * d[i];
                wetEnergy += (double) y * y;
            }
        }
        const float comp = wetEnergy > 1.0e-12 ? (float) juce::jlimit (0.05, 4.0, std::sqrt (dryEnergy / wetEnergy)) : 1.0f;

        float dryG, wetG;
        mixGains (juce::jlimit (0.0f, 1.0f, s.driveMix), dryG, wetG);
        if (s.driveMix >= 0.999f) { dryG = 0.0f; wetG = 1.0f; }   // fully wet at the top of the knob

        const float dcCoeff = onePoleCoeff (12.0, sr);
        std::vector<float> dc ((size_t) audio.getNumChannels());
        runStage (audio, n, loop, (int) (sr * 0.5), [&] (int ch, float x)
        {
            const auto c = (size_t) ch;
            float y = shape (type, x * pre) * comp;
            dc[c] += dcCoeff * (y - dc[c]);
            y -= dc[c];
            return x * dryG + y * wetG;
        });
    }

    // ---- CRUSH: quantiser + sample-and-hold
    void processCrush (juce::AudioBuffer<float>& audio, int n, const FxSettings& s)
    {
        const float bits = juce::jlimit (2.0f, 16.0f, s.crushBits);
        const float levels = std::pow (2.0f, bits - 1.0f);
        const float rate = juce::jlimit (0.0f, 1.0f, s.crushRate);
        const float step = 1.0f / (1.0f + rate * rate * 39.0f);   // up to 40x downsampling

        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            auto* d = audio.getWritePointer (ch);
            float phase = 1.0f, held = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                phase += step;
                if (phase >= 1.0f)
                {
                    phase -= 1.0f;
                    held = std::round (d[i] * levels) / levels;
                }
                d[i] = held;
            }
        }
    }

    // ---- GATE: 16-step pattern synced to the sample start
    double gateStepSamples (int rate, double beat) { return beat / (double) (1 << juce::jlimit (0, 3, rate)); }

    void processGate (juce::AudioBuffer<float>& audio, int n, const FxSettings& s, double sr, double beat, bool loop)
    {
        const double stepLen = juce::jmax (1.0, gateStepSamples (s.gateRate, beat));
        const float depth = juce::jlimit (0.0f, 1.0f, s.gateDepth);
        const auto target = [&] (long long step) { return ((s.gatePattern >> (int) (step & 15)) & 1) != 0 ? 1.0f : 1.0f - depth; };
        const double ramp = juce::jmin (sr * 0.0025, stepLen * 0.5);   // 2.5 ms

        // the step before the first: the loop's last step (wraps) or open (one-shot)
        const float startFrom = loop ? target ((long long) ((n - 1) / stepLen)) : 1.0f;

        std::vector<float> env ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto step = (long long) (i / stepLen);
            const double into = i - (double) step * stepLen;
            const float to = target (step);
            const float from = step == 0 ? startFrom : target (step - 1);
            env[(size_t) i] = into >= ramp ? to : from + (to - from) * (float) (into / ramp);
        }
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            juce::FloatVectorOperations::multiply (audio.getWritePointer (ch), env.data(), n);
    }

    // ---- TAPE STOP: the last beats slow down to a halt (varispeed read + darkening)
    void processTapeStop (juce::AudioBuffer<float>& audio, int n, const FxSettings& s, double sr, double beat)
    {
        const int len = juce::jmin (n, (int) std::round (juce::jlimit (0.25f, 4.0f, s.tapeStopBeats) * beat));
        if (len < 8)
            return;
        const int start = n - len;
        juce::AudioBuffer<float> src (audio.getNumChannels(), len + 4);
        src.clear();
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            src.copyFrom (ch, 0, audio, ch, start, len);

        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const auto* in = src.getReadPointer (ch);
            auto* out = audio.getWritePointer (ch, start);
            double pos = 0.0;
            float lp = 0.0f;
            bool first = true;
            for (int i = 0; i < len; ++i)
            {
                const float x = (float) i / (float) len;
                const float speed = std::pow (1.0f - x, 1.35f);
                const int i0 = (int) pos;
                const float t = (float) (pos - i0);
                const float ym1 = in[juce::jmax (0, i0 - 1)], y0 = in[i0], y1 = in[i0 + 1], y2 = in[i0 + 2];
                // cubic Hermite
                const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
                const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
                const float v = ((c3 * t + c2) * t + c1) * t + y0;

                const float a = onePoleCoeff (120.0 + 17000.0 * std::pow (speed, 1.5f), sr);
                lp = first ? v : lp + a * (v - lp);
                first = false;
                const float gain = std::pow (juce::jmin (1.0f, speed * 5.0f), 1.5f);
                out[i] = (i == len - 1) ? 0.0f : lp * gain;
                pos = juce::jmin ((double) len, pos + speed);
            }
        }
    }

    // ---- DELAY: tempo-synced, filtered feedback, optional ping-pong
    double delayBeats (int rate)
    {
        static constexpr double beats[] { 1.0, 0.5, 0.75, 0.25, 1.5 };
        return beats[juce::jlimit (0, 4, rate)];
    }

    struct Delay
    {
        Delay (const FxSettings& s, double sr, double beat, int channels)
            : length (juce::jmax (1, (int) std::round (delayBeats (s.delayRate) * beat))),
              feedback (juce::jlimit (0.0f, 0.95f, s.delayFeedback)),
              pingPong (s.delayPingPong && channels > 1),
              lpA (onePoleCoeff (6500.0, sr)), hpA (onePoleCoeff (160.0, sr))
        {
            for (auto& l : line)
                l.assign ((size_t) length, 0.0f);
            mixGains (juce::jlimit (0.0f, 1.0f, s.delayMix), dryG, wetG);
        }

        static double tailSeconds (const FxSettings& s, double beat, double sr)
        {
            const double d = delayBeats (s.delayRate) * beat / sr;
            const double fb = juce::jlimit (0.01, 0.95, (double) s.delayFeedback);
            return d * (1.0 + std::log (0.001) / std::log (fb));
        }

        float filt (int c, float y) noexcept
        {
            lp[c] += lpA * (y - lp[c]);
            hp[c] += hpA * (lp[c] - hp[c]);
            return lp[c] - hp[c];
        }

        void process (juce::AudioBuffer<float>& audio, int n)
        {
            const int chans = juce::jmin (2, audio.getNumChannels());
            auto* l = audio.getWritePointer (0);
            auto* r = chans > 1 ? audio.getWritePointer (1) : nullptr;
            for (int i = 0; i < n; ++i)
            {
                if (pingPong)
                {
                    const float yl = line[0][(size_t) pos], yr = line[1][(size_t) pos];
                    line[0][(size_t) pos] = 0.5f * (l[i] + r[i]) + feedback * filt (1, yr);
                    line[1][(size_t) pos] = feedback * filt (0, yl);
                    l[i] = l[i] * dryG + yl * wetG;
                    r[i] = r[i] * dryG + yr * wetG;
                }
                else
                {
                    for (int c = 0; c < chans; ++c)
                    {
                        auto* d = c == 0 ? l : r;
                        const float y = line[c][(size_t) pos];
                        line[c][(size_t) pos] = d[i] + feedback * filt (c, y);
                        d[i] = d[i] * dryG + y * wetG;
                    }
                }
                if (++pos >= length)
                    pos = 0;
            }
            // extra channels (rare): copy the left result
            for (int c = 2; c < audio.getNumChannels(); ++c)
                audio.copyFrom (c, 0, audio, 0, 0, n);
        }

        int length, pos = 0;
        float feedback;
        bool pingPong;
        float lpA, hpA, lp[2] {}, hp[2] {};
        float dryG = 1.0f, wetG = 0.0f;
        std::vector<float> line[2];
    };

    // ---- REVERB: Freeverb (juce::Reverb) with pre-delay
    struct Room
    {
        Room (const FxSettings& s, double sr)
        {
            juce::Reverb::Parameters p;
            p.roomSize = roomSize (s);
            p.damping = 0.15f + 0.8f * juce::jlimit (0.0f, 1.0f, s.reverbDamp);
            p.wetLevel = 1.0f;
            p.dryLevel = 0.0f;
            p.width = 1.0f;
            reverb.setParameters (p);
            reverb.setSampleRate (sr);
            reverb.reset();
            preDelay = juce::jmax (1, (int) (sr * (0.008 + 0.03 * s.reverbSize)));
            for (auto& l : line)
                l.assign ((size_t) preDelay, 0.0f);
            mixGains (juce::jlimit (0.0f, 1.0f, s.reverbMix), dryG, wetG);
            wetG *= 0.6f;   // Freeverb's wet is hot
        }

        static float roomSize (const FxSettings& s) { return 0.25f + 0.68f * juce::jlimit (0.0f, 1.0f, s.reverbSize); }

        static double tailSeconds (const FxSettings& s)
        {
            const double fb = roomSize (s) * 0.28 + 0.7;
            const double t60 = (1350.0 / 44100.0) * std::log (0.001) / std::log (fb);
            return juce::jmin (6.0, t60 * (1.0 - 0.35 * s.reverbDamp)) + 0.04;
        }

        void process (juce::AudioBuffer<float>& audio, int n)
        {
            const int chans = juce::jmin (2, audio.getNumChannels());
            scratch.setSize (2, n, false, false, true);
            for (int c = 0; c < 2; ++c)
            {
                const auto* in = audio.getReadPointer (juce::jmin (c, chans - 1));
                auto* w = scratch.getWritePointer (c);
                auto& l = line[c];
                int p = pos;
                for (int i = 0; i < n; ++i)
                {
                    w[i] = l[(size_t) p];
                    l[(size_t) p] = in[i];
                    if (++p >= preDelay)
                        p = 0;
                }
            }
            pos = (int) ((pos + (long long) n) % preDelay);
            reverb.processStereo (scratch.getWritePointer (0), scratch.getWritePointer (1), n);

            for (int c = 0; c < audio.getNumChannels(); ++c)
            {
                auto* d = audio.getWritePointer (c);
                if (chans == 1)
                {
                    const auto* a = scratch.getReadPointer (0);
                    const auto* b = scratch.getReadPointer (1);
                    for (int i = 0; i < n; ++i)
                        d[i] = d[i] * dryG + 0.5f * (a[i] + b[i]) * wetG;
                }
                else
                {
                    const auto* w = scratch.getReadPointer (juce::jmin (c, 1));
                    for (int i = 0; i < n; ++i)
                        d[i] = d[i] * dryG + w[i] * wetG;
                }
            }
        }

        juce::Reverb reverb;
        int preDelay = 1, pos = 0;
        float dryG = 1.0f, wetG = 0.0f;
        std::vector<float> line[2];
        juce::AudioBuffer<float> scratch;
    };

    // ---- WIDTH: mid / side
    void processWidth (juce::AudioBuffer<float>& audio, int n, const FxSettings& s)
    {
        if (audio.getNumChannels() < 2)
            return;
        const float w = juce::jlimit (0.0f, 2.0f, s.width);
        auto* l = audio.getWritePointer (0);
        auto* r = audio.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float mid = 0.5f * (l[i] + r[i]);
            const float side = 0.5f * (l[i] - r[i]) * w;
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }

    /** Delay + reverb over `num` samples of `audio` with persistent state. */
    struct TimeFx
    {
        TimeFx (const FxSettings& s, double sr, double beat, int channels)
        {
            if (s.delayOn) delay.emplace (s, sr, beat, channels);
            if (s.reverbOn) room.emplace (s, sr);
        }

        void process (juce::AudioBuffer<float>& audio, int num)
        {
            if (delay) delay->process (audio, num);
            if (room) room->process (audio, num);
        }

        std::optional<Delay> delay;
        std::optional<Room> room;
    };
}

// ==============================================================================
// Render
// ==============================================================================
void process (juce::AudioBuffer<float>& audio, const FxSettings& s, double sampleRate, double bpm, bool loop)
{
    const int n = audio.getNumSamples();
    const int chans = audio.getNumChannels();
    if (! s.anyOn() || n == 0 || chans == 0)
        return;

    const double sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
    const double beat = 60.0 / juce::jlimit (20.0, 400.0, bpm > 0.0 ? bpm : 120.0) * sr;

    // ---- static and position-locked modules on the sample itself
    if (s.filterOn)   processFilter (audio, n, s, sr, loop);
    if (s.driveOn)    processDrive (audio, n, s, sr, loop);
    if (s.crushOn)    processCrush (audio, n, s);
    if (s.gateOn)     processGate (audio, n, s, sr, beat, loop);
    if (s.tapeStopOn) processTapeStop (audio, n, s, sr, beat);

    // ---- time-based tails
    if (s.delayOn || s.reverbOn)
    {
        double tail = 0.0;
        if (s.delayOn)  tail = juce::jmax (tail, Delay::tailSeconds (s, beat, sr));
        if (s.reverbOn) tail = juce::jmax (tail, Room::tailSeconds (s));

        TimeFx timeFx (s, sr, beat, chans);

        if (loop)
        {
            // run the loop through repeatedly until the tails have settled and keep
            // the last pass: its start already holds the tail of the end
            const int passes = 1 + juce::jlimit (1, 400, (int) std::ceil (juce::jmin (tail, 10.0) * sr / n));
            juce::AudioBuffer<float> input (audio);
            for (int p = 0; p < passes; ++p)
            {
                if (p > 0)
                    for (int ch = 0; ch < chans; ++ch)
                        audio.copyFrom (ch, 0, input, ch, 0, n);
                timeFx.process (audio, n);
            }
        }
        else
        {
            // one-shot: append the tail (max 3 s), trimmed below -60 dB
            const int extra = (int) (juce::jmin (3.0, tail) * sr);
            audio.setSize (chans, n + extra, true, true, false);
            timeFx.process (audio, n + extra);

            int last = n - 1;
            for (int ch = 0; ch < chans; ++ch)
            {
                const auto* d = audio.getReadPointer (ch);
                for (int i = n + extra - 1; i > last; --i)
                    if (std::abs (d[i]) > 0.001f)
                    {
                        last = i;
                        break;
                    }
            }
            const int fade = juce::jmin ((int) (sr * 0.01), last + 1 - n);
            const int total = juce::jmin (n + extra, last + 1 + fade);
            audio.setSize (chans, total, true, true, false);
            if (total > n)
            {
                const int fadeLen = juce::jmin (total - n, (int) (sr * (total >= n + extra ? 0.3 : 0.02)));
                audio.applyGainRamp (total - fadeLen, fadeLen, 1.0f, 0.0f);
            }
        }
    }

    if (s.widthOn)
        processWidth (audio, audio.getNumSamples(), s);

    // ---- never clip
    const float peak = audio.getMagnitude (0, audio.getNumSamples());
    if (! std::isfinite (peak))
        audio.clear();
    else if (peak > 0.99f)
        audio.applyGain (0.99f / peak);
}

// ==============================================================================
// KILL
// ==============================================================================
namespace
{
    juce::uint16 parsePattern (const char* p)
    {
        juce::uint16 bits = 0;
        for (int i = 0; i < 16 && p[i] != 0; ++i)
            if (p[i] == 'x')
                bits = (juce::uint16) (bits | (1u << i));
        return bits;
    }

    // stutter / chop patterns that groove (step 0 always on so the downbeat lands)
    constexpr const char* gatePatterns[] {
        "xx.xxx.xxx.xxx.x",   // triplet-feel chop
        "x.x.x.x.x.x.xxxx",   // eighths, roll into the next bar
        "xxx.xx.xxx.xxxxx",
        "x..x..x.x..x..x.",   // 3-3-2 bounce
        "xxxxxxxx.x.x.x.x",   // half straight, half stutter
        "xx.xx.xx.xx.x.x.",
        "x.xxx.xxx.xxx.x.",
        "xxxx..xxxx..xx.x",
        "x.x.xx.xx.x.xxxx",
        "xxxxxxxxxxxx.x.x",   // end-of-bar stutter
        "xxx.xxx.xxx.x.x.",
        "x.xxx.x.x.xxx.xx"
    };

    float range (juce::Random& r, float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); }
    float logRange (juce::Random& r, float lo, float hi) { return lo * std::pow (hi / lo, r.nextFloat()); }
    template <typename T> T pick3 (int strength, T a, T b, T c) { return strength <= 0 ? a : (strength == 1 ? b : c); }
}

FxSettings randomise (juce::Random& r, int strength)
{
    strength = juce::jlimit (0, 2, strength);
    FxSettings s;

    enum { filter, drive, crush, gate, tape, delay, reverb, width, numModules };
    static constexpr int weights[numModules] { 3, 3, 2, 3, 1, 3, 3, 2 };

    const int count = strength == 0 ? 2 : (strength == 1 ? 2 + r.nextInt (2) : 3 + r.nextInt (2));
    bool on[numModules] {};
    for (int picked = 0; picked < count;)
    {
        int total = 0;
        for (int m = 0; m < numModules; ++m)
            total += on[m] ? 0 : weights[m];
        int x = r.nextInt (total), m = 0;
        for (; m < numModules; ++m)
        {
            if (on[m]) continue;
            if ((x -= weights[m]) < 0) break;
        }
        // keep it musical: tape stop is rare and never stacked with the gate on subtle
        if (m == tape && strength == 0 && on[gate])
            continue;
        on[m] = true;
        ++picked;
    }

    if ((s.filterOn = on[filter]))
    {
        const float t = r.nextFloat();
        s.filterType = t < 0.5f ? 0 : (t < 0.8f ? 1 : 2);
        if (s.filterType == 0)
            s.filterFreq = pick3 (strength, logRange (r, 3000, 9000), logRange (r, 900, 4000), logRange (r, 300, 1400));
        else if (s.filterType == 1)
            s.filterFreq = pick3 (strength, logRange (r, 80, 250), logRange (r, 200, 700), logRange (r, 500, 1600));
        else
            s.filterFreq = logRange (r, 600, 2500);
        s.filterReso = pick3 (strength, range (r, 0.7f, 1.2f), range (r, 0.8f, 3.0f), range (r, 2.0f, 6.0f));
    }

    if ((s.driveOn = on[drive]))
    {
        s.driveType = r.nextInt (3);
        s.drive = pick3 (strength, range (r, 0.15f, 0.35f), range (r, 0.3f, 0.6f), range (r, 0.6f, 0.95f));
        s.driveMix = pick3 (strength, range (r, 0.4f, 0.7f), range (r, 0.6f, 1.0f), range (r, 0.85f, 1.0f));
    }

    if ((s.crushOn = on[crush]))
    {
        s.crushBits = std::round (pick3 (strength, range (r, 10, 12), range (r, 6, 9), range (r, 3, 6)));
        s.crushRate = pick3 (strength, range (r, 0.0f, 0.15f), range (r, 0.15f, 0.4f), range (r, 0.35f, 0.7f));
    }

    if ((s.gateOn = on[gate]))
    {
        s.gatePattern = parsePattern (gatePatterns[r.nextInt ((int) std::size (gatePatterns))]);
        const float t = r.nextFloat();
        s.gateRate = strength == 2 && t < 0.3f ? 3 : (t < 0.75f ? 2 : 1);
        s.gateDepth = pick3 (strength, range (r, 0.4f, 0.6f), range (r, 0.7f, 1.0f), 1.0f);
    }

    if ((s.tapeStopOn = on[tape]))
        s.tapeStopBeats = pick3 (strength, 0.5f, 1.0f + 0.5f * (float) r.nextInt (3), 2.0f + (float) r.nextInt (3));

    if ((s.delayOn = on[delay]))
    {
        static constexpr int rates[] { 1, 2, 2, 3, 0, 4 };
        s.delayRate = rates[r.nextInt ((int) std::size (rates))];
        s.delayFeedback = pick3 (strength, range (r, 0.2f, 0.35f), range (r, 0.3f, 0.55f), range (r, 0.5f, 0.75f));
        s.delayMix = pick3 (strength, range (r, 0.12f, 0.2f), range (r, 0.18f, 0.32f), range (r, 0.3f, 0.45f));
        s.delayPingPong = r.nextFloat() < 0.7f;
    }

    if ((s.reverbOn = on[reverb]))
    {
        s.reverbSize = pick3 (strength, range (r, 0.3f, 0.5f), range (r, 0.45f, 0.75f), range (r, 0.7f, 0.95f));
        s.reverbDamp = range (r, 0.3f, 0.7f);
        s.reverbMix = pick3 (strength, range (r, 0.1f, 0.18f), range (r, 0.15f, 0.3f), range (r, 0.3f, 0.5f));
    }

    if ((s.widthOn = on[width]))
        s.width = pick3 (strength, range (r, 1.2f, 1.4f), range (r, 1.3f, 1.7f), range (r, 1.6f, 2.0f));

    // delay + reverb together: pull both back so the dry still leads
    if (s.delayOn && s.reverbOn)
    {
        s.delayMix *= 0.75f;
        s.reverbMix *= 0.75f;
    }
    return s;
}

} // namespace digga::fx
