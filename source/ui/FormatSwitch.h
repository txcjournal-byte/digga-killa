#pragma once

#include "ui/Theme.h"

namespace digga
{

/** "DRAG AS  [ WAV | MIDI ]" segmented pill: what dragging into the DAW produces. */
class FormatSwitch : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    FormatSwitch();

    void setMidi (bool shouldBeMidi);
    bool isMidi() const noexcept { return midi; }

    std::function<void (bool midi)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> pillArea() const;
    juce::Rectangle<float> segment (bool midiSegment) const;
    bool midi = false;
};

} // namespace digga
