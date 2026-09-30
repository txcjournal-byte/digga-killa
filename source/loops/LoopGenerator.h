#pragma once

#include "analysis/Analyzer.h"
#include "core/AudioTools.h"

#include <vector>

namespace digga::loops
{

struct GeneratedLoop
{
    juce::String slot;          // "A1", "A2", "B1", "B2"
    int bars = 8;
    std::vector<int> sourceBars; // which bars of the source it was built from
    juce::AudioBuffer<float> audio;   // at project tempo, loop-crossfaded
};

/** Splits the source into bars, scores every candidate section (energy,
    melodic content, and above all how well its end runs into its start) and
    renders the four best as tempo-matched, seamless loops:
    A1 = 8 bars, A2 = 16 bars, B1 = 8 bars, B2 = 16 bars. */
std::vector<GeneratedLoop> generate (const juce::AudioBuffer<float>& source, double sampleRate,
                                     const analysis::Analysis& analysis, double projectBpm);

/** Samples in `bars` bars at the given tempo. */
inline int barsToSamples (int bars, double bpm, double sampleRate)
{
    return (int) std::lround (bars * 240.0 / bpm * sampleRate);
}

} // namespace digga::loops
