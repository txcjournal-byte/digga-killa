#pragma once

#include "analysis/Structure.h"
#include "core/AudioTools.h"

#include <vector>

namespace digga::loops
{

struct GeneratedLoop
{
    juce::String name;          // section it starts: "Kick In", "Drop", ...
    int bars = 8;
    int startBar = 0;           // bar of the source where it starts
    double sourceSeconds = 0.0; // where that is in the source
    std::vector<int> sourceBars; // which bars of the source it was built from
    juce::AudioBuffer<float> audio;   // at project tempo, loop-crossfaded
};

/** Breaks the whole song into 8- and 16-bar loops that start exactly where its
    sections start (kick comes in, melody starts, drop, break...). Repeats of
    the same part (second chorus) are skipped. Loops are stretched to the
    project tempo and crossfaded so they repeat seamlessly. */
std::vector<GeneratedLoop> generateSectionLoops (const juce::AudioBuffer<float>& source, double sampleRate,
                                                 const analysis::Analysis& analysis,
                                                 const analysis::Structure& structure, double projectBpm);

/** Samples in `bars` bars at the given tempo. */
inline int barsToSamples (int bars, double bpm, double sampleRate)
{
    return (int) std::lround (bars * 240.0 / bpm * sampleRate);
}

} // namespace digga::loops
