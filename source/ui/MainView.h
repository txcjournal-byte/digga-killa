#pragma once

#include "midi/MidiExport.h"
#include "ui/EditorPanel.h"
#include "ui/Header.h"
#include "ui/SampleList.h"
#include "ui/Vinyl.h"

namespace digga
{

class TrapWinProcessor;

/** The whole UI at a fixed logical size; the plugin window scales it.
    Vinyl left, loops & one-shots right, editor below (trapwin.com style). */
class MainView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    static constexpr int logicalWidth = 1280;
    static constexpr int logicalHeight = 800;

    explicit MainView (TrapWinProcessor& processor);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refresh();
    void togglePreview (int id);
    void showRowMenu (int id);
    void dragOut (int id);
    void prefetchMidi();

    TrapWinProcessor& processor;
    juce::SharedResourcePointer<theme::Typefaces> typefaces;

    Header header;
    Vinyl vinyl;
    SampleList list;
    EditorPanel editor;
    juce::TooltipWindow tooltips { this, 700 };
    midi::TranscriptionCache transcriptions;
    int shownSelection = -1;
};

} // namespace digga
