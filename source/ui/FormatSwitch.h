#pragma once

#include "ui/Theme.h"

namespace digga
{

/** "DRAG AS  [WAV|MIDI]" — chooses what dragging a row into the DAW produces.
    Drawn like the printed x2 / /2 boxes. */
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
    juce::Rectangle<float> segment (bool midiSegment) const;
    bool midi = false;
};

} // namespace digga
