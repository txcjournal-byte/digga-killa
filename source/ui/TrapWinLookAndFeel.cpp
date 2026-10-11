#include "ui/TrapWinLookAndFeel.h"

namespace digga
{

using namespace theme;

TrapWinLookAndFeel::TrapWinLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::TextButton::buttonColourId, card2);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, bg);
    setColour (juce::PopupMenu::backgroundColourId, card2);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::TextEditor::backgroundColourId, well);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, accentL);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, accentL);
    setColour (juce::Slider::textBoxTextColourId, text);
}

void TrapWinLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const float alpha = b.isEnabled() ? 1.0f : 0.35f;

    if ((bool) b.getProperties()["chrome"] || b.getToggleState())
    {
        juce::Graphics::ScopedSaveState s (g);
        g.setOpacity (alpha);
        drawChromePill (g, r, highlighted && b.isEnabled(), down);
        return;
    }

    juce::Path pill;
    pill.addRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour ((highlighted && b.isEnabled() ? card2.brighter (0.15f) : card2).withMultipliedAlpha (alpha));
    g.fillPath (pill);
    g.setColour ((highlighted && b.isEnabled() ? accentL.withAlpha (0.6f) : line).withMultipliedAlpha (alpha));
    g.strokePath (pill, juce::PathStrokeType (1.0f));
    if (down)
    {
        g.setColour (accent.withAlpha (0.12f));
        g.fillPath (pill);
    }
}

void TrapWinLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool chrome = (bool) b.getProperties()["chrome"] || b.getToggleState();
    const auto colour = chrome ? juce::Colour (0xff05070b) : text;
    g.setColour (colour.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    const float h = juce::jmin (15.0f, b.getHeight() * 0.42f);
    g.setFont (chrome ? display (h).withExtraKerningFactor (0.04f) : mono (h * 0.9f, true).withExtraKerningFactor (0.14f));
    g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, false);
}

void TrapWinLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    drawButtonBackground (g, b, {}, highlighted, down);
    const bool on = b.getToggleState();
    g.setColour ((on ? juce::Colour (0xff05070b) : text).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.setFont (mono (11.5f, true).withExtraKerningFactor (0.14f));
    g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred, false);
}

void TrapWinLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                           float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    // ".tw-meter": dark rail, blue gradient fill with glow, small chrome thumb
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto rail = bounds.withSizeKeepingCentre (bounds.getWidth(), 8.0f);
    const float alpha = s.isEnabled() ? 1.0f : 0.4f;

    g.setColour (juce::Colour (0xff121a28).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (rail, 4.0f);

    // bipolar sliders (gain, pitch) fill from their zero point
    const double zero = s.getMinimum() < 0.0 && s.getMaximum() > 0.0 ? s.getPositionOfValue (0.0) : rail.getX();
    const float from = (float) juce::jmin ((double) sliderPos, zero), to = (float) juce::jmax ((double) sliderPos, zero);
    const auto fill = rail.withLeft (from).withRight (to);
    if (fill.getWidth() > 0.5f)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4f78c4), fill.getX(), 0.0f, accentL, fill.getRight(), 0.0f, false));
        g.setOpacity (alpha);
        g.fillRoundedRectangle (fill, 4.0f);
        g.setOpacity (1.0f);
    }

    const auto thumb = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ sliderPos, rail.getCentreY() });
    juce::Path t;
    t.addEllipse (thumb);
    juce::DropShadow (glow, 10, {}).drawForPath (g, t);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f6fb), 0.0f, thumb.getY(), juce::Colour (0xff8e98ab), 0.0f, thumb.getBottom(), false));
    g.fillPath (t);
}

void TrapWinLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                        bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    const auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    g.setColour (accent.withAlpha (over || down ? 0.6f : 0.3f));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), 3.0f);
}

void TrapWinLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (card2);
    g.setColour (line);
    g.drawRect (0, 0, width, height);
}

juce::Rectangle<int> TrapWinLookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const int w = juce::jmin (360, (int) juce::GlyphArrangement::getStringWidth (body (14.0f), tip) + 24);
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 24,
                                 pos.y + 12, w, 28).constrainedWithin (parent);
}

void TrapWinLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tip, int width, int height)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (card2);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (text);
    g.setFont (body (14.0f));
    g.drawText (tip, r.reduced (12, 0), juce::Justification::centredLeft, true);
}

} // namespace digga
