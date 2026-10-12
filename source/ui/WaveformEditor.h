#pragma once

#include "core/AudioTools.h"
#include "ui/Theme.h"

namespace digga
{

/** Professional sample view of the editor: zoom (wheel / pinch / buttons,
    down to single samples), scroll, beat grid with bar numbers, crop start /
    end markers that go anywhere (snap: off, zero crossing, 1/16, 1/8, beat,
    bar; hold Alt to bypass, Shift for fine drags), a loop-start marker that
    moves where the loop begins inside the crop (it wraps), playhead.
    All positions are fractions 0..1 of the original (unedited) audio. */
class WaveformEditor : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer
{
public:
    enum class Snap { off, zeroCrossing, sixteenth, eighth, beat, bar };

    WaveformEditor();
    ~WaveformEditor() override;

    void setAudio (AudioPtr original, double sampleRate);   // nullptr = empty state
    void setGrid (double bpm, bool isLoop);                 // bpm <= 0: no grid; loops start on a bar
    void setRange (double start, double end);
    void setLoopStart (double fraction);                    // 0..1 inside the crop; loops only
    void setPlayhead (bool playing, double fraction);        // fraction of the original
    void setSnap (Snap snap);
    Snap getSnap() const noexcept { return snap; }
    void setAccent (juce::Colour colour);                   // stem colour of the sample

    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToSelection();

    std::function<void (double start, double end)> onRangeChanged;   // when a drag ends
    std::function<void (double loopStart)> onLoopStartChanged;       // when a drag ends
    std::function<void (double fraction)> onAudition;                // click on the wave: play from here

    void paint (juce::Graphics&) override;
    void resized() override;

    // ---- extras (view state for the host, and hooks for tests)

    /** The visible window as fractions of the original (where the view is heading
        when a smooth zoom is still animating). */
    juce::Range<double> getVisibleRange() const noexcept;
    void setVisibleRange (double start, double end);

    /** Smooth (animated) zoom; off = jumps at once (tests, offline snapshots). */
    void setSmoothZoom (bool shouldAnimate) noexcept     { smoothZoom = shouldAnimate; }
    /** Keeps the playhead in view while playing and zoomed in (default on). */
    void setFollowPlayhead (bool shouldFollow) noexcept  { followPlayhead = shouldFollow; }

    /** The snap of a marker position (fraction of the original), as a drag applies it. */
    double snapPosition (double fraction, bool bypassSnap = false) const;
    /** x of a fraction of the original in this component (wave area). */
    float getXForFraction (double fraction) const noexcept;
    /** Wave / ruler / overview strip areas (component coordinates). */
    juce::Rectangle<float> getWaveArea() const noexcept      { return waveArea; }
    juce::Rectangle<float> getRulerArea() const noexcept     { return rulerArea; }
    juce::Rectangle<float> getOverviewArea() const noexcept  { return overviewArea; }

    double getRangeStart() const noexcept  { return rangeStart; }
    double getRangeEnd() const noexcept    { return rangeEnd; }
    double getLoopStart() const noexcept   { return loopStart; }

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    enum class Drag { none, start, end, loop, pan, ruler, overview };

    struct Level   // min / max / mean square per bin of binSize samples (all channels)
    {
        int binSize = 0;
        std::vector<float> mn, mx, ms;
    };

    void timerCallback() override;

    void buildCache();
    void computeColumns (double start, double samplesPerColumn, int numColumns,
                         float* mn, float* mx, float* rms) const;
    float monoSample (int index) const noexcept;
    double nearestZeroCrossing (double samplePos) const;
    double snapSamples (double samplePos, bool bypass) const;
    double beatSamples() const noexcept;

    void setViewTarget (double start, double length, bool animate);
    void zoomAround (double anchorSample, double anchorFraction, double newLength, bool animate);
    double samplesPerPixel() const noexcept;
    float sampleToX (double samplePos) const noexcept;
    double xToSample (float x) const noexcept;
    float overviewX (double samplePos) const noexcept;
    double overviewToSample (float x) const noexcept;

    Drag hitTestMarker (juce::Point<float>) const;
    double markerSamples (Drag) const noexcept;
    void updateMarkerDrag (float x, juce::ModifierKeys mods);
    void updateCursor (juce::Point<float>);

    void paintRuler (juce::Graphics&);
    void paintGrid (juce::Graphics&);
    void paintWave (juce::Graphics&);
    void paintMarkers (juce::Graphics&);
    void paintOverview (juce::Graphics&);
    juce::String formatPosition (double samplePos) const;

    AudioPtr audio;
    double sampleRate = 44100.0, bpm = 0.0;
    bool isLoop = false, playing = false;
    double rangeStart = 0.0, rangeEnd = 1.0, loopStart = 0.0, playhead = 0.0;
    Snap snap = Snap::beat;
    juce::Colour accent = theme::accentL;

    // audio + peak cache
    int numSamples = 0, numChannels = 0;
    float displayGain = 1.0f;
    std::vector<Level> levels;

    // view, in samples (current and animation target)
    double viewStart = 0.0, viewLen = 1.0, targetStart = 0.0, targetLen = 1.0;
    bool smoothZoom = true, followPlayhead = true, zoomedToSelection = false;

    // layout + paint scratch (sized in resized(), so paint does not allocate)
    juce::Rectangle<float> rulerArea, waveArea, overviewArea;
    std::vector<float> colMin, colMax, colRms, ovMin, ovMax, ovRms;
    bool overviewValid = false;
    juce::Path wavePath, rmsPath, linePath;

    // interaction
    Drag drag = Drag::none, hover = Drag::none;
    double dragRaw = 0.0, grabOffset = 0.0, fineAnchorPos = 0.0;
    float fineAnchorX = 0.0f;
    bool fineMode = false;
    double downStartValue = 0.0, downEndValue = 0.0, downLoopValue = 0.0;
    double downViewStart = 0.0, downViewLen = 0.0, downAnchor = 0.0, downAnchorFraction = 0.0;
    juce::Point<float> downPos, lastMousePos;
    juce::ModifierKeys lastMods;
    float edgeSpeed = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformEditor)
};

} // namespace digga
