#include "ui/MainView.h"

#include "PluginProcessor.h"
#include "export/Exporter.h"

namespace digga
{

MainView::MainView (TrapWinProcessor& p) : processor (p)
{
    setOpaque (true);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &vinyl, &list, &editor, &overview, &mixer })
        addAndMakeVisible (c);

    auto& engine = processor.getEngine();

    header.onDouble = [&engine] { if (const auto* a = engine.getAnalysis()) engine.setSampleBpm (a->bpm * 2.0); };
    header.onHalve = [&engine] { if (const auto* a = engine.getAnalysis()) engine.setSampleBpm (a->bpm * 0.5); };
    header.onBpmTyped = [&engine] (double bpm) { engine.setSampleBpm (bpm); };
    header.format.setMidi (processor.isDragAsMidi());
    header.format.onChange = [this] (bool midi) { processor.setDragAsMidi (midi); prefetchMidi(); };
    header.onExportAll = [this] { exportAll(); };

    overview.onSeek = [this] (float fraction) { processor.getPlayer().requestStartAt (fraction); };
    mixer.onChange = [this] (const std::array<float, 4>& levels)
    {
        auto& e = processor.getEngine();
        if (const auto* item = e.find (e.getSelected()))
        {
            auto edit = item->edit;
            edit.stems = levels;
            if (levels == item->baseMix)
                edit.stems = EditSettings().stems;
            e.setEdit (item->id, edit);
        }
    };

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
    header.setBounds (0, 0, logicalWidth, 80);
    vinyl.setBounds (24, 86, 300, 350);
    list.setBounds (336, 86, 920, 350);
    overview.setBounds (24, 446, 1232, 102);
    editor.setBounds (24, 558, 968, 234);
    mixer.setBounds (1004, 558, 252, 234);
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
    std::array<std::vector<RowModel>, SampleList::numGroups> groups;
    for (const auto& item : engine.getItems())
    {
        RowModel m;
        m.id = item.id;
        m.name = item.name;
        m.detail = item.detail (rate);
        m.length = audio::formatDuration (item.lengthSeconds (rate));
        if (item.kind == ClipKind::shot)
            m.length = juce::String (item.lengthSeconds (rate), 2) + "s";
        m.peaks = item.peaks;
        m.edited = ! item.edit.isDefault();
        m.loop = item.kind == ClipKind::loop;
        const auto mix = item.baseMix;
        if (item.kind == ClipKind::loop && std::count (mix.begin(), mix.end(), 1.0f) == 1)
            m.stem = (int) (std::find (mix.begin(), mix.end(), 1.0f) - mix.begin());
        else if (item.group == "Drums") m.stem = stems::drums;
        else if (item.group == "Bass") m.stem = stems::bass;
        else if (item.group == "Melody") m.stem = stems::other;
        else if (item.group == "Vocals") m.stem = stems::vocals;

        int group = 0;
        for (int g = 0; g < SampleList::numGroups; ++g)
            if (item.group == SampleList::groupId (g))
                group = g;
        groups[(size_t) group].push_back (std::move (m));
    }

    list.setPlaceholder (status == SampleStore::Status::loading ? "Digging through the song..." : "Drop a song on the vinyl");
    list.setRows (std::move (groups));
    list.setSelectedId (engine.getSelected());
    if (engine.getSelected() != shownSelection)
    {
        list.showTabFor (engine.getSelected());
        shownSelection = engine.getSelected();
    }

    refreshOverview();

    // stem mixer: loops only, and only with stems
    const auto* selected = engine.find (engine.getSelected());
    const auto songStems = engine.getStems();
    std::array<bool, 4> present {};
    if (songStems != nullptr)
        for (int st = 0; st < 4; ++st)
            present[(size_t) st] = songStems->present[(size_t) st];
    if (selected != nullptr && selected->canMixStems())
        mixer.setState (true, selected->stemMix(), present, {});
    else
        mixer.setState (false, { 0.0f, 0.0f, 0.0f, 0.0f }, present,
                        songStems == nullptr ? (engine.getPhase() == Engine::Phase::ready && ! engine.hasStemModel() ? "Stem model missing - reinstall" : "Stems appear with a song")
                                             : "Stem mix works on loops");

    editor.setItem (engine.find (engine.getSelected()), rate, engine.getProjectBpm());
}

void MainView::refreshOverview()
{
    const auto& engine = processor.getEngine();
    const auto songStems = engine.getStems();
    const double length = engine.getSourceSeconds();

    if (songStems.get() != shownStems || engine.getSections().size() != shownSections
        || (engine.getOverview().empty() != (length <= 0.0)))
    {
        shownStems = songStems.get();
        shownSections = engine.getSections().size();

        std::array<std::vector<float>, 4> lanes;
        std::array<bool, 4> present {};
        if (songStems != nullptr)
            for (int st = 0; st < 4; ++st)
            {
                lanes[(size_t) st] = songStems->overview[(size_t) st];
                present[(size_t) st] = songStems->present[(size_t) st];
            }

        std::vector<SongOverview::SectionView> views;
        if (const auto* a = engine.getAnalysis())
            for (const auto& sec : engine.getSections())
                views.push_back ({ sec.name, a->downbeatSeconds + sec.startBar * a->barSeconds(),
                                   juce::jmin (length, a->downbeatSeconds + (sec.startBar + sec.numBars) * a->barSeconds()) });
        overview.setSong (length, engine.getOverview(), lanes, present, views);
    }

    overview.setPlaceholder (engine.getPhase() == Engine::Phase::analysing ? engine.getStatusDetail() : juce::String ("Drop a song on the vinyl"));

    // where the selected sample comes from
    const auto* item = engine.find (engine.getSelected());
    const auto* a = engine.getAnalysis();
    if (item != nullptr && a != nullptr)
    {
        int stem = -1;
        const auto mix = item->stemMix();
        if (std::count_if (mix.begin(), mix.end(), [] (float v) { return v > 0.0f; }) == 1)
            stem = (int) (std::find_if (mix.begin(), mix.end(), [] (float v) { return v > 0.0f; }) - mix.begin());
        else if (item->group == "Drums") stem = stems::drums;
        else if (item->group == "Bass") stem = stems::bass;
        else if (item->group == "Melody") stem = stems::other;
        else if (item->group == "Vocals") stem = stems::vocals;
        if (item->kind == ClipKind::loop && item->group == "Loops" && ! item->edit.hasStemMix())
            stem = -1;

        const double end = item->kind == ClipKind::loop ? item->sourceSeconds + item->bars * a->barSeconds()
                                                        : item->sourceSeconds + juce::jmax (0.15, item->lengthSeconds (engine.getSampleRate()));
        overview.setSelection (item->sourceSeconds, end, stem);
    }
    else
        overview.setSelection (-1.0, -1.0, -1);
}

void MainView::exportAll()
{
    auto& engine = processor.getEngine();
    if (engine.getItems().empty())
        return;

    const auto songName = processor.getSampleStore().getInfo().file.getFileNameWithoutExtension();
    const auto base = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TrapWin Sampler");
    chooser = std::make_unique<juce::FileChooser> ("Export all samples to...", base, "", true);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<MainView> (this), songName] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        auto& e = safe->processor.getEngine();
        const auto* a = e.getAnalysis();
        juce::MouseCursor::showWaitCursor();
        const auto key = a != nullptr ? a->keyName : juce::String();
        const auto folder = fc.getResult().getChildFile (juce::File::createLegalFileName ((songName.isNotEmpty() ? songName : juce::String ("Song"))
                                                                                          + " - TrapWin Sampler")).getNonexistentSibling (false);
        for (const auto& item : e.getItems())
        {
            if (item.edited == nullptr)
                continue;
            int g = 0;
            for (int k = 0; k < SampleList::numGroups; ++k)
                if (item.group == SampleList::groupId (k))
                    g = k;
            const auto dir = folder.getChildFile (SampleList::groupTitle (g));
            dir.createDirectory();
            const auto name = exporter::makeFileName (item.fileTag(), e.getProjectBpm(), key);
            exporter::writeWavTo (*item.edited, e.getSampleRate(), dir.getChildFile (name + ".wav").getNonexistentSibling (false));
        }
        juce::MouseCursor::hideWaitCursor();
        if (folder.isDirectory())
            folder.revealToUser();
    });
}

void MainView::timerCallback()
{
    const auto& player = processor.getPlayer();
    vinyl.setPlayback (player.isPlaying(), player.getProgress());
    header.setProjectBpm (processor.getEngine().getProjectBpm());

    overview.setPlayhead (player.isPlaying(), player.getProgress());

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
