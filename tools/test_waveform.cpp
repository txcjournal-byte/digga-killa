// Component test for the waveform editor: zoom, snap, marker drags, callbacks, snapshots.
// Usage: TrapWinComponentTest waveform <outDir>
#include "ui/WaveformEditor.h"

#include <cstdio>

namespace
{
    using digga::WaveformEditor;

    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    bool near (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }

    constexpr double sr = 48000.0, bpm = 140.0;

    // 8 bars at 140 bpm: kick-like bursts on every beat, a hat on the off-beats, a soft tone under it
    std::shared_ptr<juce::AudioBuffer<float>> makeLoop()
    {
        const double beat = 60.0 / bpm * sr;
        const auto n = (int) std::round (beat * 32.0);
        auto buf = std::make_shared<juce::AudioBuffer<float>> (2, n);
        juce::Random rng (7);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double inBeat = std::fmod ((double) i, beat) / sr;
            const double inHalf = std::fmod ((double) i + beat * 0.5, beat) / sr;
            const double kickPhase = juce::MathConstants<double>::twoPi * (45.0 * inBeat + 90.0 * (1.0 - std::exp (-inBeat * 30.0)) / 30.0);
            const double kick = std::sin (kickPhase) * std::exp (-inBeat * 9.0) * 0.85;
            const double hat = (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-inHalf * 60.0) * 0.18;
            const double tone = std::sin (juce::MathConstants<double>::twoPi * 220.0 * t) * 0.12
                              * (0.6 + 0.4 * std::sin (juce::MathConstants<double>::twoPi * t * 0.5));
            buf->setSample (0, i, (float) (kick + hat + tone));
            buf->setSample (1, i, (float) (kick * 0.92 - hat + tone * 0.8));
        }
        return buf;
    }

    juce::MouseEvent event (juce::Component& c, juce::Point<float> pos, juce::Point<float> down,
                            juce::ModifierKeys mods, bool dragged, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                 juce::MouseInputSource::defaultTiltY, &c, &c, now, down, now, clicks, dragged);
    }

    // press at `from`, drag in a few steps to `to`, release
    void dragMouse (WaveformEditor& w, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys extra = {})
    {
        const juce::ModifierKeys mods (extra.getRawFlags() | juce::ModifierKeys::leftButtonModifier);
        w.mouseDown (event (w, from, from, mods, false));
        for (int i = 1; i <= 6; ++i)
            w.mouseDrag (event (w, from + (to - from) * ((float) i / 6.0f), from, mods, true));
        w.mouseUp (event (w, to, from, extra, true));
    }

    void save (juce::Component& c, const juce::File& dir, const juce::String& name)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
        const auto file = dir.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        const bool ok = out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
        check (ok, "snapshot " + file.getFullPathName());
    }
}

int runWaveformTest (int argc, char** argv)
{
    const juce::File dir (argc > 2 ? juce::String (argv[2]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();

    const auto loop = makeLoop();
    const double n = loop->getNumSamples();
    const double beat = 60.0 / bpm * sr;

    WaveformEditor w;
    w.setSize (1100, 230);
    w.setSmoothZoom (false);
    w.setFollowPlayhead (false);

    save (w, dir, "waveform_empty.png");

    w.setAudio (loop, sr);
    w.setGrid (bpm, true);
    w.setRange (0.0, 1.0);
    w.setLoopStart (0.0);
    w.setAccent (digga::theme::stemColours[1]);

    double gotStart = -1.0, gotEnd = -1.0, gotLoop = -1.0, gotAudition = -1.0;
    int rangeCalls = 0;
    w.onRangeChanged = [&] (double s, double e) { gotStart = s; gotEnd = e; ++rangeCalls; };
    w.onLoopStartChanged = [&] (double f) { gotLoop = f; };
    w.onAudition = [&] (double f) { gotAudition = f; };

    // ---- zoom
    auto vr = w.getVisibleRange();
    check (near (vr.getStart(), 0.0, 1e-9) && near (vr.getEnd(), 1.0, 1e-9), "starts fitted to the whole sample");

    w.zoomIn();
    vr = w.getVisibleRange();
    check (near (vr.getLength(), 0.5, 1e-9), "zoomIn halves the visible range (" + juce::String (vr.getLength()) + ")");

    for (int i = 0; i < 10; ++i)
        w.zoomOut();
    vr = w.getVisibleRange();
    check (near (vr.getStart(), 0.0, 1e-9) && near (vr.getEnd(), 1.0, 1e-9), "zoomOut is clamped to the whole sample");

    for (int i = 0; i < 40; ++i)
        w.zoomIn();
    vr = w.getVisibleRange();
    check (near (vr.getLength() * n, 16.0, 0.01), "zoomIn is clamped at 16 samples (" + juce::String (vr.getLength() * n, 2) + ")");
    check (vr.getStart() >= 0.0 && vr.getEnd() <= 1.0 + 1e-12, "zoomed range stays inside the sample");

    w.zoomToFit();
    vr = w.getVisibleRange();
    check (near (vr.getStart(), 0.0, 1e-9) && near (vr.getEnd(), 1.0, 1e-9), "zoomToFit restores the whole sample");

    // wheel zoom keeps the point under the mouse in place
    {
        const float x = w.getXForFraction (0.25);
        const juce::Point<float> p (x, w.getWaveArea().getCentreY());
        juce::MouseWheelDetails wheel { 0.0f, 0.5f, false, true, false };
        w.mouseWheelMove (event (w, p, p, {}, false), wheel);
        vr = w.getVisibleRange();
        check (vr.getLength() < 0.5 && near (w.getXForFraction (0.25), x, 0.5), "wheel zooms in around the mouse");
        wheel.deltaY = -0.5f;
        juce::MouseWheelDetails scroll { 0.0f, -0.2f, false, true, false };
        const double before = vr.getStart();
        w.mouseWheelMove (event (w, p, p, juce::ModifierKeys (juce::ModifierKeys::shiftModifier), false), scroll);
        check (w.getVisibleRange().getStart() > before && near (w.getVisibleRange().getLength(), vr.getLength(), 1e-9),
               "shift+wheel scrolls without zooming");
        w.mouseMagnify (event (w, p, p, {}, false), 0.5f);
        check (near (w.getVisibleRange().getLength(), vr.getLength() * 2.0, 1e-6), "pinch zooms out");
        w.zoomToFit();
    }

    // ---- snapping
    w.setSnap (WaveformEditor::Snap::beat);
    check (near (w.snapPosition (0.13), 0.125, 1e-6), "beat snap: 0.13 -> 0.125 (beat 5)");
    check (near (w.snapPosition (0.13, true), 0.13, 1e-12), "snap bypass leaves the position alone");
    w.setSnap (WaveformEditor::Snap::bar);
    check (near (w.snapPosition (0.13), 0.125, 1e-6) && near (w.snapPosition (0.2), 0.25, 1e-6), "bar snap");
    w.setSnap (WaveformEditor::Snap::eighth);
    check (near (w.snapPosition (0.13), 8.0 / 64.0, 1e-6), "1/8 snap");
    w.setSnap (WaveformEditor::Snap::sixteenth);
    check (near (w.snapPosition (0.13), 17.0 / 128.0, 1e-6), "1/16 snap");
    w.setSnap (WaveformEditor::Snap::zeroCrossing);
    {
        const double target = 0.4321;
        const auto s = (int) std::round (w.snapPosition (target) * n);
        auto mono = [&] (int i) { return loop->getSample (0, i) + loop->getSample (1, i); };
        const bool crossing = (mono (s) <= 0.0f) != (mono (s + 1) <= 0.0f) || (mono (s - 1) <= 0.0f) != (mono (s) <= 0.0f);
        check (crossing && std::abs (s - target * n) < sr * 0.0101, "zero-crossing snap lands on a sign change near the target");
    }
    w.setSnap (WaveformEditor::Snap::off);
    check (near (w.snapPosition (0.13), 0.13, 1e-12), "snap off");

    // ---- marker drags (beat snap)
    w.setSnap (WaveformEditor::Snap::beat);
    const float y = w.getWaveArea().getCentreY() + 40.0f;
    dragMouse (w, { w.getXForFraction (1.0), y }, { w.getXForFraction (0.61), y });
    check (rangeCalls == 1 && near (gotStart, 0.0, 1e-12) && near (gotEnd, 0.625, 1e-6),
           "end drag snaps to a beat and reports on release (" + juce::String (gotEnd, 6) + ")");

    {
        // live repaint without reporting while dragging
        const juce::ModifierKeys mods (juce::ModifierKeys::leftButtonModifier);
        const juce::Point<float> from (w.getXForFraction (0.625), y), to (w.getXForFraction (0.7), y);
        w.mouseDown (event (w, from, from, mods, false));
        w.mouseDrag (event (w, to, from, mods, true));
        check (rangeCalls == 1 && near (w.getRangeEnd(), 0.6875, 1e-6), "drag moves the marker live, reports nothing until release");
        save (w, dir, "waveform_drag.png");
        w.mouseUp (event (w, to, from, {}, true));
        check (rangeCalls == 2, "release reports once");
    }

    dragMouse (w, { w.getXForFraction (0.6875), y }, { w.getXForFraction (0.55), y }, juce::ModifierKeys (juce::ModifierKeys::altModifier));
    check (near (gotEnd, 0.55, 1.5 / w.getWaveArea().getWidth()), "Alt bypasses snap (" + juce::String (gotEnd, 5) + ")");

    w.setSnap (WaveformEditor::Snap::off);
    {
        const float x0 = w.getXForFraction (0.0);
        dragMouse (w, { x0, y }, { x0 + 100.0f, y }, juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
        const double px = gotStart * w.getWaveArea().getWidth();
        check (near (px, 10.0, 1.0), "Shift drags at 1/10 speed (100 px -> " + juce::String (px, 2) + " px)");
    }

    {
        // start can not pass the end
        dragMouse (w, { w.getXForFraction (gotStart), y }, { w.getXForFraction (0.9), y });
        check (gotStart < gotEnd && near ((gotEnd - gotStart) * n / sr, 0.005, 0.0005), "start stops 5 ms before the end");
        w.setRange (0.0, 0.625);
    }

    // ---- loop start marker
    w.setSnap (WaveformEditor::Snap::beat);
    {
        const juce::Point<float> flag (w.getXForFraction (0.0) + 5.0f, w.getWaveArea().getY() + 6.0f);
        dragMouse (w, flag, { w.getXForFraction (0.3) + 5.0f, flag.y });
        check (near (gotLoop, 0.5, 1e-6) && near (w.getLoopStart(), 0.5, 1e-6),
               "loop start: fraction of the crop, snapped (" + juce::String (gotLoop, 6) + ")");
        check (near (w.getRangeStart(), 0.0, 1e-12) && near (w.getRangeEnd(), 0.625, 1e-9), "loop drag leaves the crop alone");
    }

    // ---- click = audition
    {
        const juce::Point<float> p (w.getXForFraction (0.4), w.getWaveArea().getCentreY() - 30.0f);
        w.mouseDown (event (w, p, p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), false));
        w.mouseUp (event (w, p, p, {}, false));
        check (near (gotAudition, 0.4, 1.0 / w.getWaveArea().getWidth()), "click auditions from the clicked point");
    }

    // ---- double-click: crop, then fit
    {
        const juce::Point<float> p (w.getXForFraction (0.4), w.getWaveArea().getCentreY() - 30.0f);
        w.mouseDoubleClick (event (w, p, p, {}, false, 2));
        vr = w.getVisibleRange();
        check (vr.getStart() <= 0.0 + 1e-12 && near (vr.getEnd(), 0.625 * 1.08, 0.01), "double-click zooms to the crop");
        w.mouseDoubleClick (event (w, p, p, {}, false, 2));
        vr = w.getVisibleRange();
        check (near (vr.getLength(), 1.0, 1e-9), "double-click again zooms to fit");
    }

    // ---- overview strip scrolls
    {
        w.setVisibleRange (0.0, 0.25);
        const auto ov = w.getOverviewArea();
        const juce::Point<float> from (ov.getX() + ov.getWidth() * 0.125f, ov.getCentreY());
        dragMouse (w, from, from.translated (ov.getWidth() * 0.5f, 0.0f));
        vr = w.getVisibleRange();
        check (near (vr.getStart(), 0.5, 0.01) && near (vr.getLength(), 0.25, 1e-9), "dragging the overview box scrolls");
    }

    // ---- snapshots
    w.setLoopStart (0.25);
    w.setRange (0.0, 0.625);
    w.zoomToFit();
    w.setPlayhead (true, 0.2);
    save (w, dir, "waveform_fit.png");

    w.setVisibleRange (beat * 7.5 / n, beat * 11.0 / n);
    w.setPlayhead (true, beat * 8.6 / n);
    save (w, dir, "waveform_zoomed.png");

    w.setVisibleRange ((beat * 8.5 + 40.0) / n, (beat * 8.5 + 100.0) / n);
    save (w, dir, "waveform_samples.png");
    for (int i = 0; i < 40; ++i)
        w.zoomIn();
    save (w, dir, "waveform_16samples.png");
    check (w.getVisibleRange().getLength() * n >= 15.99, "sample-level zoom paints");

    // one-shot without tempo: time ruler, no loop marker
    w.setPlayhead (false, 0.0);
    w.setGrid (0.0, false);
    w.setRange (0.1, 0.42);
    w.setVisibleRange (0.05, 0.5);
    w.setAccent (digga::theme::stemColours[0]);
    save (w, dir, "waveform_time.png");

    // mono, short
    {
        auto mono = std::make_shared<juce::AudioBuffer<float>> (1, 2000);
        for (int i = 0; i < 2000; ++i)
            mono->setSample (0, i, (float) (std::sin (i * 0.05) * std::exp (-i / 600.0)));
        w.setAudio (mono, sr);
        w.setRange (0.0, 1.0);
        w.zoomToFit();
        save (w, dir, "waveform_mono.png");
    }

    w.setAudio (nullptr, sr);
    check (w.getVisibleRange() == juce::Range<double> (0.0, 1.0), "empty state");

    if (failures == 0)
        std::printf ("ALL CHECKS PASSED\n");
    else
        std::printf ("%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
