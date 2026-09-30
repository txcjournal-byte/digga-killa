#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace digga::kill
{

struct Context
{
    bool isLoop = true;
    double sampleRate = 44100.0;
    double bpm = 140.0;       // tempo the audio is at (loops: project tempo)
    int keyRoot = 0;
    bool minor = true;
};

/** One KILL variation. A random mix of transformations (bar/beat
    re-ordering, reversed slices, transposition by steps of the sample's
    scale, halftime, stutters, filter sweeps and note drop-outs); `strength`
    (0 = SOFT, 1 = BRUTAL) decides how many and how hard. Fully determined by
    (parent, context, strength, seed). Loops keep their exact length. */
juce::AudioBuffer<float> makeVariation (const juce::AudioBuffer<float>& parent, const Context& context,
                                        float strength, juce::uint32 seed);

} // namespace digga::kill
