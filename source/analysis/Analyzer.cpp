#include "analysis/Analyzer.h"

#include <juce_dsp/juce_dsp.h>

namespace digga::analysis
{

namespace
{
    constexpr double minBpm = 55.0, maxBpm = 210.0;

    /** Onset strength with the local mean removed (keeps only the peaks). */
    std::vector<float> emphasise (const std::vector<float>& flux, double fps)
    {
        const int n = (int) flux.size();
        const int half = juce::jmax (1, (int) (0.2 * fps));
        std::vector<float> out ((size_t) n, 0.0f);
        double running = 0.0;
        std::vector<double> prefix ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i)
            prefix[(size_t) i + 1] = prefix[(size_t) i] + flux[(size_t) i];

        float maxValue = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const int a = juce::jmax (0, i - half), b = juce::jmin (n, i + half + 1);
            running = (prefix[(size_t) b] - prefix[(size_t) a]) / (b - a);
            out[(size_t) i] = juce::jmax (0.0f, flux[(size_t) i] - (float) running);
            maxValue = juce::jmax (maxValue, out[(size_t) i]);
        }
        if (maxValue > 0.0f)
            for (auto& v : out)
                v /= maxValue;
        return out;
    }

    std::vector<float> autocorrelation (const std::vector<float>& e, int maxLag)
    {
        const int n = (int) e.size();
        std::vector<float> acf ((size_t) maxLag + 2, 0.0f);
        for (int lag = 1; lag <= maxLag + 1 && lag < n; ++lag)
        {
            double sum = 0.0;
            for (int t = 0; t + lag < n; ++t)
                sum += (double) e[(size_t) t] * e[(size_t) (t + lag)];
            acf[(size_t) lag] = (float) (sum / (n - lag));
        }
        return acf;
    }

    double tempoScore (const std::vector<float>& acf, double lag)
    {
        return sampleAt (acf, lag) + 0.5 * sampleAt (acf, 2.0 * lag) + 0.35 * sampleAt (acf, 4.0 * lag);
    }

    double tempoPrior (double bpm)
    {
        // gently prefers the trap range; x2 / /2 in the UI fix the rest
        const double octaves = std::log2 (bpm / 125.0) / 0.9;
        return std::exp (-0.5 * octaves * octaves);
    }

    double estimateTempo (const OnsetEnvelope& onsets)
    {
        const double fps = onsets.framesPerSecond();
        const auto e = emphasise (onsets.flux, fps);
        const int maxLag = (int) std::ceil (4.0 * 60.0 * fps / minBpm);
        const auto acf = autocorrelation (e, juce::jmin (maxLag, (int) e.size() - 2));

        double bestBpm = 120.0, bestScore = -1.0;
        for (double bpm = minBpm; bpm <= maxBpm; bpm += 0.25)
        {
            const double score = tempoScore (acf, 60.0 * fps / bpm) * tempoPrior (bpm);
            if (score > bestScore)
            {
                bestScore = score;
                bestBpm = bpm;
            }
        }

        // refine: the beat grid that lines up with the most onsets over the whole file
        double refined = bestBpm;
        bestScore = -1.0;
        const int n = (int) e.size();
        for (double bpm = bestBpm * 0.985; bpm <= bestBpm * 1.015; bpm += 0.01)
        {
            const double period = 60.0 * fps / bpm;
            double score = 0.0;
            for (double phase = 0.0; phase < period; phase += 0.5)
            {
                double sum = 0.0;
                for (double t = phase; t < n; t += period)
                    sum += sampleAt (e, t);
                score = juce::jmax (score, sum);
            }
            if (score > bestScore)
            {
                bestScore = score;
                refined = bpm;
            }
        }

        // productions almost always sit on whole BPM values
        const double rounded = std::round (refined);
        return std::abs (refined - rounded) < 0.35 ? rounded : std::round (refined * 10.0) / 10.0;
    }

    /** Moves a grid time onto the start of the actual attack nearby
        (-60 .. +30 ms: the spectral-flux grid tends to run late). */
    double snapToAttack (const float* mono, int numSamples, double sampleRate, double seconds)
    {
        const int block = juce::jmax (8, (int) (0.001 * sampleRate));
        const int centre = (int) (seconds * sampleRate);
        const int first = juce::jmax (0, centre - (int) (0.06 * sampleRate));
        const int count = juce::jmax (0, juce::jmin (numSamples - first, centre + (int) (0.03 * sampleRate) - first) / block);
        if (count < 3)
            return seconds;

        std::vector<float> env ((size_t) count);
        for (int b = 0; b < count; ++b)
        {
            double sum = 0.0;
            for (int i = 0; i < block; ++i)
                sum += (double) mono[first + b * block + i] * mono[first + b * block + i];
            env[(size_t) b] = (float) std::sqrt (sum / block);
        }

        // first block reaching 30% of the loudest one (low kicks make 1 ms RMS
        // wobble, so the steepest step is not reliable) ...
        const float loudest = *std::max_element (env.begin(), env.end());
        if (loudest <= 1.0e-6f)
            return seconds;
        int start = 0;
        while (start < count - 1 && env[(size_t) start] < 0.3f * loudest)
            ++start;

        // ... then back to where that rise begins
        while (start > 0 && env[(size_t) start - 1] < env[(size_t) start] && env[(size_t) start - 1] > 0.02f * loudest)
            --start;

        return (double) (first + start * block) / sampleRate;
    }

    int estimateKey (const float* mono, int numSamples, double sampleRate, bool& minor)
    {
        constexpr int order = 14, size = 1 << order;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window ((size_t) size, juce::dsp::WindowingFunction<float>::hann, false);
        std::vector<float> data ((size_t) size * 2);
        std::array<double, 12> chroma {}, bass {};

        const int lo = juce::jmax (2, (int) (50.0 * size / sampleRate));
        const int hi = juce::jmin (size / 2 - 2, (int) (2100.0 * size / sampleRate));

        struct Peak { double freq; float mag; };
        std::vector<Peak> peaks;

        auto pitchClass = [] (double f) { return ((int) std::lround (12.0 * std::log2 (f / 440.0) + 69.0) % 12 + 12) % 12; };

        for (int start = 0; start + size / 4 < numSamples; start += size / 2)
        {
            std::fill (data.begin(), data.end(), 0.0f);
            const int available = juce::jmin (size, numSamples - start);
            std::copy (mono + start, mono + start + available, data.begin());
            window.multiplyWithWindowingTable (data.data(), (size_t) size);
            fft.performFrequencyOnlyForwardTransform (data.data(), true);

            float frameMax = 0.0f;
            for (int k = lo; k <= hi; ++k)
                frameMax = juce::jmax (frameMax, data[(size_t) k]);
            if (frameMax <= 0.0f)
                continue;

            peaks.clear();
            for (int k = lo; k <= hi; ++k)
            {
                const float m = data[(size_t) k], left = data[(size_t) k - 1], right = data[(size_t) k + 1];
                if (m <= left || m < right || m < 0.04f * frameMax)
                    continue;
                const float denom = left - 2.0f * m + right;
                const double offset = std::abs (denom) > 1.0e-9f ? 0.5 * (left - right) / denom : 0.0;
                peaks.push_back ({ (k + offset) * sampleRate / size, m });
            }

            for (const auto& p : peaks)
            {
                // an overtone of a stronger lower peak mostly repeats that note (octave) or adds a fake fifth
                float weight = 1.0f;
                for (const auto& q : peaks)
                {
                    if (q.freq >= p.freq)
                        break;
                    for (int h : { 2, 3, 4, 5 })
                        if (std::abs (q.freq * h - p.freq) < p.freq * 0.015 && q.mag > 0.3f * p.mag)
                            weight = juce::jmin (weight, 0.2f);
                }

                const double w = weight * std::sqrt ((double) p.mag);
                chroma[(size_t) pitchClass (p.freq)] += w;
                if (p.freq < 200.0)
                    bass[(size_t) pitchClass (p.freq)] += w;
            }
        }


        // Albrecht & Shanahan (2013) key profiles
        static constexpr double major[12] = { 0.238, 0.006, 0.111, 0.006, 0.137, 0.094, 0.016, 0.214, 0.009, 0.080, 0.008, 0.081 };
        static constexpr double minorProfile[12] = { 0.220, 0.006, 0.104, 0.123, 0.019, 0.103, 0.012, 0.214, 0.062, 0.022, 0.061, 0.052 };

        auto correlate = [&chroma] (const double* profile, int root)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) ((i + root) % 12)]; my += profile[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double x = chroma[(size_t) ((i + root) % 12)] - mx, y = profile[i] - my;
                sxy += x * y; sxx += x * x; syy += y * y;
            }
            return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
        };

        double best = -2.0;
        int bestRoot = 9;
        minor = true;
        for (int root = 0; root < 12; ++root)
        {
            const double maj = correlate (major, root), min = correlate (minorProfile, root) + 0.03; // trap leans minor
            if (maj > best) { best = maj; bestRoot = root; minor = false; }
            if (min > best) { best = min; bestRoot = root; minor = true; }
        }

        // relative major / minor share their notes: the bass (808) decides
        const int minorRoot = minor ? bestRoot : (bestRoot + 9) % 12;
        const int majorRoot = (minorRoot + 3) % 12;
        minor = bass[(size_t) minorRoot] >= 0.7 * bass[(size_t) majorRoot];
        return minor ? minorRoot : majorRoot;
    }
}

juce::String keyToString (int root, bool minor)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((root % 12) + 12) % 12]) + (minor ? "m" : "");
}

double findDownbeat (const OnsetEnvelope& onsets, const float* mono, int numSamples, double bpm)
{
    if (bpm <= 0.0 || onsets.flux.empty())
        return 0.0;

    const double fps = onsets.framesPerSecond();
    const double period = 60.0 * fps / bpm;
    const auto e = emphasise (onsets.flux, fps);
    const auto low = emphasise (onsets.lowFlux, fps);
    const int n = (int) e.size();

    // beat phase
    double bestPhase = 0.0, bestScore = -1.0;
    for (double phase = 0.0; phase < period; phase += 0.25)
    {
        double score = 0.0;
        for (double t = phase; t < n; t += period)
            score += sampleAt (e, t);
        if (score > bestScore)
        {
            bestScore = score;
            bestPhase = phase;
        }
    }

    // which of the four beats starts the bar: kicks land on the one
    int bestBeat = 0;
    bestScore = -1.0;
    for (int beat = 0; beat < 4; ++beat)
    {
        double score = 0.0;
        for (double t = bestPhase + beat * period; t < n; t += 4.0 * period)
            score += sampleAt (low, t) + 0.35 * sampleAt (e, t);
        if (score > bestScore)
        {
            bestScore = score;
            bestBeat = beat;
        }
    }

    const double gridStart = onsets.frameToSeconds (bestPhase + bestBeat * period);
    const double bar = 240.0 / bpm;
    const double length = numSamples / onsets.sampleRate;

    // align the grid with the real attacks: median offset over up to 32 bars
    std::vector<double> offsets;
    for (double t = gridStart; t < length - 0.05 && offsets.size() < 32; t += bar)
        offsets.push_back (snapToAttack (mono, numSamples, onsets.sampleRate, t) - t);

    double offset = 0.0;
    if (! offsets.empty())
    {
        std::nth_element (offsets.begin(), offsets.begin() + (long) offsets.size() / 2, offsets.end());
        offset = offsets[offsets.size() / 2];
    }

    const double seconds = juce::jmax (0.0, gridStart + offset);
    return std::fmod (seconds, bar);
}

Analysis analyse (const float* mono, int numSamples, double sampleRate)
{
    Analysis a;
    auto onsets = std::make_shared<OnsetEnvelope> (computeOnsets (mono, numSamples, sampleRate));

    a.detectedBpm = onsets->flux.size() > 16 ? estimateTempo (*onsets) : 120.0;
    a.bpm = a.detectedBpm;
    a.downbeatSeconds = findDownbeat (*onsets, mono, numSamples, a.bpm);
    a.keyRoot = estimateKey (mono, numSamples, sampleRate, a.minor);
    a.keyName = keyToString (a.keyRoot, a.minor);
    a.onsets = std::move (onsets);
    return a;
}

} // namespace digga::analysis
