#pragma once

#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace digga
{

/** Bottom strip: six FX knobs, REVERSE switch and KILL STRENGTH slider.
    Knob bodies, scales and labels are printed in the skin; this component
    covers the whole panel and places the live controls on top of them. */
class FxPanel : public juce::Component
{
public:
    explicit FxPanel (juce::AudioProcessorValueTreeState& parameters);

    void resized() override;

    /** Panel position in design pixels. */
    static inline const juce::Rectangle<int> designBounds { 28, 700, 1290, 170 };

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    static constexpr int numKnobs = 6;
    std::array<juce::Slider, numKnobs> knobs;
    std::array<std::unique_ptr<SliderAttachment>, numKnobs> knobAttachments;

    juce::ToggleButton reverse;
    std::unique_ptr<ButtonAttachment> reverseAttachment;

    juce::Slider killStrength;
    std::unique_ptr<SliderAttachment> killStrengthAttachment;
};

} // namespace digga
