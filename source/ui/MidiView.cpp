#include "ui/MidiView.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace digga
{

using namespace theme;

namespace
{
    constexpr int toolbarHeight = 30, keysWidth = 44, rulerHeight = 15, velocityHeight = 28;
    constexpr size_t maxHistory = 100;
    constexpr float minRowHeight = 3.0f, maxRowHeight = 22.0f;

    bool isBlackKey (int pitch) noexcept
    {
        const int pc = ((pitch % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    juce::String noteName (int pitch)
    {
        static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((pitch % 12) + 12) % 12]) + juce::String (pitch / 12 - 1);
    }

    bool sameNotes (const std::vector<midi::Note>& a, const std::vector<midi::Note>& b) noexcept
    {
        return std::equal (a.begin(), a.end(), b.begin(), b.end(), [] (const midi::Note& x, const midi::Note& y)
        {
            return juce::exactlyEqual (x.startSeconds, y.startSeconds) && juce::exactlyEqual (x.endSeconds, y.endSeconds)
                && x.pitch == y.pitch && juce::exactlyEqual (x.amplitude, y.amplitude);
        });
    }

    /** Note colour by velocity: deep blue -> accent -> accentL. */
    juce::Colour velocityColour (float amplitude)
    {
        const auto low = juce::Colour (0xff34518a);
        const float a = juce::jlimit (0.0f, 1.0f, amplitude);
        return a < 0.5f ? low.interpolatedWith (accent, a * 2.0f)
                        : accent.interpolatedWith (accentL, (a - 0.5f) * 2.0f);
    }

    const juce::String dot = juce::String (" ") + juce::String::charToString ((juce::juce_wchar) 0x00b7) + " ";

    /** Cheap deterministic hash -> 0..1 (shredder animation). */
    float hash01 (int k, int salt) noexcept
    {
        auto h = (uint32_t) k * 2654435761u ^ (uint32_t) (salt + 1) * 2246822519u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        return (float) (h & 0xffffu) / 65535.0f;
    }
}

// ---------------------------------------------------------------------------- drag pill
MidiView::DragHandle::DragHandle()
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag the edited MIDI into FL Studio, Ableton, Logic...");
}

void MidiView::DragHandle::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    const bool enabled = isEnabled();
    if (! enabled)
        g.beginTransparencyLayer (0.3f);   // gradient fills ignore setOpacity
    drawChromePill (g, r, hover && enabled, false);
    g.setColour (well);
    for (int col = 0; col < 2; ++col)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse (r.getX() + 13.0f + (float) col * 4.5f, r.getCentreY() + (float) (row - 1) * 4.5f - 1.2f, 2.4f, 2.4f);
    g.setFont (display (11.5f).withExtraKerningFactor (0.04f));
    g.drawText ("DRAG MIDI", r.withTrimmedLeft (22.0f), juce::Justification::centred, false);
    if (! enabled)
        g.endTransparencyLayer();
}

void MidiView::DragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! started && isEnabled() && e.getDistanceFromDragStart() > 6)
    {
        started = true;
        if (onDragOut)
            onDragOut();
    }
}

// ---------------------------------------------------------------------------- view
MidiView::MidiView()
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    quantizeButton.setTooltip ("Snap note starts and ends to 1/16 (selection, or all notes)");
    octDownButton.setTooltip ("Selection (or all notes) one octave down");
    octUpButton.setTooltip ("Selection (or all notes) one octave up");
    cleanButton.setTooltip ("Remove notes shorter than 1/32 and very quiet ones");
    undoButton.setTooltip ("Undo the last edit (Ctrl/Cmd+Z)");

    quantizeButton.onClick = [this] { quantize(); };
    octDownButton.onClick  = [this] { transpose (-12); };
    octUpButton.onClick    = [this] { transpose (12); };
    cleanButton.onClick    = [this] { cleanUp(); };
    undoButton.onClick     = [this] { undo(); };
    dragHandle.onDragOut   = [this] { if (onDragOut) onDragOut(); };

    for (auto* b : { &quantizeButton, &octDownButton, &octUpButton, &cleanButton, &undoButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    addAndMakeVisible (dragHandle);
    updateButtons();
}

MidiView::~MidiView() = default;

void MidiView::setNotes (std::vector<midi::Note> n, double lengthSeconds, double newBpm)
{
    notes = std::move (n);
    length = juce::jmax (0.0, lengthSeconds);
    bpm = newBpm > 20.0 && newBpm < 400.0 ? newBpm : 120.0;
    emptyMessage.clear();
    history.clear();
    selected.assign (notes.size(), false);
    drag = Drag::none;
    hoverNote = -1;
    sortNotes();
    needsFit = true;
    fitView();
    updateButtons();
    repaint();
}

void MidiView::setBusy (bool transcribing)
{
    if (busy == transcribing)
        return;
    busy = transcribing;
    drag = Drag::none;
    if (busy)
        startTimerHz (30);
    else
        stopTimer();
    updateButtons();
    repaint();
}

void MidiView::setEmpty (const juce::String& message)
{
    emptyMessage = message.isNotEmpty() ? message : juce::String ("No MIDI");
    notes.clear();
    selected.clear();
    history.clear();
    drag = Drag::none;
    hoverNote = -1;
    updateButtons();
    repaint();
}

void MidiView::setPlayhead (bool isPlaying, double fraction)
{
    fraction = juce::jlimit (0.0, 1.0, fraction);
    if (playing == isPlaying && std::abs (playhead - fraction) < 1.0e-5)
        return;
    playing = isPlaying;
    playhead = fraction;
    repaint();
}

void MidiView::timerCallback()
{
    animTime += 1.0 / 30.0;
    repaint();
}

// ---------------------------------------------------------------------------- layout
void MidiView::resized()
{
    auto b = getLocalBounds();
    toolbar = b.removeFromTop (toolbarHeight);
    velocity = b.removeFromBottom (velocityHeight);
    keys = b.removeFromLeft (keysWidth).withTrimmedTop (rulerHeight);
    ruler = b.removeFromTop (rulerHeight);
    grid = b;

    // toolbar: title | buttons | info ......... DRAG MIDI
    auto t = toolbar.reduced (6, 4);
    t.removeFromLeft (keysWidth + 40);   // "SHREDDER" title
    dragHandle.setBounds (t.removeFromRight (112));
    t.removeFromRight (6);
    const int widths[] { 76, 52, 52, 56, 52 };
    juce::Button* buttons[] { &quantizeButton, &octDownButton, &octUpButton, &cleanButton, &undoButton };
    for (int i = 0; i < 5; ++i)
    {
        buttons[i]->setBounds (t.removeFromLeft (widths[i]));
        t.removeFromLeft (i == 0 || i == 2 || i == 3 ? 8 : 3);
    }
    infoArea = t.withTrimmedLeft (4);

    if (needsFit)
        fitView();
    else
        clampView();
}

// ---------------------------------------------------------------------------- geometry
double MidiView::snap (double seconds) const noexcept
{
    const double s = sixteenth();
    return std::round (seconds / s) * s;
}

double MidiView::contentLength() const noexcept
{
    double end = length;
    for (const auto& n : notes)
        end = juce::jmax (end, n.endSeconds);
    return juce::jmax (end, 240.0 / bpm);   // at least one bar
}

double MidiView::pixelsPerSecond() const noexcept
{
    return grid.getWidth() / juce::jmax (1.0e-3, viewLength);
}

float MidiView::xForTime (double seconds) const noexcept
{
    return (float) (grid.getX() + (seconds - viewStart) * pixelsPerSecond());
}

double MidiView::timeForX (float x) const noexcept
{
    return viewStart + (x - (float) grid.getX()) / pixelsPerSecond();
}

float MidiView::yForPitch (float pitch) const noexcept
{
    return (float) grid.getY() + (topPitch - (pitch + 1.0f)) * rowHeight;
}

int MidiView::pitchForY (float y) const noexcept
{
    return juce::jlimit (0, 127, (int) std::floor (topPitch - (y - (float) grid.getY()) / rowHeight));
}

juce::Rectangle<float> MidiView::getNoteBounds (int index) const
{
    if (! juce::isPositiveAndBelow (index, (int) notes.size()) || grid.isEmpty())
        return {};
    const auto& n = notes[(size_t) index];
    const float x1 = xForTime (n.startSeconds), x2 = xForTime (n.endSeconds);
    const auto r = juce::Rectangle<float> (x1, yForPitch ((float) n.pitch) + 0.5f,
                                           juce::jmax (3.0f, x2 - x1), juce::jmax (2.0f, rowHeight - (rowHeight < 6.0f ? 0.5f : 1.0f)));
    return r.intersects (grid.toFloat()) ? r : juce::Rectangle<float>();
}

int MidiView::noteAt (juce::Point<float> p, bool& onRightEdge) const
{
    onRightEdge = false;
    if (! grid.toFloat().contains (p))
        return -1;
    for (int i = (int) notes.size(); --i >= 0;)
    {
        const auto r = getNoteBounds (i).expanded (0.0f, rowHeight < 6.0f ? 1.5f : 0.0f);
        if (r.contains (p))
        {
            onRightEdge = r.getWidth() >= 8.0f && p.x >= r.getRight() - juce::jmin (6.0f, r.getWidth() * 0.35f);
            return i;
        }
    }
    return -1;
}

int MidiView::velocityNoteAt (float x) const
{
    int best = -1;
    float bestDistance = 7.0f;
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const float d = std::abs (xForTime (notes[(size_t) i].startSeconds) - x)
                      - (selected[(size_t) i] ? 0.5f : 0.0f);   // prefer the selection on ties
        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

void MidiView::fitView()
{
    if (grid.isEmpty())
        return;
    needsFit = false;

    viewStart = 0.0;
    viewLength = contentLength();

    int lo = 60, hi = 71;
    if (! notes.empty())
    {
        lo = 127; hi = 0;
        for (const auto& n : notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
        lo -= 2; hi += 2;
        while (hi - lo + 1 < 13) { --lo; ++hi; }
    }
    const float rows = (float) (hi - lo + 1);
    rowHeight = juce::jlimit (minRowHeight, 14.0f, (float) grid.getHeight() / rows);
    const float visible = (float) grid.getHeight() / rowHeight;
    topPitch = (float) (lo + hi + 1) * 0.5f + visible * 0.5f;
    clampView();
}

void MidiView::clampView()
{
    const double content = contentLength();
    viewLength = juce::jlimit (juce::jmin (content, 60.0 / bpm), content, viewLength);
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, content - viewLength), viewStart);

    rowHeight = juce::jlimit (minRowHeight, maxRowHeight, rowHeight);
    const float visible = grid.getHeight() / rowHeight;
    topPitch = juce::jlimit (juce::jmin (128.0f, visible), 128.0f, topPitch);
}

void MidiView::zoomAround (float x, double factor)
{
    if (grid.isEmpty() || factor <= 0.0)
        return;
    const double t = timeForX (x);
    viewLength /= factor;
    clampView();
    viewStart = t - (x - (float) grid.getX()) / pixelsPerSecond();
    clampView();
    repaint();
}

void MidiView::ensureVisible (int lowPitch, int highPitch)
{
    const float visible = grid.getHeight() / rowHeight;
    if ((float) highPitch + 1.0f > topPitch)
        topPitch = (float) highPitch + 2.0f;
    if ((float) lowPitch < topPitch - visible)
        topPitch = (float) lowPitch - 1.0f + visible;
    clampView();
}

// ---------------------------------------------------------------------------- edits
void MidiView::syncSelection()
{
    selected.resize (notes.size(), false);
}

void MidiView::sortNotes()
{
    syncSelection();
    std::vector<size_t> order (notes.size());
    std::iota (order.begin(), order.end(), size_t { 0 });
    std::stable_sort (order.begin(), order.end(), [this] (size_t a, size_t b)
    {
        const auto& x = notes[a];
        const auto& y = notes[b];
        return ! juce::exactlyEqual (x.startSeconds, y.startSeconds) ? x.startSeconds < y.startSeconds : x.pitch < y.pitch;
    });
    std::vector<midi::Note> sortedNotes;
    std::vector<bool> sortedSelection;
    for (auto i : order)
    {
        sortedNotes.push_back (notes[i]);
        sortedSelection.push_back (selected[i]);
    }
    notes = std::move (sortedNotes);
    selected = std::move (sortedSelection);
}

std::vector<int> MidiView::targets() const
{
    std::vector<int> result;
    const bool any = selectedCount() > 0;
    for (int i = 0; i < (int) notes.size(); ++i)
        if (! any || selected[(size_t) i])
            result.push_back (i);
    return result;
}

void MidiView::beginEdit()
{
    dragOrigin = notes;
}

void MidiView::commitEdit (const std::vector<midi::Note>& before)
{
    syncSelection();
    if (sameNotes (before, notes))
        return;
    history.push_back (before);
    if (history.size() > maxHistory)
        history.erase (history.begin());
    sortNotes();
    hoverNote = -1;
    updateButtons();
    repaint();
    if (onNotesEdited)
        onNotesEdited();
}

void MidiView::quantize()
{
    if (! editable())
        return;
    const auto before = notes;
    const double s = sixteenth();
    for (int i : targets())
    {
        auto& n = notes[(size_t) i];
        const double start = juce::jmax (0.0, snap (n.startSeconds));
        double end = snap (n.endSeconds);
        if (end < start + s * 0.5)
            end = start + s;
        n.startSeconds = start;
        n.endSeconds = end;
    }
    commitEdit (before);
}

void MidiView::transpose (int semitones)
{
    if (! editable() || semitones == 0)
        return;
    const auto before = notes;
    int lo = 127, hi = 0;
    for (int i : targets())
    {
        auto& n = notes[(size_t) i];
        n.pitch = juce::jlimit (0, 127, n.pitch + semitones);
        lo = juce::jmin (lo, n.pitch);
        hi = juce::jmax (hi, n.pitch);
    }
    if (lo <= hi)
        ensureVisible (lo, hi);
    if (! sameNotes (before, notes) && onAuditionNote && selectedCount() == 1)
        audition (notes[(size_t) targets().front()].pitch);
    commitEdit (before);
}

void MidiView::cleanUp()
{
    if (! editable())
        return;
    const auto before = notes;
    const double minLength = sixteenth() * 0.5 - 1.0e-6;   // 1/32
    constexpr float quiet = 0.12f;
    const auto doomed = targets();
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
    {
        const auto& n = notes[(size_t) *it];
        if (n.endSeconds - n.startSeconds < minLength || n.amplitude < quiet)
        {
            notes.erase (notes.begin() + *it);
            selected.erase (selected.begin() + *it);
        }
    }
    commitEdit (before);
}

void MidiView::deleteSelected()
{
    if (! editable())
        return;
    const auto before = notes;
    for (int i = (int) notes.size(); --i >= 0;)
        if (selected[(size_t) i])
        {
            notes.erase (notes.begin() + i);
            selected.erase (selected.begin() + i);
        }
    commitEdit (before);
}

bool MidiView::undo()
{
    if (history.empty() || ! editable())
        return false;
    notes = std::move (history.back());
    history.pop_back();
    selected.assign (notes.size(), false);
    drag = Drag::none;
    hoverNote = -1;

    // bring the restored notes back into view
    int lo = 127, hi = 0;
    for (const auto& n : notes)
    {
        lo = juce::jmin (lo, n.pitch);
        hi = juce::jmax (hi, n.pitch);
    }
    if (lo <= hi)
        ensureVisible (lo, hi);

    updateButtons();
    repaint();
    if (onNotesEdited)
        onNotesEdited();
    return true;
}

void MidiView::selectAll()
{
    selected.assign (notes.size(), true);
    repaint();
}

void MidiView::clearSelection()
{
    selected.assign (notes.size(), false);
    repaint();
}

void MidiView::selectInArea (juce::Rectangle<float> area, bool addToSelection)
{
    if (! addToSelection)
        selected.assign (notes.size(), false);
    syncSelection();
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto r = getNoteBounds (i);
        if (! r.isEmpty() && r.intersects (area))
            selected[(size_t) i] = true;
    }
    repaint();
}

int MidiView::selectedCount() const noexcept
{
    return (int) std::count (selected.begin(), selected.end(), true);
}

bool MidiView::isSelected (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, (int) selected.size()) && selected[(size_t) index];
}

void MidiView::audition (int pitch)
{
    lastAudition = pitch;
    if (onAuditionNote)
        onAuditionNote (pitch);
}

void MidiView::updateButtons()
{
    const bool canEdit = editable() && ! notes.empty();
    for (auto* b : { &quantizeButton, &octDownButton, &octUpButton, &cleanButton })
        b->setEnabled (canEdit);
    undoButton.setEnabled (editable() && canUndo());
    dragHandle.setEnabled (canEdit);
}

// ---------------------------------------------------------------------------- mouse
void MidiView::mouseMove (const juce::MouseEvent& e)
{
    if (! editable())
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }
    bool edge = false;
    const int over = noteAt (e.position, edge);
    if (over != hoverNote || edge != hoverEdge)
    {
        hoverNote = over;
        hoverEdge = edge;
        repaint (grid);
    }

    if (keys.contains (e.getPosition()))
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (velocity.contains (e.getPosition()) && e.x >= grid.getX())
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (over >= 0)
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void MidiView::mouseExit (const juce::MouseEvent&)
{
    if (hoverNote >= 0)
    {
        hoverNote = -1;
        repaint (grid);
    }
}

void MidiView::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    if (! editable())
        return;
    if (isShowing())
        grabKeyboardFocus();

    dragStart = dragNow = e.position;
    lastDragX = e.position.x;
    const auto p = e.getPosition();

    if (e.mods.isMiddleButtonDown() || (ruler.contains (p) && p.x >= grid.getX()))
    {
        drag = Drag::pan;
        panStart = viewStart;
        panTopPitch = topPitch;
        return;
    }

    if (keys.contains (p))
    {
        drag = Drag::keys;
        heldKey = pitchForY (e.position.y);
        audition (heldKey);
        repaint (keys);
        return;
    }

    if (velocity.contains (p) && p.x >= grid.getX())
    {
        const int i = velocityNoteAt (e.position.x);
        beginEdit();
        drag = Drag::velocity;
        if (i >= 0)
        {
            const float a = juce::jlimit (0.02f, 1.0f, ((float) velocity.getBottom() - 3.0f - e.position.y)
                                                         / ((float) velocity.getHeight() - 7.0f));
            notes[(size_t) i].amplitude = a;
            repaint();
        }
        return;
    }

    if (! grid.contains (p))
        return;

    bool edge = false;
    const int i = noteAt (e.position, edge);
    if (i >= 0)
    {
        if (e.mods.isShiftDown())
        {
            selected[(size_t) i] = ! selected[(size_t) i];
            if (! selected[(size_t) i])
            {
                repaint();
                return;
            }
        }
        else if (! selected[(size_t) i])
        {
            selected.assign (notes.size(), false);
            selected[(size_t) i] = true;
        }
        dragNote = i;
        drag = edge ? Drag::resize : Drag::move;
        beginEdit();
        audition (notes[(size_t) i].pitch);
    }
    else
    {
        drag = Drag::lasso;
        if (! e.mods.isShiftDown())
            selected.assign (notes.size(), false);
        selectionOrigin = selected;
    }
    repaint();
}

void MidiView::mouseDrag (const juce::MouseEvent& e)
{
    dragNow = e.position;
    const bool free = e.mods.isAltDown();

    switch (drag)
    {
        case Drag::none:
            return;

        case Drag::pan:
        {
            viewStart = panStart - (e.position.x - dragStart.x) / pixelsPerSecond();
            if (e.mods.isMiddleButtonDown())
                topPitch = panTopPitch + (e.position.y - dragStart.y) / rowHeight;
            clampView();
            break;
        }

        case Drag::keys:
        {
            const int k = pitchForY (e.position.y);
            if (k != heldKey)
            {
                heldKey = k;
                audition (k);
            }
            break;
        }

        case Drag::velocity:
        {
            const float a = juce::jlimit (0.02f, 1.0f, ((float) velocity.getBottom() - 3.0f - e.position.y)
                                                         / ((float) velocity.getHeight() - 7.0f));
            const float x1 = juce::jmin (lastDragX, e.position.x) - 2.0f, x2 = juce::jmax (lastDragX, e.position.x) + 2.0f;
            bool any = false;
            for (size_t i = 0; i < notes.size(); ++i)
            {
                const float x = xForTime (notes[i].startSeconds);
                if (x >= x1 && x <= x2)
                {
                    notes[i].amplitude = a;
                    any = true;
                }
            }
            if (! any)
                if (const int i = velocityNoteAt (e.position.x); i >= 0)
                    notes[(size_t) i].amplitude = a;
            lastDragX = e.position.x;
            break;
        }

        case Drag::lasso:
        {
            selected = selectionOrigin;
            selectInArea (juce::Rectangle<float> (dragStart, dragNow), true);
            break;
        }

        case Drag::move:
        {
            if (! juce::isPositiveAndBelow (dragNote, (int) dragOrigin.size()))
                break;
            const auto& anchor = dragOrigin[(size_t) dragNote];
            double dt = timeForX (e.position.x) - timeForX (dragStart.x);
            if (! free)
                dt = snap (anchor.startSeconds + dt) - anchor.startSeconds;

            int dp = pitchForY (e.position.y) - pitchForY (dragStart.y);
            double earliest = 1.0e9;
            int lo = 127, hi = 0;
            for (size_t i = 0; i < notes.size(); ++i)
                if (selected[i])
                {
                    earliest = juce::jmin (earliest, dragOrigin[i].startSeconds);
                    lo = juce::jmin (lo, dragOrigin[i].pitch);
                    hi = juce::jmax (hi, dragOrigin[i].pitch);
                }
            dt = juce::jmax (dt, -earliest);
            dp = juce::jlimit (-lo, 127 - hi, dp);

            for (size_t i = 0; i < notes.size(); ++i)
                if (selected[i])
                {
                    notes[i].startSeconds = dragOrigin[i].startSeconds + dt;
                    notes[i].endSeconds = dragOrigin[i].endSeconds + dt;
                    notes[i].pitch = dragOrigin[i].pitch + dp;
                }
            if (notes[(size_t) dragNote].pitch != lastAudition)
                audition (notes[(size_t) dragNote].pitch);
            break;
        }

        case Drag::resize:
        {
            if (! juce::isPositiveAndBelow (dragNote, (int) dragOrigin.size()))
                break;
            const auto& anchor = dragOrigin[(size_t) dragNote];
            double dt = timeForX (e.position.x) - timeForX (dragStart.x);
            if (! free)
                dt = snap (anchor.endSeconds + dt) - anchor.endSeconds;
            const double minLength = free ? sixteenth() * 0.25 : sixteenth() * 0.5;
            for (size_t i = 0; i < notes.size(); ++i)
                if (selected[i])
                    notes[i].endSeconds = juce::jmax (dragOrigin[i].startSeconds + minLength, dragOrigin[i].endSeconds + dt);
            break;
        }
    }
    repaint();
}

void MidiView::mouseUp (const juce::MouseEvent&)
{
    const auto mode = drag;
    drag = Drag::none;
    heldKey = -1;
    dragNote = -1;
    if (mode == Drag::move || mode == Drag::resize || mode == Drag::velocity)
        commitEdit (dragOrigin);
    repaint();
}

void MidiView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! editable())
        return;
    const auto p = e.getPosition();

    if (ruler.contains (p))
    {
        drag = Drag::none;
        fitView();
        repaint();
        return;
    }
    if (! grid.contains (p))
        return;

    // restore what the second click's gesture may have touched, then edit
    if (drag == Drag::move || drag == Drag::resize)
        notes = dragOrigin;
    drag = Drag::none;
    syncSelection();

    const auto before = notes;
    bool edge = false;
    const int i = noteAt (e.position, edge);
    if (i >= 0)
    {
        notes.erase (notes.begin() + i);
        selected.erase (selected.begin() + i);
    }
    else
    {
        const double s = sixteenth();
        const double start = juce::jmax (0.0, std::floor (timeForX (e.position.x) / s) * s);
        midi::Note n;
        n.startSeconds = start;
        n.endSeconds = start + s;
        n.pitch = pitchForY (e.position.y);
        n.amplitude = 0.8f;
        notes.push_back (n);
        selected.assign (notes.size(), false);
        selected.back() = true;
        audition (n.pitch);
    }
    commitEdit (before);
}

void MidiView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! editable() || grid.isEmpty())
        return;

    const float dy = wheel.deltaY, dx = wheel.deltaX;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomAround (juce::jlimit ((float) grid.getX(), (float) grid.getRight(), e.position.x),
                    std::pow (2.0, (double) dy * 3.0));
        return;
    }
    if (e.mods.isAltDown())
    {
        // vertical zoom around the pitch under the mouse
        const float pitchAtMouse = topPitch - (e.position.y - (float) grid.getY()) / rowHeight;
        rowHeight *= std::pow (2.0f, dy * 2.0f);
        clampView();
        topPitch = pitchAtMouse + (e.position.y - (float) grid.getY()) / rowHeight;
        clampView();
        repaint();
        return;
    }
    if (e.mods.isShiftDown() || std::abs (dx) > std::abs (dy))
    {
        const float amount = std::abs (dx) > std::abs (dy) ? dx : dy;
        viewStart -= (double) amount * viewLength * 0.6;
    }
    else
    {
        topPitch += dy * 18.0f;
    }
    clampView();
    repaint();
}

void MidiView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (editable())
        zoomAround (juce::jlimit ((float) grid.getX(), (float) grid.getRight(), e.position.x), scaleFactor);
}

bool MidiView::keyPressed (const juce::KeyPress& key)
{
    if (! editable())
        return false;

    const auto mods = key.getModifiers();
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (selectedCount() == 0)
            return false;
        deleteSelected();
        return true;
    }
    const auto letter = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    if (mods.isCommandDown() && letter == 'A')
    {
        selectAll();
        return true;
    }
    if (mods.isCommandDown() && letter == 'Z')
    {
        undo();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        clearSelection();
        return true;
    }
    if ((key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey) && selectedCount() > 0)
    {
        const int step = mods.isShiftDown() ? 12 : 1;
        transpose (key.getKeyCode() == juce::KeyPress::upKey ? step : -step);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------- paint
void MidiView::paint (juce::Graphics& g)
{
    g.fillAll (well);
    drawToolbar (g);

    if (busy)
    {
        drawShredder (g, getLocalBounds().withTrimmedTop (toolbarHeight).toFloat());
        return;
    }

    drawGrid (g);
    drawKeyboard (g);

    if (emptyMessage.isNotEmpty())
    {
        auto area = getLocalBounds().withTrimmedTop (toolbarHeight).toFloat();
        g.setColour (well.withAlpha (0.72f));
        g.fillRect (area);
        drawMonoLabel (g, emptyMessage, area, juce::Justification::centred, muted, 11.0f);
        return;
    }

    drawNotes (g);
    drawVelocity (g);

    // playhead across ruler, grid and velocity lane
    if (playing && length > 0.0)
    {
        const float x = xForTime (playhead * length);
        if (x >= (float) grid.getX() && x <= (float) grid.getRight())
        {
            g.setColour (accentL.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (x - 2.0f, (float) ruler.getY(), 5.0f, (float) (velocity.getBottom() - ruler.getY())));
            g.setColour (accentL);
            g.fillRect (juce::Rectangle<float> (x - 0.5f, (float) ruler.getY(), 1.5f, (float) (velocity.getBottom() - ruler.getY())));
            juce::Path head;
            head.addTriangle (x - 4.5f, (float) ruler.getY() + 3.0f, x + 5.0f, (float) ruler.getY() + 3.0f, x + 0.25f, (float) ruler.getBottom() - 2.0f);
            g.fillPath (head);
        }
    }

    if (! hasKeyboardFocus (false))
        return;
    g.setColour (accent.withAlpha (0.25f));
    g.drawRect (getLocalBounds().withTrimmedTop (toolbarHeight), 1);
}

void MidiView::drawToolbar (juce::Graphics& g)
{
    const auto r = toolbar.toFloat();
    g.setGradientFill (juce::ColourGradient (card2.brighter (0.06f), 0.0f, r.getY(), card, 0.0f, r.getBottom(), false));
    g.fillRect (r);
    g.setColour (line);
    g.fillRect (r.withTop (r.getBottom() - 1.0f));

    // chrome title
    const auto title = juce::Rectangle<float> (r.getX() + 9.0f, r.getY() + 8.0f, (float) keysWidth + 30.0f, r.getHeight() - 16.0f);
    fillChrome (g, textPath ("SHREDDER", display (13.0f), title, juce::Justification::centredLeft), false);

    if (busy || infoArea.getWidth() < 40)
        return;

    juce::String info;
    if (emptyMessage.isNotEmpty())
        info = "NO MIDI";
    else if (notes.empty())
        info = "0 NOTES" + dot + "DOUBLE-CLICK TO DRAW";
    else
    {
        int lo = 127, hi = 0;
        for (const auto& n : notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
        const int sel = selectedCount();
        info = sel > 0 ? juce::String (sel) + " / " + juce::String ((int) notes.size()) + " SEL"
                       : juce::String ((int) notes.size()) + (notes.size() == 1 ? " NOTE" : " NOTES");
        const auto range = noteName (lo) + (lo != hi ? "-" + noteName (hi) : juce::String());
        const auto full = info + dot + range + dot + juce::String (juce::roundToInt (bpm)) + " BPM";
        const auto font = mono (10.0f, true).withExtraKerningFactor (0.16f);
        if (juce::GlyphArrangement::getStringWidth (font, full) < (float) infoArea.getWidth() - 4.0f)
            info = full;
        else if (juce::GlyphArrangement::getStringWidth (font, info + dot + range) < (float) infoArea.getWidth() - 4.0f)
            info += dot + range;
    }
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (infoArea);
    drawMonoLabel (g, info, infoArea.toFloat(), juce::Justification::centredLeft, muted, 10.0f);
}

void MidiView::drawKeyboard (juce::Graphics& g)
{
    // corner above the keys (next to the bar ruler) and below (next to the velocity lane)
    g.setColour (card);
    g.fillRect (keys.getX(), ruler.getY(), keys.getWidth(), rulerHeight);
    g.fillRect (keys.getX(), velocity.getY(), keys.getWidth(), velocity.getHeight());
    drawMonoLabel (g, "VEL", juce::Rectangle<float> ((float) keys.getX(), (float) velocity.getY(), (float) keys.getWidth(), (float) velocity.getHeight()),
                   juce::Justification::centred, muted.withAlpha (0.8f), 8.5f);

    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (keys);
    const auto k = keys.toFloat();

    // white keys: soft chrome-ish ivory; black keys: dark bars over them
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9da7b8), k.getX(), 0.0f, juce::Colour (0xffd9e0ea), k.getRight(), 0.0f, false));
    g.fillRect (k);

    // notes sounding under the playhead light their keys
    std::vector<bool> lit (128, false);
    if (playing && length > 0.0 && emptyMessage.isEmpty())
        for (const auto& n : notes)
            if (playhead * length >= n.startSeconds && playhead * length < n.endSeconds)
                lit[(size_t) n.pitch] = true;
    for (size_t i = 0; i < notes.size() && i < selected.size(); ++i)
        if (selected[i])
            lit[(size_t) notes[i].pitch] = true;

    const int lowest = pitchForY (k.getBottom() - 0.1f), highest = pitchForY (k.getY());
    const float blackWidth = k.getWidth() * 0.6f;

    for (int p = lowest; p <= highest; ++p)
    {
        const float y = yForPitch ((float) p);
        const bool held = p == heldKey;
        if (! isBlackKey (p))
        {
            if (held || lit[(size_t) p])
            {
                g.setColour (held ? accentL : accent.withAlpha (0.55f));
                g.fillRect (k.getX(), y, k.getWidth(), rowHeight);
            }
            // boundary between B|C and E|F: full width line at the row edge
            const int pc = p % 12;
            if (pc == 0 || pc == 5)
            {
                g.setColour (juce::Colour (0xff5d6678).withAlpha (0.8f));
                g.fillRect (k.getX(), y + rowHeight - 0.5f, k.getWidth(), 1.0f);
            }
        }
    }
    for (int p = lowest; p <= highest; ++p)
    {
        if (! isBlackKey (p))
            continue;
        const float y = yForPitch ((float) p);
        // white key boundary hidden behind the black key's centre
        g.setColour (juce::Colour (0xff5d6678).withAlpha (0.6f));
        g.fillRect (k.getX() + blackWidth, y + rowHeight * 0.5f - 0.5f, k.getWidth() - blackWidth, 1.0f);

        const auto bk = juce::Rectangle<float> (k.getX(), y + 0.5f, blackWidth, juce::jmax (1.5f, rowHeight - 1.0f));
        const bool on = p == heldKey || lit[(size_t) p];
        g.setGradientFill (juce::ColourGradient (on ? accent.darker (0.2f) : juce::Colour (0xff070a10), bk.getX(), 0.0f,
                                                 on ? accentL : juce::Colour (0xff222b3b), bk.getRight(), 0.0f, false));
        g.fillRoundedRectangle (bk, juce::jmin (2.0f, rowHeight * 0.25f));
    }

    // C labels
    const float labelHeight = juce::jlimit (8.0f, 10.0f, rowHeight + 1.0f);
    g.setFont (mono (labelHeight, true));
    for (int p = lowest; p <= highest; ++p)
        if (p % 12 == 0)
        {
            const float y = yForPitch ((float) p) + rowHeight * 0.5f;
            g.setColour (juce::Colour (0xff2a3346));
            g.drawText (noteName (p), juce::Rectangle<float> (k.getX(), y - labelHeight * 0.5f - 1.0f, k.getWidth() - 3.0f, labelHeight + 2.0f),
                        juce::Justification::centredRight, false);
        }

    // edge shadow into the grid
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), k.getRight() - 6.0f, 0.0f,
                                             juce::Colours::black.withAlpha (0.35f), k.getRight(), 0.0f, false));
    g.fillRect (k.withLeft (k.getRight() - 6.0f));
}

void MidiView::drawGrid (juce::Graphics& g)
{
    // ruler
    g.setColour (card);
    g.fillRect (ruler);
    g.setColour (line);
    g.fillRect (ruler.getX(), ruler.getBottom() - 1, ruler.getWidth(), 1);

    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (grid.withTop (ruler.getY()));
    const auto gr = grid.toFloat();

    // pitch rows (black keys shaded)
    const int lowest = pitchForY (gr.getBottom() - 0.1f), highest = pitchForY (gr.getY());
    for (int p = lowest; p <= highest; ++p)
    {
        const float y = yForPitch ((float) p);
        g.setColour (isBlackKey (p) ? juce::Colour (0xff070a10) : juce::Colour (0xff0c121d));
        g.fillRect (gr.getX(), y, gr.getWidth(), rowHeight);
        if (rowHeight >= 5.0f || p % 12 == 0)
        {
            g.setColour (p % 12 == 0 ? line.withMultipliedAlpha (1.4f) : juce::Colour (0x0d9fc3ff));
            g.fillRect (gr.getX(), y + rowHeight - 0.5f, gr.getWidth(), 1.0f);
        }
    }

    // time lines: 1/16, beats, bars
    const double s16 = sixteenth();
    const double px16 = s16 * pixelsPerSecond();
    const int first = (int) std::floor (viewStart / s16), last = (int) std::ceil ((viewStart + viewLength) / s16);
    const double pxBar = px16 * 16.0;
    const int barLabelStep = pxBar >= 34.0 ? 1 : pxBar >= 17.0 ? 2 : 4;
    g.setFont (mono (9.5f, true));
    for (int i = juce::jmax (0, first); i <= last; ++i)
    {
        const float x = std::round (xForTime (i * s16));
        if (i % 16 == 0)
        {
            g.setColour (juce::Colour (0x429fc3ff));
            g.fillRect (x, gr.getY(), 1.0f, gr.getHeight());
            g.setColour (line.withMultipliedAlpha (1.8f));
            g.fillRect (x, (float) ruler.getY() + 3.0f, 1.0f, (float) rulerHeight - 3.0f);
            const int bar = i / 16;
            if (bar % barLabelStep == 0)
            {
                g.setColour (text2);
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, (float) ruler.getY(), 30.0f, (float) rulerHeight - 1.0f),
                            juce::Justification::centredLeft, false);
            }
        }
        else if (i % 4 == 0)
        {
            if (px16 * 4.0 < 7.0)
                continue;
            g.setColour (juce::Colour (0x1f9fc3ff));
            g.fillRect (x, gr.getY(), 1.0f, gr.getHeight());
            g.setColour (line);
            g.fillRect (x, (float) ruler.getBottom() - 5.0f, 1.0f, 4.0f);
        }
        else if (px16 >= 9.0)
        {
            g.setColour (juce::Colour (0x0c9fc3ff));
            g.fillRect (x, gr.getY(), 1.0f, gr.getHeight());
        }
    }

    // past the clip end
    if (length > 0.0)
    {
        const float xEnd = xForTime (length);
        if (xEnd < gr.getRight())
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRect (gr.withLeft (juce::jmax (gr.getX(), xEnd)));
            g.setColour (accent.withAlpha (0.5f));
            g.fillRect (xEnd, (float) ruler.getY(), 1.0f, gr.getBottom() - (float) ruler.getY());
        }
    }
}

void MidiView::drawNotes (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (grid);
    const float radius = juce::jlimit (1.0f, 3.0f, rowHeight * 0.25f);

    // unselected first, selected (with glow) on top
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const bool sel = selected[(size_t) i];
            if (sel != (pass == 1))
                continue;
            const auto r = getNoteBounds (i);
            if (r.isEmpty())
                continue;
            const auto& n = notes[(size_t) i];

            if (sel)
            {
                g.setColour (accentL.withAlpha (0.10f));
                g.fillRoundedRectangle (r.expanded (4.0f), radius + 4.0f);
                g.setColour (accentL.withAlpha (0.22f));
                g.fillRoundedRectangle (r.expanded (2.0f), radius + 2.0f);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7f9fd), 0.0f, r.getY(),
                                                         juce::Colour (0xffc4d6f5), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, radius);
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            }
            else
            {
                const auto c = velocityColour (n.amplitude);
                if (rowHeight < 7.0f)
                {
                    // thin rows: solid bar, no outline eating the colour
                    g.setColour (i == hoverNote ? c.brighter (0.4f) : c);
                    g.fillRoundedRectangle (r, radius);
                    continue;
                }
                g.setGradientFill (juce::ColourGradient (c.brighter (0.18f), 0.0f, r.getY(), c.darker (0.25f), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, radius);
                g.setColour (i == hoverNote ? accentL : c.darker (0.9f).withAlpha (0.9f));
                g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
                if (rowHeight >= 6.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.22f));
                    g.fillRect (r.getX() + radius, r.getY() + 1.0f, juce::jmax (0.0f, r.getWidth() - radius * 2.0f), 1.0f);
                }
            }

            // resize grip when hovering the right edge
            if (i == hoverNote && hoverEdge && drag == Drag::none)
            {
                g.setColour (sel ? well : juce::Colours::white.withAlpha (0.8f));
                g.fillRect (r.getRight() - 3.5f, r.getY() + 2.0f, 1.5f, juce::jmax (1.0f, r.getHeight() - 4.0f));
            }

            // note name inside when there is room
            if (rowHeight >= 9.0f && r.getWidth() >= 26.0f)
            {
                g.setColour (sel ? juce::Colour (0xff1a2438) : juce::Colour (0xff050a14).withAlpha (0.8f));
                g.setFont (mono (juce::jmin (9.5f, rowHeight - 1.0f), true));
                g.drawText (noteName (n.pitch), r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        }

    // lasso
    if (drag == Drag::lasso)
    {
        const auto lasso = juce::Rectangle<float> (dragStart, dragNow);
        g.setColour (accent.withAlpha (0.10f));
        g.fillRect (lasso);
        g.setColour (accentL.withAlpha (0.75f));
        g.drawRect (lasso, 1.0f);
    }
}

void MidiView::drawVelocity (juce::Graphics& g)
{
    const auto v = velocity.withLeft (grid.getX()).toFloat();
    g.setColour (card);
    g.fillRect (v);
    g.setColour (line);
    g.fillRect (velocity.getX(), velocity.getY(), velocity.getWidth(), 1);
    g.setColour (juce::Colour (0x0f9fc3ff));
    g.fillRect (v.getX(), v.getY() + 3.0f + (v.getHeight() - 7.0f) * 0.5f, v.getWidth(), 1.0f);

    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (v.toNearestInt());
    const float base = v.getBottom() - 3.0f, span = v.getHeight() - 7.0f;

    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const bool sel = selected[(size_t) i];
            if (sel != (pass == 1))
                continue;
            const auto& n = notes[(size_t) i];
            const float x = xForTime (n.startSeconds);
            if (x < v.getX() - 4.0f || x > v.getRight() + 4.0f)
                continue;
            const float top = base - juce::jlimit (0.0f, 1.0f, n.amplitude) * span;
            const auto c = sel ? juce::Colour (0xfff2f6ff) : velocityColour (n.amplitude);

            // bar spanning the note's length (faint), stem and head
            const float x2 = juce::jmax (x + 2.0f, xForTime (n.endSeconds) - 1.0f);
            g.setColour (c.withAlpha (sel ? 0.22f : 0.12f));
            g.fillRect (x, top, x2 - x, base - top);
            g.setColour (c);
            g.fillRect (x, top, 1.5f, base - top);
            g.fillEllipse (x - 2.25f, top - 2.5f, 5.0f, 5.0f);
        }
}

void MidiView::drawShredder (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float t = (float) animTime;
    g.setColour (well);
    g.fillRect (area);

    auto inner = area.reduced (16.0f, 10.0f);
    const auto labelRow = inner.removeFromTop (18.0f);
    const auto footRow = inner.removeFromBottom (14.0f);
    const auto band = inner.withSizeKeepingCentre (inner.getWidth(), juce::jmin (inner.getHeight() - 8.0f, 112.0f));
    const float cy = band.getCentreY(), bladeX = band.getCentreX();
    constexpr float speed = 70.0f;

    // ---- left: the audio, travelling into the blade and cut into ribbons near it
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (band.withRight (bladeX - 5.0f).toNearestInt());
        const float fadeIn = 140.0f, shredZone = 64.0f;
        for (float x = band.getX(); x < bladeX - 5.0f; x += 3.0f)
        {
            const float u = x - t * speed;
            const float env = 0.55f + 0.45f * std::sin (u * 0.011f);
            const float a = juce::jlimit (0.06f, 1.0f, env * std::abs (0.55f * std::sin (u * 0.047f) + 0.32f * std::sin (u * 0.131f + 1.3f)
                                                                       + 0.25f * std::sin (u * 0.023f + 0.4f)));
            const float h = a * band.getHeight() * 0.46f;
            const float fade = juce::jlimit (0.0f, 1.0f, (x - band.getX()) / fadeIn);
            const float near = juce::jlimit (0.0f, 1.0f, 1.0f - (bladeX - x) / shredZone);
            const auto c = accent.interpolatedWith (accentL, near).withAlpha (0.25f + 0.65f * fade);

            if (near <= 0.0f)
            {
                g.setColour (c);
                g.fillRect (x, cy - h, 2.0f, h * 2.0f);
                continue;
            }
            // shredded: horizontal strips, slightly scattered as they reach the blade
            const float strip = 6.0f, gap = 1.0f + near * 2.0f;
            int k = 0;
            for (float y = cy - h; y < cy + h; y += strip, ++k)
            {
                const float jitter = near * near * 3.0f * (k % 2 == 0 ? 1.0f : -1.0f);
                g.setColour (c.withMultipliedAlpha (1.0f - near * 0.25f * (float) (k % 3)));
                g.fillRect (x, y + jitter, 2.0f, juce::jmin (strip - gap, cy + h - y));
            }
        }
    }

    // ---- right: MIDI notes coming out of the blade on a faint piano-roll grid
    {
        const auto out = band.withLeft (bladeX + 5.0f);
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (out.toNearestInt());
        constexpr int rows = 9;
        const float rh = out.getHeight() / (float) rows;
        for (int r = 0; r <= rows; ++r)
        {
            g.setColour (juce::Colour (r == 0 || r == rows ? 0x1a9fc3ff : 0x0d9fc3ff));
            g.fillRect (out.getX(), out.getY() + (float) r * rh - 0.5f, out.getWidth(), 1.0f);
        }
        const float beat = 44.0f, offset = std::fmod (t * speed, beat);
        for (float x = out.getX() + offset; x < out.getRight(); x += beat)
        {
            g.setColour (juce::Colour (0x129fc3ff));
            g.fillRect (x, out.getY(), 1.0f, out.getHeight());
        }

        constexpr float interval = 0.2f;
        const int newest = (int) std::floor (t / interval);
        const int oldest = newest - (int) (out.getWidth() / (speed * interval)) - 8;
        for (int k = oldest; k <= newest; ++k)
        {
            const float age = t - (float) k * interval;
            if (hash01 (k, 7) < 0.25f)
                continue;   // rests
            const float w = 10.0f + std::floor (hash01 (k, 1) * 4.0f) * 9.0f;
            const int row = juce::jlimit (0, rows - 1, 4 + juce::roundToInt (3.2f * std::sin ((float) k * 0.7f) + (hash01 (k, 2) - 0.5f) * 2.0f));
            const float right = bladeX + 5.0f + age * speed;
            auto r = juce::Rectangle<float> (right - w, out.getY() + (float) (rows - 1 - row) * rh + 1.0f, w, rh - 2.0f);
            r.setLeft (juce::jmax (r.getX(), bladeX + 5.0f));
            if (r.getWidth() < 1.0f)
                continue;
            const float fade = juce::jlimit (0.0f, 1.0f, (out.getRight() - r.getX()) / 160.0f);
            const auto c = velocityColour (0.35f + 0.65f * hash01 (k, 3));
            const bool fresh = age < 0.35f;
            if (fresh)
            {
                g.setColour (accentL.withAlpha (0.25f * (1.0f - age / 0.35f) * fade));
                g.fillRoundedRectangle (r.expanded (3.0f), 4.0f);
            }
            g.setGradientFill (juce::ColourGradient (c.brighter (0.2f).withMultipliedAlpha (fade), 0.0f, r.getY(),
                                                     c.darker (0.25f).withMultipliedAlpha (fade), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 2.0f);
        }
    }

    // ---- the blade: chrome bar with teeth and a glowing cut line
    {
        const auto bladeRect = juce::Rectangle<float> (bladeX - 4.0f, band.getY() - 10.0f, 8.0f, band.getHeight() + 20.0f);
        juce::Path blade;
        blade.addRoundedRectangle (bladeRect, 3.0f);
        // teeth on both sides
        for (float y = bladeRect.getY() + 6.0f; y < bladeRect.getBottom() - 6.0f; y += 7.0f)
        {
            const float shift = std::fmod (t * 40.0f, 7.0f);
            const float yy = juce::jmin (bladeRect.getBottom() - 9.0f, y + shift);
            blade.addTriangle (bladeRect.getX(), yy, bladeRect.getX() - 3.5f, yy + 2.5f, bladeRect.getX(), yy + 5.0f);
            blade.addTriangle (bladeRect.getRight(), yy, bladeRect.getRight() + 3.5f, yy + 2.5f, bladeRect.getRight(), yy + 5.0f);
        }
        juce::DropShadow (glow.withAlpha (0.8f), 18, {}).drawForPath (g, blade);
        juce::ColourGradient chrome (juce::Colour (0xff8e98ab), bladeRect.getX() - 4.0f, 0.0f, juce::Colour (0xff9aa3b6), bladeRect.getRight() + 4.0f, 0.0f, false);
        chrome.addColour (0.35, juce::Colour (0xfff5f7fc));
        chrome.addColour (0.55, juce::Colour (0xff6f7a90));
        chrome.addColour (0.75, juce::Colour (0xffdfe5ef));
        g.setGradientFill (chrome);
        g.fillPath (blade);

        const float pulse = 0.55f + 0.45f * std::sin (t * 6.0f);
        g.setColour (accentL.withAlpha (0.35f + 0.4f * pulse));
        g.fillRect (bladeX - 0.5f, bladeRect.getY() + 3.0f, 1.0f, bladeRect.getHeight() - 6.0f);

        // sparks
        const int frame = (int) (t * 15.0f);
        for (int k = 0; k < 6; ++k)
        {
            const int id = frame * 6 + k;
            const float life = hash01 (id, 4);
            const float sx = bladeX + 6.0f + hash01 (id, 5) * 26.0f;
            const float sy = cy + (hash01 (id, 6) - 0.5f) * band.getHeight() * 0.9f;
            g.setColour (accentL.withAlpha (0.2f + 0.6f * life));
            g.fillEllipse (sx, sy, 1.6f + life * 1.4f, 1.6f + life * 1.4f);
        }
    }

    // ---- captions
    const int dots = ((int) (t * 3.0f)) % 4;
    const auto caption = juce::String ("SHREDDING TO MIDI") + juce::String::repeatedString (".", dots)
                       + juce::String::repeatedString (" ", 3 - dots);
    drawMonoLabel (g, caption, labelRow, juce::Justification::centred, accentL, 11.0f);
    drawMonoLabel (g, "AUDIO", footRow, juce::Justification::centredLeft, muted.withAlpha (0.7f), 8.5f);
    drawMonoLabel (g, "MIDI", footRow, juce::Justification::centredRight, muted.withAlpha (0.7f), 8.5f);
    drawMonoLabel (g, "BASIC PITCH" + dot + "POLYPHONIC", footRow, juce::Justification::centred, muted.withAlpha (0.45f), 8.5f);
}

} // namespace digga
