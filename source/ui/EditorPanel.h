#pragma once

#include "core/Engine.h"
#include "ui/FxRack.h"
#include "ui/MidiView.h"
#include "ui/Theme.h"
#include "ui/WaveformEditor.h"

namespace digga
{

/** Chrome "DRAG TO DAW" pill: dragging it drops the edited sample in the DAW. */
class DragPill : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    explicit DragPill (juce::String labelText = "DRAG TO DAW");
    std::function<void()> onDragOut;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { started = false; }
private:
    juce::String label;
    bool hover = false, started = false;
};

/** Bottom card: the selected sample, big, in three views:
    WAVE  - the pro sample editor (zoom, free crop markers with snap, loop start),
            fades / gain / pitch below;
    MIDI  - the "shredder": the sample turned into notes in a piano roll;
    FX    - the effect rack (filter, drive, crush, gate, tape stop, delay, reverb, width, KILL).
    Play it, and drag it into the DAW raw or with the effects. */
class EditorPanel : public juce::Component
{
public:
    enum class View { wave, midi, fx };

    EditorPanel();
    ~EditorPanel() override;

    /** nullptr shows the empty state. */
    void setItem (const SampleItem* item, double sampleRate, double bpm);
    void setPlayback (bool isPlaying, float progress);
    /** Which stems the song has (stem toggles for loops). */
    void setStems (bool available, const std::array<bool, 4>& present);

    View getView() const noexcept { return view; }
    void setView (View v);
    bool isDragWithFx() const noexcept { return dragWithFx; }
    MidiView& getMidiView() noexcept { return midiView; }

    std::function<void (int id, const EditSettings&)> onEdit;
    std::function<void (int id)> onPlay, onDragOut, onDragChops;
    std::function<void (int id, double fraction)> onAudition;   // click on the wave
    std::function<void (View)> onViewChanged;
    std::function<void (int id, const std::array<float, 4>& levels)> onStemMix;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void commit();
    void syncControls();
    void updateSnapButton();
    void layoutView();
    void syncStems();

    struct LabelledSlider
    {
        juce::Slider slider;
        juce::String label;
        std::function<juce::String (double)> format;
    };

    int id = -1;
    bool isLoop = false, playing = false, dragWithFx = true;
    double sampleRate = 44100.0, originalSeconds = 0.0, bpm = 0.0;
    juce::String title, detail;
    EditSettings edit;
    float progress = 0.0f;
    View view = View::wave;
    AudioPtr shownAudio;
    bool canMixStems = false, stemsAvailable = false;
    std::array<bool, 4> stemsPresent {};
    std::array<float, 4> stemLevels { 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<juce::TextButton, 4> stemButtons;
    juce::Rectangle<int> stemsArea;

    juce::Rectangle<int> mainArea, controlsArea, headerArea;
    juce::TextButton play { "Play" }, reverse { "Reverse" }, reset { "Reset" };
    juce::TextButton waveTab { "Wave" }, midiTab { "MIDI" }, fxTab { "FX" };
    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, zoomFit { "Fit" }, zoomSel { "Sel" }, snapButton;
    juce::TextButton dragRaw { "Raw" }, dragFx { "FX" };
    DragPill drag;
    DragPill dragChops { "DRAG CHOPS" };
    std::array<LabelledSlider, 4> sliders;

    WaveformEditor wave;
    MidiView midiView;
    FxRack fxRack;
};

} // namespace digga
