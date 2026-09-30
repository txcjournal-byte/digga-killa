#include "oneshots/OneShotExtractor.h"

#include "analysis/Features.h"

namespace digga::oneshots
{

namespace
{
    struct Hit
    {
        int start = 0, end = 0;
        float quality = 0.0f;
        std::array<float, 5> features {};
        float centroid = 0.0f;
    };

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

std::vector<juce::AudioBuffer<float>> extract (const juce::AudioBuffer<float>& source, double sampleRate, int maxShots)
{
    std::vector<juce::AudioBuffer<float>> shots;
    const int length = source.getNumSamples();
    if (length == 0)
        return shots;

    const auto monoBuffer = audio::toMono (source);
    const float* mono = monoBuffer.getReadPointer (0);
    const auto env = analysis::computeOnsets (mono, length, sampleRate);
    const auto peaks = pickPeaks (env);
    const float globalPeak = juce::jmax (1.0e-6f, audio::peak (monoBuffer));

    std::vector<Hit> hits;
    std::vector<int> starts;
    for (int frame : peaks)
        starts.push_back (refineStart (mono, length, (int) (env.frameToSeconds (frame) * sampleRate), sampleRate));

    for (size_t i = 0; i < starts.size(); ++i)
    {
        Hit hit;
        hit.start = starts[i];
        const int next = i + 1 < starts.size() ? starts[i + 1] : length;
        hit.end = findEnd (mono, length, hit.start, next, sampleRate);
        const int len = hit.end - hit.start;
        if (len < (int) (0.05 * sampleRate))
            continue;

        const int attack = juce::jmin (len, (int) (0.12 * sampleRate));
        float attackPeak = 0.0f;
        for (int k = 0; k < attack; ++k)
            attackPeak = juce::jmax (attackPeak, std::abs (mono[hit.start + k]));
        if (attackPeak < 0.06f * globalPeak)
            continue;

        const auto f = analysis::describe (mono + hit.start, attack, sampleRate);
        const int frame = (int) env.secondsToFrame (hit.start / sampleRate);
        const float strength = juce::isPositiveAndBelow (frame + 1, (int) env.flux.size()) ? env.flux[(size_t) frame + 1] : 0.0f;

        hit.centroid = f.centroid;
        hit.features = { std::log2 (juce::jmax (40.0f, f.centroid)), f.flatness * 4.0f, f.lowRatio * 3.0f,
                         std::log2 ((float) len / (float) sampleRate + 0.05f), f.tonalness * 2.0f };
        hit.quality = attackPeak / globalPeak + 0.3f * strength / juce::jmax (1.0f, *std::max_element (env.flux.begin(), env.flux.end()));
        hits.push_back (hit);
    }

    if (hits.empty())
    {
        // no clear hits: take the loudest half second
        Hit hit;
        const int win = juce::jmin (length, (int) (0.5 * sampleRate));
        int best = 0;
        float bestPeak = -1.0f;
        for (int s = 0; s + win <= length; s += win / 4 + 1)
        {
            const float p = monoBuffer.getMagnitude (0, s, win);
            if (p > bestPeak) { bestPeak = p; best = s; }
        }
        hit.start = best;
        hit.end = best + win;
        hits.push_back (hit);
    }

    // z-score features, then farthest-point selection seeded by the best hit
    std::array<float, 5> mean {}, deviation {};
    for (const auto& h : hits)
        for (size_t k = 0; k < mean.size(); ++k)
            mean[k] += h.features[k] / (float) hits.size();
    for (const auto& h : hits)
        for (size_t k = 0; k < mean.size(); ++k)
            deviation[k] += juce::square (h.features[k] - mean[k]) / (float) hits.size();
    for (auto& d : deviation)
        d = juce::jmax (1.0e-3f, std::sqrt (d));

    auto distance = [&] (const Hit& a, const Hit& b)
    {
        float d = 0.0f;
        for (size_t k = 0; k < mean.size(); ++k)
            d += juce::square ((a.features[k] - b.features[k]) / deviation[k]);
        return std::sqrt (d);
    };

    std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.quality > b.quality; });
    if (hits.size() > 64)
        hits.resize (64);

    std::vector<size_t> chosen { 0 };
    while ((int) chosen.size() < maxShots && chosen.size() < hits.size())
    {
        size_t best = 0;
        float bestScore = -1.0f;
        for (size_t i = 0; i < hits.size(); ++i)
        {
            if (std::find (chosen.begin(), chosen.end(), i) != chosen.end())
                continue;
            float nearest = 1.0e9f;
            for (auto c : chosen)
                nearest = juce::jmin (nearest, distance (hits[i], hits[c]));
            const float score = nearest + 0.8f * hits[i].quality;
            if (score > bestScore) { bestScore = score; best = i; }
        }
        chosen.push_back (best);
    }

    std::sort (chosen.begin(), chosen.end(), [&] (size_t a, size_t b) { return hits[a].centroid < hits[b].centroid; });

    for (auto index : chosen)
    {
        const auto& hit = hits[index];
        const int len = hit.end - hit.start;
        juce::AudioBuffer<float> shot (source.getNumChannels(), len);
        for (int ch = 0; ch < source.getNumChannels(); ++ch)
            shot.copyFrom (ch, 0, source, ch, hit.start, len);

        audio::fadeIn (shot, 0, juce::jmax (8, (int) (0.001 * sampleRate)));
        audio::fadeOut (shot, len, juce::jlimit (16, (int) (0.03 * sampleRate), len / 5));
        audio::normalise (shot, 0.944f);   // -0.5 dBFS
        shots.push_back (std::move (shot));
    }

    return shots;
}

} // namespace digga::oneshots
