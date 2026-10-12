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
    Vinyl left, the sample browser right, the editor below (trapwin.com style). */
class MainView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    static constexpr int logicalWidth = 1200;
    static constexpr int logicalHeight = 750;

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
    void exportAll();
    void updateMidiView();

    TrapWinProcessor& processor;
    juce::SharedResourcePointer<theme::Typefaces> typefaces;

    Header header;
    Vinyl vinyl;
    SampleList list;
    EditorPanel editor;
    std::unique_ptr<juce::FileChooser> chooser;
    AudioPtr shreddedAudio;          // clip whose notes the MIDI view shows
    bool midiEdited = false;         // the user changed the shredded notes
    juce::TooltipWindow tooltips { this, 700 };
    midi::TranscriptionCache transcriptions;
    int shownSelection = -1;
};

} // namespace digga
