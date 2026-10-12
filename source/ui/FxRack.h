#pragma once

#include "fx/FxChain.h"
#include "ui/Theme.h"

namespace digga
{

/** The effect strip of the editor: a few fun, simple modules, left to right
    HALFTIME (speed pills), REPEAT (stutter grid pills), FILTER (one bipolar
    knob: low-pass left, high-pass right), DELAY (mix + rate) and REVERB (mix),
    plus KILL (random rack) and RESET. Reports a new FxSettings when a
    gesture ends. */
class FxRack : public juce::Component
{
public:
    FxRack();
    ~FxRack() override;

    void setSettings (const fx::FxSettings& settings);   // does not call onChange
    const fx::FxSettings& getSettings() const noexcept { return settings; }
    void setEnabled (bool enabled, const juce::String& message = {});

    std::function<void (const fx::FxSettings&)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Parts;   // the controls (FxRack.cpp)

    void commit();  // end of a gesture: report the settings
    void refresh(); // controls <- settings

    fx::FxSettings settings;
    bool enabled = false;
    juce::String message;
    std::unique_ptr<Parts> parts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxRack)
};

} // namespace digga
