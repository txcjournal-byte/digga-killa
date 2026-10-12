#pragma once

#include "midi/BasicPitch.h"
#include "ui/Theme.h"

#include <vector>

namespace digga
{

/** "Shredder": the selected sample turned into MIDI, shown and edited in a
    piano roll inside the plugin. Keyboard on the left, beat grid, zoom and
    scroll; select (click / lasso), move, resize, delete, draw (double click)
    notes; quantize, octave up / down, remove short notes, velocity by
    dragging the lower lane. The edited notes are what gets dragged out. */
class MidiView : public juce::Component
{
public:
    MidiView();
    ~MidiView() override;

    /** Fresh transcription (replaces any edits). lengthSeconds = clip length. */
    void setNotes (std::vector<midi::Note> notes, double lengthSeconds, double bpm);
    void setBusy (bool transcribing);                 // shows the shredder animation
    void setEmpty (const juce::String& message);      // no sample / nothing pitched
    const std::vector<midi::Note>& getNotes() const noexcept { return notes; }
    void setPlayhead (bool playing, double fraction);  // fraction of the clip

    std::function<void()> onNotesEdited;
    std::function<void()> onDragOut;                   // drag the (edited) MIDI into the DAW
    std::function<void (int pitch)> onAuditionNote;    // clicking a key / note

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::vector<midi::Note> notes;
    double length = 0.0, bpm = 120.0;
    bool busy = false, playing = false;
    double playhead = 0.0;
    juce::String emptyMessage;
};

} // namespace digga
