#include "ui/MainView.h"

#include "PluginProcessor.h"

namespace digga
{

namespace
{
    const juce::String dash (juce::CharPointer_UTF8 (" \xe2\x80\x93 "));

    std::vector<TrackRowModel> placeholderLoops()
    {
        std::vector<TrackRowModel> rows;
        for (const auto* slot : { "A1", "A2", "B1", "B2" })
        {
            TrackRowModel row;
            const bool isLong = slot[1] == '2';
            row.name = juce::String (slot) + dash + "Loop " + (isLong ? "16" : "8") + " bars";
            row.duration = "-:--";
            rows.push_back (row);
        }
        return rows;
    }

    std::vector<TrackRowModel> placeholderShots()
    {
        std::vector<TrackRowModel> rows;
        for (int i = 1; i <= 8; ++i)
        {
            TrackRowModel row;
            row.name = "Shot " + juce::String (i);
            row.duration = "-:--";
            rows.push_back (row);
        }
        return rows;
    }
}

MainView::MainView (DiggaKillaProcessor& p)
    : processor (p),
      background (theme::skinBackground()),
      loops (TrackRow::Style::loop, 44, 38),
      shots (TrackRow::Style::shot, 52, 52),
      fx (p.getParameters())
{
    setOpaque (true);

    for (auto* c : std::initializer_list<juce::Component*> { &tempo, &tempo.doubleButton, &tempo.halveButton,
                                                             &loops, &shots, &record, &fx })
        addAndMakeVisible (c);

    // enabled once tempo detection exists (phase 2)
    tempo.doubleButton.setEnabled (false);
    tempo.halveButton.setEnabled (false);

    loops.setRows (placeholderLoops());
    shots.setRows (placeholderShots());

    record.onFileChosen = [this] (const juce::File& file) { processor.getSampleStore().loadFile (file); };
    record.onPlayToggled = [this]
    {
        auto& player = processor.getPlayer();
        if (player.isPlaying())
            player.requestStop();
        else
            player.requestStart();
    };

    processor.getSampleStore().addChangeListener (this);
    refreshSampleStatus();
    startTimerHz (30);
}

MainView::~MainView()
{
    processor.getSampleStore().removeChangeListener (this);
}

void MainView::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (background, getLocalBounds().toFloat());
}

// All positions are design pixels measured on docs/design.png.
void MainView::resized()
{
    tempo.setBounds (TempoDisplay::textBounds);
    tempo.doubleButton.setBounds (1250, 47, 36, 28);
    tempo.halveButton.setBounds (1287, 47, 36, 28);
    loops.setBounds (28, 263, 500, 424);
    shots.setBounds (978, 262, 340, 424);
    record.setBounds (525, 226, 428, 428);
    fx.setBounds (FxPanel::designBounds);
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshSampleStatus();
}

void MainView::refreshSampleStatus()
{
    const auto& store = processor.getSampleStore();
    record.setStatus (store.getStatus(), store.getInfo());
}

void MainView::timerCallback()
{
    const auto& player = processor.getPlayer();
    record.setPlayback (player.isPlaying(), player.getProgress());
    tempo.setProjectBpm (processor.getHostBpm());
}

} // namespace digga
