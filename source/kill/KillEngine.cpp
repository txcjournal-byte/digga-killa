#include "kill/KillEngine.h"

#include "core/AudioTools.h"
#include "dsp/Stretcher.h"

#include <juce_dsp/juce_dsp.h>

namespace digga::kill
{

namespace
{
    enum class Transform { scaleShift, rearrange, halftime, reverse, stutter, dropout, filterSweep };

    class Variation
    {
    public:
        Variation (const juce::AudioBuffer<float>& parent, const Context& c, float s, juce::uint32 seed)
            : ctx (c), strength (juce::jlimit (0.0f, 1.0f, s)), rng ((juce::int64) seed * 7919 + 17)
        {
            work.makeCopyOf (parent);
            edge = juce::jmax (8, (int) (0.002 * ctx.sampleRate));

            if (ctx.isLoop && ctx.bpm > 0.0)
            {
                beat = 60.0 / ctx.bpm * ctx.sampleRate;
                numBeats = juce::jmax (1, (int) std::lround (work.getNumSamples() / beat));
            }

            if (! ctx.isLoop || numBeats < 4)
            {
                // one-shots (or tiny loops): eight equal slices stand in for beats
                isShot = ! ctx.isLoop;
                numBeats = 8;
                beat = work.getNumSamples() / 8.0;
            }
            numBars = juce::jmax (1, numBeats / 4);
        }

        juce::AudioBuffer<float> run()
        {
            const auto chosen = chooseTransforms();
            const float parentPeak = juce::jmax (0.3f, audio::peak (work));

            for (auto t : { Transform::scaleShift, Transform::rearrange, Transform::halftime, Transform::reverse,
                            Transform::stutter, Transform::dropout, Transform::filterSweep })
            {
                if (std::find (chosen.begin(), chosen.end(), t) == chosen.end())
                    continue;

                switch (t)
                {
                    case Transform::scaleShift:  scaleShift(); break;
                    case Transform::rearrange:   rearrange(); break;
                    case Transform::halftime:    halftime(); break;
                    case Transform::reverse:     reverse(); break;
                    case Transform::stutter:     stutter(); break;
                    case Transform::dropout:     dropout(); break;
                    case Transform::filterSweep: filterSweep(); break;
                }
            }

            audio::normalise (work, parentPeak);
            if (isShot)
            {
                audio::fadeOut (work, work.getNumSamples(), juce::jmin (work.getNumSamples() / 5, (int) (0.01 * ctx.sampleRate)));
            }
            else
            {
                audio::fadeIn (work, 0, edge);
                audio::fadeOut (work, work.getNumSamples(), edge);
            }
            return std::move (work);
        }

    private:
        // ---------------------------------------------------------------- choice
        std::vector<Transform> chooseTransforms()
        {
            std::vector<std::pair<Transform, float>> pool;
            if (isShot)
                pool = { { Transform::scaleShift, 1.3f }, { Transform::reverse, 0.8f }, { Transform::stutter, 0.7f },
                         { Transform::filterSweep, 0.8f }, { Transform::halftime, 0.6f }, { Transform::dropout, 0.35f } };
            else
                pool = { { Transform::rearrange, 1.1f }, { Transform::scaleShift, 0.9f }, { Transform::reverse, 0.8f },
                         { Transform::stutter, 0.8f }, { Transform::filterSweep, 0.7f }, { Transform::dropout, 0.6f },
                         { Transform::halftime, 0.3f + 0.4f * strength } };

            const int count = juce::jlimit (1, 5, 1 + (int) std::floor (strength * 3.2f + rng.nextFloat() * 0.9f));
            std::vector<Transform> chosen;

            while ((int) chosen.size() < count && ! pool.empty())
            {
                float total = 0.0f;
                for (auto& p : pool)
                    total += p.second;
                float r = rng.nextFloat() * total;
                size_t index = 0;
                for (; index + 1 < pool.size(); ++index)
                {
                    r -= pool[index].second;
                    if (r <= 0.0f)
                        break;
                }
                chosen.push_back (pool[index].first);
                pool.erase (pool.begin() + (long) index);
            }
            return chosen;
        }

        // ---------------------------------------------------------------- helpers
        int unitStart (double units, double unitLength) const
        {
            return juce::jlimit (0, work.getNumSamples(), (int) std::lround (units * unitLength));
        }

        /** Copies [srcStart, srcStart+len) of src into dst at dstStart with
            short edge fades so slices never click. */
        void pasteSlice (juce::AudioBuffer<float>& dst, int dstStart, const juce::AudioBuffer<float>& src,
                         int srcStart, int len, bool reversed, float gain = 1.0f) const
        {
            len = juce::jmin (len, dst.getNumSamples() - dstStart, src.getNumSamples() - srcStart);
            if (len <= 0)
                return;
            const int fade = juce::jmin (edge, len / 2);

            for (int ch = 0; ch < dst.getNumChannels(); ++ch)
            {
                const auto* s = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1), srcStart);
                auto* d = dst.getWritePointer (ch, dstStart);
                for (int i = 0; i < len; ++i)
                {
                    float g = gain;
                    if (i < fade) g *= (float) i / (float) fade;
                    if (len - 1 - i < fade) g *= (float) (len - 1 - i) / (float) fade;
                    d[i] = (reversed ? s[len - 1 - i] : s[i]) * g;
                }
            }
        }

        void silence (int start, int len)
        {
            len = juce::jmin (len, work.getNumSamples() - start);
            if (len <= 0)
                return;
            const int fade = juce::jmin (edge, len / 2);
            for (int ch = 0; ch < work.getNumChannels(); ++ch)
            {
                auto* d = work.getWritePointer (ch, start);
                for (int i = 0; i < len; ++i)
                {
                    float g = 0.0f;
                    if (i < fade) g = 1.0f - (float) i / (float) fade;
                    if (len - 1 - i < fade) g = juce::jmax (g, 1.0f - (float) (len - 1 - i) / (float) fade);
                    d[i] *= g;
                }
            }
        }

        std::vector<int> scaleShifts (bool wide) const
        {
            static constexpr int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
            static constexpr int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
            const int* scale = ctx.minor ? minorScale : majorScale;

            auto degree = [scale] (int d)
            {
                const int octave = (int) std::floor (d / 7.0);
                return scale[((d % 7) + 7) % 7] + 12 * octave;
            };

            std::vector<int> shifts;
            for (int d : { -4, -3, -2, 2, 3, 4 })
                shifts.push_back (degree (d));
            if (wide)
                for (int s : { -12, 12, degree (-5), degree (5) })
                    shifts.push_back (s);
            return shifts;
        }

        // ---------------------------------------------------------------- transforms
        void scaleShift()
        {
            const auto shifts = scaleShifts (strength > 0.6f);
            auto pick = [&] { return shifts[(size_t) rng.nextInt ((int) shifts.size())]; };

            if (isShot || numBars < 2 || strength < 0.45f)
            {
                work = dsp::pitchShift (work, pick(), ctx.sampleRate);
                return;
            }

            // a transposition "progression": blocks of bars follow different scale steps
            const int blockBars = strength > 0.8f && numBars >= 4 ? 1 : 2;
            std::vector<int> blockShift;
            std::vector<int> distinct;
            for (int bar = 0; bar < numBars; bar += blockBars)
            {
                const int s = rng.nextFloat() < 0.35f ? 0 : pick();
                blockShift.push_back (s);
                if (s != 0 && std::find (distinct.begin(), distinct.end(), s) == distinct.end() && distinct.size() < 3)
                    distinct.push_back (s);
            }

            if (distinct.empty())
            {
                work = dsp::pitchShift (work, pick(), ctx.sampleRate);
                return;
            }

            std::vector<std::pair<int, juce::AudioBuffer<float>>> versions;
            for (int s : distinct)
                versions.emplace_back (s, dsp::pitchShift (work, s, ctx.sampleRate));

            const juce::AudioBuffer<float> original (work);
            const int xfade = (int) (0.006 * ctx.sampleRate);
            const double barLength = beat * 4.0;

            for (size_t b = 0; b < blockShift.size(); ++b)
            {
                int s = blockShift[b];
                if (s != 0 && std::find (distinct.begin(), distinct.end(), s) == distinct.end())
                    s = distinct.front();

                const auto* source = &original;
                for (auto& v : versions)
                    if (v.first == s)
                        source = &v.second;

                const int start = unitStart ((double) b * blockBars, barLength);
                const int end = b + 1 == blockShift.size() ? work.getNumSamples()
                                                            : unitStart ((double) (b + 1) * blockBars, barLength);
                for (int ch = 0; ch < work.getNumChannels(); ++ch)
                    work.copyFrom (ch, start, *source, ch, start, end - start);

                if (b > 0 && start >= xfade / 2)
                {
                    // smooth the block join
                    juce::AudioBuffer<float> joined (work.getNumChannels(), xfade);
                    for (int ch = 0; ch < work.getNumChannels(); ++ch)
                        joined.copyFrom (ch, 0, *source, ch, start - xfade / 2, juce::jmin (xfade, work.getNumSamples() - start + xfade / 2));
                    audio::crossfadeInto (work, start - xfade / 2, joined, 0, xfade);
                }
            }
        }

        void rearrange()
        {
            const double sub = strength > 0.6f && rng.nextFloat() < 0.6f ? 0.5 : 1.0;
            const double unitLength = beat * sub;
            const int unitsPerBar = (int) std::lround (4.0 / sub);
            const juce::AudioBuffer<float> original (work);
            bool changed = false;

            for (int bar = 0; bar < numBars; ++bar)
            {
                if (rng.nextFloat() > 0.35f + 0.6f * strength && ! (bar == numBars - 1 && ! changed))
                    continue;

                std::vector<std::pair<int, int>> map; // (source bar, source unit) per unit
                for (int u = 0; u < unitsPerBar; ++u)
                    map.emplace_back (bar, u);

                const int swaps = 1 + (int) (strength * unitsPerBar * 0.5f);
                for (int k = 0; k < swaps; ++k)
                {
                    const int i = rng.nextInt (unitsPerBar), j = rng.nextInt (unitsPerBar);
                    if (strength < 0.8f && (i == 0 || j == 0))
                        continue; // keep the downbeat
                    std::swap (map[(size_t) i], map[(size_t) j]);
                }

                if (strength > 0.5f && numBars > 1 && rng.nextFloat() < strength * 0.6f)
                {
                    // borrow a unit from another bar
                    const int u = 1 + rng.nextInt (unitsPerBar - 1);
                    map[(size_t) u] = { rng.nextInt (numBars), rng.nextInt (unitsPerBar) };
                }

                for (int u = 0; u < unitsPerBar; ++u)
                {
                    const auto [srcBar, srcUnit] = map[(size_t) u];
                    if (srcBar == bar && srcUnit == u)
                        continue;
                    const int dst = unitStart (bar * unitsPerBar + u, unitLength);
                    const int src = unitStart (srcBar * unitsPerBar + srcUnit, unitLength);
                    const int len = unitStart (bar * unitsPerBar + u + 1, unitLength) - dst;
                    pasteSlice (work, dst, original, src, len, false);
                    changed = true;
                }
            }
        }

        void halftime()
        {
            const int n = work.getNumSamples();
            const bool tape = rng.nextFloat() < (isShot ? 0.5f : 0.3f * strength);

            if (isShot)
            {
                const int newLength = (int) (n * (tape ? 1.5 : 1.6));
                work = tape ? audio::resample (work, newLength) : dsp::stretch (work, newLength, 0.0, ctx.sampleRate);
                return;
            }

            // first half of the loop, played at half speed over the full length
            const int halfBars = juce::jmax (1, numBars / 2);
            const int half = juce::jmin (n, unitStart (halfBars * 4.0, beat));
            juce::AudioBuffer<float> firstHalf (work.getNumChannels(), half);
            for (int ch = 0; ch < work.getNumChannels(); ++ch)
                firstHalf.copyFrom (ch, 0, work, ch, 0, half);

            work = tape ? audio::resample (firstHalf, n) : dsp::stretch (firstHalf, n, 0.0, ctx.sampleRate);
        }

        void reverse()
        {
            const juce::AudioBuffer<float> original (work);

            if (isShot)
            {
                // reversed hit: swells into the attack
                pasteSlice (work, 0, original, 0, work.getNumSamples(), true);
                return;
            }

            const double unitLength = strength > 0.7f && rng.nextFloat() < 0.4f ? beat * 4.0
                                    : rng.nextBool() ? beat : beat * 0.5;
            const int units = juce::jmax (1, (int) std::lround (work.getNumSamples() / unitLength));
            const float probability = 0.12f + 0.3f * strength;
            bool any = false;

            for (int u = 0; u < units; ++u)
            {
                if (rng.nextFloat() > probability && ! (u == units - 1 && ! any))
                    continue;
                const int start = unitStart (u, unitLength);
                const int len = unitStart (u + 1, unitLength) - start;
                pasteSlice (work, start, original, start, len, true);
                any = true;
            }
        }

        void stutter()
        {
            const juce::AudioBuffer<float> original (work);

            if (isShot)
            {
                const int unit = juce::jmax (edge * 2, work.getNumSamples() / (strength > 0.6f ? 16 : 12));
                const int repeats = 2 + rng.nextInt (3);
                for (int r = 1; r <= repeats; ++r)
                    pasteSlice (work, r * unit, original, 0, unit, false, 0.75f + 0.25f * (float) r / (float) repeats);
                return;
            }

            const int events = juce::jmax (1, (int) std::lround ((0.5f + strength * 2.5f) * numBars / 4.0f));
            for (int e = 0; e < events; ++e)
            {
                const int bar = rng.nextInt (numBars);
                const int beatInBar = rng.nextFloat() < 0.7f ? 3 : rng.nextInt (4);
                const double regionBeats = strength > 0.7f && beatInBar <= 2 ? 2.0 : 1.0;
                const double division = strength > 0.65f && rng.nextBool() ? 8.0 : 4.0; // 1/32 or 1/16
                const double unitLength = beat / division;

                const double regionStart = bar * 4.0 + beatInBar;
                const int repeats = (int) std::lround (regionBeats * division);
                const int src = unitStart (regionStart, beat);
                const int len = unitStart (regionStart + 1.0 / division, beat) - src;

                for (int r = 0; r < repeats; ++r)
                {
                    const int dst = unitStart (regionStart * division + r, unitLength);
                    const float gain = 0.65f + 0.35f * (float) r / (float) juce::jmax (1, repeats - 1);
                    pasteSlice (work, dst, original, src, len, false, gain);
                }
            }
        }

        void dropout()
        {
            const double unitLength = isShot ? work.getNumSamples() / 16.0 : beat * 0.5;
            const int units = juce::jmax (1, (int) std::lround (work.getNumSamples() / unitLength));
            const float probability = 0.08f + 0.22f * strength;
            bool any = false;

            for (int u = isShot ? units / 3 : 0; u < units; ++u)
            {
                const bool downbeat = ! isShot && u % 8 == 0;
                if (downbeat && strength < 0.7f)
                    continue;
                if (rng.nextFloat() > probability && ! (u == units - 1 && ! any))
                    continue;
                const int start = unitStart (u, unitLength);
                silence (start, unitStart (u + 1, unitLength) - start);
                any = true;
            }
        }

        void filterSweep()
        {
            const int n = work.getNumSamples();
            const int kind = rng.nextInt (3); // 0 LP opening, 1 LP closing, 2 HP rising
            const float resonance = 0.8f + 2.2f * strength;
            const int cycles = ! isShot && strength > 0.5f && numBars >= 4 ? (rng.nextBool() ? numBars / 2 : numBars / 4) : 1;
            const int cycleLength = juce::jmax (1, n / juce::jmax (1, cycles));

            juce::dsp::StateVariableTPTFilter<float> filter;
            filter.prepare ({ ctx.sampleRate, 32, (juce::uint32) work.getNumChannels() });
            filter.setType (kind == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                      : juce::dsp::StateVariableTPTFilterType::lowpass);
            filter.setResonance (resonance);

            const float lo = kind == 2 ? 30.0f : 280.0f;
            const float hi = kind == 2 ? 1400.0f + 1600.0f * strength : 17000.0f;

            for (int i = 0; i < n; ++i)
            {
                if (i % 32 == 0)
                {
                    float t = (float) (i % cycleLength) / (float) cycleLength;
                    if (kind == 1) t = 1.0f - t;
                    const float shaped = kind == 2 ? t * t : std::sqrt (t);
                    filter.setCutoffFrequency (juce::jmin ((float) ctx.sampleRate * 0.45f, lo * std::pow (hi / lo, shaped)));
                }
                for (int ch = 0; ch < work.getNumChannels(); ++ch)
                {
                    auto* d = work.getWritePointer (ch);
                    d[i] = filter.processSample (ch, d[i]);
                }
            }
        }

        Context ctx;
        float strength;
        juce::Random rng;
        juce::AudioBuffer<float> work;
        double beat = 1.0;
        int numBeats = 8, numBars = 1, edge = 64;
        bool isShot = false;
    };
}

juce::AudioBuffer<float> makeVariation (const juce::AudioBuffer<float>& parent, const Context& context,
                                        float strength, juce::uint32 seed)
{
    if (parent.getNumSamples() == 0)
        return parent;
    return Variation (parent, context, strength, seed).run();
}

} // namespace digga::kill
