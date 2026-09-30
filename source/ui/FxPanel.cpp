#include "ui/FxPanel.h"

#include "PluginProcessor.h"

namespace digga
{

FxPanel::FxPanel (juce::AudioProcessorValueTreeState& parameters)
{
    const std::array<const char*, numKnobs> ids { ParamIDs::reverb, ParamIDs::delay, ParamIDs::distortion,
                                                  ParamIDs::filter, ParamIDs::pitch, ParamIDs::mix };
    knobLabels = { "REVERB", "DELAY", "DISTORTION", "FILTER", "PITCH", "MIX" };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.setName (knobLabels[i]);
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

void FxPanel::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto bounds = getLocalBounds();

    g.setColour (ink);
    g.setFont (mono (17.0f, true));

    for (size_t i = 0; i < knobs.size(); ++i)
        g.drawText (knobLabels[i], knobs[i].getBounds().withY (knobs[i].getBottom() + 2).withHeight (24)
                                                        .expanded (20, 0),
                    juce::Justification::centred, false);

    // dividers
    g.setColour (ink.withAlpha (0.85f));
    g.fillRect (reverseArea.getX() - 1, bounds.getY() + 22, 2, bounds.getHeight() - 44);
    g.fillRect (strengthArea.getX() - 1, bounds.getY() + 22, 2, bounds.getHeight() - 44);

    g.drawText ("REVERSE", reverseArea.withHeight (40).withY (bounds.getY() + 26),
                juce::Justification::centred, false);
    g.drawText ("KILL STRENGTH", strengthArea.withHeight (40).withY (bounds.getY() + 26),
                juce::Justification::centred, false);

    g.setFont (mono (14.0f, true));
    const auto scale = strengthArea.reduced (22, 0).withY (killStrength.getBottom()).withHeight (20);
    g.drawText ("SOFT", scale, juce::Justification::centredLeft, false);
    g.drawText ("BRUTAL", scale, juce::Justification::centredRight, false);
}

void FxPanel::resized()
{
    auto area = getLocalBounds();
    strengthArea = area.removeFromRight (232);
    reverseArea = area.removeFromRight (126);
    knobsArea = area;

    const int slot = knobsArea.getWidth() / numKnobs;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto cell = knobsArea.withWidth (slot).withX (knobsArea.getX() + slot * (int) i);
        knobs[i].setBounds (cell.withSizeKeepingCentre (112, 112).withY (area.getY() + 12));
    }

    reverse.setBounds (reverseArea.withSizeKeepingCentre (60, 32).withY (area.getY() + 78));
    killStrength.setBounds (strengthArea.reduced (22, 0).withHeight (30).withY (area.getY() + 78));
}

} // namespace digga
