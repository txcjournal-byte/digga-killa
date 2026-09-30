#pragma once

#include "ui/Theme.h"

namespace digga
{

/** "SAMPLE 94 BPM · F#m → PROJECT 140 BPM" plus the ×2 / ÷2 correction buttons. */
class TempoDisplay : public juce::Component
{
public:
    TempoDisplay();

    void setSampleText (const juce::String& text);
    void setProjectBpm (double bpm);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::TextButton doubleButton, halveButton;

private:
    juce::String sampleText { "SAMPLE --- BPM" };
    juce::String projectText { "PROJECT --- BPM" };
};

/** Top strip: TrapVST wordmark, big DIGGA KILLA title, tempo readout. */
class Header : public juce::Component
{
public:
    Header();

    void paint (juce::Graphics&) override;
    void resized() override;

    TempoDisplay tempo;

private:
    juce::Rectangle<float> logoArea, titleArea;
};

} // namespace digga
