#pragma once

#include "PluginProcessor.h"
#include "ui/MainView.h"
#include "ui/TrapWinLookAndFeel.h"

namespace digga
{

class TrapWinEditor : public juce::AudioProcessorEditor
{
public:
    explicit TrapWinEditor (TrapWinProcessor&);
    ~TrapWinEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

private:
    static int initialWidth (int savedWidth);
    void fitToScreen();

    TrapWinProcessor& processor;
    bool sizeReady = false;
    bool fittedToScreen = false;
    TrapWinLookAndFeel lookAndFeel;
    MainView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapWinEditor)
};

} // namespace digga
