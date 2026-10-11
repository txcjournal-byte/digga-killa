#include "ui/MainView.h"

#include "PluginProcessor.h"
#include "export/Exporter.h"

namespace digga
{

MainView::MainView (TrapWinProcessor& p) : processor (p)
{
    setOpaque (true);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &vinyl, &list, &editor })
        addAndMakeVisible (c);

    auto& engine = processor.getEngine();

    header.onDouble = [&engine] { if (const auto* a = engine.getAnalysis()) engine.setSampleBpm (a->bpm * 2.0); };
    header.onHalve = [&engine] { if (const auto* a = engine.getAnalysis()) engine.setSampleBpm (a->bpm * 0.5); };
    header.onBpmTyped = [&engine] (double bpm) { engine.setSampleBpm (bpm); };
    header.format.setMidi (processor.isDragAsMidi());
    header.format.onChange = [this] (bool midi) { processor.setDragAsMidi (midi); prefetchMidi(); };

    vinyl.onFileChosen = [this] (const juce::File& file)
    {
        processor.getEngine().newSampleChosen();
        processor.getSampleStore().loadFile (file);
    };
    vinyl.onPlayToggled = [this]
    {
        auto& player = processor.getPlayer();
        if (player.isPlaying()) player.requestStop(); else player.requestStart();
    };

    list.onSelect = [&engine, this] (int id) { engine.select (id); prefetchMidi(); };
    list.onPlay = [this] (int id) { togglePreview (id); };
    list.onDragOut = [this] (int id) { dragOut (id); };
    list.onMenu = [this] (int id) { showRowMenu (id); };

    editor.onEdit = [&engine] (int id, const EditSettings& e) { engine.setEdit (id, e); };
    editor.onPlay = [this] (int id) { togglePreview (id); };
    editor.onDragOut = [this] (int id) { dragOut (id); };

    processor.getSampleStore().addChangeListener (this);
    engine.addChangeListener (this);
    refresh();
    startTimerHz (30);
}

MainView::~MainView()
{
    processor.getSampleStore().removeChangeListener (this);
    processor.getEngine().removeChangeListener (this);
}

void MainView::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (bg);

    // faint blue haze behind the header like the web hero
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x224676c4), (float) getWidth() * 0.5f, -120.0f,
                                             juce::Colour (0x004676c4), (float) getWidth() * 0.5f, 260.0f, true));
    g.fillRect (getLocalBounds());

    drawCard (g, vinyl.getBounds().toFloat());
}

void MainView::resized()
{
    header.setBounds (0, 0, logicalWidth, 92);
    vinyl.setBounds (24, 100, 400, 372);
    list.setBounds (440, 100, 816, 372);
    editor.setBounds (24, 488, 1232, 290);
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void MainView::refresh()
{
    const auto& store = processor.getSampleStore();
    const auto& engine = processor.getEngine();

    auto status = store.getStatus();
    juce::String detail;
    if (status == SampleStore::Status::ready && engine.getPhase() != Engine::Phase::ready)
    {
        status = SampleStore::Status::loading;
        detail = engine.getStatusDetail();
    }
    vinyl.setStatus (status, store.getInfo(), detail);

    if (const auto* a = engine.getAnalysis())
        header.setSample (a->bpm, a->keyName);
    else
        header.setSample (0.0, {});
    header.setTempoFixEnabled (engine.getAnalysis() != nullptr && engine.getPhase() == Engine::Phase::ready);

    const double rate = engine.getSampleRate();
    std::vector<RowModel> loops, shots;
    for (const auto& item : engine.getItems())
    {
        RowModel m;
        m.id = item.id;
        m.name = item.name;
        m.detail = item.detail (rate);
        m.length = audio::formatDuration (item.lengthSeconds (rate));
        m.peaks = item.peaks;
        m.edited = ! item.edit.isDefault();
        (item.kind == ClipKind::loop ? loops : shots).push_back (std::move (m));
    }

    list.setPlaceholder (status == SampleStore::Status::loading ? "Digging through the song..." : "Drop a song on the vinyl");
    list.setRows (std::move (loops), std::move (shots));
    list.setSelectedId (engine.getSelected());
    if (engine.getSelected() != shownSelection)
    {
        list.showTabFor (engine.getSelected());
        shownSelection = engine.getSelected();
    }

    editor.setItem (engine.find (engine.getSelected()), rate, engine.getProjectBpm());
}

void MainView::timerCallback()
{
    const auto& player = processor.getPlayer();
    vinyl.setPlayback (player.isPlaying(), player.getProgress());
    header.setProjectBpm (processor.getEngine().getProjectBpm());

    const auto& clips = processor.getClipPlayer();
    const int previewing = clips.getPreviewId();
    list.setPlayingId (previewing);
    editor.setPlayback (previewing >= 0 && previewing == processor.getEngine().getSelected(), clips.getPreviewProgress());
}

void MainView::togglePreview (int id)
{
    auto& clips = processor.getClipPlayer();
    processor.getEngine().select (id);
    if (clips.getPreviewId() == id)
        clips.stopPreview();
    else
        clips.preview (id);
}

void MainView::showRowMenu (int id)
{
    const auto* item = processor.getEngine().find (id);
    if (item == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Play / stop");
    menu.addItem (2, "Reset edits", ! item->edit.isDefault());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition() }),
                        [safe = juce::Component::SafePointer<MainView> (this), id] (int result)
    {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->togglePreview (id);
        else if (result == 2)
            safe->processor.getEngine().setEdit (id, {});
    });
}

void MainView::prefetchMidi()
{
    if (! processor.isDragAsMidi())
        return;
    const auto& engine = processor.getEngine();
    if (const auto* item = engine.find (engine.getSelected()))
        transcriptions.prefetch (item->edited, engine.getSampleRate());
}

void MainView::dragOut (int id)
{
    auto& engine = processor.getEngine();
    const auto* item = engine.find (id);
    if (item == nullptr || item->edited == nullptr)
        return;

    const auto* a = engine.getAnalysis();
    const double bpm = engine.getProjectBpm();
    const auto fileName = exporter::makeFileName (item->fileTag(), bpm, a != nullptr ? a->keyName : juce::String());
    juce::File file;

    juce::MouseCursor::showWaitCursor();
    if (processor.isDragAsMidi())
    {
        midi::ExportOptions options;
        options.bpm = bpm;
        options.lengthSeconds = item->lengthSeconds (engine.getSampleRate());
        if (const auto notes = transcriptions.get (item->edited, engine.getSampleRate()))
            file = midi::writeToTempFile (*notes, options, fileName);
    }
    else
    {
        file = exporter::writeWav (*item->edited, engine.getSampleRate(), fileName);
    }
    juce::MouseCursor::hideWaitCursor();

    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

} // namespace digga
