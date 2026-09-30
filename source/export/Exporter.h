#pragma once

#include "core/AudioTools.h"
#include "dsp/FxChain.h"

#include <juce_core/juce_core.h>

namespace digga::exporter
{

struct Request
{
    AudioPtr audio;
    bool isLoop = true;
    bool reverse = false;
    dsp::FxParams fx;
    double sampleRate = 44100.0;
    juce::String fileName;       // without extension
};

/** Renders the clip exactly as it plays (REVERSE + effects) into a 24-bit
    WAV in the temp folder and returns the file. Loops stay exactly loop-long:
    effect tails wrap around into the start. */
juce::File renderToTempFile (const Request& request);

/** Offline render (shared by export and tests). */
juce::AudioBuffer<float> render (const Request& request);

/** "DiggaKilla_A2_KillMix3_140bpm_F#m" */
juce::String makeFileName (const juce::String& tag, double bpm, const juce::String& key);

juce::File getExportFolder();

} // namespace digga::exporter
