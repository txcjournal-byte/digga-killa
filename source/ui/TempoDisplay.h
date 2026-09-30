#pragma once

#include "ui/Theme.h"

namespace digga
{

/** Invisible hit area over a button that is printed in the skin; only
    paints a light press / hover tint. */
class SkinButton : public juce::Button
{
public:
    explicit SkinButton (const juce::String& name) : juce::Button (name) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** "SAMPLE 94 BPM · F#m → PROJECT 140 BPM" (the ×2 / ÷2 boxes are printed
    in the skin; SkinButtons sit on top of them). */
class TempoDisplay : public juce::Component
{
public:
    TempoDisplay();

    void setSample (double bpm, const juce::String& key);
    void setProjectBpm (double bpm);

    void paint (juce::Graphics&) override;

    /** Bounds of the text, in the parent's coordinate space (design pixels). */
    static inline const juce::Rectangle<int> textBounds { 1030, 50, 214, 26 };

    SkinButton doubleButton { "x2" }, halveButton { "/2" };

private:
    juce::String sampleText, projectText;
};

} // namespace digga
