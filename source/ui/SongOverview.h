#pragma once

#include "analysis/Structure.h"
#include "ui/Theme.h"

#include <array>
#include <vector>

namespace digga
{

/** The whole song at a glance: its sections (where the kick comes in, the
    drop, the hook ...) over one lane per stem, the part the selected sample
    was cut from, and the playhead. Click to play the song from there. */
class SongOverview : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    struct SectionView
    {
        juce::String name;
        double start = 0.0, end = 0.0;   // seconds
    };

    SongOverview();

    void setSong (double lengthSeconds, std::vector<float> mixPeaks,
                  std::array<std::vector<float>, 4> stemPeaks, std::array<bool, 4> stemPresent,
                  std::vector<SectionView> sections);
    void setSelection (double startSeconds, double endSeconds, int stem);   // stem -1 = whole mix
    void setPlayhead (bool playing, float fraction);
    void setPlaceholder (const juce::String& text);

    std::function<void (float fraction)> onSeek;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> laneArea() const;

    double length = 0.0;
    std::vector<float> mix;
    std::array<std::vector<float>, 4> stems;
    std::array<bool, 4> present {};
    std::vector<SectionView> sections;
    double selStart = -1.0, selEnd = -1.0;
    int selStem = -1;
    bool playing = false;
    float playhead = 0.0f;
    juce::String placeholder { "Drop a song on the vinyl" };
};

} // namespace digga
