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
class MidiView : public juce::Component,
                 private juce::Timer
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

    // ---- edit commands (toolbar / keys; also used by the tests). Commands act on
    //      the selection, or on all notes when nothing is selected.
    void quantize();                    // starts (and ends) to 1/16 at the bpm
    void transpose (int semitones);     // OCT - / OCT + use +-12
    void cleanUp();                     // drops notes shorter than 1/32 and very quiet ones
    void deleteSelected();
    bool undo();                        // false when there is nothing to undo
    bool canUndo() const noexcept       { return ! history.empty(); }

    void selectAll();
    void clearSelection();
    /** Selects the notes touching a rectangle in local coordinates (the lasso). */
    void selectInArea (juce::Rectangle<float> area, bool addToSelection);
    int selectedCount() const noexcept;
    bool isSelected (int index) const noexcept;
    /** Where a note is drawn, in local coordinates (empty when off screen). */
    juce::Rectangle<float> getNoteBounds (int index) const;
    juce::Rectangle<int> getGridArea() const noexcept { return grid; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    /** Chrome "DRAG MIDI" pill: starts the external drag after a few pixels. */
    struct DragHandle : public juce::Component,
                        public juce::SettableTooltipClient
    {
        DragHandle();
        std::function<void()> onDragOut;
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override    { started = false; }
        bool hover = false, started = false;
    };

    enum class Drag { none, lasso, move, resize, velocity, keys, pan };

    std::vector<midi::Note> notes;
    std::vector<bool> selected;                      // parallel to notes
    std::vector<std::vector<midi::Note>> history;    // undo snapshots
    double length = 0.0, bpm = 120.0;
    bool busy = false, playing = false;
    double playhead = 0.0;
    juce::String emptyMessage;

    // view: seconds on x, semitone rows on y (topPitch = pitch boundary at the grid top)
    double viewStart = 0.0, viewLength = 4.0;
    float topPitch = 72.0f, rowHeight = 8.0f;

    // interaction
    Drag drag = Drag::none;
    std::vector<midi::Note> dragOrigin;
    std::vector<bool> selectionOrigin;
    int dragNote = -1, hoverNote = -1, heldKey = -1, lastAudition = -1;
    bool hoverEdge = false;
    juce::Point<float> dragStart, dragNow;
    float lastDragX = 0.0f;
    double panStart = 0.0;
    float panTopPitch = 0.0f;
    bool needsFit = true;

    double animTime = 0.0;

    juce::TextButton quantizeButton { "Quantize" }, octDownButton { "Oct -" }, octUpButton { "Oct +" },
                     cleanButton { "Clean" }, undoButton { "Undo" };
    DragHandle dragHandle;

    juce::Rectangle<int> toolbar, keys, ruler, grid, velocity, infoArea;

    // geometry
    double sixteenth() const noexcept   { return 15.0 / bpm; }
    double snap (double seconds) const noexcept;
    double contentLength() const noexcept;
    double pixelsPerSecond() const noexcept;
    float xForTime (double seconds) const noexcept;
    double timeForX (float x) const noexcept;
    float yForPitch (float pitch) const noexcept;    // top of the row
    int pitchForY (float y) const noexcept;
    int noteAt (juce::Point<float> p, bool& onRightEdge) const;
    int velocityNoteAt (float x) const;

    void fitView();
    void clampView();
    void zoomAround (float x, double factor);
    void ensureVisible (int lowPitch, int highPitch);
    bool editable() const noexcept     { return ! busy && emptyMessage.isEmpty(); }

    // edits
    void beginEdit();                 // snapshot before a gesture
    void commitEdit (const std::vector<midi::Note>& before);
    void sortNotes();
    void syncSelection();
    std::vector<int> targets() const; // selection, or all notes
    void audition (int pitch);
    void updateButtons();

    // drawing
    void drawToolbar (juce::Graphics&);
    void drawKeyboard (juce::Graphics&);
    void drawGrid (juce::Graphics&);
    void drawNotes (juce::Graphics&);
    void drawVelocity (juce::Graphics&);
    void drawShredder (juce::Graphics&, juce::Rectangle<float> area);

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiView)
};

} // namespace digga
