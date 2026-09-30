#include "ui/TrackRow.h"

namespace digga
{

// ---------------------------------------------------------------------------
PlayButton::PlayButton() : juce::Button ("Play")
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PlayButton::setPlaying (bool shouldShowStop)
{
    if (playing != shouldShowStop)
    {
        playing = shouldShowStop;
        repaint();
    }
}

void PlayButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    bounds = bounds.withSizeKeepingCentre (side, side);

    auto colour = isEnabled() ? (playing ? red : ink) : muted.withAlpha (0.45f);
    if (isEnabled() && highlighted)
        colour = colour.brighter (0.25f);
    if (down)
        bounds = bounds.reduced (0.8f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (paperLight);
    const auto icon = bounds.reduced (side * 0.32f);

    if (playing)
    {
        g.fillRect (icon);
    }
    else
    {
        juce::Path triangle;
        triangle.addTriangle (icon.getX() + side * 0.04f, icon.getY(),
                              icon.getX() + side * 0.04f, icon.getBottom(),
                              icon.getRight() + side * 0.04f, icon.getCentreY());
        g.fillPath (triangle);
    }
}

// ---------------------------------------------------------------------------
KillStamp::KillStamp() : juce::Button ("KILL")
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("KILL: generate 6 variations");
}

void KillStamp::setRowSelected (bool selected)
{
    if (rowSelected != selected)
    {
        rowSelected = selected;
        repaint();
    }
}

void KillStamp::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace theme;
    const auto bounds = getLocalBounds().toFloat();
    const auto stamp = bounds.reduced (3.0f, 2.0f);
    const bool hot = isEnabled() && (highlighted || down || rowSelected);

    const auto colour = ! isEnabled() ? muted.withAlpha (0.35f)
                        : hot         ? red
                                      : muted.withAlpha (0.8f);

    const juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (-2.5f),
                                                     bounds.getCentreX(), bounds.getCentreY()));
    if (down)
        g.addTransform (juce::AffineTransform::scale (0.96f, 0.96f, bounds.getCentreX(), bounds.getCentreY()));

    juce::Path ink;
    ink.addRectangle (stamp);
    juce::Path inner;
    inner.addRectangle (stamp.reduced (2.2f));
    ink.setUsingNonZeroWinding (false);
    ink.addPath (inner);                        // outer frame ring
    ink.addRectangle (stamp.reduced (3.6f));
    juce::Path hole;
    hole.addRectangle (stamp.reduced (4.6f));
    ink.addPath (hole);                         // thin inner frame ring
    const auto text = textPath ("KILL", display (40.0f), stamp.reduced (7.5f, 6.0f), true);

    g.setColour (colour);
    g.fillPath (ink);
    g.fillPath (text);
    g.strokePath (text, juce::PathStrokeType (0.8f));
    ink.addPath (text);
    addGrunge (g, ink, paper, 404 + getX(), 0.9f);
}

// ---------------------------------------------------------------------------
DragHandle::DragHandle()
{
    setTooltip ("Drag into your DAW");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragHandle::enablementChanged()
{
    setMouseCursor (isEnabled() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void DragHandle::paint (juce::Graphics& g)
{
    const auto centre = getLocalBounds().toFloat().getCentre();
    const float gap = 5.0f, dot = 3.2f;
    g.setColour (isEnabled() ? theme::ink : theme::muted.withAlpha (0.5f));

    for (int col = 0; col < 2; ++col)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse (centre.x + ((float) col - 0.5f) * gap - dot * 0.5f,
                           centre.y + (float) (row - 1) * gap - dot * 0.5f, dot, dot);
}

// ---------------------------------------------------------------------------
TrackRow::TrackRow (Style s) : style (s)
{
    for (auto* c : std::initializer_list<juce::Component*> { &play, &kill, &handle })
        addAndMakeVisible (c);
}

void TrackRow::setModel (TrackRowModel newModel)
{
    model = std::move (newModel);
    const bool live = ! model.placeholder;
    play.setEnabled (live);
    kill.setEnabled (live);
    handle.setEnabled (live);
    resized();
    repaint();
}

void TrackRow::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        kill.setRowSelected (selected);
        repaint();
    }
}

void TrackRow::mouseDown (const juce::MouseEvent&)
{
    if (! model.placeholder && onSelect)
        onSelect();
}

void TrackRow::resized()
{
    auto area = getLocalBounds();
    const int h = area.getHeight();
    const int buttonSize = 30;

    area.removeFromLeft (4 + model.depth * indentPerLevel);
    play.setBounds (area.removeFromLeft (buttonSize).withSizeKeepingCentre (buttonSize, buttonSize));
    area.removeFromLeft (10);

    kill.setBounds (area.removeFromRight (58).withSizeKeepingCentre (58, juce::jmin (h - 6, 32)));
    area.removeFromRight (6);
    handle.setBounds (area.removeFromRight (22));
    area.removeFromRight (4);
    durationArea = area.removeFromRight (48).toFloat();
    area.removeFromRight (6);

    if (style == Style::loop)
    {
        nameArea = area.removeFromLeft (model.depth > 0 ? 116 : 120).toFloat();
        area.removeFromLeft (4);
        waveArea = area.toFloat().reduced (0.0f, (float) h * 0.14f);
    }
    else
    {
        waveArea = area.removeFromLeft (juce::jmin (64, area.getWidth() / 2)).toFloat().reduced (0.0f, (float) h * 0.2f);
        area.removeFromLeft (14);
        nameArea = area.toFloat();
    }
}

void TrackRow::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto bounds = getLocalBounds().toFloat();
    const float alpha = model.placeholder ? 0.42f : 1.0f;

    if (selected)
    {
        g.setColour (red.withAlpha (0.2f));
        g.fillRect (bounds.reduced (0.0f, 1.0f));
        g.setColour (red.withAlpha (0.55f));
        g.drawRect (bounds.reduced (0.0f, 1.0f), 1.0f);
    }

    // tree connectors for KILL variations
    if (model.depth > 0)
    {
        const float x = 4.0f + (float) (model.depth - 1) * indentPerLevel + 22.0f;
        const float midY = bounds.getCentreY();
        g.setColour (ink.withAlpha (0.8f * alpha));
        g.drawLine (x, 0.0f, x, model.lastSibling ? midY : bounds.getBottom(), 1.3f);
        g.drawLine (x, midY, (float) play.getX() - 4.0f, midY, 1.3f);
    }

    // separator
    g.setColour (ink.withAlpha (0.18f));
    g.drawHorizontalLine (getHeight() - 1, (float) play.getX(), bounds.getRight());

    const auto textColour = (selected ? red : ink).withMultipliedAlpha (alpha);
    g.setColour (textColour);
    g.setFont (mono (style == Style::loop ? 15.5f : 16.0f, model.depth == 0));
    g.drawFittedText (model.name, nameArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    g.setFont (mono (15.0f));
    g.drawText (model.duration, durationArea, juce::Justification::centred, false);

    g.setColour ((selected ? red : ink).withMultipliedAlpha (alpha));
    drawWaveform (g, waveArea);
}

void TrackRow::drawWaveform (juce::Graphics& g, juce::Rectangle<float> area) const
{
    if (area.isEmpty())
        return;

    if (model.peaks.empty())
    {
        // nothing yet: dotted baseline
        const float y = area.getCentreY();
        const float dash[] = { 2.0f, 4.0f };
        g.drawDashedLine ({ area.getX(), y, area.getRight(), y }, dash, 2, 1.0f);
        return;
    }

    juce::Path wave;
    const auto n = (int) model.peaks.size();
    const float step = area.getWidth() / (float) n;
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;

    for (int i = 0; i < n; ++i)
    {
        const float x = area.getX() + (float) i * step;
        const float amp = juce::jmax (0.5f, model.peaks[(size_t) i] * half);
        wave.addRectangle (x, mid - amp, juce::jmax (1.0f, step * 0.8f), amp * 2.0f);
    }

    g.fillPath (wave);
}

} // namespace digga
