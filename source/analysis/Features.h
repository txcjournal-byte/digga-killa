#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

namespace digga::analysis
{

/** Spectral summary of a stretch of audio (used to score loops and to keep
    one-shots diverse). */
struct SpectralFeatures
{
    float rms = 0.0f;
    float centroid = 0.0f;     // Hz
    float flatness = 0.0f;     // 0 = tonal, 1 = noise
    float lowRatio = 0.0f;     // energy share below 150 Hz
    float tonalness = 0.0f;    // chroma peakiness, 0..1
    std::array<float, 12> chroma {};   // sums to 1
};

SpectralFeatures describe (const float* mono, int numSamples, double sampleRate);

/** Spectral-flux onset strength, one value per hop. */
struct OnsetEnvelope
{
    std::vector<float> flux;      // full band
    std::vector<float> lowFlux;   // < 150 Hz (kicks / 808s)
    int hop = 512;
    int frameSize = 2048;
    double sampleRate = 44100.0;

    double framesPerSecond() const noexcept { return sampleRate / hop; }
    double frameToSeconds (double frame) const noexcept { return (frame * hop + frameSize * 0.5) / sampleRate; }
    double secondsToFrame (double seconds) const noexcept { return (seconds * sampleRate - frameSize * 0.5) / hop; }
};

OnsetEnvelope computeOnsets (const float* mono, int numSamples, double sampleRate);

/** Linear interpolation into a vector, 0 outside. */
float sampleAt (const std::vector<float>& v, double index) noexcept;

float cosineSimilarity (const float* a, const float* b, int n) noexcept;

} // namespace digga::analysis
