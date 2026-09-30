#include "ui/DiggaLookAndFeel.h"

namespace digga
{

DiggaLookAndFeel::DiggaLookAndFeel()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, paper);
    setColour (juce::TextButton::buttonColourId, paper);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, red);
    setColour (juce::Label::textColourId, ink);
    setColour (juce::Slider::textBoxTextColourId, ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, paper);
    setColour (juce::Slider::textBoxHighlightColourId, red.withAlpha (0.3f));
    setColour (juce::TooltipWindow::backgroundColourId, ink);
    setColour (juce::TooltipWindow::textColourId, paper);
    setColour (juce::TooltipWindow::outlineColourId, ink);
    setColour (juce::PopupMenu::backgroundColourId, paperLight);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, red);
    setColour (juce::PopupMenu::highlightedTextColourId, paperLight);
    setColour (juce::ScrollBar::thumbColourId, ink);
}

void DiggaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider& slider)
{
    using namespace theme;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const float alpha = slider.isEnabled() ? 1.0f : 0.45f;

    auto pointAt = [centre] (float angle, float r)
    {
        return centre.getPointOnCircumference (r, angle);
    };

    if (slider.getProperties()["skinned"])
    {
        // body and scale are printed in the skin: only the red pointer is live
        const float bodyRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.45f + 1.0f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        juce::Path pointer;
        pointer.startNewSubPath (pointAt (angle, bodyRadius * 0.47f));
        pointer.lineTo (pointAt (angle, bodyRadius * 0.97f));
        g.setColour (juce::Colour (0xffc62a2f).withAlpha (alpha));
        g.strokePath (pointer, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::butt));
        return;
    }

    // scale ticks
    constexpr int numTicks = 11;
    for (int i = 0; i < numTicks; ++i)
    {
        const float angle = startAngle + (float) i / (float) (numTicks - 1) * (endAngle - startAngle);
        const bool major = i == 0 || i == numTicks / 2 || i == numTicks - 1;
        g.setColour (ink.withAlpha (alpha));
        g.drawLine ({ pointAt (angle, radius * (major ? 0.84f : 0.88f)), pointAt (angle, radius) },
                    major ? 2.4f : 1.6f);
    }

    // body
    const float bodyRadius = radius * 0.76f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.22f * alpha));
    g.fillEllipse (body.translated (1.5f, 3.0f).expanded (1.0f));

    juce::ColourGradient gradient (juce::Colour (0xff2e2c29), centre.translated (-bodyRadius * 0.35f, -bodyRadius * 0.45f),
                                   juce::Colour (0xff0b0a09), centre.translated (bodyRadius * 0.6f, bodyRadius * 0.7f), true);
    g.setGradientFill (gradient);
    g.setOpacity (alpha);
    g.fillEllipse (body);
    g.setOpacity (1.0f);

    juce::Path bodyPath;
    bodyPath.addEllipse (body);
    addGrunge (g, bodyPath, juce::Colour (0xff5a5650).withAlpha (0.5f), slider.getName().hashCode(), 0.8f);

    // machined inner ring
    g.setColour (paper.withAlpha (0.18f * alpha));
    g.drawEllipse (body.reduced (bodyRadius * 0.1f), 1.2f);

    // pointer
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    g.setColour (red.withAlpha (alpha));
    juce::Path pointer;
    pointer.startNewSubPath (pointAt (angle, bodyRadius * 0.5f));
    pointer.lineTo (pointAt (angle, bodyRadius * 0.98f));
    g.strokePath (pointer, juce::PathStrokeType (bodyRadius * 0.09f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void DiggaLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float, float, juce::Slider::SliderStyle style,
                                         juce::Slider& slider)
{
    using namespace theme;

    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float trackHeight = juce::jmin (10.0f, bounds.getHeight() * 0.35f);
    const auto track = bounds.withSizeKeepingCentre (bounds.getWidth(), trackHeight);
    const float alpha = slider.isEnabled() ? 1.0f : 0.45f;

    g.setColour (inkSoft.withAlpha (alpha));
    g.fillRoundedRectangle (track, trackHeight * 0.5f);

    g.setColour (red.withAlpha (alpha));
    g.fillRoundedRectangle (track.withRight (sliderPos), trackHeight * 0.5f);

    const float thumbRadius = juce::jmin (bounds.getHeight() * 0.5f, 15.0f);
    const auto thumb = juce::Rectangle<float> (thumbRadius * 2.0f, thumbRadius * 2.0f)
                           .withCentre ({ sliderPos, bounds.getCentreY() });

    g.setColour (juce::Colours::black.withAlpha (0.25f * alpha));
    g.fillEllipse (thumb.translated (1.0f, 2.0f));
    g.setColour (ink.withAlpha (alpha));
    g.fillEllipse (thumb);
    g.setColour (paper.withAlpha (0.25f * alpha));
    g.drawEllipse (thumb.reduced (thumbRadius * 0.3f), 1.2f);
}

void DiggaLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    using namespace theme;
    const auto bounds = button.getLocalBounds().toFloat();
    const auto pill = bounds.reduced (1.0f);
    const bool on = button.getToggleState();
    const float alpha = button.isEnabled() ? 1.0f : 0.45f;
    const float radius = pill.getHeight() * 0.5f;

    g.setColour ((on ? red : juce::Colour (0xff232220)).withAlpha (alpha));
    g.fillRoundedRectangle (pill, radius);
    g.setColour (juce::Colours::black.withAlpha (0.5f * alpha));
    g.drawRoundedRectangle (pill, radius, 1.0f);

    const auto knob = juce::Rectangle<float> (pill.getHeight(), pill.getHeight())
                          .withX (on ? pill.getRight() - pill.getHeight() : pill.getX())
                          .withY (pill.getY())
                          .reduced (3.0f);

    g.setColour ((highlighted ? paperLight : paper).withAlpha (alpha));
    g.fillEllipse (knob);
    g.setColour (ink.withAlpha (0.35f * alpha));
    g.drawEllipse (knob, 1.0f);
}

void DiggaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                             bool highlighted, bool down)
{
    using namespace theme;
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.75f);
    const float alpha = button.isEnabled() ? 1.0f : 0.4f;

    if (button.isEnabled() && (highlighted || down))
    {
        g.setColour (ink.withAlpha (down ? 0.16f : 0.08f));
        g.fillRect (bounds);
    }

    g.setColour ((button.getToggleState() ? red : ink).withAlpha (alpha));
    g.drawRect (bounds, 1.5f);
}

juce::Font DiggaLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme::mono ((float) buttonHeight * 0.62f, true);
}

void DiggaLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                      bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y, thumbSize, height);

    g.setColour (theme::ink.withAlpha (over || down ? 0.7f : 0.4f));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), 2.0f);
}

juce::Font DiggaLookAndFeel::getLabelFont (juce::Label& label)
{
    return theme::mono (label.getFont().getHeight(), true);
}

juce::Font DiggaLookAndFeel::getPopupMenuFont()
{
    return theme::mono (16.0f, true);
}

} // namespace digga
