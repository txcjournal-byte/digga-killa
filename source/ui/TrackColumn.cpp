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
            auto* row = rows[i].get();
            auto forward = [row] (std::function<void (int)>& target)
            {
                return [row, &target] { if (target) target (row->getModel().id); };
            };
            row->onSelect = forward (onSelect);
            row->onPlay = forward (onPlay);
            row->onKill = forward (onKill);
            row->onToggle = forward (onToggle);
            row->onMenu = forward (onMenu);
            row->onDragOut = forward (onDragOut);
            content.addAndMakeVisible (*row);
        }

        rows[i]->setModel (models[i]);
        rows[i]->setSelected (models[i].id >= 0 && models[i].id == selectedId);
        rows[i]->setPlaying (models[i].id >= 0 && models[i].id == playingId);
    }

    layoutRows();
}

void TrackColumn::setSelectedId (int id)
{
    selectedId = id;
    for (auto& row : rows)
        row->setSelected (id >= 0 && row->getModel().id == id);
}

void TrackColumn::setPlayingId (int id)
{
    if (playingId == id)
        return;
    playingId = id;
    for (auto& row : rows)
        row->setPlaying (id >= 0 && row->getModel().id == id);
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
