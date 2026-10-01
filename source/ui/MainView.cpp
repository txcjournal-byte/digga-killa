#include "ui/MainView.h"

#include "PluginProcessor.h"
#include "export/Exporter.h"

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
      shots (TrackRow::Style::shot, 52, 40),
      fx (p.getParameters())
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    for (auto* c : std::initializer_list<juce::Component*> { &tempo, &tempo.doubleButton, &tempo.halveButton,
                                                             &format, &loops, &shots, &record, &fx })
        addAndMakeVisible (c);

    format.setMidi (processor.isDragAsMidi());
    format.onChange = [this] (bool midi)
    {
        processor.setDragAsMidi (midi);
        prefetchMidi();
    };

    auto& engine = processor.getEngine();

    tempo.doubleButton.onClick = [&engine]
    {
        if (const auto* a = engine.getAnalysis())
            engine.setSampleBpm (a->bpm * 2.0);
    };
    tempo.halveButton.onClick = [&engine]
    {
        if (const auto* a = engine.getAnalysis())
            engine.setSampleBpm (a->bpm * 0.5);
    };
    tempo.onBpmTyped = [&engine] (double bpm) { engine.setSampleBpm (bpm); };

    wireColumn (loops);
    wireColumn (shots);

    record.onFileChosen = [this] (const juce::File& file)
    {
        processor.getEngine().newSampleChosen();
        processor.getSampleStore().loadFile (file);
    };
    record.onPlayToggled = [this]
    {
        auto& player = processor.getPlayer();
        if (player.isPlaying())
            player.requestStop();
        else
            player.requestStart();
    };

    processor.getSampleStore().addChangeListener (this);
    engine.addChangeListener (this);
    refreshSampleStatus();
    refreshResults();
    startTimerHz (30);
}

MainView::~MainView()
{
    processor.getSampleStore().removeChangeListener (this);
    processor.getEngine().removeChangeListener (this);
}

void MainView::wireColumn (TrackColumn& column)
{
    auto& engine = processor.getEngine();

    column.onSelect = [this, &engine] (int id)
    {
        engine.select (id);
        grabKeyboardFocus();
        prefetchMidi();
    };
    column.onPlay = [this] (int id) { togglePreview (id); };
    column.onKill = [this, &engine] (int id)
    {
        engine.select (id);
        const auto strength = processor.getParameters().getRawParameterValue (ParamIDs::killStrength)->load();
        engine.kill (id, strength);
    };
    column.onToggle = [&engine] (int id)
    {
        if (const auto* node = engine.getTree().find (id))
            engine.setExpanded (id, ! node->expanded);
    };
    column.onMenu = [this] (int id) { showRowMenu (id); };
    column.onDragOut = [this] (int id) { dragOut (id); };
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
    format.setBounds (1150, 84, 173, 28);
    loops.setBounds (28, 263, 500, 424);
    shots.setBounds (978, 262, 340, 424);
    record.setBounds (525, 226, 428, 428);
    fx.setBounds (FxPanel::designBounds);
}

bool MainView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        processor.getEngine().undo();
        return true;
    }
    return false;
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshSampleStatus();
    refreshResults();
}

void MainView::refreshSampleStatus()
{
    const auto& store = processor.getSampleStore();
    const auto& engine = processor.getEngine();

    auto status = store.getStatus();
    auto info = store.getInfo();
    juce::String detail;

    if (status == SampleStore::Status::ready)
    {
        switch (engine.getPhase())
        {
            case Engine::Phase::empty:
            case Engine::Phase::analysing:  status = SampleStore::Status::loading; detail = "FINDING TEMPO & KEY"; break;
            case Engine::Phase::generating: status = SampleStore::Status::loading; detail = "CUTTING LOOPS & SHOTS"; break;
            case Engine::Phase::ready:      break;
        }
    }

    record.setStatus (status, info, detail);

    if (const auto* a = engine.getAnalysis())
        tempo.setSample (a->bpm, a->keyName);
    else
        tempo.setSample (0.0, {});

    const bool canFix = engine.getAnalysis() != nullptr && engine.getPhase() == Engine::Phase::ready;
    tempo.doubleButton.setEnabled (canFix);
    tempo.halveButton.setEnabled (canFix);
}

void MainView::refreshResults()
{
    const auto& engine = processor.getEngine();
    const auto& tree = engine.getTree();

    if (tree.isEmpty())
    {
        loops.setRows (placeholderLoops());
        shots.setRows (placeholderShots());
        return;
    }

    const double rate = engine.getSampleRate();

    auto build = [&] (ClipKind kind)
    {
        std::vector<TrackRowModel> models;
        for (const auto& row : tree.visibleRows (kind))
        {
            TrackRowModel m;
            m.id = row.node->id;
            m.name = row.node->displayName();
            m.duration = audio::formatDuration (row.node->lengthSeconds (rate));
            m.depth = row.depth;
            m.lastSibling = row.lastSibling;
            m.placeholder = false;
            m.hasChildren = row.hasChildren;
            m.expanded = row.node->expanded;
            m.busy = engine.isBusy (m.id);
            m.peaks = row.node->peaks;
            models.push_back (std::move (m));
        }
        return models;
    };

    loops.setRows (build (ClipKind::loop));
    shots.setRows (build (ClipKind::shot));
    loops.setSelectedId (engine.getSelected());
    shots.setSelectedId (engine.getSelected());
}

void MainView::timerCallback()
{
    const auto& player = processor.getPlayer();
    record.setPlayback (player.isPlaying(), player.getProgress());
    tempo.setProjectBpm (processor.getEngine().getProjectBpm());

    const int previewing = processor.getClipPlayer().getPreviewId();
    loops.setPlayingId (previewing);
    shots.setPlayingId (previewing);
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
    auto& engine = processor.getEngine();
    const auto* node = engine.getTree().find (id);
    if (node == nullptr)
        return;

    const bool hasChildren = ! engine.getTree().childrenOf (id).empty();

    juce::PopupMenu menu;
    menu.addItem (1, "KILL again (new 6 variations)");
    if (hasChildren)
        menu.addItem (2, node->expanded ? "Collapse variations" : "Expand variations");
    menu.addItem (3, "Undo last KILL", engine.canUndo());
    if (! node->isRoot())
    {
        menu.addSeparator();
        menu.addItem (4, "Copy seed " + juce::String ((juce::int64) node->seed));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition() }),
                        [safe = juce::Component::SafePointer<MainView> (this), id, seed = node->seed] (int result)
    {
        if (safe == nullptr)
            return;
        auto& e = safe->processor.getEngine();
        switch (result)
        {
            case 1: e.kill (id, safe->processor.getParameters().getRawParameterValue (ParamIDs::killStrength)->load()); break;
            case 2: if (const auto* n = e.getTree().find (id)) e.setExpanded (id, ! n->expanded); break;
            case 3: e.undo(); break;
            case 4: juce::SystemClipboard::copyTextToClipboard (juce::String ((juce::int64) seed)); break;
            default: break;
        }
    });
}

void MainView::prefetchMidi()
{
    // transcribing takes a moment: start on the selected row before it is dragged
    if (! processor.isDragAsMidi())
        return;
    const auto& engine = processor.getEngine();
    if (const auto* node = engine.getTree().find (engine.getSelected()))
        transcriptions.prefetch (node->audio, engine.getSampleRate());
}

void MainView::dragOut (int id)
{
    auto& engine = processor.getEngine();
    const auto* node = engine.getTree().find (id);
    if (node == nullptr || node->audio == nullptr)
        return;

    const auto* a = engine.getAnalysis();
    const auto fileName = exporter::makeFileName (node->fileTag(), engine.getProjectBpm(), a != nullptr ? a->keyName : juce::String());
    juce::File file;

    juce::MouseCursor::showWaitCursor();

    if (processor.isDragAsMidi())
    {
        midi::ExportOptions options;
        options.bpm = engine.getProjectBpm();
        options.lengthSeconds = node->lengthSeconds (engine.getSampleRate());
        options.transpose = juce::roundToInt (processor.getFxParams().pitch);
        options.reverse = processor.isReverseOn();

        if (const auto notes = transcriptions.get (node->audio, engine.getSampleRate()))
            file = midi::writeToTempFile (*notes, options, fileName);
    }
    else
    {
        exporter::Request request;
        request.audio = node->audio;
        request.isLoop = node->kind == ClipKind::loop;
        request.reverse = processor.isReverseOn();
        request.fx = processor.getFxParams();
        request.fx.bpm = engine.getProjectBpm();
        request.sampleRate = engine.getSampleRate();
        request.fileName = fileName;
        file = exporter::renderToTempFile (request);
    }

    juce::MouseCursor::hideWaitCursor();

    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

} // namespace digga
