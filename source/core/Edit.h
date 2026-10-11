#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace digga
{

/** What the editor does to one sample (non-destructive). */
struct EditSettings
{
    double start = 0.0, end = 1.0;   // crop, fractions of the original
    float fadeInMs = 0.0f, fadeOutMs = 0.0f;
    float gainDb = 0.0f;
    float pitch = 0.0f;              // semitones, length kept
    bool reverse = false;

    bool isDefault() const noexcept;
    juce::ValueTree toValueTree (const juce::String& key) const;
    static EditSettings fromValueTree (const juce::ValueTree& tree);

    bool operator== (const EditSettings& other) const noexcept;
    bool operator!= (const EditSettings& other) const noexcept { return ! (*this == other); }
};

/** Applies the settings to the original audio (crop -> pitch -> reverse ->
    fades -> gain). Offline; may take a moment when pitch is used. */
juce::AudioBuffer<float> renderEdit (const juce::AudioBuffer<float>& original, const EditSettings& edit, double sampleRate);

} // namespace digga
