#include "ui/FxPanel.h"

#include "PluginProcessor.h"

namespace digga
{

FxPanel::FxPanel (juce::AudioProcessorValueTreeState& parameters)
{
    const std::array<const char*, numKnobs> ids { ParamIDs::reverb, ParamIDs::delay, ParamIDs::distortion,
                                                  ParamIDs::filter, ParamIDs::pitch, ParamIDs::mix };
    const std::array<const char*, numKnobs> names { "Reverb", "Delay", "Distortion", "Filter", "Pitch", "Mix" };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.setName (names[i]);
        knob.getProperties().set ("skinned", true);
        knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                  juce::MathConstants<float>::pi * 2.75f, true);
        knob.setPopupDisplayEnabled (true, true, this);
        knob.setMouseDragSensitivity (220);
        addAndMakeVisible (knob);
        knobAttachments[i] = std::make_unique<SliderAttachment> (parameters, ids[i], knob);
    }

    reverse.setTooltip ("Reverse playback and export");
    addAndMakeVisible (reverse);
    reverseAttachment = std::make_unique<ButtonAttachment> (parameters, ParamIDs::reverse, reverse);

    killStrength.setSliderStyle (juce::Slider::LinearHorizontal);
    killStrength.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    killStrength.setPopupDisplayEnabled (true, true, this);
    killStrength.setTooltip ("How hard KILL mangles: SOFT -> BRUTAL");
    addAndMakeVisible (killStrength);
    killStrengthAttachment = std::make_unique<SliderAttachment> (parameters, ParamIDs::killStrength, killStrength);
}

void FxPanel::resized()
{
    // centres of the printed knobs (design pixels, see tools/make_skin.py)
    constexpr std::array<float, numKnobs> knobX { 124.0f, 270.5f, 420.5f, 569.5f, 719.5f, 873.5f };
    constexpr float knobY = 770.0f, knobSize = 100.0f;
    const auto origin = designBounds.getPosition().toFloat();

    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i].setBounds (juce::Rectangle<float> (knobSize, knobSize)
                                .withCentre (juce::Point<float> (knobX[i], knobY) - origin).toNearestInt());

    reverse.setBounds (juce::Rectangle<int> (998, 764, 56, 28) - designBounds.getPosition());
    killStrength.setBounds (juce::Rectangle<int> (1098, 757, 218, 32) - designBounds.getPosition());
}

} // namespace digga
