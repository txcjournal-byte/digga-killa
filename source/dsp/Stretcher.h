#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace digga::dsp
{

/** Offline time-stretch and/or pitch-shift (signalsmith-stretch, MIT).
    Output has exactly `outputLength` samples; semitones changes pitch
    independently of length. Deterministic. */
juce::AudioBuffer<float> stretch (const juce::AudioBuffer<float>& input, int outputLength,
                                  double semitones, double sampleRate);

inline juce::AudioBuffer<float> pitchShift (const juce::AudioBuffer<float>& input, double semitones, double sampleRate)
{
    return stretch (input, input.getNumSamples(), semitones, sampleRate);
}

} // namespace digga::dsp
