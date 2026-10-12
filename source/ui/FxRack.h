#pragma once

#include "fx/FxChain.h"
#include "ui/Theme.h"

namespace digga
{

/** The effect rack card of the editor: one module per effect (on/off +
    its knobs), the gate step sequencer and the KILL button (random rack).
    Reports a new FxSettings when a gesture ends. */
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
    fx::FxSettings settings;
    bool enabled = false;
    juce::String message;
};

} // namespace digga
