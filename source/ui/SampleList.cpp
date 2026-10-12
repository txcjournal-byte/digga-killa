#include "ui/SampleList.h"

namespace digga
{

using namespace theme;

// ---------------------------------------------------------------------------- row
SampleRow::SampleRow()
{
    setTooltip ("Click to edit below, drag into your DAW");
}

void SampleRow::setModel (const RowModel& m) { model = m; repaint(); }
void SampleRow::setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
void SampleRow::setPlaying (bool p)  { if (p != playing) { playing = p; repaint(); } }

juce::Rectangle<float> SampleRow::playArea() const
{
    return juce::Rectangle<float> (34.0f, 34.0f).withCentre ({ 30.0f, (float) getHeight() * 0.5f });
}

void SampleRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f, 3.0f);

    if (selected || hover)
    {
        juce::Path shape;
        shape.addRoundedRectangle (r, 14.0f);
        if (selected)
            juce::DropShadow (glow.withAlpha (0.35f), 18, {}).drawForPath (g, shape);
        g.setColour (selected ? card2.brighter (0.12f) : card2);
        g.fillPath (shape);
        g.setColour (selected ? accentL.withAlpha (0.55f) : line);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    // play / stop
    const auto p = playArea();
    if (playing)
        drawChromePill (g, p, false, false);
    else
    {
        g.setColour (well);
        g.fillEllipse (p);
        g.setColour (line.withAlpha (0.4f));
        g.drawEllipse (p.reduced (0.5f), 1.0f);
    }
    g.setColour (playing ? well : text);
    const auto icon = p.reduced (11.5f);
    if (playing)
        g.fillRoundedRectangle (icon, 1.5f);
    else
    {
        juce::Path tri;
        tri.addTriangle (icon.getX() + 1.5f, icon.getY(), icon.getX() + 1.5f, icon.getBottom(), icon.getRight() + 1.5f, icon.getCentreY());
        g.fillPath (tri);
    }

    // texts
    auto content = r.withTrimmedLeft (60.0f).withTrimmedRight (14.0f);
    auto grip = content.removeFromRight (18.0f);
    auto length = content.removeFromRight (54.0f);
    auto textArea = content.removeFromLeft (juce::jmin (230.0f, content.getWidth() * 0.42f));
    content.removeFromRight (10.0f);
    auto wave = content.reduced (6.0f, 10.0f);

    auto nameLine = textArea.removeFromTop (textArea.getHeight() * 0.55f);
    g.setColour (text);
    g.setFont (bold (17.5f));
    g.drawFittedText (model.name.toUpperCase(), nameLine.toNearestInt(), juce::Justification::bottomLeft, 1, 0.8f);
    if (model.edited)
    {
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), model.name.toUpperCase());
        g.setColour (accentL);
        g.fillEllipse (nameLine.getX() + juce::jmin (w, nameLine.getWidth()) + 6.0f, nameLine.getBottom() - 11.0f, 6.0f, 6.0f);
    }
    drawMonoLabel (g, model.detail, textArea.withTrimmedTop (2.0f), juce::Justification::topLeft, text2.withAlpha (0.8f), 11.5f);

    const auto colour = model.stem >= 0 ? stemColours[model.stem] : accentL;
    drawWaveform (g, model.peaks, wave, selected ? colour : colour.withMultipliedBrightness (0.8f),
                  colour.darker (selected ? 0.5f : 0.8f));

    g.setColour (text2);
    g.setFont (mono (13.0f, true));
    g.drawText (model.length, length, juce::Justification::centred, false);

    g.setColour (hover || selected ? text2 : muted.withAlpha (0.7f));
    for (int col = 0; col < 2; ++col)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse (grip.getCentreX() + ((float) col - 0.5f) * 6.0f - 1.6f, grip.getCentreY() + (float) (row - 1) * 6.0f - 1.6f, 3.2f, 3.2f);
}

void SampleRow::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    pressedPlay = playArea().contains (e.position);

    if (e.mods.isPopupMenu())
    {
        if (onSelect) onSelect();
        if (onMenu) onMenu();
        return;
    }
    if (! pressedPlay && onSelect)
        onSelect();
}

void SampleRow::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragStarted && ! e.mods.isPopupMenu() && e.getDistanceFromDragStart() > 6)
    {
        dragStarted = true;
        if (onDragOut)
            onDragOut();
    }
}

void SampleRow::mouseUp (const juce::MouseEvent& e)
{
    if (! dragStarted && pressedPlay && playArea().contains (e.position) && onPlay)
        onPlay();
    dragStarted = false;
    pressedPlay = false;
}

// ---------------------------------------------------------------------------- list
const char* SampleList::groupId (int index)
{
    static const char* ids[] = { "Loops", "Drums", "Bass", "Melody", "Vocals" };
    return ids[juce::jlimit (0, numGroups - 1, index)];
}

const char* SampleList::groupTitle (int index)
{
    static const char* titles[] = { "Loops", "Drums", "808 & Bass", "Melody", "Vocals" };
    return titles[juce::jlimit (0, numGroups - 1, index)];
}

SampleList::SampleList()
{
    for (int i = 0; i < numGroups; ++i)
    {
        tabs[(size_t) i].onClick = [this, i] { shownGroup = i; rebuild(); };
        addAndMakeVisible (tabs[(size_t) i]);
    }
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    rebuild();
}

void SampleList::setRows (std::array<std::vector<RowModel>, numGroups> g)
{
    groups = std::move (g);
    rebuild();
}

void SampleList::setPlaceholder (const juce::String& t)
{
    placeholder = t;
    repaint();
}

void SampleList::showTabFor (int id)
{
    for (int i = 0; i < numGroups; ++i)
        for (const auto& m : groups[(size_t) i])
            if (m.id == id)
            {
                if (i != shownGroup)
                {
                    shownGroup = i;
                    rebuild();
                }
                return;
            }
}

void SampleList::setSelectedId (int id)
{
    selectedId = id;
    for (auto& row : rows)
        row->setSelected (row->getModel().id == id);
}

void SampleList::setPlayingId (int id)
{
    if (playingId == id)
        return;
    playingId = id;
    for (auto& row : rows)
        row->setPlaying (id >= 0 && row->getModel().id == id);
}

void SampleList::rebuild()
{
    for (int i = 0; i < numGroups; ++i)
    {
        auto& tab = tabs[(size_t) i];
        const auto count = (int) groups[(size_t) i].size();
        tab.setButtonText (juce::String (groupTitle (i)) + (count > 0 ? "  " + juce::String (count) : juce::String()));
        tab.getProperties().set ("chrome", i == shownGroup);
        tab.setEnabled (count > 0 || i == shownGroup);
        tab.repaint();
    }

    const auto& models = groups[(size_t) shownGroup];
    rows.resize (models.size());
    for (size_t i = 0; i < models.size(); ++i)
    {
        if (rows[i] == nullptr)
        {
            rows[i] = std::make_unique<SampleRow>();
            auto* row = rows[i].get();
            auto forward = [row] (std::function<void (int)>& target) { return [row, &target] { if (target) target (row->getModel().id); }; };
            row->onSelect = forward (onSelect);
            row->onPlay = forward (onPlay);
            row->onDragOut = forward (onDragOut);
            row->onMenu = forward (onMenu);
            content.addAndMakeVisible (*row);
        }
        rows[i]->setModel (models[i]);
        rows[i]->setSelected (models[i].id == selectedId);
        rows[i]->setPlaying (models[i].id == playingId);
    }
    layoutRows();
    repaint();
}

void SampleList::resized()
{
    auto r = getLocalBounds().reduced (18, 16);
    auto tabRow = r.removeFromTop (34);
    const int widths[] = { 112, 112, 140, 120, 116 };
    for (int i = 0; i < numGroups; ++i)
    {
        tabs[(size_t) i].setBounds (tabRow.removeFromLeft (widths[i]));
        tabRow.removeFromLeft (8);
    }
    r.removeFromTop (10);
    viewport.setBounds (r);
    layoutRows();
}

void SampleList::layoutRows()
{
    const int h = (int) rows.size() * rowHeight;
    const int w = viewport.getWidth() - (h > viewport.getHeight() ? viewport.getScrollBarThickness() + 4 : 0);
    content.setSize (juce::jmax (1, w), h);
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, (int) i * rowHeight, w, rowHeight);
}

void SampleList::paint (juce::Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat().reduced (1.0f));
    if (rows.empty())
        drawMonoLabel (g, placeholder, viewport.getBounds().toFloat(), juce::Justification::centred, muted, 12.0f);
}

} // namespace digga
