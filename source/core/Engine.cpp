#include "core/Engine.h"

#include "loops/LoopGenerator.h"
#include "oneshots/OneShotExtractor.h"

namespace digga
{

namespace
{
    constexpr int loopPeaks = 600, shotPeaks = 300;

    template <typename Fn>
    void onMessageThread (juce::WeakReference<Engine> weak, Fn&& fn)
    {
        juce::MessageManager::callAsync ([weak, fn = std::forward<Fn> (fn)]() mutable
        {
            if (auto* engine = weak.get())
                fn (*engine);
        });
    }
}

// ---------------------------------------------------------------------------- SampleItem
juce::String SampleItem::detail (double rate) const
{
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    const int at = (int) juce::jmax (0.0, sourceSeconds);
    const auto from = "from " + juce::String (at / 60) + ":" + juce::String (at % 60).paddedLeft ('0', 2);
    if (kind == ClipKind::loop)
        return juce::String (bars) + " bars" + dot + from;
    return audio::formatDuration (lengthSeconds (rate)) + dot + from;
}

juce::String SampleItem::fileTag() const
{
    auto tag = name.replaceCharacter (' ', '_');
    if (kind == ClipKind::loop)
        tag << "_" << bars << "bars";
    return tag.removeCharacters ("\\/:*?\"<>|");
}

double SampleItem::lengthSeconds (double rate) const
{
    return edited != nullptr && rate > 0.0 ? edited->getNumSamples() / rate : 0.0;
}

// ---------------------------------------------------------------------------- Engine
Engine::Engine (JobQueue& j, ClipPlayer& p) : jobs (j), player (p)
{
    juce::WeakReference<Engine> warmUp (this);   // master created before worker threads copy it
}

Engine::~Engine() = default;

const SampleItem* Engine::find (int id) const
{
    for (const auto& item : items)
        if (item.id == id)
            return &item;
    return nullptr;
}

double Engine::getProjectBpm() const noexcept
{
    if (hostBpm > 0.0)
        return hostBpm;
    return hasAnalysis ? analysisResult.bpm : 120.0;
}

void Engine::sourceReady (AudioPtr audio, double rate)
{
    onMessageThread (this, [audio, rate] (Engine& e)
    {
        e.source = audio;
        e.sampleRate = rate;
        e.startAnalysis();
    });
}

void Engine::newSampleChosen()
{
    ++generation;
    source = monoSource = nullptr;
    posteriors = nullptr;
    notes = nullptr;
    hasAnalysis = false;
    sampleBpmOverride = 0.0;
    items.clear();
    pendingEdits.clear();
    pendingSelectedKey.clear();
    selectedId = -1;
    player.setSelected (-1);
    phase = Phase::empty;
    publishBank();
    storeState();
    sendChangeMessage();
}

void Engine::startAnalysis()
{
    if (source == nullptr)
        return;

    // keep edits of the current results when the same song is re-analysed (sample-rate change)
    for (const auto& item : items)
        if (! item.edit.isDefault() && pendingEdits.count (item.key) == 0)
            pendingEdits[item.key] = item.edit;
    if (const auto* selected = find (selectedId))
        if (pendingSelectedKey.isEmpty())
            pendingSelectedKey = selected->key;

    const auto gen = ++generation;
    phase = Phase::analysing;
    statusDetail = "FINDING TEMPO, KEY & MELODY";
    sendChangeMessage();

    juce::WeakReference<Engine> weak (this);
    jobs.add ([weak, gen, src = source, rate = sampleRate, tempoOverride = sampleBpmOverride]
    {
        auto mono = std::make_shared<const juce::AudioBuffer<float>> (audio::toMono (*src));
        auto result = analysis::analyse (mono->getReadPointer (0), mono->getNumSamples(), rate);
        if (tempoOverride > 0.0)
        {
            result.bpm = tempoOverride;
            result.downbeatSeconds = analysis::findDownbeat (*result.onsets, mono->getReadPointer (0),
                                                             mono->getNumSamples(), tempoOverride);
        }

        // what is pitched, and when (melody starts, notes for one-shots)
        const auto modelRate = midi::toModelRate (*src, rate);
        auto post = std::make_shared<const midi::Posteriors> (midi::computePosteriors (modelRate.data(), (int) modelRate.size()));
        auto decoded = std::make_shared<const std::vector<midi::Note>> (midi::decodeNotes (*post));

        onMessageThread (weak, [gen, result, mono, post, decoded] (Engine& e)
        {
            if (gen != e.generation)
                return;
            e.analysisResult = result;
            e.monoSource = mono;
            e.posteriors = post;
            e.notes = decoded;
            e.hasAnalysis = true;
            e.startGeneration();
        });
    });
}

void Engine::applyOverrideTempo()
{
    if (! hasAnalysis || monoSource == nullptr)
        return;
    analysisResult.bpm = sampleBpmOverride > 0.0 ? sampleBpmOverride : analysisResult.detectedBpm;
    analysisResult.downbeatSeconds = analysis::findDownbeat (*analysisResult.onsets, monoSource->getReadPointer (0),
                                                             monoSource->getNumSamples(), analysisResult.bpm);
}

void Engine::setSampleBpm (double bpm)
{
    if (! hasAnalysis)
        return;
    sampleBpmOverride = juce::jlimit (40.0, 250.0, std::round (bpm * 100.0) / 100.0);
    applyOverrideTempo();
    startGeneration();
}

void Engine::setHostBpm (double bpm)
{
    if (juce::approximatelyEqual (bpm, hostBpm))
        return;
    hostBpm = bpm;
    if (phase == Phase::ready && std::abs (getProjectBpm() - generatedBpm) > 0.01)
        startGeneration();
    sendChangeMessage();
}

void Engine::startGeneration()
{
    if (! hasAnalysis || source == nullptr)
        return;

    for (const auto& item : items)
        if (! item.edit.isDefault() && pendingEdits.count (item.key) == 0)
            pendingEdits[item.key] = item.edit;
    if (const auto* selected = find (selectedId))
        if (pendingSelectedKey.isEmpty())
            pendingSelectedKey = selected->key;

    const auto gen = ++generation;
    phase = Phase::generating;
    statusDetail = "CUTTING LOOPS & ONE-SHOTS";
    sendChangeMessage();

    const double bpm = getProjectBpm();
    juce::WeakReference<Engine> weak (this);

    jobs.add ([weak, gen, src = source, rate = sampleRate, a = analysisResult, post = posteriors, decoded = notes, bpm]
    {
        const auto structure = analysis::analyseStructure (*src, rate, a, *post);
        std::vector<SampleItem> made;

        int loopIndex = 1;
        for (auto& loop : loops::generateSectionLoops (*src, rate, a, structure, bpm))
        {
            SampleItem item;
            item.kind = ClipKind::loop;
            item.key = "L" + juce::String (loopIndex++);
            item.name = loop.name;
            item.bars = loop.bars;
            item.sourceSeconds = loop.sourceSeconds;
            item.original = std::make_shared<const juce::AudioBuffer<float>> (std::move (loop.audio));
            item.edited = item.original;
            item.originalPeaks = audio::computePeaks (*item.original, loopPeaks);
            item.peaks = item.originalPeaks;
            made.push_back (std::move (item));
        }

        int shotIndex = 1;
        for (auto& shot : oneshots::extract (*src, rate, *decoded))
        {
            SampleItem item;
            item.kind = ClipKind::shot;
            item.key = "S" + juce::String (shotIndex++);
            item.name = shot.name;
            item.category = shot.category;
            item.sourceSeconds = shot.sourceSeconds;
            item.original = std::make_shared<const juce::AudioBuffer<float>> (std::move (shot.audio));
            item.edited = item.original;
            item.originalPeaks = audio::computePeaks (*item.original, shotPeaks);
            item.peaks = item.originalPeaks;
            made.push_back (std::move (item));
        }

        onMessageThread (weak, [gen, made, bpm] (Engine& e)
        {
            if (gen != e.generation)
                return;

            e.items = made;
            for (auto& item : e.items)
                item.id = e.nextId++;

            e.generatedBpm = bpm;
            e.phase = Phase::ready;
            e.statusDetail.clear();

            e.selectedId = -1;
            for (const auto& item : e.items)
                if (item.key == e.pendingSelectedKey)
                    e.selectedId = item.id;
            if (e.selectedId < 0 && ! e.items.empty())
                e.selectedId = e.items.front().id;
            e.player.setSelected (e.selectedId);
            e.pendingSelectedKey.clear();

            // re-apply editor settings saved with the project / before regeneration
            const auto edits = e.pendingEdits;
            e.pendingEdits.clear();
            for (auto& item : e.items)
                if (const auto it = edits.find (item.key); it != edits.end())
                {
                    item.edit = it->second;
                    e.renderEditFor (item.id);
                }

            e.publishBank();
            e.storeState();
            e.sendChangeMessage();
        });
    });
}

// ---------------------------------------------------------------------------- selection & edits
void Engine::select (int id)
{
    if (find (id) == nullptr || id == selectedId)
        return;
    selectedId = id;
    player.setSelected (id);
    storeState();
    sendChangeMessage();
}

void Engine::setEdit (int id, const EditSettings& settings)
{
    for (auto& item : items)
    {
        if (item.id != id || item.edit == settings)
            continue;
        item.edit = settings;
        renderEditFor (id);
        storeState();
        sendChangeMessage();
        return;
    }
}

void Engine::renderEditFor (int id)
{
    const auto* item = find (id);
    if (item == nullptr)
        return;

    const auto token = ++editTokens[id];
    juce::WeakReference<Engine> weak (this);

    if (item->edit.isDefault())
    {
        for (auto& i : items)
            if (i.id == id)
            {
                i.edited = i.original;
                i.peaks = i.originalPeaks;
            }
        publishBank();
        return;
    }

    // edits are quick; their own queue keeps them from waiting behind an analysis
    editJobs.add ([weak, id, token, gen = generation, original = item->original, edit = item->edit,
                  rate = sampleRate, peaks = (int) item->originalPeaks.size()]
    {
        auto rendered = std::make_shared<const juce::AudioBuffer<float>> (renderEdit (*original, edit, rate));
        auto renderedPeaks = audio::computePeaks (*rendered, peaks);

        onMessageThread (weak, [id, token, gen, rendered, renderedPeaks] (Engine& e)
        {
            if (gen != e.generation || e.editTokens[id] != token)
                return;
            for (auto& i : e.items)
                if (i.id == id)
                {
                    i.edited = rendered;
                    i.peaks = renderedPeaks;
                }
            e.publishBank();
            e.sendChangeMessage();
        });
    });
}

// ---------------------------------------------------------------------------- audio thread handoff
void Engine::publishBank()
{
    auto bank = std::make_unique<ClipBank>();
    for (const auto& item : items)
    {
        if (item.edited == nullptr)
            continue;
        ClipEntry entry;
        entry.id = item.id;
        entry.audio = item.edited;
        entry.isLoop = item.kind == ClipKind::loop;
        entry.beats = entry.isLoop && generatedBpm > 0.0 ? item.edited->getNumSamples() / sampleRate * generatedBpm / 60.0 : 0.0;
        bank->entries.push_back (std::move (entry));
    }
    std::sort (bank->entries.begin(), bank->entries.end(), [] (const ClipEntry& a, const ClipEntry& b) { return a.id < b.id; });
    player.setBank (std::move (bank));
}

// ---------------------------------------------------------------------------- state
void Engine::storeState()
{
    juce::ValueTree state ("Engine");
    state.setProperty ("sampleBpm", sampleBpmOverride, nullptr);

    juce::String selected = pendingSelectedKey;
    if (const auto* item = find (selectedId))
        selected = item->key;
    state.setProperty ("selected", selected, nullptr);

    auto edits = pendingEdits;
    for (const auto& item : items)
        if (! item.edit.isDefault())
            edits[item.key] = item.edit;
    for (const auto& [key, edit] : edits)
        state.appendChild (edit.toValueTree (key), nullptr);

    const juce::ScopedLock sl (stateLock);
    savedState = state;
}

juce::ValueTree Engine::getState() const
{
    const juce::ScopedLock sl (stateLock);
    return savedState.createCopy();
}

void Engine::setState (const juce::ValueTree& state, bool sampleWillReload)
{
    if (! state.hasType ("Engine"))
        return;

    const auto copy = state.createCopy();
    onMessageThread (this, [copy, sampleWillReload] (Engine& e)
    {
        e.sampleBpmOverride = (double) copy.getProperty ("sampleBpm", 0.0);
        e.pendingSelectedKey = copy["selected"].toString();
        e.pendingEdits.clear();
        for (const auto& child : copy)
            if (child.hasType ("Edit"))
                e.pendingEdits[child["key"].toString()] = EditSettings::fromValueTree (child);

        e.storeState();

        if (! sampleWillReload && e.hasAnalysis)
        {
            e.items.clear();   // regenerate so the restored edits are applied to fresh results
            e.applyOverrideTempo();
            e.startGeneration();
        }
    });
}

} // namespace digga
