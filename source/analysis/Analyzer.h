#pragma once

#include "analysis/Features.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace digga::analysis
{

struct Analysis
{
    double detectedBpm = 0.0;    // what the analysis found
    double bpm = 0.0;            // in use (detected or user override)
    double downbeatSeconds = 0.0; // first bar start, within the first bar
    int keyRoot = 0;             // 0 = C
    bool minor = true;
    juce::String keyName;        // "F#m"
    std::shared_ptr<const OnsetEnvelope> onsets;

    double barSeconds() const noexcept { return bpm > 0.0 ? 240.0 / bpm : 0.0; }
};

/** Tempo, bar grid and key of a (mono) signal. */
Analysis analyse (const float* mono, int numSamples, double sampleRate);

/** Re-derives the bar grid for a given tempo (used by x2 / /2 / manual BPM). */
double findDownbeat (const OnsetEnvelope& onsets, const float* mono, int numSamples, double bpm);

juce::String keyToString (int root, bool minor);

} // namespace digga::analysis
