#include "ui/TempoDisplay.h"

namespace digga
{

namespace
{
    juce::String bpmText (double bpm)
    {
        if (bpm <= 0.0)
            return "---";
        return juce::String (bpm, juce::exactlyEqual (bpm, std::round (bpm)) ? 0 : 1);
    }
}

void SkinButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    if (! isEnabled() || ! (highlighted || down))
        return;

    g.setColour (theme::ink.withAlpha (down ? 0.16f : 0.08f));
    g.fillRect (getLocalBounds().reduced (2));
}

TempoDisplay::TempoDisplay()
{
    setInterceptsMouseClicks (false, false);
    doubleButton.setTooltip ("Detected tempo x2");
    halveButton.setTooltip ("Detected tempo / 2");
    setSample (0.0, {});
    setProjectBpm (0.0);
}

void TempoDisplay::setSample (double bpm, const juce::String& key)
{
    auto text = "SAMPLE " + bpmText (bpm) + " BPM";
    if (key.isNotEmpty())
        text << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << key;

    if (text != sampleText)
    {
        sampleText = text;
        repaint();
    }
}

void TempoDisplay::setProjectBpm (double bpm)
{
    const auto text = "PROJECT " + bpmText (bpm) + " BPM";
    if (text != projectText)
    {
        projectText = text;
        repaint();
    }
}

void TempoDisplay::paint (juce::Graphics& g)
{
    const auto font = theme::condensed (16.5f);
    const juce::String arrow (juce::CharPointer_UTF8 (" \xe2\x86\x92 "));
    const auto full = sampleText + arrow + projectText;

    g.setFont (font);
    g.setColour (theme::ink);
    g.drawFittedText (full, getLocalBounds(), juce::Justification::centredRight, 1, 0.8f);
}

} // namespace digga
