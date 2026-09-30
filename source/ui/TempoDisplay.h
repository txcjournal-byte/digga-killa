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
class TempoDisplay : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    TempoDisplay();
    ~TempoDisplay() override;

    void setSample (double bpm, const juce::String& key);
    void setProjectBpm (double bpm);

    /** Called with a typed-in sample tempo. */
    std::function<void (double)> onBpmTyped;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Bounds of the text, in the parent's coordinate space (design pixels). */
    static inline const juce::Rectangle<int> textBounds { 1030, 50, 214, 26 };

    SkinButton doubleButton { "x2" }, halveButton { "/2" };

private:
    void finishEditing (bool commit);

    juce::String sampleText, projectText;
    double sampleBpm = 0.0;
    std::unique_ptr<juce::TextEditor> editor;
};

} // namespace digga
