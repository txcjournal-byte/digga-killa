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

void PlayButton::setRowSelected (bool isSelected)
{
    if (rowSelected != isSelected)
    {
        rowSelected = isSelected;
        repaint();
    }
}

void PlayButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace theme;
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    bounds = bounds.withSizeKeepingCentre (side, side);

    auto colour = ! isEnabled() ? muted.withAlpha (0.45f)
                  : (playing || rowSelected) ? juce::Colour (0xff9e1b21)
                                              : juce::Colour (0xff1d1c1a);
    if (isEnabled() && highlighted)
        colour = colour.brighter (0.25f);
    if (down)
        bounds = bounds.reduced (0.8f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (paperLight);
    const auto icon = bounds.reduced (side * 0.3f);

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
    const bool hot = isEnabled() && (highlighted || down || rowSelected);
    auto area = getLocalBounds().toFloat();
    if (down)
        area = area.reduced (1.0f);

    const auto stamp = skinKillStamp();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    if (hot)
    {
        g.drawImage (stamp, area, juce::RectanglePlacement::centred);
    }
    else
    {
        // same print, muted ink
        g.setColour (isEnabled() ? juce::Colour (0xff7d776c) : muted.withAlpha (0.4f));
        g.drawImage (stamp, area, juce::RectanglePlacement::centred, true);
    }
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

void DragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && isEnabled() && e.getDistanceFromDragStart() > 4)
    {
        dragging = true;
        if (onDragOut)
            onDragOut();
    }
}

void DragHandle::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

void DragHandle::paint (juce::Graphics& g)
{
    const auto centre = getLocalBounds().toFloat().getCentre();
    const float gap = 6.5f, dot = 4.0f;
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

    play.onClick = [this] { if (onPlay) onPlay(); };
    kill.onClick = [this] { if (onKill) onKill(); };
    handle.onDragOut = [this] { if (onDragOut) onDragOut(); };
}

void TrackRow::setModel (TrackRowModel newModel)
{
    model = std::move (newModel);
    const bool live = ! model.placeholder;
    play.setEnabled (live);
    play.setRowSelected (selected);
    kill.setEnabled (live);
    handle.setEnabled (live);
    setTooltip (live ? "Click to select, drag into your DAW" : juce::String());
    resized();
    repaint();
}

void TrackRow::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        kill.setRowSelected (selected);
        play.setRowSelected (selected);
        repaint();
    }
}

juce::Rectangle<float> TrackRow::getToggleArea() const
{
    return { (float) play.getRight() + 1.0f, 0.0f, 12.0f, (float) getHeight() };
}

void TrackRow::mouseDown (const juce::MouseEvent& e)
{
    if (model.placeholder)
        return;

    if (e.mods.isPopupMenu())
    {
        if (onSelect) onSelect();
        if (onMenu) onMenu();
        return;
    }

    if (model.hasChildren && getToggleArea().contains (e.position))
    {
        if (onToggle) onToggle();
        return;
    }

    if (onSelect)
        onSelect();
}

void TrackRow::mouseDrag (const juce::MouseEvent& e)
{
    // the whole row is a drag source: pull it into the DAW
    if (! dragStarted && ! model.placeholder && ! e.mods.isPopupMenu() && e.getDistanceFromDragStart() > 6)
    {
        dragStarted = true;
        if (onDragOut)
            onDragOut();
    }
}

void TrackRow::mouseUp (const juce::MouseEvent&)
{
    dragStarted = false;
}

void TrackRow::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! model.placeholder && model.hasChildren && onToggle)
        onToggle();
}

// Offsets below are measured from docs/design.png (row-relative pixels).
void TrackRow::resized()
{
    const int h = getHeight();
    const int indent = model.depth * indentPerLevel;
    auto centredY = [h] (int size) { return (h - size) / 2; };

    if (style == Style::loop)
    {
        play.setBounds (4 + indent, centredY (32), 32, 32);
        kill.setBounds (getWidth() - 50, centredY (36), 48, 36);
        handle.setBounds (kill.getX() - 38, 0, 20, h);
        durationArea = { (float) handle.getX() - 64.0f, 0.0f, 50.0f, (float) h };
        nameArea = { (float) play.getRight() + 13.0f, 0.0f, model.depth > 0 ? 106.0f : 120.0f, (float) h };
        waveArea = juce::Rectangle<float>::leftTopRightBottom (nameArea.getRight() + 4.0f, (float) h * 0.12f,
                                                              durationArea.getX() - 4.0f, (float) h * 0.88f);
    }
    else
    {
        const int shotIndent = model.depth * shotIndentPerLevel;
        play.setBounds (6 + shotIndent, centredY (32), 32, 32);
        kill.setBounds (getWidth() - 50, centredY (36), 48, 36);
        handle.setBounds (kill.getX() - 38, 0, 20, h);
        durationArea = { (float) handle.getX() - (model.depth > 0 ? 52.0f : 64.0f), 0.0f, 44.0f, (float) h };
        waveArea = { (float) play.getRight() + 12.0f, (float) h * 0.14f, model.depth > 0 ? 36.0f : 58.0f, (float) h * 0.72f };
        nameArea = juce::Rectangle<float>::leftTopRightBottom (waveArea.getRight() + 12.0f, 0.0f,
                                                              durationArea.getX(), (float) h);
    }
}

void TrackRow::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto bounds = getLocalBounds().toFloat();
    const float alpha = model.placeholder ? 0.4f : 1.0f;

    if (selected)
    {
        const auto band = bounds.reduced (0.0f, 1.0f);
        g.setColour (red.withAlpha (0.18f));
        g.fillRect (band);
        g.setColour (red.withAlpha (0.45f));
        g.drawRect (band, 1.0f);
    }

    // tree connectors for KILL variations
    if (model.depth > 0)
    {
        const float x = style == Style::loop ? 25.0f + (float) (model.depth - 1) * indentPerLevel
                                             : 22.0f + (float) (model.depth - 1) * shotIndentPerLevel;
        const float midY = bounds.getCentreY();
        g.setColour (ink.withAlpha (0.85f * alpha));
        g.fillRect (x, 0.0f, 1.3f, (model.lastSibling ? midY : bounds.getBottom()));
        g.fillRect (x, midY - 0.6f, (float) play.getX() - 6.0f - x, 1.3f);
    }

    // row rule
    g.setColour (ink.withAlpha (0.28f));
    const float ruleStart = model.depth > 0 ? (float) play.getX() - 4.0f : 0.0f;
    g.fillRect (ruleStart, bounds.getBottom() - 1.0f, bounds.getRight() - ruleStart, 1.0f);

    const auto textColour = (selected ? red : ink).withMultipliedAlpha (alpha);
    g.setColour (textColour);
    g.setFont (condensed (18.5f).withExtraKerningFactor (0.035f));
    g.drawFittedText (model.name, nameArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);

    g.setFont (condensed (17.0f).withExtraKerningFactor (0.02f));
    if (model.busy)
    {
        g.setColour (red);
        g.drawFittedText ("KILLING", durationArea.toNearestInt().expanded (6, 0), juce::Justification::centred, 1, 0.7f);
    }
    else
    {
        g.drawText (model.duration, durationArea, juce::Justification::centred, false);
    }

    // fold marker for rows with KILL variations
    if (model.hasChildren)
    {
        const auto area = getToggleArea().withSizeKeepingCentre (8.0f, 8.0f).translated (1.0f, 0.0f);
        juce::Path marker;
        if (model.expanded)
            marker.addTriangle (area.getX(), area.getY() + 1.5f, area.getRight(), area.getY() + 1.5f, area.getCentreX(), area.getBottom() - 0.5f);
        else
            marker.addTriangle (area.getX() + 1.5f, area.getY(), area.getX() + 1.5f, area.getBottom(), area.getRight() - 0.5f, area.getCentreY());
        g.setColour (textColour.withMultipliedAlpha (0.8f));
        g.fillPath (marker);
    }

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

    // filled, mirrored envelope like a printed waveform
    const auto n = (int) model.peaks.size();
    const float step = area.getWidth() / (float) juce::jmax (1, n - 1);
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;

    juce::Path wave;
    wave.startNewSubPath (area.getX(), mid);
    for (int i = 0; i < n; ++i)
        wave.lineTo (area.getX() + (float) i * step, mid - juce::jmax (0.6f, model.peaks[(size_t) i] * half));
    for (int i = n - 1; i >= 0; --i)
        wave.lineTo (area.getX() + (float) i * step, mid + juce::jmax (0.6f, model.peaks[(size_t) i] * half));
    wave.closeSubPath();

    g.fillPath (wave);
}

} // namespace digga
