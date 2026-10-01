#include "ui/FormatSwitch.h"

namespace digga
{

FormatSwitch::FormatSwitch()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Drag rows into the DAW as audio (WAV) or as notes (MIDI)");
}

void FormatSwitch::setMidi (bool shouldBeMidi)
{
    if (midi != shouldBeMidi)
    {
        midi = shouldBeMidi;
        repaint();
    }
}

juce::Rectangle<float> FormatSwitch::segment (bool midiSegment) const
{
    auto boxes = getLocalBounds().toFloat().removeFromRight (108.0f).reduced (0.75f);
    return midiSegment ? boxes.removeFromRight (60.0f) : boxes.removeFromLeft (46.0f);
}

void FormatSwitch::paint (juce::Graphics& g)
{
    using namespace theme;

    g.setFont (condensed (15.0f, true));
    g.setColour (ink);
    g.drawText ("DRAG AS", getLocalBounds().toFloat().withTrimmedRight (114.0f), juce::Justification::centredRight, false);

    for (bool m : { false, true })
    {
        const auto box = segment (m);
        const bool on = m == midi;
        if (on)
        {
            g.setColour (ink);
            g.fillRect (box);
        }
        g.setColour (ink);
        g.drawRect (box, 1.5f);
        g.setColour (on ? paperLight : ink);
        g.setFont (condensed (17.0f, true));
        g.drawText (m ? "MIDI" : "WAV", box, juce::Justification::centred, false);
    }
}

void FormatSwitch::mouseDown (const juce::MouseEvent& e)
{
    for (bool m : { false, true })
    {
        if (segment (m).contains (e.position) && m != midi)
        {
            setMidi (m);
            if (onChange)
                onChange (midi);
        }
    }
}

} // namespace digga
