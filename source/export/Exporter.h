#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace digga::exporter
{

/** Writes a 24-bit WAV into the export folder and returns it (never fails
    on a file a DAW still holds open: picks a new name instead). */
juce::File writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& fileName);

/** "TrapWin_Drop_16bars_140bpm_F#m" */
juce::String makeFileName (const juce::String& tag, double bpm, const juce::String& key);

/** Where dragged files are rendered: <temp>/TrapWin Sampler */
juce::File getExportFolder();

} // namespace digga::exporter
