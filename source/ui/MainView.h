#pragma once

#include "ui/FxPanel.h"
#include "ui/Header.h"
#include "ui/PaperBackground.h"
#include "ui/RecordLabel.h"
#include "ui/TrackColumn.h"

namespace digga
{

class DiggaKillaProcessor;

/** The whole UI laid out at a fixed logical size (that of design.png); the
    editor scales it with a transform, so everything stays vector-sharp. */
class MainView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    static constexpr int logicalWidth = 1344;
    static constexpr int logicalHeight = 896;

    explicit MainView (DiggaKillaProcessor& processor);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshSampleStatus();

    DiggaKillaProcessor& processor;
    juce::SharedResourcePointer<theme::Typefaces> typefaces;
    PaperBackground paper;

    Header header;
    TrackColumn loops, shots;
    RecordLabel record;
    FxPanel fx;
    juce::TooltipWindow tooltips { this, 600 };
};

} // namespace digga
