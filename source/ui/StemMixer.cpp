#include "ui/StemMixer.h"

namespace digga
{

using namespace theme;

StemMixer::StemMixer() = default;

void StemMixer::setState (bool e, const std::array<float, 4>& l, const std::array<bool, 4>& p, const juce::String& m)
{
    if (dragging >= 0)
        return;   // keep the gesture's values until it ends
    enabled = e;
    levels = l;
    present = p;
    message = m;
    for (int i = 0; i < 4; ++i)
        if (levels[(size_t) i] > 0.0f)
            beforeMute[(size_t) i] = levels[(size_t) i];
    repaint();
}

StemMixer::Strip StemMixer::strip (int index) const
{
    auto area = getLocalBounds().toFloat().reduced (18.0f, 16.0f).withTrimmedTop (26.0f);
    const float w = area.getWidth() / 4.0f;
    auto col = juce::Rectangle<float> (area.getX() + w * (float) index, area.getY(), w, area.getHeight()).reduced (5.0f, 0.0f);

    Strip s;
    s.label = col.removeFromBottom (16.0f);
    col.removeFromBottom (6.0f);
    auto buttons = col.removeFromBottom (20.0f);
    s.mute = buttons.removeFromLeft (buttons.getWidth() * 0.5f).reduced (1.5f, 0.0f);
    s.solo = buttons.reduced (1.5f, 0.0f);
    col.removeFromBottom (8.0f);
    s.fader = col.withSizeKeepingCentre (juce::jmin (22.0f, col.getWidth()), col.getHeight());
    return s;
}

int StemMixer::stripAt (juce::Point<float> p) const
{
    for (int i = 0; i < 4; ++i)
    {
        const auto s = strip (i);
        if (s.fader.getUnion (s.mute).getUnion (s.solo).expanded (8.0f, 4.0f).contains (p))
            return i;
    }
    return -1;
}

void StemMixer::paint (juce::Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat().reduced (1.0f));
    auto title = getLocalBounds().toFloat().reduced (20.0f, 14.0f).removeFromTop (20.0f);
    drawMonoLabel (g, "STEMS", title, juce::Justification::centredLeft, text2, 11.0f);

    for (int i = 0; i < 4; ++i)
    {
        const auto s = strip (i);
        const bool active = enabled && present[(size_t) i];
        const auto colour = stemColours[i].withAlpha (active ? 1.0f : 0.3f);
        const float level = enabled ? levels[(size_t) i] : 0.0f;

        // fader track and fill
        g.setColour (well);
        g.fillRoundedRectangle (s.fader, 6.0f);
        g.setColour (line);
        g.drawRoundedRectangle (s.fader, 6.0f, 1.0f);
        if (level > 0.0f)
        {
            auto fill = s.fader.reduced (3.0f);
            fill = fill.removeFromBottom (fill.getHeight() * level);
            g.setGradientFill (juce::ColourGradient (colour, fill.getX(), fill.getY(), colour.darker (0.8f), fill.getX(), fill.getBottom(), false));
            g.fillRoundedRectangle (fill, 4.0f);
        }

        // thumb
        if (enabled)
        {
            const float y = s.fader.getBottom() - 3.0f - (s.fader.getHeight() - 6.0f) * level;
            drawChromePill (g, juce::Rectangle<float> (s.fader.getWidth() + 10.0f, 9.0f).withCentre ({ s.fader.getCentreX(), y }), false, false);
        }

        // mute / solo
        const bool isMuted = enabled && level <= 0.0f;
        bool soloed = enabled && level > 0.0f;
        for (int k = 0; k < 4; ++k)
            if (k != i && levels[(size_t) k] > 0.0f)
                soloed = false;
        for (auto [area, label, on] : { std::tuple { s.mute, "M", isMuted }, std::tuple { s.solo, "S", soloed } })
        {
            if (on)
                drawChromePill (g, area, false, false);
            else
            {
                g.setColour (card2);
                g.fillRoundedRectangle (area, area.getHeight() * 0.5f);
                g.setColour (line);
                g.drawRoundedRectangle (area, area.getHeight() * 0.5f, 1.0f);
            }
            g.setColour (on ? well : (enabled ? text2 : theme::muted.withAlpha (0.4f)));
            g.setFont (mono (10.5f, true));
            g.drawText (label, area, juce::Justification::centred, false);
        }

        drawMonoLabel (g, stemLabels[i], s.label.expanded (8.0f, 0.0f), juce::Justification::centred,
                       active ? colour : theme::muted.withAlpha (0.5f), 8.5f);
    }

    if (! enabled && message.isNotEmpty())
    {
        auto r = getLocalBounds().toFloat().reduced (24.0f, 60.0f);
        g.setColour (card.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 10.0f);
        drawMonoLabel (g, message, r.reduced (8.0f), juce::Justification::centred, text2, 9.5f);
    }
}

void StemMixer::setFromPosition (int index, float y)
{
    const auto f = strip (index).fader.reduced (3.0f);
    levels[(size_t) index] = juce::jlimit (0.0f, 1.0f, (f.getBottom() - y) / f.getHeight());
    if (levels[(size_t) index] < 0.03f)
        levels[(size_t) index] = 0.0f;
    repaint();
}

void StemMixer::commit()
{
    if (onChange)
        onChange (levels);
}

void StemMixer::mouseDown (const juce::MouseEvent& e)
{
    if (! enabled)
        return;
    const int i = stripAt (e.position);
    if (i < 0)
        return;
    const auto s = strip (i);

    if (s.mute.contains (e.position))
    {
        if (levels[(size_t) i] > 0.0f) { beforeMute[(size_t) i] = levels[(size_t) i]; levels[(size_t) i] = 0.0f; }
        else levels[(size_t) i] = beforeMute[(size_t) i] > 0.0f ? beforeMute[(size_t) i] : 1.0f;
        repaint();
        commit();
        return;
    }
    if (s.solo.contains (e.position))
    {
        bool alreadySolo = levels[(size_t) i] > 0.0f;
        for (int k = 0; k < 4; ++k)
            if (k != i && levels[(size_t) k] > 0.0f)
                alreadySolo = false;
        for (int k = 0; k < 4; ++k)
            levels[(size_t) k] = alreadySolo ? (present[(size_t) k] ? 1.0f : 0.0f) : (k == i ? 1.0f : 0.0f);
        repaint();
        commit();
        return;
    }
    if (s.fader.expanded (8.0f, 4.0f).contains (e.position))
    {
        dragging = i;
        setFromPosition (i, e.position.y);
    }
}

void StemMixer::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging >= 0)
        setFromPosition (dragging, e.position.y);
}

void StemMixer::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    dragging = -1;
    commit();
}

void StemMixer::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! enabled)
        return;
    const int i = stripAt (e.position);
    if (i >= 0 && strip (i).fader.expanded (8.0f, 4.0f).contains (e.position))
    {
        dragging = -1;
        levels[(size_t) i] = 1.0f;
        repaint();
        commit();
    }
}

} // namespace digga
