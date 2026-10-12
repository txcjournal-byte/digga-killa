#pragma once

#include "analysis/Structure.h"
#include "core/AudioTools.h"

#include <functional>
#include <vector>

namespace digga::loops
{

/** Where a loop comes from: the bars of the source it plays, in order. The
    same plan renders the full mix or any stem mix. */
struct LoopPlan
{
    juce::String name;          // section it starts: "Kick In", "Drop", ...
    int bars = 8;
    int startBar = 0;           // bar of the source where it starts
    double sourceSeconds = 0.0; // where that is in the source
    std::vector<int> sourceBars;                  // which bars of the source, in order
    std::vector<std::pair<int, int>> barRanges;   // their [start, end) in samples
};

/** Breaks the whole song into 8- and 16-bar loops that start exactly where its
    sections start (kick comes in, melody starts, drop, break...). Repeats of
    the same part (second chorus) are skipped. Short material (a loop, not a
    song) gets its best-sounding 8- and 16-bar phrase. */
std::vector<LoopPlan> planSectionLoops (const juce::AudioBuffer<float>& source, double sampleRate,
                                        const analysis::Analysis& analysis,
                                        const analysis::Structure& structure);

/** Returns `length` samples of the (mixed) source starting at `start`. */
using RegionSource = std::function<juce::AudioBuffer<float> (int start, int length)>;

/** Renders a plan: bars joined (crossfading jumps), stretched to the project
    tempo and crossfaded so the loop repeats seamlessly, normalised to -1 dBFS
    (silence stays silent). */
juce::AudioBuffer<float> renderLoop (const LoopPlan& plan, const RegionSource& source, int sourceLength,
                                     double sampleRate, double sourceBpm, double projectBpm);

/** renderLoop on a buffer. */
juce::AudioBuffer<float> renderLoop (const LoopPlan& plan, const juce::AudioBuffer<float>& source,
                                     double sampleRate, double sourceBpm, double projectBpm);

/** Samples in `bars` bars at the given tempo. */
inline int barsToSamples (int bars, double bpm, double sampleRate)
{
    return (int) std::lround (bars * 240.0 / bpm * sampleRate);
}

} // namespace digga::loops
