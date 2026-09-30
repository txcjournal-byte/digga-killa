#include "analysis/Features.h"

#include <juce_dsp/juce_dsp.h>

namespace digga::analysis
{

namespace
{
    constexpr int fftOrder = 11;
    constexpr int fftSize = 1 << fftOrder;

    struct FftFrames
    {
        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
        std::vector<float> data = std::vector<float> ((size_t) fftSize * 2);

        /** Magnitude spectrum (fftSize / 2 + 1 bins) of the frame starting at `start`. */
        const float* magnitudes (const float* mono, int numSamples, int start)
        {
            std::fill (data.begin(), data.end(), 0.0f);
            const int available = juce::jlimit (0, fftSize, numSamples - start);
            if (available > 0 && start >= 0)
                std::copy (mono + start, mono + start + available, data.begin());
            window.multiplyWithWindowingTable (data.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (data.data(), true);
            return data.data();
        }
    };

    int pitchClassOfBin (int bin, double sampleRate)
    {
        const double f = bin * sampleRate / fftSize;
        return ((int) std::lround (12.0 * std::log2 (f / 440.0) + 69.0) % 12 + 12) % 12;
    }
}

float sampleAt (const std::vector<float>& v, double index) noexcept
{
    if (index < 0.0 || v.empty())
        return 0.0f;
    const auto i0 = (size_t) index;
    if (i0 + 1 >= v.size())
        return i0 < v.size() ? v[i0] : 0.0f;
    const float frac = (float) (index - (double) i0);
    return v[i0] + (v[i0 + 1] - v[i0]) * frac;
}

float cosineSimilarity (const float* a, const float* b, int n) noexcept
{
    double ab = 0, aa = 0, bb = 0;
    for (int i = 0; i < n; ++i)
    {
        ab += (double) a[i] * b[i];
        aa += (double) a[i] * a[i];
        bb += (double) b[i] * b[i];
    }
    return aa > 0 && bb > 0 ? (float) (ab / std::sqrt (aa * bb)) : 0.0f;
}

SpectralFeatures describe (const float* mono, int numSamples, double sampleRate)
{
    SpectralFeatures f;
    if (numSamples <= 0)
        return f;

    double sumSq = 0;
    for (int i = 0; i < numSamples; ++i)
        sumSq += (double) mono[i] * mono[i];
    f.rms = (float) std::sqrt (sumSq / numSamples);

    FftFrames frames;
    const int numBins = fftSize / 2;
    std::vector<double> spectrum ((size_t) numBins, 0.0);
    const int hop = fftSize / 2;
    int count = 0;

    for (int start = 0; start < juce::jmax (1, numSamples - fftSize / 2); start += hop)
    {
        const float* mag = frames.magnitudes (mono, numSamples, start);
        for (int k = 0; k < numBins; ++k)
            spectrum[(size_t) k] += mag[k];
        ++count;
    }

    if (count == 0)
        return f;

    const double binHz = sampleRate / fftSize;
    double total = 0, weighted = 0, low = 0, logSum = 0, energy = 0;
    int flatBins = 0;

    for (int k = 1; k < numBins; ++k)
    {
        const double m = spectrum[(size_t) k] / count;
        const double hz = k * binHz;
        total += m;
        weighted += m * hz;
        energy += m * m;
        if (hz < 150.0)
            low += m * m;
        if (hz > 40.0 && hz < 12000.0)
        {
            logSum += std::log (m + 1.0e-9);
            ++flatBins;
        }
        if (hz > 60.0 && hz < 4000.0)
            f.chroma[(size_t) pitchClassOfBin (k, sampleRate)] += (float) m;
    }

    f.centroid = total > 0 ? (float) (weighted / total) : 0.0f;
    f.lowRatio = energy > 0 ? (float) (low / energy) : 0.0f;

    double linMean = 0;
    for (int k = 1; k < numBins; ++k)
    {
        const double hz = k * binHz;
        if (hz > 40.0 && hz < 12000.0)
            linMean += spectrum[(size_t) k] / count;
    }
    linMean /= juce::jmax (1, flatBins);
    f.flatness = linMean > 0 ? (float) juce::jlimit (0.0, 1.0, std::exp (logSum / juce::jmax (1, flatBins)) / linMean) : 1.0f;

    float chromaSum = 0, chromaMax = 0;
    for (auto c : f.chroma)
    {
        chromaSum += c;
        chromaMax = juce::jmax (chromaMax, c);
    }
    if (chromaSum > 0)
    {
        for (auto& c : f.chroma)
            c /= chromaSum;
        // 1/12 = flat chroma (noise), towards 1 = one clear pitch class
        f.tonalness = juce::jlimit (0.0f, 1.0f, (chromaMax / chromaSum - 1.0f / 12.0f) / (0.5f - 1.0f / 12.0f));
    }

    return f;
}

OnsetEnvelope computeOnsets (const float* mono, int numSamples, double sampleRate)
{
    OnsetEnvelope env;
    env.sampleRate = sampleRate;
    env.frameSize = fftSize;
    env.hop = juce::jmax (128, (int) std::lround (512.0 * sampleRate / 48000.0));

    const int numBins = fftSize / 2;
    const int maxBin = juce::jmin (numBins, (int) (11000.0 * fftSize / sampleRate));
    const int lowBin = juce::jmax (2, (int) (150.0 * fftSize / sampleRate));
    const int numFrames = juce::jmax (0, (numSamples - fftSize) / env.hop + 1);

    env.flux.assign ((size_t) numFrames, 0.0f);
    env.lowFlux.assign ((size_t) numFrames, 0.0f);

    FftFrames frames;
    std::vector<float> previous ((size_t) numBins, 0.0f), current ((size_t) numBins, 0.0f);

    for (int t = 0; t < numFrames; ++t)
    {
        const float* mag = frames.magnitudes (mono, numSamples, t * env.hop);
        float flux = 0.0f, lowFlux = 0.0f;

        for (int k = 1; k < maxBin; ++k)
        {
            current[(size_t) k] = std::log1p (100.0f * mag[k]);
            const float d = current[(size_t) k] - previous[(size_t) k];
            if (d > 0.0f)
            {
                flux += d;
                if (k < lowBin)
                    lowFlux += d;
            }
        }

        if (t > 0)
        {
            env.flux[(size_t) t] = flux;
            env.lowFlux[(size_t) t] = lowFlux;
        }
        std::swap (previous, current);
    }

    return env;
}

} // namespace digga::analysis
