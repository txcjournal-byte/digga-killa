#pragma once

#include "midi/MidiExport.h"
#include "ui/EditorPanel.h"
#include "ui/Header.h"
#include "ui/SampleList.h"
#include "ui/SongOverview.h"
#include "ui/StemMixer.h"
#include "ui/Vinyl.h"

namespace digga
{

class TrapWinProcessor;

/** The whole UI at a fixed logical size; the plugin window scales it.
    Vinyl left, the sample browser right, the song overview (stems and
    sections) below, then the editor with its stem mixer (trapwin.com style). */
class MainView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    static constexpr int logicalWidth = 1440;
    static constexpr int logicalHeight = 900;

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
    void refreshOverview();
    void updateMidiView();

    TrapWinProcessor& processor;
    juce::SharedResourcePointer<theme::Typefaces> typefaces;

    Header header;
    Vinyl vinyl;
    SampleList list;
    EditorPanel editor;
    SongOverview overview;
    StemMixer mixer;
    std::unique_ptr<juce::FileChooser> chooser;
    const void* shownStems = nullptr;
    size_t shownSections = 0;
    AudioPtr shreddedAudio;          // clip whose notes the MIDI view shows
    bool midiEdited = false;         // the user changed the shredded notes
    juce::TooltipWindow tooltips { this, 700 };
    midi::TranscriptionCache transcriptions;
    int shownSelection = -1;
};

} // namespace digga
