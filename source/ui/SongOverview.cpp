#include "ui/SongOverview.h"

namespace digga
{

using namespace theme;

SongOverview::SongOverview()
{
    setTooltip ("Click to play the song from here");
}

void SongOverview::setSong (double lengthSeconds, std::vector<float> mixPeaks,
                            std::array<std::vector<float>, 4> stemPeaks, std::array<bool, 4> stemPresent,
                            std::vector<SectionView> s)
{
    length = lengthSeconds;
    mix = std::move (mixPeaks);
    stems = std::move (stemPeaks);

    // each lane fills its height (quiet stems stay readable); leakage-only stems stay flat
    for (size_t st = 0; st < stems.size(); ++st)
    {
        auto& lane = stems[st];
        const float peak = lane.empty() ? 0.0f : *std::max_element (lane.begin(), lane.end());
        if (peak > 1.0e-4f && stemPresent[st])
            for (auto& v : lane)
                v = juce::jmin (1.0f, v / peak);
    }
    present = stemPresent;
    sections = std::move (s);
    repaint();
}

void SongOverview::setSelection (double start, double end, int stem)
{
    if (juce::approximatelyEqual (start, selStart) && juce::approximatelyEqual (end, selEnd) && stem == selStem)
        return;
    selStart = start;
    selEnd = end;
    selStem = stem;
    repaint();
}

void SongOverview::setPlayhead (bool isPlaying, float fraction)
{
    if (isPlaying == playing && std::abs (fraction - playhead) < 0.0005f)
        return;
    playing = isPlaying;
    playhead = fraction;
    repaint();
}

void SongOverview::setPlaceholder (const juce::String& t)
{
    placeholder = t;
    repaint();
}

juce::Rectangle<float> SongOverview::laneArea() const
{
    return getLocalBounds().toFloat().reduced (20.0f, 14.0f).withTrimmedLeft (96.0f).withTrimmedTop (22.0f);
}

void SongOverview::paint (juce::Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat().reduced (1.0f));

    auto bounds = getLocalBounds().toFloat().reduced (20.0f, 14.0f);
    auto labels = bounds.removeFromLeft (96.0f);
    const auto lanes = laneArea();

    drawMonoLabel (g, "SONG", labels.removeFromTop (22.0f), juce::Justification::centredLeft, text2, 11.0f);

    if (length <= 0.0 || mix.empty())
    {
        drawMonoLabel (g, placeholder, lanes, juce::Justification::centred, muted, 11.5f);
        return;
    }

    const bool hasStems = std::any_of (stems.begin(), stems.end(), [] (const auto& v) { return ! v.empty(); });
    auto xOf = [&] (double seconds) { return lanes.getX() + (float) (seconds / length) * lanes.getWidth(); };

    // ---- sections: bands with their names along the top
    const auto top = getLocalBounds().toFloat().reduced (20.0f, 14.0f).withTrimmedLeft (96.0f).removeFromTop (20.0f);
    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto& s = sections[i];
        const float x0 = xOf (s.start), x1 = xOf (s.end);
        if (i % 2 == 1)
        {
            g.setColour (juce::Colour (0x0a9fc3ff));
            g.fillRect (juce::Rectangle<float> (x0, lanes.getY(), x1 - x0, lanes.getHeight()));
        }
        g.setColour (line.withAlpha (0.5f));
        g.drawVerticalLine ((int) x0, top.getY() + 2.0f, lanes.getBottom());
        if (x1 - x0 > 28.0f)
            drawMonoLabel (g, s.name, juce::Rectangle<float> (x0 + 5.0f, top.getY(), x1 - x0 - 8.0f, top.getHeight()),
                           juce::Justification::centredLeft, text2, 9.5f);
    }

    // ---- lanes
    if (hasStems)
    {
        const float laneH = lanes.getHeight() / 4.0f;
        for (int st = 0; st < 4; ++st)
        {
            auto lane = juce::Rectangle<float> (lanes.getX(), lanes.getY() + laneH * (float) st, lanes.getWidth(), laneH);
            auto label = juce::Rectangle<float> (labels.getX(), lane.getY(), labels.getWidth() - 8.0f, laneH);
            const auto colour = stemColours[st];
            g.setColour (colour.withAlpha (present[(size_t) st] ? 1.0f : 0.3f));
            g.fillEllipse (label.getX(), label.getCentreY() - 3.0f, 6.0f, 6.0f);
            drawMonoLabel (g, stemLabels[st], label.withTrimmedLeft (12.0f), juce::Justification::centredLeft,
                           present[(size_t) st] ? text2 : muted.withAlpha (0.5f), 9.0f);
            const bool dim = selStem >= 0 && selStem != st;
            drawWaveform (g, stems[(size_t) st], lane.reduced (0.0f, 1.5f),
                          colour.withAlpha (dim ? 0.35f : 0.95f), colour.darker (0.6f).withAlpha (dim ? 0.25f : 0.7f));
        }
    }
    else
    {
        drawMonoLabel (g, "MIX", labels.withHeight (lanes.getHeight()).withY (lanes.getY()), juce::Justification::centredLeft, text2, 9.5f);
        drawWaveform (g, mix, lanes, accentL, juce::Colour (0xff4f78c4));
    }

    // ---- where the selected sample comes from
    if (selStart >= 0.0 && selEnd > selStart)
    {
        const float x0 = xOf (selStart), x1 = juce::jmax (x0 + 2.0f, xOf (juce::jmin (selEnd, length)));
        const auto r = juce::Rectangle<float> (x0, lanes.getY() - 2.0f, x1 - x0, lanes.getHeight() + 4.0f);
        g.setColour (accentL.withAlpha (0.12f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (accentL.withAlpha (0.85f));
        g.drawRoundedRectangle (r, 3.0f, 1.2f);
    }

    // ---- playhead
    if (playing)
    {
        const float x = lanes.getX() + playhead * lanes.getWidth();
        g.setColour (text);
        g.fillRect (juce::Rectangle<float> (x - 0.75f, lanes.getY() - 4.0f, 1.5f, lanes.getHeight() + 8.0f));
    }
}

void SongOverview::mouseDown (const juce::MouseEvent& e)
{
    const auto lanes = laneArea();
    if (length > 0.0 && lanes.expanded (0.0f, 24.0f).contains (e.position) && onSeek)
        onSeek (juce::jlimit (0.0f, 1.0f, (e.position.x - lanes.getX()) / lanes.getWidth()));
}

} // namespace digga
