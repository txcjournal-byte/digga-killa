#include "ui/FormatSwitch.h"

namespace digga
{

FormatSwitch::FormatSwitch()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Drag samples into your DAW as audio (WAV) or as notes (MIDI)");
}

void FormatSwitch::setMidi (bool shouldBeMidi)
{
    if (midi != shouldBeMidi)
    {
        midi = shouldBeMidi;
        repaint();
    }
}

juce::Rectangle<float> FormatSwitch::pillArea() const
{
    return getLocalBounds().toFloat().removeFromRight (128.0f).reduced (1.0f);
}

juce::Rectangle<float> FormatSwitch::segment (bool midiSegment) const
{
    auto p = pillArea().reduced (3.0f);
    return midiSegment ? p.removeFromRight (p.getWidth() * 0.5f) : p.removeFromLeft (p.getWidth() * 0.5f);
}

void FormatSwitch::paint (juce::Graphics& g)
{
    using namespace theme;
    theme::drawMonoLabel (g, "Drag as", getLocalBounds().toFloat().withTrimmedRight (136.0f), juce::Justification::centredRight, muted, 11.0f);

    const auto p = pillArea();
    g.setColour (well);
    g.fillRoundedRectangle (p, p.getHeight() * 0.5f);
    g.setColour (line);
    g.drawRoundedRectangle (p, p.getHeight() * 0.5f, 1.0f);

    for (bool m : { false, true })
    {
        const auto s = segment (m);
        if (m == midi)
            drawChromePill (g, s, false, false);
        g.setColour (m == midi ? well : text2);
        g.setFont (mono (12.0f, true).withExtraKerningFactor (0.14f));
        g.drawText (m ? "MIDI" : "WAV", s, juce::Justification::centred, false);
    }
}

void FormatSwitch::mouseDown (const juce::MouseEvent& e)
{
    for (bool m : { false, true })
        if (segment (m).contains (e.position) && m != midi)
        {
            setMidi (m);
            if (onChange)
                onChange (midi);
        }
}

} // namespace digga
