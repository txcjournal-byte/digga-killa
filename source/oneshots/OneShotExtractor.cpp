#include "oneshots/OneShotExtractor.h"

#include "analysis/Features.h"

#include <juce_dsp/juce_dsp.h>

namespace digga::oneshots
{

namespace
{
    struct Hit
    {
        int start = 0, end = 0;
        float quality = 0.0f;
        juce::String category, name;
        std::array<float, 4> features {};
        float centroid = 0.0f;
    };

    juce::String noteName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }

    juce::String pitchClassName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((midi % 12) + 12) % 12];
    }

    /** lowpass (low hits, after a 20 ms click) or highpass (hats) the shot. */
    void cleanTail (juce::AudioBuffer<float>& b, double sr, bool lowHit)
    {
        juce::dsp::IIR::Filter<float> filters[2];
        for (auto& f : filters)
            f.coefficients = lowHit ? juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 1800.0f, 0.6f)
                                    : juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 2500.0f, 0.6f);
        const int click = lowHit ? (int) (0.02 * sr) : 0;
        const int blend = juce::jmax (1, (int) (0.015 * sr));
        for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float filtered = filters[ch].processSample (d[i]);
                const float w = juce::jlimit (0.0f, 1.0f, (float) (i - click) / (float) blend);
                d[i] = d[i] + (filtered - d[i]) * w;
            }
        }
    }

    float rmsOf (const float* x, int n)
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += (double) x[i] * x[i];
        return n > 0 ? (float) std::sqrt (sum / n) : 0.0f;
    }

    std::vector<int> pickPeaks (const analysis::OnsetEnvelope& env)
    {
        const auto& e = env.flux;
        const int n = (int) e.size();
        std::vector<int> peaks;
        if (n < 3)
            return peaks;

        const float maxValue = *std::max_element (e.begin(), e.end());
        const int radius = juce::jmax (2, (int) (0.08 * env.framesPerSecond()));
        const int minGap = juce::jmax (1, (int) (0.06 * env.framesPerSecond()));
        std::vector<float> window;

        for (int t = 1; t < n - 1; ++t)
        {
            if (e[(size_t) t] < e[(size_t) t - 1] || e[(size_t) t] < e[(size_t) t + 1])
                continue;

            window.clear();
            for (int k = juce::jmax (0, t - radius); k <= juce::jmin (n - 1, t + radius); ++k)
                window.push_back (e[(size_t) k]);
            std::nth_element (window.begin(), window.begin() + (long) window.size() / 2, window.end());
            const float threshold = window[window.size() / 2] * 1.5f + 0.06f * maxValue;

            if (e[(size_t) t] > threshold && (peaks.empty() || t - peaks.back() >= minGap))
                peaks.push_back (t);
        }
        return peaks;
    }

    /** Attack start: first 1 ms block reaching 25% of the local peak. */
    int refineStart (const float* mono, int length, int guess, double sr)
    {
        const int from = juce::jmax (0, guess - (int) (0.03 * sr));
        const int to = juce::jmin (length, guess + (int) (0.03 * sr));
        const int block = juce::jmax (8, (int) (0.001 * sr));

        float localPeak = 0.0f;
        for (int i = from; i < to; ++i)
            localPeak = juce::jmax (localPeak, std::abs (mono[i]));

        for (int i = from; i + block <= to; i += block / 2)
        {
            float m = 0.0f;
            for (int k = 0; k < block; ++k)
                m = juce::jmax (m, std::abs (mono[i + k]));
            if (m >= 0.25f * localPeak)
                return juce::jmax (0, i - block);
        }
        return from;
    }

    /** End of decay: 10 ms envelope falls 45 dB under its peak (or the next hit). */
    int findEnd (const float* mono, int length, int start, int limit, double sr)
    {
        const int block = juce::jmax (16, (int) (0.01 * sr));
        const int maxLength = (int) (2.5 * sr);
        limit = juce::jmin (limit, length, start + maxLength);

        float peakRms = 0.0f;
        int pos = start;
        for (; pos + block <= limit; pos += block)
        {
            double sum = 0.0;
            for (int k = 0; k < block; ++k)
                sum += (double) mono[pos + k] * mono[pos + k];
            const float rms = (float) std::sqrt (sum / block);
            peakRms = juce::jmax (peakRms, rms);
            if (pos > start + (int) (0.05 * sr) && rms < peakRms * 0.0056f)   // -45 dB
                return pos + block;
        }
        return limit;
    }
}

std::vector<Shot> extract (const juce::AudioBuffer<float>& source, double sampleRate, const std::vector<midi::Note>& notes)
{
    std::vector<Shot> shots;
    const int length = source.getNumSamples();
    if (length == 0)
        return shots;

    const auto monoBuffer = audio::toMono (source);
    const float* mono = monoBuffer.getReadPointer (0);
    const auto env = analysis::computeOnsets (mono, length, sampleRate);
    const auto peaks = pickPeaks (env);
    const float globalPeak = juce::jmax (1.0e-6f, audio::peak (monoBuffer));

    std::vector<int> starts;
    std::vector<float> lowStrength;
    for (int frame : peaks)
    {
        starts.push_back (refineStart (mono, length, (int) (env.frameToSeconds (frame) * sampleRate), sampleRate));
        float low = 0.0f;
        for (int k = frame - 1; k <= frame + 2; ++k)
            if (juce::isPositiveAndBelow (k, (int) env.lowFlux.size()))
                low = juce::jmax (low, env.lowFlux[(size_t) k]);
        lowStrength.push_back (low);
    }

    auto pitchedAt = [&] (double from, double to, int minPitch, int maxPitch)
    {
        // strongest note sounding in this time range, -1 if none
        int best = -1;
        double bestOverlap = 0.0;
        for (const auto& n : notes)
        {
            if (n.pitch < minPitch || n.pitch > maxPitch)
                continue;
            const double overlap = juce::jmin (to, n.endSeconds) - juce::jmax (from, n.startSeconds);
            if (overlap > bestOverlap)
            {
                bestOverlap = overlap;
                best = n.pitch;
            }
        }
        return bestOverlap > 0.5 * (to - from) ? best : -1;
    };

    auto melodicActivity = [&] (double from, double to)
    {
        int count = 0;
        for (const auto& n : notes)
            if (n.pitch >= 48 && n.endSeconds > from && n.startSeconds < to)
                ++count;
        return count;
    };

    // ---------------------------------------------------------------- drum hits
    std::vector<Hit> hits;
    for (size_t i = 0; i < starts.size(); ++i)
    {
        Hit hit;
        hit.start = starts[i];
        const int next = i + 1 < starts.size() ? starts[i + 1] : length;
        hit.end = findEnd (mono, length, hit.start, next, sampleRate);
        const int len = hit.end - hit.start;
        if (len < (int) (0.04 * sampleRate))
            continue;

        const int attack = juce::jmin (len, (int) (0.1 * sampleRate));
        const float attackPeak = monoBuffer.getMagnitude (0, hit.start, attack);
        if (attackPeak < 0.08f * globalPeak)
            continue;

        const auto f = analysis::describe (mono + hit.start, attack, sampleRate);
        const double t0 = hit.start / sampleRate, t1 = hit.end / sampleRate;

        // category from where the attack's energy sits
        // noise-like attacks are snares / hats even when a previous kick still rings underneath
        const bool noisy = f.flatness > 0.5f;
        const bool low = ! noisy && f.lowRatio > 0.45f && f.highRatio < 0.3f;
        if (low)
        {
            // low hits ring until the next low hit: hats on top get filtered out below
            int nextLow = length;
            for (size_t j = i + 1; j < starts.size(); ++j)
                if (lowStrength[j] > 0.5f * lowStrength[i])
                {
                    nextLow = starts[j];
                    break;
                }
            hit.end = findEnd (mono, length, hit.start, nextLow, sampleRate);
        }

        double lengthSec = (hit.end - hit.start) / sampleRate;
        const double t1Low = hit.end / sampleRate;
        if (low)
        {
            const int bass = pitchedAt (t0, t1Low, 21, 47);
            hit.category = lengthSec > 0.35 && bass >= 0 ? "808" : "Kick";
            if (hit.category == "Kick")
                hit.end = juce::jmin (hit.end, hit.start + (int) (0.7 * sampleRate));   // a kick, not the 808 under it
            // the octave of a sub note is unreliable; the pitch class is what matters
            hit.name = bass >= 0 && hit.category == "808" ? "808 " + pitchClassName (bass) : juce::String();
            lengthSec = (hit.end - hit.start) / sampleRate;
        }
        else if (noisy || f.highRatio > 0.5f)
        {
            // noise: a hat is short and thin, a snare has body below the highs
            if (lengthSec < 0.1)
                hit.category = "Hat";
            else if (lengthSec < 0.3 && (f.midRatio > 0.08f || f.lowRatio > 0.3f))
                hit.category = "Snare";
            else
                hit.category = "Open Hat";
        }
        else if (f.midRatio > 0.25f && f.flatness > 0.15f)
            hit.category = "Snare";
        else
            hit.category = "Perc";

        // quality: clean attack, nothing on top, full decay, loud enough
        const int pre = juce::jmin (hit.start, (int) (0.03 * sampleRate));
        const float before = rmsOf (mono + hit.start - pre, pre);
        const float body = rmsOf (mono + hit.start, attack);
        const float cleanAttack = 1.0f - juce::jlimit (0.0f, 1.0f, before / juce::jmax (1.0e-6f, body));
        const bool fullDecay = hit.end < (low ? length : next);
        const int clutter = hit.category == "808" ? 0 : melodicActivity (t0, t1);

        hit.quality = 0.9f * cleanAttack + (fullDecay ? 0.4f : 0.0f) + 0.4f * attackPeak / globalPeak
                    - 0.25f * (float) juce::jmin (clutter, 4);
        hit.centroid = f.centroid;
        hit.features = { std::log2 (juce::jmax (40.0f, f.centroid)), f.flatness * 4.0f, f.lowRatio * 3.0f,
                         std::log2 ((float) lengthSec + 0.05f) };
        if (hit.category == "Kick" || hit.category == "808")
            hit.quality += 0.6f * (float) juce::jmin (lengthSec, 0.5);   // full-bodied low hits
        hits.push_back (hit);
    }

    // ---------------------------------------------------------------- notes & chords
    std::vector<Hit> tonal;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const double duration = n.endSeconds - n.startSeconds;
        if (n.pitch < 48 || duration < 0.2 || n.amplitude < 0.35f)
            continue;

        // notes struck together with this one
        std::vector<int> together { n.pitch };
        for (const auto& m : notes)
            if (&m != &n && m.pitch >= 40 && std::abs (m.startSeconds - n.startSeconds) < 0.05)
                together.push_back (m.pitch);
        const bool lowest = *std::min_element (together.begin(), together.end()) == n.pitch;
        if (together.size() >= 3 && ! lowest)
            continue;   // a chord is represented by its lowest note

        // drum hits inside the note make it unusable as a clean one-shot
        int drums = 0;
        for (int s : starts)
        {
            const double t = s / sampleRate;
            if (t > n.startSeconds + 0.03 && t < n.endSeconds)
                ++drums;
        }

        Hit hit;
        hit.start = juce::jmax (0, (int) ((n.startSeconds - 0.005) * sampleRate));
        hit.end = juce::jmin (length, (int) ((juce::jmin (n.endSeconds, n.startSeconds + 2.5) + 0.15) * sampleRate));
        if (hit.end - hit.start < (int) (0.15 * sampleRate))
            continue;

        if (together.size() >= 3)
        {
            const int root = n.pitch;
            bool minorThird = false, majorThird = false;
            for (int p : together)
            {
                minorThird |= (p - root) % 12 == 3;
                majorThird |= (p - root) % 12 == 4;
            }
            hit.category = "Chord";
            hit.name = "Chord " + pitchClassName (root) + (minorThird && ! majorThird ? "m" : "");
        }
        else if (together.size() == 1)
        {
            hit.category = "Note";
            hit.name = "Note " + noteName (n.pitch);
        }
        else
            continue;   // two-note dyads are ambiguous

        hit.quality = n.amplitude + 0.3f * (float) juce::jmin (duration, 1.0) - 0.35f * (float) juce::jmin (drums, 4);
        hit.centroid = 440.0f * std::pow (2.0f, (float) (n.pitch - 69) / 12.0f);
        hit.features = { (float) n.pitch / 12.0f, (float) together.size(), 0.0f, (float) duration };
        tonal.push_back (hit);
    }

    // ---------------------------------------------------------------- pick per category
    struct Quota { const char* category; int count; };
    static constexpr Quota quotas[] = { { "Kick", 2 }, { "808", 2 }, { "Snare", 2 }, { "Hat", 2 }, { "Open Hat", 1 },
                                        { "Perc", 1 }, { "Chord", 2 }, { "Note", 3 } };

    std::vector<Hit> all (hits);
    all.insert (all.end(), tonal.begin(), tonal.end());

    for (const auto& quota : quotas)
    {
        std::vector<const Hit*> candidates;
        for (const auto& h : all)
            if (h.category == quota.category && h.quality > 0.2f)
                candidates.push_back (&h);
        std::sort (candidates.begin(), candidates.end(), [] (const Hit* a, const Hit* b) { return a->quality > b->quality; });

        std::vector<const Hit*> chosen;
        for (const auto* c : candidates)
        {
            if ((int) chosen.size() >= quota.count)
                break;
            bool tooSimilar = false;
            for (const auto* o : chosen)
            {
                float d = 0.0f;
                for (size_t k = 0; k < c->features.size(); ++k)
                    d += std::abs (c->features[k] - o->features[k]);
                tooSimilar |= d < 0.35f || (c->name == o->name && c->name.isNotEmpty());
            }
            if (! tooSimilar)
                chosen.push_back (c);
        }

        int index = 1;
        for (const auto* c : chosen)
        {
            Shot shot;
            shot.category = quota.category;
            shot.name = c->name.isNotEmpty() ? c->name
                      : chosen.size() > 1 ? juce::String (quota.category) + " " + juce::String (index) : juce::String (quota.category);
            ++index;
            shot.sourceSeconds = c->start / sampleRate;

            const int len = c->end - c->start;
            shot.audio.setSize (source.getNumChannels(), len);
            for (int ch = 0; ch < source.getNumChannels(); ++ch)
                shot.audio.copyFrom (ch, 0, source, ch, c->start, len);

            // spectral clean-up of what else was playing: low hits lose hat bleed
            // after the click, hats lose low end
            if (shot.category == "Kick" || shot.category == "808")
                cleanTail (shot.audio, sampleRate, true);
            else if (shot.category == "Hat" || shot.category == "Open Hat")
                cleanTail (shot.audio, sampleRate, false);

            audio::fadeIn (shot.audio, 0, juce::jmax (8, (int) (0.001 * sampleRate)));
            audio::fadeOut (shot.audio, len, juce::jlimit (16, (int) (0.04 * sampleRate), len / 4));
            audio::normalise (shot.audio, 0.944f);   // -0.5 dBFS
            shots.push_back (std::move (shot));
        }
    }

    return shots;
}

} // namespace digga::oneshots
