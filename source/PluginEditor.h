#pragma once

#include "PluginProcessor.h"
#include "ui/DiggaLookAndFeel.h"
#include "ui/MainView.h"

namespace digga
{

class DiggaKillaEditor : public juce::AudioProcessorEditor
{
public:
    explicit DiggaKillaEditor (DiggaKillaProcessor&);
    ~DiggaKillaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DiggaLookAndFeel lookAndFeel;
    MainView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiggaKillaEditor)
};

} // namespace digga
