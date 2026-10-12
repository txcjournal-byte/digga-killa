// Component test for the midi part: the "shredder" piano roll (MidiView).
// Usage: TrapWinComponentTest midi <outDir>
// Edits (quantize, octave, delete, undo, clean, draw), mouse gestures (lasso,
// move), keys, callbacks; PNG snapshots of every state; and a real Basic Pitch
// transcription of a synthetic tone sequence.
#include "midi/BasicPitch.h"
#include "ui/MidiView.h"
#include "ui/TrapWinLookAndFeel.h"

#include <cmath>
#include <cstdio>

using namespace digga;

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    void save (juce::Component& c, const juce::File& file)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        std::printf ("  wrote %s\n", file.getFullPathName().toRawUTF8());
    }

    /** Synthetic mouse event on the view (local coordinates). */
    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos,
                            juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &c, &c, now, downPos, now, clicks, pos != downPos);
    }

    void dragGesture (juce::Component& c, juce::Point<float> from, juce::Point<float> to,
                      juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        c.mouseDown (mouse (c, from, from, mods));
        for (int step = 1; step <= 6; ++step)
            c.mouseDrag (mouse (c, from + (to - from) * ((float) step / 6.0f), from, mods));
        c.mouseUp (mouse (c, to, from, mods.withoutMouseButtons()));
    }

    bool onGrid (double seconds, double step)
    {
        const double k = seconds / step;
        return std::abs (k - std::round (k)) < 1.0e-6;
    }

    /** 2 bars at 140 bpm: a slightly loose melody (C5 area), a C-major chord
        in bar 2, one blip (too short) and one ghost note (too quiet). */
    std::vector<midi::Note> melody (double bpm)
    {
        const double s16 = 15.0 / bpm;
        std::vector<midi::Note> notes;
        const int pitches[] { 72, 74, 76, 79, 77, 76, 74, 72, 79, 81, 79, 77, 76, 74, 76, 84 };
        for (int i = 0; i < 16; ++i)
        {
            midi::Note n;
            const double loose = ((i * 7) % 5 - 2) * 0.006;   // +-12 ms timing slop
            n.startSeconds = juce::jmax (0.0, i * 2 * s16 + loose);
            n.endSeconds = n.startSeconds + s16 * (i % 4 == 3 ? 1.9 : 1.4);
            n.pitch = pitches[i];
            n.amplitude = 0.35f + 0.6f * (float) ((i * 5) % 8) / 7.0f;
            notes.push_back (n);
        }
        for (int p : { 55, 60, 64 })   // chord, bar 2 beat 1, one beat long
            notes.push_back ({ 16 * s16 + 0.004, 20 * s16, p, 0.7f });
        notes.push_back ({ 10 * s16, 10 * s16 + 0.02, 70, 0.6f });    // blip
        notes.push_back ({ 26 * s16, 27 * s16, 67, 0.05f });          // ghost
        return notes;
    }

    juce::AudioBuffer<float> toneSequence (double sampleRate, const std::vector<int>& pitches, double noteSeconds)
    {
        const int perNote = (int) (noteSeconds * sampleRate);
        juce::AudioBuffer<float> buffer (1, perNote * (int) pitches.size() + (int) sampleRate / 2);
        buffer.clear();
        auto* d = buffer.getWritePointer (0);
        for (size_t k = 0; k < pitches.size(); ++k)
        {
            const double f = 440.0 * std::pow (2.0, (pitches[k] - 69) / 12.0);
            for (int i = 0; i < perNote; ++i)
            {
                const double t = i / sampleRate;
                const double env = std::min (1.0, t * 200.0) * std::exp (-t * 2.5) * std::min (1.0, (perNote - i) / (0.01 * sampleRate));
                const double v = std::sin (juce::MathConstants<double>::twoPi * f * t)
                               + 0.35 * std::sin (juce::MathConstants<double>::twoPi * 2.0 * f * t)
                               + 0.15 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * f * t);
                d[(int) k * perNote + i] = (float) (0.35 * env * v);
            }
        }
        return buffer;
    }
}

int runMidiTest (int argc, char** argv)
{
    const juce::File outDir (argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])
                                      : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("midi_out"));
    outDir.createDirectory();

    TrapWinLookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    {
        constexpr double bpm = 140.0, s16 = 15.0 / bpm;
        const double length = 32 * s16;   // 2 bars
        MidiView view;
        view.setSize (1100, 230);

        int edits = 0, lastAudition = -1;
        view.onNotesEdited = [&] { ++edits; };
        view.onAuditionNote = [&] (int p) { lastAudition = p; };

        const auto source = melody (bpm);
        view.setNotes (source, length, bpm);
        const int n = (int) source.size();
        check ((int) view.getNotes().size() == n, "setNotes keeps all " + juce::String (n) + " notes");
        check (view.selectedCount() == 0 && ! view.canUndo(), "fresh notes: no selection, no undo");

        bool allVisible = true;
        for (int i = 0; i < n; ++i)
            allVisible = allVisible && view.getGridArea().toFloat().contains (view.getNoteBounds (i).getCentre());
        check (allVisible, "auto-fit shows every note");

        view.setPlayhead (true, 0.56);
        save (view, outDir.getChildFile ("midi_notes.png"));
        view.setPlayhead (false, 0.0);

        // ---- quantize
        view.quantize();
        bool quantized = true;
        for (const auto& note : view.getNotes())
            quantized = quantized && onGrid (note.startSeconds, s16) && note.endSeconds > note.startSeconds;
        check (quantized, "quantize snaps every start to 1/16");
        check (edits == 1, "onNotesEdited fires on quantize");

        // ---- octave up / undo
        const auto beforeOctave = view.getNotes();
        view.selectAll();
        check (view.selectedCount() == n, "selectAll selects every note");
        view.transpose (12);
        bool up = true;
        for (size_t i = 0; i < beforeOctave.size(); ++i)
            up = up && view.getNotes()[i].pitch == beforeOctave[i].pitch + 12;
        check (up, "OCT + adds 12 to every selected note");
        check (view.undo() && view.getNotes()[0].pitch == beforeOctave[0].pitch, "undo restores the octave");
        check (edits == 3, "onNotesEdited fires on transpose and undo");

        // ---- lasso around the chord (3 notes in bar 2)
        juce::Rectangle<float> chord;
        for (int i = 0; i < n; ++i)
            if (view.getNotes()[(size_t) i].pitch <= 64)
                chord = chord.isEmpty() ? view.getNoteBounds (i) : chord.getUnion (view.getNoteBounds (i));
        dragGesture (view, chord.getTopLeft().translated (-3.0f, -2.0f), chord.getBottomRight().translated (-4.0f, 2.0f));
        int chordSelected = 0;
        for (int i = 0; i < n; ++i)
            if (view.isSelected (i))
                chordSelected += view.getNotes()[(size_t) i].pitch <= 64 ? 1 : 100;
        check (chordSelected == 3, "lasso selects exactly the 3 chord notes (" + juce::String (chordSelected) + ")");
        check (edits == 3, "lasso alone is not an edit");
        save (view, outDir.getChildFile ("midi_selected.png"));

        // ---- click a note: selects it alone and auditions it
        {
            const int target = 3;
            const auto r = view.getNoteBounds (target);
            const auto p = r.getCentre().withX (r.getX() + 3.0f);
            view.mouseDown (mouse (view, p, p));
            view.mouseUp (mouse (view, p, p, {}));
            check (view.selectedCount() == 1 && view.isSelected (target), "click selects one note");
            check (lastAudition == view.getNotes()[(size_t) target].pitch, "click auditions the note");
            check (edits == 3, "a plain click is not an edit");
        }

        // ---- move by mouse: 2 sixteenths later, one semitone up (snapped)
        {
            int sel = -1;
            for (int i = 0; i < n; ++i)
                if (view.isSelected (i))
                    sel = i;
            const auto before = view.getNotes()[(size_t) sel];
            const auto r = view.getNoteBounds (sel);
            const auto from = r.getCentre().withX (r.getX() + 3.0f);
            const float px16 = view.getNoteBounds (sel).getWidth() / (float) ((before.endSeconds - before.startSeconds) / s16);
            const float row = r.getHeight() + 1.0f;
            dragGesture (view, from, from + juce::Point<float> (px16 * 2.2f, -row));
            int moved = -1;
            for (int i = 0; i < n; ++i)
                if (view.isSelected (i))
                    moved = i;
            const auto after = view.getNotes()[(size_t) moved];
            check (after.pitch == before.pitch + 1 && std::abs (after.startSeconds - (before.startSeconds + 2 * s16)) < 1.0e-6,
                   "drag moves a note (+2/16 snapped, +1 semitone)");
            check (std::abs ((after.endSeconds - after.startSeconds) - (before.endSeconds - before.startSeconds)) < 1.0e-9,
                   "move keeps the length");
            check (edits == 4, "onNotesEdited fires on move");

            // resize from the right edge
            const auto rr = view.getNoteBounds (moved);
            const auto edge = juce::Point<float> (rr.getRight() - 2.0f, rr.getCentreY());
            dragGesture (view, edge, edge.translated (px16 * 2.0f, 0.0f));
            int resized = -1;
            for (int i = 0; i < n; ++i)
                if (view.isSelected (i))
                    resized = i;
            const auto longer = view.getNotes()[(size_t) resized];
            check (std::abs (longer.startSeconds - after.startSeconds) < 1.0e-9 && onGrid (longer.endSeconds, s16)
                       && longer.endSeconds > after.endSeconds + s16,
                   "drag on the right edge resizes (end snapped)");
            check (view.undo() && view.undo(), "undo resize and move");
            check (std::abs (view.getNotes()[(size_t) sel].startSeconds - before.startSeconds) < 1.0e-9
                       && view.getNotes()[(size_t) sel].pitch == before.pitch,
                   "undo restores the moved note");
        }

        // ---- delete / undo
        dragGesture (view, chord.getTopLeft().translated (-3.0f, -2.0f), chord.getBottomRight().translated (-4.0f, 2.0f));
        check (view.selectedCount() == 3, "lasso again");
        view.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        int chordTop = 0;
        for (int i = 0; i < n; ++i)
            if (view.isSelected (i))
                chordTop = juce::jmax (chordTop, view.getNotes()[(size_t) i].pitch);
        check (chordTop == 65, "arrow up transposes the selection by a semitone");
        view.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
        check ((int) view.getNotes().size() == n - 3 && view.selectedCount() == 0, "Delete removes the selected notes");
        view.undo();
        check ((int) view.getNotes().size() == n, "undo brings them back");
        view.undo();
        bool chordBack = false;
        for (const auto& note : view.getNotes())
            chordBack = chordBack || note.pitch == 64;
        check (chordBack, "undo the transpose too");

        // ---- clean: drops the blip (< 1/32) and the ghost note (on a fresh, unquantized transcription)
        view.setNotes (melody (bpm), length, bpm);
        view.cleanUp();
        bool clean = (int) view.getNotes().size() == n - 2;
        for (const auto& note : view.getNotes())
            clean = clean && note.endSeconds - note.startSeconds >= s16 * 0.5 - 1.0e-6 && note.amplitude >= 0.12f;
        check (clean, "CLEAN removes the short and the quiet note");

        // ---- draw (double click empty) / erase (double click note)
        {
            const auto grid = view.getGridArea().toFloat();
            const auto p = juce::Point<float> (grid.getRight() - 30.0f, grid.getBottom() - 3.0f);
            bool free = true;
            for (int i = 0; i < (int) view.getNotes().size(); ++i)
                free = free && ! view.getNoteBounds (i).contains (p);
            const int count = (int) view.getNotes().size();
            view.mouseDoubleClick (mouse (view, p, p, juce::ModifierKeys::leftButtonModifier, 2));
            const auto& all = view.getNotes();
            int added = -1;
            for (int i = 0; i < (int) all.size(); ++i)
                if (view.isSelected (i))
                    added = i;
            check (free && (int) all.size() == count + 1 && added >= 0
                       && std::abs (all[(size_t) added].endSeconds - all[(size_t) added].startSeconds - s16) < 1.0e-9
                       && std::abs (all[(size_t) added].amplitude - 0.8f) < 1.0e-6 && onGrid (all[(size_t) added].startSeconds, s16),
                   "double-click on empty space draws a 1/16 note (velocity 0.8)");
            const auto q = view.getNoteBounds (added).getCentre();
            view.mouseDoubleClick (mouse (view, q, q, juce::ModifierKeys::leftButtonModifier, 2));
            check ((int) view.getNotes().size() == count, "double-click on a note deletes it");
        }

        // ---- velocity lane
        {
            const auto grid = view.getGridArea();
            const float x = view.getNoteBounds (0).getX();
            const auto p = juce::Point<float> (x, (float) grid.getBottom() + 4.0f);   // near the top of the lane
            const auto before = view.getNotes()[0].amplitude;
            dragGesture (view, p, p);
            check (view.getNotes()[0].amplitude > 0.8f && std::abs (view.getNotes()[0].amplitude - before) > 0.01f, "velocity lane sets the amplitude");
        }

        // ---- keyboard: ctrl/cmd+A, key click auditions
        view.keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0));
        check (view.selectedCount() == (int) view.getNotes().size(), "Ctrl/Cmd+A selects all");
        {
            lastAudition = -1;
            const auto p = juce::Point<float> (10.0f, view.getNoteBounds (0).getCentreY());
            view.mouseDown (mouse (view, p, p));
            view.mouseUp (mouse (view, p, p, {}));
            check (lastAudition == view.getNotes()[0].pitch, "clicking a key auditions its pitch");
        }

        int dragged = 0;
        view.onDragOut = [&] { ++dragged; };
        view.clearSelection();
        view.setPlayhead (true, 0.3);
        save (view, outDir.getChildFile ("midi_edited.png"));

        // ---- compact size
        view.setSize (700, 200);
        view.setNotes (melody (bpm), length, bpm);
        check (view.getGridArea().getWidth() > 500 && view.getGridArea().getHeight() > 100, "layout works at 700 x 200");
        save (view, outDir.getChildFile ("midi_small.png"));
        view.setSize (1100, 230);

        // ---- busy / empty
        view.setBusy (true);
        save (view, outDir.getChildFile ("midi_busy.png"));
        view.setBusy (false);
        view.setEmpty ("Pick a loop or one-shot to shred it into MIDI");
        check (view.getNotes().empty(), "setEmpty clears the notes");
        view.quantize();
        check (! view.canUndo(), "empty state is read-only");
        save (view, outDir.getChildFile ("midi_empty.png"));
        check (dragged == 0, "no drag-out without a drag");
    }

    // ---- real Basic Pitch output on a synthetic tone sequence
    {
        constexpr double sr = 44100.0, bpm = 120.0;
        const std::vector<int> pitches { 60, 64, 67, 72, 71, 67, 64, 62 };
        const auto audio = toneSequence (sr, pitches, 0.25);
        const auto transcribed = midi::transcribe (audio, sr);
        int matched = 0;
        for (int p : pitches)
            for (const auto& note : transcribed)
                if (note.pitch == p)
                {
                    ++matched;
                    break;
                }
        check (matched >= 6, "Basic Pitch finds the tone sequence (" + juce::String (matched) + "/8 pitches, "
                                 + juce::String ((int) transcribed.size()) + " notes)");
        MidiView view;
        view.setSize (1100, 230);
        view.setNotes (transcribed, audio.getNumSamples() / sr, bpm);
        save (view, outDir.getChildFile ("midi_basicpitch.png"));
    }

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    if (failures == 0)
        std::printf ("ALL CHECKS PASSED\n");
    else
        std::printf ("%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
