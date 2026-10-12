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
    header.onExportAll = [this] { exportAll(); };

    editor.onStemMix = [this] (int id, const std::array<float, 4>& levels)
    {
        auto& e = processor.getEngine();
        if (const auto* item = e.find (id))
        {
            auto edit = item->edit;
            edit.stems = levels == item->baseMix ? EditSettings().stems : levels;
            e.setEdit (id, edit);
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
    editor.onAudition = [this] (int id, double fraction)
    {
        // a point of the original -> the same point of the clip as heard
        auto& e = processor.getEngine();
        const auto* item = e.find (id);
        if (item == nullptr)
            return;
        const auto& ed = item->edit;
        if (fraction < ed.start || fraction > ed.end)
            return;
        double f = (fraction - ed.start) / juce::jmax (1.0e-9, ed.end - ed.start);
        f = std::fmod (f - ed.loopStart + 1.0, 1.0);
        if (ed.reverse)
            f = 1.0 - f;
        e.select (id);
        processor.getClipPlayer().previewFrom (id, (float) f);
    };
    editor.onViewChanged = [this] (EditorPanel::View) { updateMidiView(); };

    auto& midiView = editor.getMidiView();
    midiView.onNotesEdited = [this] { midiEdited = true; };
    midiView.onDragOut = [this]
    {
        auto& e = processor.getEngine();
        const auto* item = e.find (e.getSelected());
        const auto& notes = editor.getMidiView().getNotes();
        if (item == nullptr || notes.empty())
            return;
        const auto* a = e.getAnalysis();
        midi::ExportOptions options;
        options.bpm = e.getProjectBpm();
        options.lengthSeconds = item->lengthSeconds (e.getSampleRate());
        const auto file = midi::writeToTempFile (notes, options,
                                                 exporter::makeFileName (item->fileTag(), options.bpm, a != nullptr ? a->keyName : juce::String()));
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    };
    editor.onPlay = [this] (int id) { togglePreview (id); };
    editor.onDragOut = [this] (int id) { dragOut (id); };
    editor.onDragChops = [this] (int id) { dragChops (id); };

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
    header.setBounds (0, 0, logicalWidth, 70);
    vinyl.setBounds (20, 76, 268, 276);
    list.setBounds (300, 76, 880, 276);
    editor.setBounds (20, 364, 1160, 366);
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

    // stem toggles of the editor: loops only, and only with stems
    const auto songStems = engine.getStems();
    std::array<bool, 4> present {};
    if (songStems != nullptr)
        for (int st = 0; st < 4; ++st)
            present[(size_t) st] = songStems->present[(size_t) st];
    editor.setStems (songStems != nullptr, present);

    editor.setItem (engine.find (engine.getSelected()), rate, engine.getProjectBpm());
    updateMidiView();
}

void MainView::updateMidiView()
{
    auto& midiView = editor.getMidiView();
    if (editor.getView() != EditorPanel::View::midi)
        return;

    const auto& engine = processor.getEngine();
    const auto* item = engine.find (engine.getSelected());
    if (item == nullptr || item->dry == nullptr)
    {
        shreddedAudio = nullptr;
        midiView.setEmpty ("Pick a loop or one-shot to shred it into MIDI");
        return;
    }

    // the sample as edited, without effects, is what gets shredded
    if (item->dry == shreddedAudio)
        return;

    if (const auto notes = transcriptions.tryGet (item->dry, engine.getSampleRate(), engine.pitchedSource (item->id)))
    {
        shreddedAudio = item->dry;
        midiEdited = false;
        midiView.setBusy (false);
        if (notes->empty())
            midiView.setEmpty ("Nothing pitched in this sample - try a melody or 808 loop");
        else
            midiView.setNotes (*notes, item->lengthSeconds (engine.getSampleRate()), engine.getProjectBpm());
    }
    else
        midiView.setBusy (true);
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

    if (editor.getView() == EditorPanel::View::midi && shreddedAudio == nullptr)
        updateMidiView();
    else if (editor.getView() == EditorPanel::View::midi)
    {
        const auto& e = processor.getEngine();
        if (const auto* item = e.find (e.getSelected()); item != nullptr && item->dry != shreddedAudio)
            updateMidiView();
    }

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
        transcriptions.prefetch (item->dry, engine.getSampleRate(), engine.pitchedSource (item->id));
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
    const bool withFx = editor.isDragWithFx() && item->edit.fx.anyOn();
    const auto audio = withFx || item->dry == nullptr ? item->edited : item->dry;

    if (processor.isDragAsMidi())
    {
        midi::ExportOptions options;
        options.bpm = bpm;
        options.lengthSeconds = item->lengthSeconds (engine.getSampleRate());
        // the notes as edited in the shredder, else a fresh transcription
        if (item->dry == shreddedAudio && ! editor.getMidiView().getNotes().empty())
            file = midi::writeToTempFile (editor.getMidiView().getNotes(), options, fileName);
        else if (const auto notes = transcriptions.get (item->dry, engine.getSampleRate(), engine.pitchedSource (item->id)))
            file = midi::writeToTempFile (*notes, options, fileName);
    }
    else
    {
        file = exporter::writeWav (*audio, engine.getSampleRate(), withFx ? fileName + "_FX" : fileName);
    }
    juce::MouseCursor::hideWaitCursor();

    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void MainView::dragChops (int id)
{
    auto& engine = processor.getEngine();
    const auto* item = engine.find (id);
    const auto chops = engine.getChops (id);
    if (item == nullptr || item->edited == nullptr || chops == nullptr || chops->empty())
        return;

    // one note per chop, placed where the chop plays in the loop: on the
    // TrapWin Sampler channel (this loop selected) it sounds like the WAV
    const double rate = engine.getSampleRate();
    const int length = item->edited->getNumSamples();
    std::vector<midi::Note> notes;
    for (size_t k = 0; k < chops->size(); ++k)
    {
        midi::Note n;
        n.startSeconds = (*chops)[k] / rate;
        n.endSeconds = (k + 1 < chops->size() ? (*chops)[k + 1] : length) / rate;
        n.pitch = chopBaseNote + (int) k;
        n.amplitude = 1.0f;   // full velocity = the level of the WAV
        notes.push_back (n);
    }

    const auto* a = engine.getAnalysis();
    midi::ExportOptions options;
    options.bpm = engine.getProjectBpm();
    options.lengthSeconds = item->lengthSeconds (rate);
    const auto file = midi::writeToTempFile (notes, options,
                                             exporter::makeFileName (item->fileTag() + "_Chops", options.bpm,
                                                                     a != nullptr ? a->keyName : juce::String()));
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

} // namespace digga
