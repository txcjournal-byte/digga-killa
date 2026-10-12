#pragma once

#include "ui/Theme.h"

#include <array>
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
    bool loop = false;
    int stem = -1;   // stem colour for the waveform, -1 = mix
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

/** Browser card: one tab per group (LOOPS, DRUMS, 808 & BASS, MELODY,
    VOCALS) over a scrolling list of rows. */
class SampleList : public juce::Component
{
public:
    static constexpr int numGroups = 5;
    static const char* groupId (int index);      // "Loops", "Drums", "Bass", "Melody", "Vocals"
    static const char* groupTitle (int index);   // "Loops", "Drums", "808 & Bass", ...

    SampleList();

    void setRows (std::array<std::vector<RowModel>, numGroups> groups);
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

    std::array<juce::TextButton, numGroups> tabs;
    int shownGroup = 0;
    std::array<std::vector<RowModel>, numGroups> groups;
    juce::String placeholder { "Loops and one-shots appear here" };

    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<SampleRow>> rows;
    int selectedId = -1, playingId = -1;

    static constexpr int rowHeight = 54;
};

} // namespace digga
