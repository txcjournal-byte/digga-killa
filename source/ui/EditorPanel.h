#pragma once

#include "core/Engine.h"
#include "ui/Theme.h"

namespace digga
{

/** Chrome "DRAG TO DAW" pill: dragging it drops the edited sample in the DAW. */
class DragPill : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    DragPill();
    std::function<void()> onDragOut;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { started = false; }
private:
    bool hover = false, started = false;
};

/** Bottom card: the selected sample, big. Crop with the handles (loops snap
    to beats), fades, gain, pitch, reverse; play it and drag it out. */
class EditorPanel : public juce::Component
{
public:
    EditorPanel();

    /** nullptr shows the empty state. */
    void setItem (const SampleItem* item, double sampleRate, double bpm);
    void setPlayback (bool isPlaying, float progress);

    std::function<void (int id, const EditSettings&)> onEdit;
    std::function<void (int id)> onPlay, onDragOut;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void commit();
    void syncControls();
    double snap (double fraction) const;
    float toX (double fraction) const;
    double toFraction (float x) const;

    struct LabelledSlider
    {
        juce::Slider slider;
        juce::String label;
        std::function<juce::String (double)> format;
    };

    int id = -1;
    bool isLoop = false, playing = false;
    int bars = 0;
    double sampleRate = 44100.0, originalSeconds = 0.0;
    juce::String title, detail;
    std::vector<float> peaks;
    EditSettings edit;
    float progress = 0.0f;
    int dragging = 0;   // 0 none, 1 start handle, 2 end handle

    juce::Rectangle<int> waveArea;
    juce::TextButton play { "Play" }, reverse { "Reverse" }, reset { "Reset" };
    DragPill drag;
    std::array<LabelledSlider, 4> sliders;
};

} // namespace digga
