#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include "fx/FxChain.h"

#include <array>

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
    /** Loops: level of each stem (vocals, drums, bass, melody), 0..1;
        negative = as generated. */
    std::array<float, 4> stems { -1.0f, -1.0f, -1.0f, -1.0f };
    /** Loops: where playback starts inside the crop, 0..1 (the audio wraps). */
    double loopStart = 0.0;
    /** Effect rack (after crop / pitch / reverse). */
    fx::FxSettings fx;

    bool hasStemMix() const noexcept { return stems[0] >= 0.0f; }
    bool isDefault() const noexcept;
    juce::ValueTree toValueTree (const juce::String& key) const;
    static EditSettings fromValueTree (const juce::ValueTree& tree);

    bool operator== (const EditSettings& other) const noexcept;
    bool operator!= (const EditSettings& other) const noexcept { return ! (*this == other); }
};

/** Applies the settings to the original audio (crop -> loop start -> pitch ->
    reverse -> effects -> fades -> gain). Offline; may take a moment with
    pitch or effects. `bpm` syncs the effects; loops keep their length and
    stay seamless. withFx = false renders the same without the effect rack. */
juce::AudioBuffer<float> renderEdit (const juce::AudioBuffer<float>& original, const EditSettings& edit, double sampleRate,
                                     double bpm = 0.0, bool isLoop = false, bool withFx = true);

} // namespace digga
