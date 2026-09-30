#include "ui/TrackColumn.h"

namespace digga
{

TrackColumn::TrackColumn (TrackRow::Style s, int rootRowHeight, int childRowHeight)
    : style (s), rootHeight (rootRowHeight), childHeight (childRowHeight)
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

void TrackColumn::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutRows();
}

void TrackColumn::layoutRows()
{
    int contentHeight = 0;
    for (const auto& row : rows)
        contentHeight += row->getModel().depth > 0 ? childHeight : rootHeight;

    const bool needsScroll = contentHeight > viewport.getHeight();
    const int rowWidth = needsScroll ? viewport.getWidth() - viewport.getScrollBarThickness() - 2 : viewport.getWidth();

    content.setSize (rowWidth, contentHeight);

    int y = 0;
    for (const auto& row : rows)
    {
        const int h = row->getModel().depth > 0 ? childHeight : rootHeight;
        row->setBounds (0, y, rowWidth, h);
        y += h;
    }
}

} // namespace digga
