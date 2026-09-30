#pragma once

#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace digga
{

/** Bottom strip: six FX knobs, REVERSE switch and KILL STRENGTH slider. */
class FxPanel : public juce::Component
{
public:
    explicit FxPanel (juce::AudioProcessorValueTreeState& parameters);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    static constexpr int numKnobs = 6;
    std::array<juce::Slider, numKnobs> knobs;
    std::array<std::unique_ptr<SliderAttachment>, numKnobs> knobAttachments;
    std::array<juce::String, numKnobs> knobLabels;

    juce::ToggleButton reverse;
    std::unique_ptr<ButtonAttachment> reverseAttachment;

    juce::Slider killStrength;
    std::unique_ptr<SliderAttachment> killStrengthAttachment;

    juce::Rectangle<int> knobsArea, reverseArea, strengthArea;
};

} // namespace digga
