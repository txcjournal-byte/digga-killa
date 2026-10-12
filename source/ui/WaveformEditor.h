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
                       public juce::SettableTooltipClient
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

private:
    AudioPtr audio;
    double sampleRate = 44100.0, bpm = 0.0;
    bool isLoop = false, playing = false;
    double rangeStart = 0.0, rangeEnd = 1.0, loopStart = 0.0, playhead = 0.0;
    Snap snap = Snap::beat;
    juce::Colour accent = theme::accentL;
};

} // namespace digga
