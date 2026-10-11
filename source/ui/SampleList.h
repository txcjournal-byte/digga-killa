#pragma once

#include "ui/Theme.h"

#include <memory>
#include <vector>

namespace digga
{

struct RowModel
{
    int id = -1;
    juce::String name, detail, length;
    std::vector<float> peaks;
    bool edited = false;
};

/** One sample in the list: play, name + where it came from, waveform,
    length, drag grip. The whole row can be dragged into a DAW. */
class SampleRow : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    SampleRow();

    void setModel (const RowModel& m);
    const RowModel& getModel() const noexcept { return model; }
    void setSelected (bool s);
    void setPlaying (bool p);

    std::function<void()> onSelect, onPlay, onDragOut, onMenu;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> playArea() const;

    RowModel model;
    bool selected = false, playing = false, hover = false, dragStarted = false, pressedPlay = false;
};

/** Right card: LOOPS / ONE-SHOTS tabs over a scrolling list of rows. */
class SampleList : public juce::Component
{
public:
    SampleList();

    void setRows (std::vector<RowModel> loops, std::vector<RowModel> shots);
    void setSelectedId (int id);
    void setPlayingId (int id);
    void showTabFor (int id);
    void setPlaceholder (const juce::String& text);

    std::function<void (int)> onSelect, onPlay, onDragOut, onMenu;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuild();
    void layoutRows();

    juce::TextButton loopsTab, shotsTab;
    bool showingShots = false;
    std::vector<RowModel> loops, shots;
    juce::String placeholder { "Loops and one-shots appear here" };

    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<SampleRow>> rows;
    int selectedId = -1, playingId = -1;

    static constexpr int rowHeight = 54;
};

} // namespace digga
