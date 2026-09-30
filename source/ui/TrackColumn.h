#pragma once

#include "ui/TrackRow.h"

#include <memory>
#include <vector>

namespace digga
{

/** Titled, scrollable tracklist ("LOOPS" / "ONE-SHOTS"). */
class TrackColumn : public juce::Component
{
public:
    TrackColumn (juce::String title, TrackRow::Style style, int rowHeight);

    void setRows (const std::vector<TrackRowModel>& models);
    int getNumRows() const noexcept { return (int) rows.size(); }
    TrackRow* getRow (int index) const { return juce::isPositiveAndBelow (index, rows.size()) ? rows[(size_t) index].get() : nullptr; }

    void setSelectedRow (int index);
    int getSelectedRow() const noexcept { return selectedRow; }

    std::function<void (int)> onRowSelected;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int headingHeight = 52;

private:
    void layoutRows();

    juce::String title;
    TrackRow::Style style;
    int rowHeight;
    int selectedRow = -1;

    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<TrackRow>> rows;
};

} // namespace digga
