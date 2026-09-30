#pragma once

#include "ui/TrackRow.h"

#include <memory>
#include <vector>

namespace digga
{

/** Scrollable tracklist ("LOOPS" / "ONE-SHOTS" headings are printed in the
    skin). Root rows and KILL variations can have different heights, as in
    the design. */
class TrackColumn : public juce::Component
{
public:
    TrackColumn (TrackRow::Style style, int rootRowHeight, int childRowHeight);

    void setRows (const std::vector<TrackRowModel>& models);
    int getNumRows() const noexcept { return (int) rows.size(); }
    TrackRow* getRow (int index) const { return juce::isPositiveAndBelow (index, rows.size()) ? rows[(size_t) index].get() : nullptr; }

    /** Highlights the row showing this node (or none). */
    void setSelectedId (int id);
    void setPlayingId (int id);

    std::function<void (int id)> onSelect, onPlay, onKill, onToggle, onMenu, onDragOut;

    void resized() override;

private:
    void layoutRows();

    TrackRow::Style style;
    int rootHeight, childHeight;
    int selectedId = -1, playingId = -1;

    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<TrackRow>> rows;
};

} // namespace digga
