#include "ui/TrackColumn.h"

namespace digga
{

TrackColumn::TrackColumn (juce::String t, TrackRow::Style s, int height)
    : title (std::move (t)), style (s), rowHeight (height)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

void TrackColumn::setRows (const std::vector<TrackRowModel>& models)
{
    rows.resize (models.size());

    for (size_t i = 0; i < models.size(); ++i)
    {
        if (rows[i] == nullptr)
        {
            rows[i] = std::make_unique<TrackRow> (style);
            const int index = (int) i;
            rows[i]->onSelect = [this, index] { setSelectedRow (index); if (onRowSelected) onRowSelected (index); };
            content.addAndMakeVisible (*rows[i]);
        }

        rows[i]->setModel (models[i]);
    }

    if (selectedRow >= (int) rows.size())
        selectedRow = -1;

    layoutRows();
}

void TrackColumn::setSelectedRow (int index)
{
    selectedRow = index;
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setSelected ((int) i == index);
}

void TrackColumn::paint (juce::Graphics& g)
{
    const auto heading = getLocalBounds().removeFromTop (headingHeight).toFloat().reduced (2.0f, 8.0f);
    const auto path = theme::textPath (title, theme::display (40.0f), heading, false,
                                       juce::Justification::centredLeft);
    g.setColour (theme::ink);
    g.fillPath (path);
    g.strokePath (path, juce::PathStrokeType (1.0f));
    theme::addGrunge (g, path, theme::paper, title.hashCode(), 1.2f);
}

void TrackColumn::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (headingHeight));
    layoutRows();
}

void TrackColumn::layoutRows()
{
    const int width = viewport.getMaximumVisibleWidth();
    const int contentHeight = (int) rows.size() * rowHeight;
    const bool needsScroll = contentHeight > viewport.getHeight();
    const int rowWidth = needsScroll ? viewport.getWidth() - viewport.getScrollBarThickness() - 2 : viewport.getWidth();

    content.setSize (juce::jmax (width, rowWidth), contentHeight);

    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, (int) i * rowHeight, rowWidth, rowHeight);
}

} // namespace digga
