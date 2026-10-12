#include "core/Engine.h"

#include <mutex>

namespace digga
{

namespace
{
    constexpr int loopPeaks = 600, shotPeaks = 300, overviewPoints = 1600;

    /** The stem model is big: one copy shared by every instance, released
        when no analysis needs it. */
    std::shared_ptr<const stems::Separator> acquireSeparator()
    {
        static std::mutex mutex;
        static std::weak_ptr<const stems::Separator> cache;
        const std::lock_guard<std::mutex> lock (mutex);
        if (auto existing = cache.lock())
            return existing;
        const auto file = stems::Separator::findModelFile();
        if (! file.existsAsFile())
            return {};
        auto separator = std::make_shared<stems::Separator>();
        if (! separator->load (file))
            return {};
        cache = separator;
        return separator;
    }

    juce::AudioBuffer<float> monoSum (std::initializer_list<const juce::AudioBuffer<float>*> buffers, int length)
    {
        juce::AudioBuffer<float> out (1, length);
        out.clear();
        for (const auto* b : buffers)
            for (int ch = 0; ch < b->getNumChannels(); ++ch)
                out.addFrom (0, 0, *b, ch, 0, length, 1.0f / (float) b->getNumChannels());
        return out;
    }

    juce::String groupForShot (const juce::String& category)
    {
        if (category == "808") return "Bass";
        if (category == "Chord" || category == "Note") return "Melody";
        if (category == "Vocal") return "Vocals";
        return "Drums";
    }

    /** Loop audio for a stem mix, rendered from the separated song. */
    juce::AudioBuffer<float> renderStemLoop (const loops::LoopPlan& plan, const stems::SongStems& songStems,
                                             const std::array<float, stems::numStems>& mix,
                                             double rate, double sourceBpm, double projectBpm)
    {
        return loops::renderLoop (plan, [&songStems, mix] (int start, int length) { return songStems.mix (start, length, mix); },
                                  songStems.length, rate, sourceBpm, projectBpm);
    }

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
    const auto length = juce::String (lengthSeconds (rate), 2) + " s";
    if (category.isNotEmpty() && ! name.startsWith (category))
        return category + dot + length + dot + from;
    return length + dot + from;
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

bool Engine::hasStemModel() const
{
    return stems::Separator::findModelFile().existsAsFile();
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
    latestGeneration->store (++generation);
    source = monoSource = nullptr;
    posteriors = nullptr;
    notes = nullptr;
    songStems = nullptr;
    shots = nullptr;
    overview.clear();
    sections.clear();
    progress = 0.0f;
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
    latestGeneration->store (gen);
    phase = Phase::analysing;
    progress = 0.0f;
    statusDetail = "SPLITTING STEMS";
    sendChangeMessage();

    juce::WeakReference<Engine> weak (this);
    jobs.add ([weak, gen, src = source, rate = sampleRate, tempoOverride = sampleBpmOverride, latest = latestGeneration]
    {
        auto status = [weak, gen] (juce::String text, float value)
        {
            onMessageThread (weak, [gen, text, value] (Engine& e)
            {
                if (gen != e.generation || e.phase != Phase::analysing)
                    return;
                e.statusDetail = text;
                e.progress = value;
                e.sendChangeMessage();
            });
        };
        auto stillWanted = [latest, gen] { return latest->load() == gen; };   // a newer song cancels this one

        const int length = src->getNumSamples();
        const auto overviewPeaks = audio::computePeaks (*src, overviewPoints);

        // ---- 1. stems
        std::shared_ptr<stems::SongStems> separated;
        stems::StemBuffers stemAudio;
        if (auto separator = acquireSeparator())
        {
            const bool done = separator->separate (*src, rate, stemAudio, [&] (float p)
            {
                status ("SPLITTING STEMS " + juce::String (juce::roundToInt (p * 100.0f)) + "%", p * 0.6f);
                return stillWanted();
            });
            if (! stillWanted())
                return;

            if (done)
            {
                separated = std::make_shared<stems::SongStems>();
                separated->length = length;
                separated->channels = src->getNumChannels();
                float mixRms = 0.0f;
                for (int ch = 0; ch < src->getNumChannels(); ++ch)
                    mixRms = juce::jmax (mixRms, src->getRMSLevel (ch, 0, length));
                for (int st = 0; st < stems::numStems; ++st)
                {
                    const auto& b = stemAudio[(size_t) st];
                    separated->stems[(size_t) st].assign (b);
                    separated->overview[(size_t) st] = audio::computePeaks (b, overviewPoints);

                    // present: loud somewhere (1 s windows), not just leakage
                    const int window = (int) rate;
                    float loudest = 0.0f;
                    for (int i = 0; i + window <= length; i += window / 2)
                        for (int ch = 0; ch < b.getNumChannels(); ++ch)
                            loudest = juce::jmax (loudest, b.getRMSLevel (ch, i, window));
                    if (length < window)
                        for (int ch = 0; ch < b.getNumChannels(); ++ch)
                            loudest = juce::jmax (loudest, b.getRMSLevel (ch, 0, length));
                    separated->present[(size_t) st] = loudest > 0.25f * mixRms;
                }
            }
        }

        // ---- 2. tempo from the drums, key from what is pitched
        status ("FINDING TEMPO & KEY", separated != nullptr ? 0.62f : 0.1f);
        const bool useStems = separated != nullptr;
        const auto& present = useStems ? separated->present : std::array<bool, stems::numStems> {};

        auto mono = std::make_shared<const juce::AudioBuffer<float>> (
            useStems && present[stems::drums]
                ? monoSum ({ &stemAudio[stems::drums], &stemAudio[stems::bass] }, length)
                : audio::toMono (*src));
        auto result = analysis::analyse (mono->getReadPointer (0), mono->getNumSamples(), rate);

        if (useStems && (present[stems::other] || present[stems::bass]))
        {
            const auto pitched = monoSum ({ &stemAudio[stems::other], &stemAudio[stems::bass] }, length);
            const auto keyed = analysis::analyse (pitched.getReadPointer (0), length, rate);
            result.keyRoot = keyed.keyRoot;
            result.minor = keyed.minor;
            result.keyName = keyed.keyName;
        }

        if (tempoOverride > 0.0)
        {
            result.bpm = tempoOverride;
            result.downbeatSeconds = analysis::findDownbeat (*result.onsets, mono->getReadPointer (0),
                                                             mono->getNumSamples(), tempoOverride);
        }
        if (! stillWanted())
            return;

        // ---- 3. melody notes (Basic Pitch on the melody stem, or the mix)
        status ("READING THE MELODY", useStems ? 0.7f : 0.3f);
        const auto& melodySource = useStems ? stemAudio[stems::other] : *src;
        const auto modelRate = midi::toModelRate (melodySource, rate);
        auto post = std::make_shared<const midi::Posteriors> (midi::computePosteriors (modelRate.data(), (int) modelRate.size()));
        auto decoded = std::make_shared<const std::vector<midi::Note>> (midi::decodeNotes (*post));
        if (! stillWanted())
            return;

        // ---- 4. one-shots (do not depend on tempo)
        status ("CUTTING ONE-SHOTS", useStems ? 0.85f : 0.6f);
        std::vector<oneshots::Shot> found;
        if (useStems)
        {
            oneshots::StemSet set;
            set.drums = &stemAudio[stems::drums];   set.hasDrums = present[stems::drums];
            set.bass = &stemAudio[stems::bass];     set.hasBass = present[stems::bass];
            set.melody = &stemAudio[stems::other];  set.hasMelody = present[stems::other];
            set.vocals = &stemAudio[stems::vocals]; set.hasVocals = present[stems::vocals];
            found = oneshots::extractFromStems (set, rate, *decoded);
        }
        else
            found = oneshots::extract (*src, rate, *decoded);
        auto shotList = std::make_shared<const std::vector<oneshots::Shot>> (std::move (found));
        std::shared_ptr<const stems::SongStems> stemsResult = separated;

        onMessageThread (weak, [gen, result, mono, post, decoded, shotList, stemsResult, overviewPeaks] (Engine& e)
        {
            if (gen != e.generation)
                return;
            e.analysisResult = result;
            e.monoSource = mono;
            e.posteriors = post;
            e.notes = decoded;
            e.shots = shotList;
            e.songStems = stemsResult;
            e.overview = overviewPeaks;
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
    latestGeneration->store (gen);
    phase = Phase::generating;
    statusDetail = "CUTTING LOOPS & ONE-SHOTS";
    sendChangeMessage();

    const double bpm = getProjectBpm();
    juce::WeakReference<Engine> weak (this);

    jobs.add ([weak, gen, src = source, rate = sampleRate, a = analysisResult, post = posteriors,
               shotList = shots, songStems = songStems, bpm]
    {
        const auto structure = analysis::analyseStructure (*src, rate, a, *post, songStems.get());
        std::vector<SampleItem> made;

        auto addLoop = [&] (const loops::LoopPlan& plan, juce::AudioBuffer<float> audio, const juce::String& key,
                            const juce::String& name, const juce::String& group, const std::array<float, stems::numStems>& mix)
        {
            SampleItem item;
            item.kind = ClipKind::loop;
            item.key = key;
            item.name = name;
            item.group = group;
            item.bars = plan.bars;
            item.sourceSeconds = plan.sourceSeconds;
            item.plan = songStems != nullptr ? std::make_shared<const loops::LoopPlan> (plan) : nullptr;
            item.baseMix = mix;
            item.original = std::make_shared<const juce::AudioBuffer<float>> (std::move (audio));
            item.edited = item.original;
            item.dry = item.original;
            item.originalPeaks = audio::computePeaks (*item.original, loopPeaks);
            item.peaks = item.originalPeaks;
            made.push_back (std::move (item));
        };

        const auto plans = loops::planSectionLoops (*src, rate, a, structure);

        // ---- full-mix loops, one per section start
        int index = 1;
        for (const auto& plan : plans)
            addLoop (plan, loops::renderLoop (plan, *src, rate, a.bpm, bpm), "L" + juce::String (index++), plan.name,
                     "Loops", { 1.0f, 1.0f, 1.0f, 1.0f });

        // ---- stem loops: the sections where each stem is strongest
        if (songStems != nullptr && ! structure.bars.empty())
        {
            struct StemGroup { int stem; const char* group; const char* label; const char* key; };
            static constexpr StemGroup groups[] = { { stems::drums, "Drums", "Drums", "LD" },
                                                    { stems::bass, "Bass", "808", "LB" },
                                                    { stems::other, "Melody", "Melody", "LM" },
                                                    { stems::vocals, "Vocals", "Vox", "LV" } };
            for (const auto& g : groups)
            {
                if (! songStems->present[(size_t) g.stem])
                    continue;

                std::vector<std::pair<float, const loops::LoopPlan*>> ranked;
                for (const auto& plan : plans)
                {
                    float level = 0.0f;
                    for (int b : plan.sourceBars)
                        if (juce::isPositiveAndBelow (b, (int) structure.bars.size()))
                            level += structure.bars[(size_t) b].stems[(size_t) g.stem];
                    level /= (float) juce::jmax (1, (int) plan.sourceBars.size());
                    if (level > 0.35f)
                        ranked.push_back ({ level, &plan });
                }
                std::sort (ranked.begin(), ranked.end(), [] (const auto& x, const auto& y) { return x.first > y.first; });

                // up to three, different sections first
                std::vector<const loops::LoopPlan*> chosen;
                for (int pass = 0; pass < 2; ++pass)
                    for (const auto& [level, plan] : ranked)
                    {
                        if (chosen.size() >= 3)
                            break;
                        const bool taken = std::find (chosen.begin(), chosen.end(), plan) != chosen.end();
                        const bool sameName = std::any_of (chosen.begin(), chosen.end(), [&] (const loops::LoopPlan* c) { return c->name == plan->name; });
                        if (! taken && (pass == 1 || ! sameName))
                            chosen.push_back (plan);
                    }
                std::sort (chosen.begin(), chosen.end(), [] (const auto* x, const auto* y) { return x->startBar < y->startBar; });

                int n = 1;
                for (const auto* plan : chosen)
                {
                    std::array<float, stems::numStems> mix {};
                    mix[(size_t) g.stem] = 1.0f;
                    addLoop (*plan, renderStemLoop (*plan, *songStems, mix, rate, a.bpm, bpm),
                             g.key + juce::String (n++), plan->name + " " + g.label, g.group, mix);
                }
            }
        }

        // ---- one-shots
        int shotIndex = 1;
        if (shotList != nullptr)
            for (const auto& shot : *shotList)
            {
                SampleItem item;
                item.kind = ClipKind::shot;
                item.key = "S" + juce::String (shotIndex++);
                item.name = shot.name;
                item.category = shot.category;
                item.group = groupForShot (shot.category);
                item.sourceSeconds = shot.sourceSeconds;
                item.original = std::make_shared<const juce::AudioBuffer<float>> (shot.audio);
                item.edited = item.original;
                item.dry = item.original;
                item.originalPeaks = audio::computePeaks (*item.original, shotPeaks);
                item.peaks = item.originalPeaks;
                made.push_back (std::move (item));
            }

        const auto foundSections = structure.sections;
        onMessageThread (weak, [gen, made, bpm, foundSections] (Engine& e)
        {
            if (gen != e.generation)
                return;
            e.sections = foundSections;

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
                i.edited = i.dry = i.original;
                i.peaks = i.originalPeaks;
            }
        publishBank();
        return;
    }

    // edits are quick; their own queue keeps them from waiting behind an analysis
    editJobs.add ([weak, id, token, gen = generation, original = item->original, edit = item->edit,
                  rate = sampleRate, peaks = (int) item->originalPeaks.size(), plan = item->plan,
                  baseMix = item->baseMix, songStems = songStems, sourceBpm = analysisResult.bpm, bpm = generatedBpm,
                  isLoop = item->kind == ClipKind::loop]
    {
        // a changed stem mix re-renders the loop from the stems first
        auto base = original;
        if (edit.hasStemMix() && plan != nullptr && songStems != nullptr && edit.stems != baseMix)
            base = std::make_shared<const juce::AudioBuffer<float>> (renderStemLoop (*plan, *songStems, edit.stems, rate, sourceBpm, bpm));

        auto rendered = std::make_shared<const juce::AudioBuffer<float>> (renderEdit (*base, edit, rate, bpm, plan != nullptr || isLoop));
        auto dry = edit.fx.anyOn() ? std::make_shared<const juce::AudioBuffer<float>> (renderEdit (*base, edit, rate, bpm, plan != nullptr || isLoop, false))
                                   : rendered;
        auto renderedPeaks = audio::computePeaks (*rendered, peaks);

        onMessageThread (weak, [id, token, gen, rendered, dry, renderedPeaks] (Engine& e)
        {
            if (gen != e.generation || e.editTokens[id] != token)
                return;
            for (auto& i : e.items)
                if (i.id == id)
                {
                    i.edited = rendered;
                    i.dry = dry;
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
