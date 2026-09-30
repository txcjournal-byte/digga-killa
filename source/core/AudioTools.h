#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

namespace digga
{

using AudioPtr = std::shared_ptr<const juce::AudioBuffer<float>>;

namespace audio
{
    /** 0..1 overview for waveform drawing. */
    std::vector<float> computePeaks (const juce::AudioBuffer<float>& buffer, int numPoints);

    juce::AudioBuffer<float> toMono (const juce::AudioBuffer<float>& buffer);

    void fadeIn (juce::AudioBuffer<float>& buffer, int start, int length);
    void fadeOut (juce::AudioBuffer<float>& buffer, int endExclusive, int length);

    /** Scales so the absolute peak equals targetPeak (no-op on silence). */
    void normalise (juce::AudioBuffer<float>& buffer, float targetPeak);

    float peak (const juce::AudioBuffer<float>& buffer);

    /** Equal-power crossfade of `incoming` over the first `length` samples
        of `dest` starting at destStart, fading out what is already there. */
    void crossfadeInto (juce::AudioBuffer<float>& dest, int destStart,
                        const juce::AudioBuffer<float>& incoming, int incomingStart, int length);

    /** Plain varispeed resample (changes pitch and length together). */
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source, int newLength);

    juce::String formatDuration (double seconds);
}

} // namespace digga
