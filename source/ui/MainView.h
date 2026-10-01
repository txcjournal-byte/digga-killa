#pragma once

#include "midi/MidiExport.h"
#include "ui/FormatSwitch.h"
#include "ui/FxPanel.h"
#include "ui/RecordLabel.h"
#include "ui/TempoDisplay.h"
#include "ui/TrackColumn.h"

namespace digga
{

class DiggaKillaProcessor;

/** The whole UI at the pixel size of docs/design.png. The static artwork is
    the skin cut from that file; live controls sit on the exact spots they
    occupy in it. The editor scales this view uniformly. */
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
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refreshSampleStatus();
    void refreshResults();
    void wireColumn (TrackColumn& column);
    void togglePreview (int id);
    void showRowMenu (int id);
    void dragOut (int id);
    void prefetchMidi();

    DiggaKillaProcessor& processor;
    juce::SharedResourcePointer<theme::Typefaces> typefaces;
    juce::Image background;

    TempoDisplay tempo;
    FormatSwitch format;
    TrackColumn loops, shots;
    RecordLabel record;
    FxPanel fx;
    juce::TooltipWindow tooltips { this, 600 };
    midi::TranscriptionCache transcriptions;
};

} // namespace digga
