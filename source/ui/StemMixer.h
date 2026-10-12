#pragma once

#include "ui/Theme.h"

#include <array>

namespace digga
{

/** Stem mixer for the selected loop: one fader per stem (vocals, drums,
    808 / bass, melody) with mute and solo. Changes are reported when a
    gesture ends (the loop is re-rendered from the stems). */
class StemMixer : public juce::Component
{
public:
    StemMixer();

    void setState (bool enabled, const std::array<float, 4>& levels, const std::array<bool, 4>& present,
                   const juce::String& message);

    std::function<void (const std::array<float, 4>&)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    struct Strip { juce::Rectangle<float> label, fader, mute, solo; };
    Strip strip (int index) const;
    int stripAt (juce::Point<float> p) const;
    void setFromPosition (int index, float y);
    void commit();

    bool enabled = false;
    std::array<float, 4> levels { 1.0f, 1.0f, 1.0f, 1.0f }, beforeMute { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<bool, 4> present {};
    juce::String message;
    int dragging = -1;
};

} // namespace digga
