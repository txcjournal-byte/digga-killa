#include "ui/WaveformEditor.h"

namespace digga
{

using namespace theme;

namespace
{
    constexpr double minViewSamples = 16.0;     // deepest zoom: ~16 samples across the view
    constexpr double minCropSeconds = 0.005;    // start < end by at least 5 ms
    constexpr int firstBinSize = 16, binFactor = 4;
    constexpr float markerGrab = 8.0f;          // px around a marker that grab it
    constexpr float sidePad = 10.0f;            // room for the handles at the ends

    const juce::Colour loopColour { 0xff7ef0cf };   // mint: loop start (differs from the blue crop)

    juce::String dot() { return juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")); }

    // a nice ruler step (1, 2, 5 x 10^n seconds) at least minSeconds wide
    double niceStep (double minSeconds, int& minorDivisions)
    {
        for (int e = -5; e <= 4; ++e)
            for (int m : { 1, 2, 5 })
            {
                const double s = m * std::pow (10.0, e);
                if (s >= minSeconds)
                {
                    minorDivisions = m == 2 ? 4 : 5;
                    return s;
                }
            }
        minorDivisions = 5;
        return 10000.0;
    }
}

WaveformEditor::WaveformEditor()
{
    setOpaque (false);
    setTooltip (juce::String (juce::CharPointer_UTF8 (
        "Wheel / pinch: zoom \xc2\xb7 Shift+wheel: scroll \xc2\xb7 Drag ruler: zoom \xc2\xb7 Drag wave: scroll \xc2\xb7 "
        "Click: audition \xc2\xb7 Double-click: zoom to crop / fit \xc2\xb7 Alt: no snap \xc2\xb7 Shift: fine")));
}

WaveformEditor::~WaveformEditor()
{
    stopTimer();
}

//==============================================================================
void WaveformEditor::setAudio (AudioPtr a, double sr)
{
    const bool same = a == audio;
    audio = std::move (a);
    sampleRate = sr > 0.0 ? sr : 44100.0;
    if (same)
    {
        repaint();
        return;
    }

    drag = hover = Drag::none;
    buildCache();
    zoomedToSelection = false;
    if (numSamples > 0)
        setViewTarget (0.0, (double) numSamples, false);
    repaint();
}

void WaveformEditor::setGrid (double b, bool loop)
{
    bpm = b;
    isLoop = loop;
    repaint();
}

void WaveformEditor::setRange (double s, double e)
{
    if (drag == Drag::start || drag == Drag::end)
        return;   // the user owns the markers while dragging
    rangeStart = juce::jlimit (0.0, 1.0, juce::jmin (s, e));
    rangeEnd = juce::jlimit (rangeStart, 1.0, juce::jmax (s, e));
    repaint();
}

void WaveformEditor::setLoopStart (double f)
{
    if (drag == Drag::loop)
        return;
    loopStart = juce::jlimit (0.0, 1.0, f);
    repaint();
}

void WaveformEditor::setPlayhead (bool p, double f)
{
    if (p == playing && std::abs (f - playhead) < 1.0e-9)
        return;

    const float oldX = sampleToX (playhead * numSamples);
    const bool wasPlaying = playing;
    playing = p;
    playhead = juce::jlimit (0.0, 1.0, f);

    if (numSamples <= 0)
        return;

    // follow: page the view along when the playhead leaves it
    const double pos = playhead * numSamples;
    if (playing && followPlayhead && drag == Drag::none && targetLen < numSamples * 0.999
        && (pos < targetStart || pos > targetStart + targetLen * 0.98))
    {
        setViewTarget (pos - targetLen * 0.05, targetLen, false);
        repaint();
        return;
    }

    // only the strips the playhead leaves and enters
    const float newX = sampleToX (pos);
    const auto h = (float) getHeight();
    if (wasPlaying)
        repaint (juce::Rectangle<float> (oldX - 7.0f, 0.0f, 14.0f, h).getSmallestIntegerContainer());
    repaint (juce::Rectangle<float> (newX - 7.0f, 0.0f, 14.0f, h).getSmallestIntegerContainer());
    repaint (overviewArea.getSmallestIntegerContainer());
}

void WaveformEditor::setSnap (Snap s)
{
    snap = s;
}

void WaveformEditor::setAccent (juce::Colour c)
{
    accent = c;
    repaint();
}

//==============================================================================
void WaveformEditor::buildCache()
{
    levels.clear();
    overviewValid = false;
    numSamples = audio != nullptr ? audio->getNumSamples() : 0;
    numChannels = audio != nullptr ? audio->getNumChannels() : 0;
    displayGain = 1.0f;
    if (numSamples <= 0 || numChannels <= 0)
    {
        numSamples = 0;
        return;
    }

    const auto& buf = *audio;
    float peak = 0.0f;

    // finest level straight from the audio
    {
        Level l;
        l.binSize = firstBinSize;
        const int bins = (numSamples + firstBinSize - 1) / firstBinSize;
        l.mn.resize ((size_t) bins);
        l.mx.resize ((size_t) bins);
        l.ms.resize ((size_t) bins);
        for (int b = 0; b < bins; ++b)
        {
            const int s0 = b * firstBinSize, s1 = juce::jmin (numSamples, s0 + firstBinSize);
            float lo = 1.0e9f, hi = -1.0e9f;
            double sq = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* d = buf.getReadPointer (ch);
                for (int i = s0; i < s1; ++i)
                {
                    lo = juce::jmin (lo, d[i]);
                    hi = juce::jmax (hi, d[i]);
                    sq += (double) d[i] * d[i];
                }
            }
            l.mn[(size_t) b] = lo;
            l.mx[(size_t) b] = hi;
            l.ms[(size_t) b] = (float) (sq / juce::jmax (1, (s1 - s0) * numChannels));
            peak = juce::jmax (peak, -lo, hi);
        }
        levels.push_back (std::move (l));
    }

    // coarser levels, each binFactor times wider
    while (levels.back().mn.size() > 256)
    {
        const auto& prev = levels.back();
        Level l;
        l.binSize = prev.binSize * binFactor;
        const size_t n = (prev.mn.size() + binFactor - 1) / binFactor;
        l.mn.resize (n);
        l.mx.resize (n);
        l.ms.resize (n);
        for (size_t b = 0; b < n; ++b)
        {
            const size_t p0 = b * binFactor, p1 = juce::jmin (prev.mn.size(), p0 + binFactor);
            float lo = prev.mn[p0], hi = prev.mx[p0], sq = 0.0f;
            for (size_t p = p0; p < p1; ++p)
            {
                lo = juce::jmin (lo, prev.mn[p]);
                hi = juce::jmax (hi, prev.mx[p]);
                sq += prev.ms[p];
            }
            l.mn[b] = lo;
            l.mx[b] = hi;
            l.ms[b] = sq / (float) (p1 - p0);
        }
        levels.push_back (std::move (l));
    }

    // quiet material is shown bigger (up to +12 dB), loud material as is
    if (peak > 1.0e-5f)
        displayGain = juce::jlimit (1.0f, 4.0f, 0.95f / peak);
}

void WaveformEditor::computeColumns (double start, double spc, int numColumns,
                                     float* mn, float* mx, float* rms) const
{
    // the coarsest level that still has at least one bin per column
    const Level* level = nullptr;
    for (auto it = levels.rbegin(); it != levels.rend(); ++it)
        if ((double) it->binSize <= spc)
        {
            level = &*it;
            break;
        }

    const auto& buf = *audio;
    for (int c = 0; c < numColumns; ++c)
    {
        const double a = start + c * spc;
        const auto ia = (int) std::floor (a);
        if (ia >= numSamples || a + spc < 0.0)
        {
            mn[c] = mx[c] = rms[c] = 0.0f;
            continue;
        }

        const int i0 = juce::jmax (0, ia);
        float lo = 1.0e9f, hi = -1.0e9f;
        double sq = 0.0;
        int count = 0;

        if (level != nullptr)
        {
            const int bs = level->binSize;
            const auto bins = (int) level->mn.size();
            const int b0 = juce::jmin (bins - 1, i0 / bs);
            const int b1 = juce::jlimit (b0 + 1, bins, (int) std::ceil ((a + spc) / bs));
            for (int b = b0; b < b1; ++b)
            {
                lo = juce::jmin (lo, level->mn[(size_t) b]);
                hi = juce::jmax (hi, level->mx[(size_t) b]);
                sq += level->ms[(size_t) b];
            }
            count = b1 - b0;
        }
        else
        {
            // close in: the samples themselves (+ the next one, so the trace is continuous)
            const int i1 = juce::jlimit (i0 + 1, numSamples, (int) std::floor (a + spc) + 1);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* d = buf.getReadPointer (ch);
                for (int i = i0; i < i1; ++i)
                {
                    lo = juce::jmin (lo, d[i]);
                    hi = juce::jmax (hi, d[i]);
                    sq += (double) d[i] * d[i];
                }
            }
            count = (i1 - i0) * numChannels;
        }

        mn[c] = lo;
        mx[c] = hi;
        rms[c] = (float) std::sqrt (sq / juce::jmax (1, count));
    }
}

float WaveformEditor::monoSample (int index) const noexcept
{
    float sum = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        sum += audio->getSample (ch, index);
    return sum / (float) juce::jmax (1, numChannels);
}

double WaveformEditor::nearestZeroCrossing (double pos) const
{
    if (numSamples < 2)
        return pos;

    // search a small window around the position (a few px, at most 10 ms)
    const auto centre = juce::jlimit (1, numSamples - 1, (int) std::round (pos));
    const auto window = (int) juce::jlimit (32.0, juce::jmax (32.0, sampleRate * 0.01), 8.0 * samplesPerPixel());

    auto crossingAt = [this] (int i) -> int
    {
        if (i < 1 || i >= numSamples)
            return -1;
        const float a = monoSample (i - 1), b = monoSample (i);
        if ((a < 0.0f) == (b < 0.0f) && std::abs (b) > 0.0f)
            return -1;
        return std::abs (a) < std::abs (b) ? i - 1 : i;
    };

    for (int d = 0; d <= window; ++d)
    {
        if (const int r = crossingAt (centre + d); r >= 0)
            return (double) r;
        if (const int l = crossingAt (centre - d); l >= 0)
            return (double) l;
    }
    return pos;
}

double WaveformEditor::beatSamples() const noexcept
{
    return bpm > 0.0 ? 60.0 / bpm * sampleRate : 0.0;
}

double WaveformEditor::snapSamples (double pos, bool bypass) const
{
    if (bypass || snap == Snap::off || numSamples <= 0)
        return pos;
    if (snap == Snap::zeroCrossing)
        return nearestZeroCrossing (pos);

    const double beat = beatSamples();
    if (beat <= 0.0)
        return pos;   // no tempo: nothing to snap to

    const double step = beat * (snap == Snap::sixteenth ? 0.25 : snap == Snap::eighth ? 0.5 : snap == Snap::beat ? 1.0 : 4.0);
    return juce::jlimit (0.0, (double) numSamples, std::round (pos / step) * step);
}

double WaveformEditor::snapPosition (double fraction, bool bypassSnap) const
{
    if (numSamples <= 0)
        return fraction;
    return snapSamples (fraction * numSamples, bypassSnap) / numSamples;
}

//==============================================================================
juce::Range<double> WaveformEditor::getVisibleRange() const noexcept
{
    if (numSamples <= 0)
        return { 0.0, 1.0 };
    return { targetStart / numSamples, (targetStart + targetLen) / numSamples };
}

void WaveformEditor::setVisibleRange (double start, double end)
{
    if (numSamples > 0)
        setViewTarget (start * numSamples, (end - start) * numSamples, false);
}

void WaveformEditor::setViewTarget (double start, double length, bool animate)
{
    if (numSamples <= 0)
        return;

    const double n = numSamples;
    length = juce::jlimit (juce::jmin (minViewSamples, n), n, length);
    start = juce::jlimit (0.0, n - length, start);
    targetStart = start;
    targetLen = length;

    if (animate && smoothZoom)
    {
        if (! isTimerRunning())
            startTimerHz (60);
    }
    else
    {
        viewStart = start;
        viewLen = length;
        repaint();
    }
}

void WaveformEditor::zoomAround (double anchor, double anchorFraction, double newLength, bool animate)
{
    if (numSamples <= 0)
        return;
    newLength = juce::jlimit (juce::jmin (minViewSamples, (double) numSamples), (double) numSamples, newLength);
    setViewTarget (anchor - anchorFraction * newLength, newLength, animate);
}

void WaveformEditor::zoomIn()
{
    const double anchor = playing && playhead * numSamples >= targetStart && playhead * numSamples <= targetStart + targetLen
                              ? playhead * numSamples : targetStart + targetLen * 0.5;
    zoomAround (anchor, (anchor - targetStart) / juce::jmax (1.0, targetLen), targetLen * 0.5, true);
    zoomedToSelection = false;
}

void WaveformEditor::zoomOut()
{
    const double anchor = targetStart + targetLen * 0.5;
    zoomAround (anchor, 0.5, targetLen * 2.0, true);
    zoomedToSelection = false;
}

void WaveformEditor::zoomToFit()
{
    setViewTarget (0.0, (double) numSamples, true);
    zoomedToSelection = false;
}

void WaveformEditor::zoomToSelection()
{
    const double s = rangeStart * numSamples, e = rangeEnd * numSamples;
    const double pad = (e - s) * 0.04;
    setViewTarget (s - pad, (e - s) + 2.0 * pad, true);
    zoomedToSelection = true;
}

void WaveformEditor::timerCallback()
{
    bool busy = false;

    // edge scroll while a marker is dragged past the side of the view
    if ((drag == Drag::start || drag == Drag::end || drag == Drag::loop) && std::abs (edgeSpeed) > 0.0f)
    {
        setViewTarget (targetStart + edgeSpeed * targetLen * 0.02, targetLen, false);
        updateMarkerDrag (lastMousePos.x, lastMods);
        busy = true;
    }

    // smooth zoom / scroll towards the target
    const double lenErr = std::abs (viewLen - targetLen) / juce::jmax (1.0, targetLen);
    const double posErr = std::abs (viewStart - targetStart) / juce::jmax (1.0e-9, samplesPerPixel());
    if (lenErr > 0.002 || posErr > 0.25)
    {
        constexpr double k = 0.32;
        if (lenErr > 0.001)
        {
            // the point that stays put between the current and the target view
            const double fixed = (targetStart * viewLen - viewStart * targetLen) / (viewLen - targetLen);
            const double a = (fixed - viewStart) / viewLen;
            const double len = std::exp (std::log (viewLen) + (std::log (targetLen) - std::log (viewLen)) * k);
            if (std::isfinite (fixed) && std::abs (a) < 4.0)
            {
                viewStart = fixed - a * len;
                viewLen = len;
            }
            else
            {
                viewStart += (targetStart - viewStart) * k;
                viewLen = len;
            }
        }
        else
        {
            viewStart += (targetStart - viewStart) * k;
            viewLen = targetLen;
        }
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, numSamples - viewLen), viewStart);
        busy = true;
    }
    else
    {
        viewStart = targetStart;
        viewLen = targetLen;
    }

    repaint();
    if (! busy)
        stopTimer();
}

//==============================================================================
double WaveformEditor::samplesPerPixel() const noexcept
{
    return viewLen / juce::jmax (1.0f, waveArea.getWidth());
}

float WaveformEditor::sampleToX (double s) const noexcept
{
    return waveArea.getX() + (float) ((s - viewStart) / juce::jmax (1.0e-9, viewLen) * waveArea.getWidth());
}

double WaveformEditor::xToSample (float x) const noexcept
{
    return viewStart + (double) (x - waveArea.getX()) / juce::jmax (1.0f, waveArea.getWidth()) * viewLen;
}

float WaveformEditor::getXForFraction (double fraction) const noexcept
{
    return sampleToX (fraction * numSamples);
}

float WaveformEditor::overviewX (double s) const noexcept
{
    return overviewArea.getX() + (float) (s / juce::jmax (1, numSamples)) * overviewArea.getWidth();
}

double WaveformEditor::overviewToSample (float x) const noexcept
{
    return (double) (x - overviewArea.getX()) / juce::jmax (1.0f, overviewArea.getWidth()) * numSamples;
}

void WaveformEditor::resized()
{
    auto b = getLocalBounds().toFloat();
    rulerArea = b.removeFromTop (20.0f);
    auto strip = b.removeFromBottom (17.0f);
    waveArea = b.reduced (sidePad, 0.0f);
    rulerArea = rulerArea.reduced (sidePad, 0.0f);
    overviewArea = strip.reduced (sidePad, 0.0f).withTrimmedTop (3.0f).withTrimmedBottom (4.0f);

    const auto cols = (size_t) juce::jmax (1, (int) std::ceil (waveArea.getWidth()) + 5);
    colMin.assign (cols, 0.0f);
    colMax.assign (cols, 0.0f);
    colRms.assign (cols, 0.0f);
    const auto ovCols = (size_t) juce::jmax (1, (int) std::ceil (overviewArea.getWidth()));
    ovMin.assign (ovCols, 0.0f);
    ovMax.assign (ovCols, 0.0f);
    ovRms.assign (ovCols, 0.0f);
    overviewValid = false;
}

//==============================================================================
double WaveformEditor::markerSamples (Drag d) const noexcept
{
    const double s = rangeStart * numSamples, e = rangeEnd * numSamples;
    switch (d)
    {
        case Drag::start: return s;
        case Drag::end:   return e;
        case Drag::loop:  return s + loopStart * (e - s);
        case Drag::none: case Drag::pan: case Drag::ruler: case Drag::overview: break;
    }
    return 0.0;
}

WaveformEditor::Drag WaveformEditor::hitTestMarker (juce::Point<float> p) const
{
    if (numSamples <= 0 || p.y < waveArea.getY() || p.y > waveArea.getBottom())
        return Drag::none;

    const float sx = sampleToX (markerSamples (Drag::start)), ex = sampleToX (markerSamples (Drag::end));
    const float lx = sampleToX (markerSamples (Drag::loop));

    // the loop flag sits at the top and wins there
    if (isLoop && p.y < waveArea.getY() + 22.0f && p.x > lx - 4.0f && p.x < lx + 14.0f)
        return Drag::loop;

    const float ds = std::abs (p.x - sx), de = std::abs (p.x - ex);
    if (juce::jmin (ds, de) <= markerGrab)
    {
        if (std::abs (ds - de) < 0.5f)
            return p.x <= (sx + ex) * 0.5f ? Drag::start : Drag::end;
        return ds < de ? Drag::start : Drag::end;
    }

    if (isLoop && std::abs (p.x - lx) <= 5.0f)
        return Drag::loop;
    return Drag::none;
}

void WaveformEditor::updateCursor (juce::Point<float> p)
{
    auto type = juce::MouseCursor::NormalCursor;
    const auto m = drag != Drag::none ? drag : hitTestMarker (p);
    if (m == Drag::start || m == Drag::end || m == Drag::loop)
        type = juce::MouseCursor::LeftRightResizeCursor;
    else if (m == Drag::pan || m == Drag::overview || (m == Drag::none && overviewArea.expanded (0.0f, 3.0f).contains (p)))
        type = drag == Drag::none ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor;
    else if (m == Drag::ruler || rulerArea.contains (p))
        type = juce::MouseCursor::UpDownLeftRightResizeCursor;
    setMouseCursor (type);
}

void WaveformEditor::mouseMove (const juce::MouseEvent& e)
{
    if (numSamples <= 0)
        return;
    const auto h = hitTestMarker (e.position);
    if (h != hover)
    {
        hover = h;
        repaint();
    }
    updateCursor (e.position);
}

void WaveformEditor::mouseExit (const juce::MouseEvent&)
{
    if (hover != Drag::none)
    {
        hover = Drag::none;
        repaint();
    }
}

void WaveformEditor::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    edgeSpeed = 0.0f;
    if (numSamples <= 0)
        return;

    downPos = lastMousePos = e.position;
    lastMods = e.mods;
    downViewStart = targetStart;
    downViewLen = targetLen;
    downStartValue = rangeStart;
    downEndValue = rangeEnd;
    downLoopValue = loopStart;

    // jump any running animation to its end so drags work on what is shown
    viewStart = targetStart;
    viewLen = targetLen;

    if (overviewArea.expanded (0.0f, 3.0f).contains (e.position))
    {
        drag = Drag::overview;
        const double s = overviewToSample (e.position.x);
        if (s >= targetStart && s <= targetStart + targetLen)
            grabOffset = s - targetStart;
        else
        {
            grabOffset = targetLen * 0.5;
            setViewTarget (s - grabOffset, targetLen, false);
        }
    }
    else if (const auto m = hitTestMarker (e.position); m != Drag::none)
    {
        drag = m;
        dragRaw = markerSamples (m);
        grabOffset = xToSample (e.position.x) - dragRaw;
        fineMode = e.mods.isShiftDown();
        fineAnchorX = e.position.x;
        fineAnchorPos = dragRaw;
    }
    else if (rulerArea.withX (0.0f).withWidth ((float) getWidth()).contains (e.position))
    {
        drag = Drag::ruler;
        downAnchor = xToSample (e.position.x);
        downAnchorFraction = (downAnchor - targetStart) / juce::jmax (1.0, targetLen);
    }
    else
    {
        drag = Drag::pan;
    }

    updateCursor (e.position);
    repaint();
}

void WaveformEditor::updateMarkerDrag (float x, juce::ModifierKeys mods)
{
    const double spp = samplesPerPixel();
    const bool fine = mods.isShiftDown();
    if (fine != fineMode)
    {
        // re-anchor so switching speed never makes the marker jump
        if (fine)
        {
            fineAnchorX = x;
            fineAnchorPos = dragRaw;
        }
        else
        {
            grabOffset = xToSample (x) - dragRaw;
        }
        fineMode = fine;
    }

    dragRaw = fine ? fineAnchorPos + (x - fineAnchorX) * spp * 0.1 : xToSample (x) - grabOffset;
    dragRaw = juce::jlimit (0.0, (double) numSamples, dragRaw);

    const double pos = snapSamples (dragRaw, mods.isAltDown());
    const double n = numSamples;
    const double minLen = juce::jmin (n * 0.5, minCropSeconds * sampleRate);
    const double s = rangeStart * n, e = rangeEnd * n;

    if (drag == Drag::start)
        rangeStart = juce::jlimit (0.0, e - minLen, pos) / n;
    else if (drag == Drag::end)
        rangeEnd = juce::jlimit (s + minLen, n, pos) / n;
    else if (drag == Drag::loop)
    {
        double f = (juce::jlimit (s, e, pos) - s) / juce::jmax (1.0, e - s);
        if (f >= 1.0 - 1.0e-9)
            f = 0.0;   // the end of the crop is its start again (it wraps)
        loopStart = juce::jlimit (0.0, 1.0, f);
    }
    repaint();
}

void WaveformEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (numSamples <= 0 || drag == Drag::none)
        return;

    lastMousePos = e.position;
    lastMods = e.mods;

    switch (drag)
    {
        case Drag::start: case Drag::end: case Drag::loop:
        {
            updateMarkerDrag (e.position.x, e.mods);

            // push the view along when dragging past its sides
            const float l = waveArea.getX() + 14.0f, r = waveArea.getRight() - 14.0f;
            edgeSpeed = e.mods.isShiftDown() ? 0.0f
                      : e.position.x < l ? -juce::jmin (1.0f, (l - e.position.x) / 40.0f + 0.1f)
                      : e.position.x > r ?  juce::jmin (1.0f, (e.position.x - r) / 40.0f + 0.1f) : 0.0f;
            if (std::abs (edgeSpeed) > 0.0f && ! isTimerRunning())
                startTimerHz (60);
            break;
        }
        case Drag::pan:
            if (e.mouseWasDraggedSinceMouseDown())
            {
                const double spp = downViewLen / juce::jmax (1.0f, waveArea.getWidth());
                setViewTarget (downViewStart - (e.position.x - downPos.x) * spp, downViewLen, false);
                zoomedToSelection = false;
                updateCursor (e.position);
            }
            break;
        case Drag::ruler:
        {
            // drag down = zoom in, sideways = scroll (as on a DAW ruler)
            const double len = downViewLen * std::exp ((double) -(e.position.y - downPos.y) * 0.015);
            const double clamped = juce::jlimit (juce::jmin (minViewSamples, (double) numSamples), (double) numSamples, len);
            const double spp = clamped / juce::jmax (1.0f, waveArea.getWidth());
            zoomAround (downAnchor - (e.position.x - downPos.x) * spp, downAnchorFraction, clamped, false);
            zoomedToSelection = false;
            break;
        }
        case Drag::overview:
            setViewTarget (overviewToSample (e.position.x) - grabOffset, targetLen, false);
            zoomedToSelection = false;
            break;
        case Drag::none:
            break;
    }
}

void WaveformEditor::mouseUp (const juce::MouseEvent& e)
{
    const auto was = drag;
    drag = Drag::none;
    edgeSpeed = 0.0f;

    if (was == Drag::start || was == Drag::end)
    {
        if ((! juce::exactlyEqual (rangeStart, downStartValue) || ! juce::exactlyEqual (rangeEnd, downEndValue)) && onRangeChanged != nullptr)
            onRangeChanged (rangeStart, rangeEnd);
    }
    else if (was == Drag::loop)
    {
        if (! juce::exactlyEqual (loopStart, downLoopValue) && onLoopStartChanged != nullptr)
            onLoopStartChanged (loopStart);
    }
    else if (was == Drag::pan && ! e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() < 2
             && waveArea.contains (e.position) && numSamples > 0)
    {
        if (onAudition != nullptr)
            onAudition (juce::jlimit (0.0, 1.0, xToSample (e.position.x) / numSamples));
    }

    hover = hitTestMarker (e.position);
    updateCursor (e.position);
    repaint();
}

void WaveformEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (numSamples <= 0 || hitTestMarker (e.position) != Drag::none || overviewArea.expanded (0.0f, 3.0f).contains (e.position))
        return;
    if (zoomedToSelection)
        zoomToFit();
    else
        zoomToSelection();
}

void WaveformEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (numSamples <= 0)
        return;

    const bool horizontal = std::abs (w.deltaX) > std::abs (w.deltaY);
    if (horizontal || e.mods.isShiftDown() || overviewArea.expanded (0.0f, 3.0f).contains (e.position))
    {
        // scroll
        const float d = horizontal ? -w.deltaX : -w.deltaY;
        setViewTarget (targetStart + (double) d * targetLen * 0.4, targetLen, ! w.isSmooth);
    }
    else
    {
        // zoom around the mouse
        const double anchor = xToSample (e.position.x);
        const double frac = (anchor - viewStart) / juce::jmax (1.0, viewLen);
        const double target = targetLen * std::exp ((double) -w.deltaY * 2.4);
        // keep the anchor under the mouse in both the shown and the target view
        const double a = juce::jlimit (0.0, 1.0, frac);
        zoomAround (anchor, a, target, ! w.isSmooth);
    }
    zoomedToSelection = false;
}

void WaveformEditor::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (numSamples <= 0 || scaleFactor <= 0.0f)
        return;
    const double anchor = xToSample (e.position.x);
    const double frac = juce::jlimit (0.0, 1.0, (anchor - viewStart) / juce::jmax (1.0, viewLen));
    zoomAround (anchor, frac, targetLen / scaleFactor, false);
    zoomedToSelection = false;
}

//==============================================================================
juce::String WaveformEditor::formatPosition (double s) const
{
    const double secs = s / sampleRate;
    const juce::String time = juce::String (secs, secs < 10.0 ? 3 : 2) + " s";
    const double beat = beatSamples();
    if (beat <= 0.0)
        return time;
    const double beats = s / beat + 1.0e-6;
    const int bar = (int) std::floor (beats / 4.0) + 1;
    const int b = (int) std::floor (std::fmod (beats, 4.0)) + 1;
    const int six = (int) std::floor (std::fmod (beats * 4.0, 4.0)) + 1;
    return juce::String (bar) + "." + juce::String (b) + "." + juce::String (six) + dot() + time;
}

void WaveformEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (bounds.reduced (0.5f), 12.0f);
    g.setColour (well);
    g.fillPath (shape);

    if (numSamples <= 0)
    {
        g.setColour (line.withMultipliedAlpha (0.5f));
        g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX() + 24.0f, bounds.getRight() - 24.0f);
        const auto label = bounds.withSizeKeepingCentre (220.0f, 22.0f);
        g.setColour (well);
        g.fillRect (label.reduced (20.0f, 0.0f));
        drawMonoLabel (g, "Select a sample", label, juce::Justification::centred, muted, 11.0f);
        g.setColour (line);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
        return;
    }

    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shape);

        // ruler strip + overview strip backgrounds
        g.setColour (card2);
        g.fillRect (bounds.withHeight (rulerArea.getHeight()));
        g.setColour (line.withMultipliedAlpha (0.8f));
        g.drawHorizontalLine ((int) rulerArea.getBottom(), bounds.getX(), bounds.getRight());
        g.setColour (line.withMultipliedAlpha (0.5f));
        g.drawHorizontalLine ((int) waveArea.getBottom(), bounds.getX(), bounds.getRight());

        paintGrid (g);
        paintWave (g);
        paintRuler (g);
        paintMarkers (g);
        paintOverview (g);
    }

    g.setColour (line);
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

void WaveformEditor::paintGrid (juce::Graphics& g)
{
    const float top = waveArea.getY(), bottom = waveArea.getBottom();
    const double spp = samplesPerPixel();
    const double viewEnd = viewStart + viewLen;
    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (waveArea.expanded (1.0f, 0.0f).toNearestInt());

    const double beat = beatSamples();
    if (beat > 0.0)
    {
        const double pxBeat = beat / spp;
        auto lines = [&] (double step, juce::Colour c)
        {
            g.setColour (c);
            for (auto k = (int64_t) std::ceil (viewStart / step); k * step <= viewEnd; ++k)
                g.fillRect (juce::Rectangle<float> (std::round (sampleToX ((double) k * step)), top, 1.0f, bottom - top));
        };

        if (pxBeat / 4.0 >= 10.0)
            lines (beat * 0.25, accentL.withAlpha (0.035f));
        if (pxBeat >= 8.0)
            lines (beat, accentL.withAlpha (0.075f));

        int barStep = 1;
        while (barStep * 4.0 * pxBeat < 8.0)
            barStep *= 2;
        lines (beat * 4.0 * barStep, accentL.withAlpha (0.16f));
    }
    else
    {
        int minor = 5;
        const double step = niceStep (80.0 * spp / sampleRate, minor) * sampleRate;
        g.setColour (accentL.withAlpha (0.07f));
        for (auto k = (int64_t) std::ceil (viewStart / step); k * step <= viewEnd; ++k)
            g.fillRect (juce::Rectangle<float> (std::round (sampleToX ((double) k * step)), top, 1.0f, bottom - top));
    }

    // centre line
    g.setColour (accentL.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float> (waveArea.getX(), std::round (waveArea.getCentreY()), waveArea.getWidth(), 1.0f));
}

void WaveformEditor::paintRuler (juce::Graphics& g)
{
    const double spp = samplesPerPixel();
    const double viewEnd = viewStart + viewLen;
    const float bottom = rulerArea.getBottom();
    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (rulerArea.expanded (sidePad, 0.0f).toNearestInt());

    const auto labelFont = mono (10.0f, true);
    const auto smallFont = mono (8.5f);
    const double beat = beatSamples();

    if (beat > 0.0)
    {
        const double pxBeat = beat / spp, pxBar = pxBeat * 4.0;

        // beat ticks
        if (pxBeat >= 6.0)
        {
            g.setColour (muted.withAlpha (0.35f));
            for (auto k = (int64_t) std::ceil (viewStart / beat); k * beat <= viewEnd; ++k)
                if (k % 4 != 0)
                    g.fillRect (juce::Rectangle<float> (std::round (sampleToX ((double) k * beat)), bottom - 4.0f, 1.0f, 4.0f));
        }

        // bars, numbered from 1 (a loop starts on a bar at sample 0)
        int lineStep = 1, labelStep = 1;
        while (lineStep * pxBar < 8.0)
            lineStep *= 2;
        while (labelStep * pxBar < 34.0)
            labelStep *= 2;

        const double bar = beat * 4.0;
        for (auto k = (int64_t) std::ceil (viewStart / bar / lineStep) * lineStep; k * bar <= viewEnd; k += lineStep)
        {
            const float x = std::round (sampleToX ((double) k * bar));
            const bool labelled = k % labelStep == 0;
            g.setColour (labelled ? text2.withAlpha (0.55f) : muted.withAlpha (0.4f));
            g.fillRect (juce::Rectangle<float> (x, labelled ? rulerArea.getY() + 4.0f : bottom - 7.0f, 1.0f, labelled ? bottom - rulerArea.getY() - 4.0f : 7.0f));
            if (labelled)
            {
                g.setColour (text2);
                g.setFont (labelFont);
                g.drawText (juce::String (k + 1), juce::Rectangle<float> (x + 4.0f, rulerArea.getY() + 2.0f, 40.0f, 14.0f),
                            juce::Justification::centredLeft, false);
            }
        }

        // close in: beat numbers (bar.beat)
        if (pxBeat >= 56.0)
        {
            g.setFont (smallFont);
            g.setColour (muted);
            for (auto k = (int64_t) std::ceil (viewStart / beat); k * beat <= viewEnd; ++k)
                if (k % 4 != 0)
                {
                    const float x = std::round (sampleToX ((double) k * beat));
                    g.drawText (juce::String (k / 4 + 1) + "." + juce::String (k % 4 + 1),
                                juce::Rectangle<float> (x + 4.0f, rulerArea.getY() + 3.0f, 40.0f, 12.0f),
                                juce::Justification::centredLeft, false);
                }
        }
    }
    else
    {
        // time ruler in seconds
        int minor = 5;
        const double stepSec = niceStep (80.0 * spp / sampleRate, minor);
        const double step = stepSec * sampleRate, minorStep = step / minor;
        const int decimals = juce::jlimit (0, 5, (int) std::ceil (-std::log10 (stepSec) - 1.0e-9));

        g.setColour (muted.withAlpha (0.35f));
        for (auto k = (int64_t) std::ceil (viewStart / minorStep); k * minorStep <= viewEnd; ++k)
            g.fillRect (juce::Rectangle<float> (std::round (sampleToX ((double) k * minorStep)), bottom - 4.0f, 1.0f, 4.0f));

        g.setFont (labelFont);
        for (auto k = (int64_t) std::ceil (viewStart / step); k * step <= viewEnd; ++k)
        {
            const float x = std::round (sampleToX ((double) k * step));
            g.setColour (text2.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (x, rulerArea.getY() + 4.0f, 1.0f, bottom - rulerArea.getY() - 4.0f));
            g.setColour (text2);
            g.drawText (juce::String ((double) k * stepSec, decimals) + "s",
                        juce::Rectangle<float> (x + 4.0f, rulerArea.getY() + 2.0f, 70.0f, 14.0f),
                        juce::Justification::centredLeft, false);
        }
    }

    // zoom readout on the right while zoomed in
    if (targetLen < numSamples * 0.999)
    {
        const double secs = viewLen / sampleRate;
        const juce::String z = spp < 1.0 ? juce::String (juce::roundToInt (viewLen)) + " smp"
                                         : juce::String (secs < 1.0 ? secs * 1000.0 : secs, secs < 1.0 ? 1 : 2) + (secs < 1.0 ? " ms" : " s");
        const auto r = juce::Rectangle<float> (rulerArea.getRight() - 96.0f, rulerArea.getY() + 2.0f, 96.0f, 14.0f);
        g.setColour (card2);
        g.fillRect (r.withTrimmedLeft (30.0f).withRight ((float) getWidth()));
        drawMonoLabel (g, z, r, juce::Justification::centredRight, accentL.withAlpha (0.8f), 9.0f);
    }
}

void WaveformEditor::paintWave (juce::Graphics& g)
{
    const double spp = samplesPerPixel();
    const float mid = waveArea.getCentreY();
    const float half = waveArea.getHeight() * 0.5f - 7.0f;
    const float gain = displayGain * half;
    const float x0 = sampleToX (rangeStart * numSamples), x1 = sampleToX (rangeEnd * numSamples);
    const auto crop = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (x0, waveArea.getX() - 2.0f), waveArea.getY(),
                                                                  juce::jmin (x1, waveArea.getRight() + 2.0f), waveArea.getBottom());

    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (waveArea.toNearestInt());

    const juce::Colour top = accent.brighter (0.35f), bottom = accent.withMultipliedBrightness (0.55f);

    if (spp > 0.45)
    {
        // envelope: one min / max (+ rms) per pixel column
        // two spare columns each side, so the outline's ends fall outside the clip
        const int cols = juce::jmin ((int) colMin.size(), (int) std::ceil (waveArea.getWidth()) + 5);
        computeColumns (viewStart - 2.0 * spp, spp, cols, colMin.data(), colMax.data(), colRms.data());

        wavePath.clear();
        rmsPath.clear();
        const float left = waveArea.getX() - 2.0f;
        for (int c = 0; c < cols; ++c)
        {
            const float x = left + (float) c;
            const float y = mid - juce::jmax (0.5f, colMax[(size_t) c] * gain);
            const float r = mid - juce::jmax (0.5f, colRms[(size_t) c] * gain);
            if (c == 0)
            {
                wavePath.startNewSubPath (x, y);
                rmsPath.startNewSubPath (x, r);
            }
            else
            {
                wavePath.lineTo (x, y);
                rmsPath.lineTo (x, r);
            }
        }
        for (int c = cols; --c >= 0;)
        {
            const float x = left + (float) c;
            wavePath.lineTo (x, mid - juce::jmin (-0.5f, colMin[(size_t) c] * gain));
            rmsPath.lineTo (x, mid + juce::jmax (0.5f, colRms[(size_t) c] * gain));
        }
        wavePath.closeSubPath();
        rmsPath.closeSubPath();

        // rms body fades out close in, where it stops meaning much
        const float rmsAlpha = (float) juce::jlimit (0.0, 1.0, (spp - 24.0) / 120.0);

        // outside the crop: dim grey
        {
            const juce::Graphics::ScopedSaveState outer (g);
            g.excludeClipRegion (crop.getSmallestIntegerContainer());
            g.setColour (muted.withAlpha (0.30f));
            g.fillPath (wavePath);
            if (rmsAlpha > 0.0f)
            {
                g.setColour (muted.withAlpha (0.22f * rmsAlpha));
                g.fillPath (rmsPath);
            }
        }

        // inside the crop: the accent
        if (! crop.isEmpty())
        {
            const juce::Graphics::ScopedSaveState inner (g);
            g.reduceClipRegion (crop.getSmallestIntegerContainer());
            juce::ColourGradient grad (top.withAlpha (0.78f), 0.0f, mid - half, bottom.withAlpha (0.78f), 0.0f, mid + half, false);
            grad.addColour (0.5, accent.withAlpha (0.62f));
            g.setGradientFill (grad);
            g.fillPath (wavePath);
            if (rmsAlpha > 0.0f)
            {
                juce::ColourGradient body (top.brighter (0.5f).withAlpha (0.9f * rmsAlpha), 0.0f, mid - half * 0.6f,
                                           accent.withAlpha (0.75f * rmsAlpha), 0.0f, mid + half * 0.6f, false);
                g.setGradientFill (body);
                g.fillPath (rmsPath);
            }
            g.setColour (top.withAlpha (0.35f));
            g.strokePath (wavePath, juce::PathStrokeType (0.8f));
        }
    }
    else
    {
        // close in: the samples as a line (dots + stems once they spread out)
        const double pps = 1.0 / spp;
        const int i0 = juce::jmax (0, (int) std::floor (viewStart) - 1);
        const int i1 = juce::jmin (numSamples - 1, (int) std::ceil (viewStart + viewLen) + 1);

        auto drawChannels = [&] (bool inside)
        {
            for (int ch = numChannels; --ch >= 0;)
            {
                const float* d = audio->getReadPointer (ch);
                const bool primary = ch == 0;
                const juce::Colour c = inside ? (primary ? accent.brighter (0.4f) : accent.withAlpha (0.55f))
                                              : muted.withAlpha (primary ? 0.55f : 0.3f);

                linePath.clear();
                for (int i = i0; i <= i1; ++i)
                {
                    const float x = sampleToX (i), y = mid - d[i] * gain;
                    if (i == i0) linePath.startNewSubPath (x, y);
                    else         linePath.lineTo (x, y);
                }

                if (pps >= 5.0)
                {
                    g.setColour (c.withMultipliedAlpha (0.25f));
                    for (int i = i0; i <= i1; ++i)
                    {
                        const float x = sampleToX (i), y = mid - d[i] * gain;
                        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x - 0.5f, juce::jmin (y, mid), x + 0.5f, juce::jmax (y, mid)));
                    }
                }

                g.setColour (c);
                g.strokePath (linePath, juce::PathStrokeType (primary ? 1.6f : 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                if (pps >= 5.0)
                {
                    const float r = (float) juce::jlimit (1.5, 3.0, pps / 8.0);
                    for (int i = i0; i <= i1; ++i)
                    {
                        const float x = sampleToX (i), y = mid - d[i] * gain;
                        g.setColour (well);
                        g.fillEllipse (x - r - 0.5f, y - r - 0.5f, 2.0f * r + 1.0f, 2.0f * r + 1.0f);
                        g.setColour (c);
                        g.fillEllipse (x - r, y - r, 2.0f * r, 2.0f * r);
                    }
                }
            }
        };

        {
            const juce::Graphics::ScopedSaveState outer (g);
            g.excludeClipRegion (crop.getSmallestIntegerContainer());
            drawChannels (false);
        }
        if (! crop.isEmpty())
        {
            const juce::Graphics::ScopedSaveState inner (g);
            g.reduceClipRegion (crop.getSmallestIntegerContainer());
            drawChannels (true);
        }
    }

    // outside the crop is shaded
    g.setColour (juce::Colours::black.withAlpha (0.32f));
    if (x0 > waveArea.getX())
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (waveArea.getX(), waveArea.getY(), juce::jmin (x0, waveArea.getRight()), waveArea.getBottom()));
    if (x1 < waveArea.getRight())
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmax (x1, waveArea.getX()), waveArea.getY(), waveArea.getRight(), waveArea.getBottom()));

    // loop: the part before the loop start plays after the end (wrap), tinted
    if (isLoop && loopStart > 0.0)
    {
        const float lx = sampleToX (markerSamples (Drag::loop));
        const auto pre = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (x0, waveArea.getX()), waveArea.getY(),
                                                                     juce::jmin (lx, waveArea.getRight()), waveArea.getBottom());
        if (! pre.isEmpty())
        {
            g.setColour (loopColour.withAlpha (0.045f));
            g.fillRect (pre);
        }
    }
}

void WaveformEditor::paintMarkers (juce::Graphics& g)
{
    const float top = waveArea.getY(), bottom = waveArea.getBottom();
    const float x0 = sampleToX (rangeStart * numSamples), x1 = sampleToX (rangeEnd * numSamples);
    const float left = waveArea.getX() - sidePad + 1.0f, right = waveArea.getRight() + sidePad - 1.0f;

    // loop start: mint dashed line + flag at the top
    if (isLoop)
    {
        const float lx = sampleToX (markerSamples (Drag::loop));
        if (lx >= left - 2.0f && lx <= right + 2.0f)
        {
            const bool active = hover == Drag::loop || drag == Drag::loop;
            const float dashes[] = { 4.0f, 3.0f };
            g.setColour (loopColour.withAlpha (active ? 0.95f : 0.7f));
            g.drawDashedLine (juce::Line<float> (lx, top, lx, bottom), dashes, 2, 1.0f);

            juce::Path flag;
            flag.startNewSubPath (lx - 0.5f, top);
            flag.lineTo (lx + 11.0f, top);
            flag.lineTo (lx + 11.0f, top + 9.0f);
            flag.lineTo (lx + 4.0f, top + 14.0f);
            flag.lineTo (lx - 0.5f, top + 14.0f);
            flag.closeSubPath();
            juce::DropShadow (loopColour.withAlpha (active ? 0.6f : 0.35f), 8, {}).drawForPath (g, flag);
            g.setColour (loopColour);
            g.fillPath (flag);

            if (lx + 90.0f < right || lx < x1 - 90.0f)
                drawMonoLabel (g, "Loop start", juce::Rectangle<float> (lx + 15.0f, top + 1.0f, 90.0f, 12.0f),
                               juce::Justification::centredLeft, loopColour.withAlpha (active ? 0.95f : 0.7f), 8.5f);
        }
    }

    // crop start / end: glowing line, chrome grip, bracket tab at the top
    for (int i = 0; i < 2; ++i)
    {
        const float x = i == 0 ? x0 : x1;
        if (x < left - 6.0f || x > right + 6.0f)
            continue;

        const auto which = i == 0 ? Drag::start : Drag::end;
        const bool active = hover == which || drag == which;
        g.setColour (accentL.withAlpha (active ? 0.22f : 0.12f));
        g.fillRect (juce::Rectangle<float> (x - 3.0f, top, 6.0f, bottom - top));
        g.setColour (active ? text : accentL);
        g.fillRect (juce::Rectangle<float> (x - 0.75f, top, 1.5f, bottom - top));

        // bracket tab pointing into the crop
        const float dir = i == 0 ? 1.0f : -1.0f;
        juce::Path tab;
        tab.startNewSubPath (x, bottom);
        tab.lineTo (x + dir * 9.0f, bottom);
        tab.lineTo (x, bottom - 9.0f);
        tab.closeSubPath();
        g.setColour (active ? text : accentL);
        g.fillPath (tab);

        const auto pill = juce::Rectangle<float> (x - 4.5f, waveArea.getCentreY() - 16.0f, 9.0f, 32.0f);
        drawChromePill (g, pill, active, drag == which);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (float dy : { -4.0f, 0.0f, 4.0f })
            g.fillRect (juce::Rectangle<float> (x - 2.0f, pill.getCentreY() + dy - 0.5f, 4.0f, 1.0f));
    }

    // playhead
    if (playing)
    {
        const float x = sampleToX (playhead * numSamples);
        if (x >= waveArea.getX() - 1.0f && x <= waveArea.getRight() + 1.0f)
        {
            g.setColour (text.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (x - 2.5f, top, 5.0f, bottom - top));
            g.setColour (text);
            g.fillRect (juce::Rectangle<float> (x - 0.75f, top, 1.5f, bottom - top));
            juce::Path tri;
            tri.addTriangle (x - 5.0f, rulerArea.getBottom() - 7.0f, x + 5.0f, rulerArea.getBottom() - 7.0f, x, rulerArea.getBottom());
            g.fillPath (tri);
        }
    }

    // readout bubble while dragging a marker
    if (drag == Drag::start || drag == Drag::end || drag == Drag::loop)
    {
        const double pos = markerSamples (drag);
        juce::String s = formatPosition (pos);
        if (drag != Drag::loop)
            s << dot() << "len " << juce::String ((rangeEnd - rangeStart) * numSamples / sampleRate, 3) << " s";
        else
            s = "loop " + s;
        const auto font = mono (9.5f, true);
        const float w = juce::GlyphArrangement::getStringWidth (font, s) + 18.0f;
        const float x = sampleToX (pos);
        auto bubble = juce::Rectangle<float> (x + 8.0f, top + 18.0f, w, 18.0f);
        if (bubble.getRight() > waveArea.getRight())
            bubble.setX (x - 8.0f - w);
        bubble.setX (juce::jmax (waveArea.getX(), bubble.getX()));
        g.setColour (card.withAlpha (0.92f));
        g.fillRoundedRectangle (bubble, 9.0f);
        g.setColour ((drag == Drag::loop ? loopColour : accentL).withAlpha (0.6f));
        g.drawRoundedRectangle (bubble.reduced (0.5f), 9.0f, 1.0f);
        g.setColour (text);
        g.setFont (font);
        g.drawText (s, bubble, juce::Justification::centred, false);
    }
}

void WaveformEditor::paintOverview (juce::Graphics& g)
{
    const auto ov = overviewArea;
    if (ov.isEmpty())
        return;

    const int cols = (int) ovMin.size();
    if (! overviewValid)
    {
        computeColumns (0.0, (double) numSamples / cols, cols, ovMin.data(), ovMax.data(), ovRms.data());
        overviewValid = true;
    }

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (ov, 3.0f);

    const float mid = ov.getCentreY(), half = ov.getHeight() * 0.5f - 1.0f;
    const float cs = overviewX (rangeStart * numSamples), ce = overviewX (rangeEnd * numSamples);
    for (int c = 0; c < cols; ++c)
    {
        const float x = ov.getX() + (float) c;
        const bool inside = x >= cs && x <= ce;
        const float hi = juce::jmax (0.5f, ovMax[(size_t) c] * displayGain * half);
        const float lo = juce::jmax (0.5f, -ovMin[(size_t) c] * displayGain * half);
        g.setColour (inside ? accent.withAlpha (0.75f) : muted.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x, mid - hi, 1.0f, hi + lo));
    }

    if (playing)
    {
        g.setColour (text);
        g.fillRect (juce::Rectangle<float> (overviewX (playhead * numSamples) - 0.5f, ov.getY(), 1.0f, ov.getHeight()));
    }

    // the visible window
    const bool zoomed = targetLen < numSamples * 0.999;
    auto box = juce::Rectangle<float>::leftTopRightBottom (overviewX (viewStart), ov.getY() - 1.0f,
                                                           overviewX (viewStart + viewLen), ov.getBottom() + 1.0f);
    if (box.getWidth() < 4.0f)
        box = box.withSizeKeepingCentre (4.0f, box.getHeight());
    g.setColour (accentL.withAlpha (zoomed ? 0.14f : 0.05f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (accentL.withAlpha (zoomed ? (drag == Drag::overview ? 1.0f : 0.75f) : 0.22f));
    g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);
}

} // namespace digga
