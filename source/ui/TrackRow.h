#pragma once

#include "ui/Theme.h"

#include <vector>

namespace digga
{

/** Black square play / stop button. */
class PlayButton : public juce::Button
{
public:
    PlayButton();
    void setPlaying (bool shouldShowStop);
    bool isShowingStop() const noexcept { return playing; }
    void setRowSelected (bool isSelected);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool playing = false;
    bool rowSelected = false;
};

/** Rubber-stamp "KILL" button: muted grey until hovered or its row is selected. */
class KillStamp : public juce::Button
{
public:
    KillStamp();
    void setRowSelected (bool selected);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool rowSelected = false;
};

/** Six-dot grip used to drag a result into the DAW. */
class DragHandle : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    DragHandle();
    void paint (juce::Graphics&) override;
    void enablementChanged() override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onDragOut;

private:
    bool dragging = false;
};

struct TrackRowModel
{
    int id = -1;                     // ResultNode id
    juce::String name;               // "A2 – Loop 16 bars"
    juce::String duration;           // "0:32"
    int depth = 0;                   // 0 = root, 1+ = KILL variation level
    bool lastSibling = false;        // tree connector ends here
    bool placeholder = true;         // nothing generated yet
    bool hasChildren = false;        // has KILL variations
    bool expanded = true;
    bool busy = false;               // KILL running on this row
    std::vector<float> peaks;        // 0..1 waveform overview
};

/** One line of the LOOPS / ONE-SHOTS tracklist. */
class TrackRow : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    enum class Style { loop, shot };

    explicit TrackRow (Style style);

    void setModel (TrackRowModel newModel);
    const TrackRowModel& getModel() const noexcept { return model; }

    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    std::function<void()> onSelect, onPlay, onKill, onToggle, onMenu, onDragOut;

    void setPlaying (bool isPlaying) { play.setPlaying (isPlaying); }

    PlayButton play;
    KillStamp kill;
    DragHandle handle;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> getToggleArea() const;
    void drawWaveform (juce::Graphics&, juce::Rectangle<float> area) const;

    Style style;
    TrackRowModel model;
    bool selected = false;
    bool dragStarted = false;
    juce::Rectangle<float> nameArea, waveArea, durationArea;

    static constexpr int indentPerLevel = 51;
    static constexpr int shotIndentPerLevel = 24;
};

} // namespace digga
