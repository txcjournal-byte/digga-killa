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
      loops ("LOOPS", TrackRow::Style::loop, 42),
      shots ("ONE-SHOTS", TrackRow::Style::shot, 52),
      fx (p.getParameters())
{
    setOpaque (true);

    for (auto* c : std::initializer_list<juce::Component*> { &header, &loops, &shots, &record, &fx })
        addAndMakeVisible (c);

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
    paper.draw (g, getLocalBounds());

    g.setColour (theme::ink);
    g.fillRect (30.0f, 204.0f, 1284.0f, 2.5f);      // under the title
    g.fillRect (30.0f, 692.0f, 1284.0f, 2.0f);      // above the FX strip
    g.fillRect (962.0f, 214.0f, 1.5f, 470.0f);      // one-shots divider
}

void MainView::resized()
{
    header.setBounds (28, 0, 1288, 200);
    loops.setBounds (28, 212, 492, 472);
    record.setBounds (522, 222, 432, 432);
    shots.setBounds (972, 212, 344, 472);
    fx.setBounds (28, 698, 1288, 186);
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
    header.tempo.setProjectBpm (processor.getHostBpm());
}

} // namespace digga
