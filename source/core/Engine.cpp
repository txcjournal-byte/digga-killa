#include "core/Engine.h"

#include "kill/KillEngine.h"
#include "loops/LoopGenerator.h"
#include "oneshots/OneShotExtractor.h"

#include <thread>

namespace digga
{

namespace
{
    constexpr int loopPeaks = 180, shotPeaks = 70;
    constexpr size_t maxUndo = 30;

    template <typename Fn>
    void onMessageThread (juce::WeakReference<Engine> weak, Fn&& fn)
    {
        juce::MessageManager::callAsync ([weak, fn = std::forward<Fn> (fn)]() mutable
        {
            if (auto* engine = weak.get())
                fn (*engine);
        });
    }

    juce::String seedsToString (const std::array<juce::uint32, Engine::variationsPerKill>& seeds)
    {
        juce::StringArray parts;
        for (auto s : seeds)
            parts.add (juce::String ((juce::int64) s));
        return parts.joinIntoString (",");
    }

    std::array<juce::uint32, Engine::variationsPerKill> seedsFromString (const juce::String& text)
    {
        std::array<juce::uint32, Engine::variationsPerKill> seeds {};
        const auto parts = juce::StringArray::fromTokens (text, ",", {});
        for (int i = 0; i < juce::jmin ((int) seeds.size(), parts.size()); ++i)
            seeds[(size_t) i] = (juce::uint32) parts[i].getLargeIntValue();
        return seeds;
    }
}

Engine::Engine (JobQueue& j, ClipPlayer& p) : jobs (j), player (p)
{
    // create the weak-reference master up front so worker threads can copy it
    juce::WeakReference<Engine> warmUp (this);
}

Engine::~Engine() = default;

double Engine::getProjectBpm() const noexcept
{
    if (hostBpm > 0.0)
        return hostBpm;
    return hasAnalysis ? analysisResult.bpm : 120.0;
}

// ---------------------------------------------------------------------------- source & analysis
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
    source = nullptr;
    monoSource = nullptr;
    hasAnalysis = false;
    sampleBpmOverride = 0.0;
    tree.clear();
    undoStack.clear();
    busy.clear();
    replay.clear();
    pendingSpec.reset();
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

    const auto gen = ++generation;
    phase = Phase::analysing;
    busy.clear();
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

        onMessageThread (weak, [gen, result, mono] (Engine& e)
        {
            if (gen != e.generation)
                return;
            e.analysisResult = result;
            e.monoSource = mono;
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

// ---------------------------------------------------------------------------- generation
void Engine::startGeneration()
{
    if (! hasAnalysis || source == nullptr)
        return;

    auto spec = pendingSpec.has_value() && pendingSpec->authoritative ? *pendingSpec : specFromTree();
    spec.authoritative = true;
    pendingSpec = spec;   // kept until the new results arrive (state saves stay complete)

    const auto gen = ++generation;
    phase = Phase::generating;
    busy.clear();
    replay.clear();
    sendChangeMessage();

    const double bpm = getProjectBpm();
    juce::WeakReference<Engine> weak (this);

    jobs.add ([weak, gen, src = source, rate = sampleRate, a = analysisResult, bpm]
    {
        std::vector<ResultNode> nodes;

        for (auto& loop : loops::generate (*src, rate, a, bpm))
        {
            ResultNode n;
            n.kind = ClipKind::loop;
            n.slot = loop.slot;
            n.bars = loop.bars;
            auto buffer = std::make_shared<const juce::AudioBuffer<float>> (std::move (loop.audio));
            n.peaks = audio::computePeaks (*buffer, loopPeaks);
            n.audio = std::move (buffer);
            nodes.push_back (std::move (n));
        }

        int index = 1;
        for (auto& shot : oneshots::extract (*src, rate, 8))
        {
            ResultNode n;
            n.kind = ClipKind::shot;
            n.slot = juce::String (index++);
            auto buffer = std::make_shared<const juce::AudioBuffer<float>> (std::move (shot));
            n.peaks = audio::computePeaks (*buffer, shotPeaks);
            n.audio = std::move (buffer);
            nodes.push_back (std::move (n));
        }

        onMessageThread (weak, [gen, nodes, bpm] (Engine& e)
        {
            if (gen != e.generation)
                return;

            const auto restored = e.pendingSpec.value_or (Spec {});
            e.pendingSpec.reset();

            e.tree.clear();
            e.undoStack.clear();
            for (const auto& n : nodes)
                e.tree.add (n);

            e.generatedBpm = bpm;
            e.phase = Phase::ready;

            e.selectedId = -1;
            if (const auto* selected = e.tree.findByKey (restored.selectedKey))
                e.selectedId = selected->id;
            if (e.selectedId < 0)
                for (const auto& n : e.tree.all())
                    if (n.isRoot()) { e.selectedId = n.id; break; }
            e.player.setSelected (e.selectedId);

            e.replay = restored.batches;
            e.pendingSpec = Spec { {}, restored.collapsed, restored.selectedKey, false };   // folding / selection for replayed nodes

            e.publishBank();
            e.storeState();
            e.sendChangeMessage();
            e.pumpReplay();
        });
    });
}

// ---------------------------------------------------------------------------- KILL
void Engine::kill (int nodeId, float strength)
{
    if (phase != Phase::ready || isBusy (nodeId))
        return;

    std::array<juce::uint32, variationsPerKill> seeds {};
    auto& random = juce::Random::getSystemRandom();
    for (auto& s : seeds)
        s = (juce::uint32) random.nextInt();

    startKill (nodeId, seeds, juce::jlimit (0.0f, 1.0f, strength), false);
}

void Engine::startKill (int nodeId, const std::array<juce::uint32, variationsPerKill>& seeds, float strength, bool replaying)
{
    const auto* node = tree.find (nodeId);
    if (node == nullptr || node->audio == nullptr || ! hasAnalysis)
    {
        if (replaying)
            pumpReplay();
        return;
    }

    busy.insert (nodeId);
    sendChangeMessage();

    kill::Context context;
    context.isLoop = node->kind == ClipKind::loop;
    context.sampleRate = sampleRate;
    context.bpm = generatedBpm;
    context.keyRoot = analysisResult.keyRoot;
    context.minor = analysisResult.minor;

    juce::WeakReference<Engine> weak (this);
    jobs.add ([weak, gen = generation, nodeId, parent = node->audio, context, seeds, strength, replaying]
    {
        std::array<AudioPtr, variationsPerKill> results;
        std::array<std::vector<float>, variationsPerKill> peaks;
        {
            std::vector<std::thread> workers;
            for (size_t i = 0; i < seeds.size(); ++i)
                workers.emplace_back ([&, i]
                {
                    auto buffer = std::make_shared<const juce::AudioBuffer<float>> (
                        kill::makeVariation (*parent, context, strength, seeds[i]));
                    peaks[i] = audio::computePeaks (*buffer, context.isLoop ? loopPeaks : shotPeaks);
                    results[i] = std::move (buffer);
                });
            for (auto& w : workers)
                w.join();
        }

        onMessageThread (weak, [gen, nodeId, results, peaks, seeds, strength, replaying] (Engine& e)
        {
            e.busy.erase (nodeId);
            if (gen != e.generation)
                return;

            const auto* parentNode = e.tree.find (nodeId);
            if (parentNode == nullptr)
            {
                e.sendChangeMessage();
                if (replaying)
                    e.pumpReplay();
                return;
            }

            if (! replaying)
            {
                e.undoStack.push_back (e.tree);
                if (e.undoStack.size() > maxUndo)
                    e.undoStack.erase (e.undoStack.begin());
            }

            const auto kind = parentNode->kind;
            const auto slot = parentNode->slot;
            const auto bars = parentNode->bars;
            const auto path = parentNode->path;

            e.tree.removeDescendants (nodeId);
            for (size_t i = 0; i < results.size(); ++i)
            {
                ResultNode child;
                child.parentId = nodeId;
                child.kind = kind;
                child.slot = slot;
                child.bars = bars;
                child.path = path;
                child.path.push_back ((int) i + 1);
                child.seed = seeds[i];
                child.strength = strength;
                child.audio = results[i];
                child.peaks = peaks[i];
                e.tree.add (std::move (child));
            }

            if (auto* p = e.tree.find (nodeId))
                p->expanded = true;

            if (replaying && e.pendingSpec.has_value())
            {
                for (const auto& key : e.pendingSpec->collapsed)
                    if (const auto* n = e.tree.findByKey (key))
                        e.tree.find (n->id)->expanded = false;
                if (const auto* n = e.tree.findByKey (e.pendingSpec->selectedKey))
                {
                    e.selectedId = n->id;
                    e.player.setSelected (n->id);
                }
            }

            if (e.tree.find (e.selectedId) == nullptr)
                e.select (nodeId);

            e.publishBank();
            e.storeState();
            e.sendChangeMessage();

            if (replaying)
                e.pumpReplay();
        });
    });
}

void Engine::pumpReplay()
{
    while (! replay.empty())
    {
        const auto batch = replay.front();
        replay.erase (replay.begin());
        if (const auto* node = tree.findByKey (batch.parentKey))
        {
            startKill (node->id, batch.seeds, batch.strength, true);
            return;
        }
    }

    // replay finished
    if (pendingSpec.has_value() && ! pendingSpec->authoritative)
    {
        pendingSpec.reset();
        storeState();
    }
}

void Engine::undo()
{
    if (undoStack.empty())
        return;

    tree = undoStack.back();
    undoStack.pop_back();

    if (tree.find (selectedId) == nullptr)
    {
        selectedId = -1;
        for (const auto& n : tree.all())
            if (n.isRoot()) { selectedId = n.id; break; }
        player.setSelected (selectedId);
    }

    publishBank();
    storeState();
    sendChangeMessage();
}

void Engine::setExpanded (int nodeId, bool expanded)
{
    if (auto* node = tree.find (nodeId))
    {
        node->expanded = expanded;
        storeState();
        sendChangeMessage();
    }
}

void Engine::select (int nodeId)
{
    if (tree.find (nodeId) == nullptr)
        return;
    selectedId = nodeId;
    player.setSelected (nodeId);
    storeState();
    sendChangeMessage();
}

// ---------------------------------------------------------------------------- audio thread handoff
void Engine::publishBank()
{
    auto bank = std::make_unique<ClipBank>();
    for (const auto& n : tree.all())
    {
        if (n.audio == nullptr)
            continue;
        ClipEntry entry;
        entry.id = n.id;
        entry.audio = n.audio;
        entry.isLoop = n.kind == ClipKind::loop;
        entry.beats = entry.isLoop ? n.bars * 4.0 : 0.0;
        bank->entries.push_back (std::move (entry));
    }
    std::sort (bank->entries.begin(), bank->entries.end(), [] (const ClipEntry& a, const ClipEntry& b) { return a.id < b.id; });
    player.setBank (std::move (bank));
}

// ---------------------------------------------------------------------------- state
Engine::Spec Engine::specFromTree() const
{
    Spec spec;
    for (const auto& n : tree.all())
    {
        auto children = tree.childrenOf (n.id);
        if (children.empty())
            continue;

        Batch batch;
        batch.parentKey = n.key();
        for (int childId : children)
        {
            const auto* child = tree.find (childId);
            const int index = child->path.empty() ? 0 : child->path.back() - 1;
            if (juce::isPositiveAndBelow (index, variationsPerKill))
                batch.seeds[(size_t) index] = child->seed;
            batch.strength = child->strength;
        }
        spec.batches.push_back (batch);

        if (! n.expanded)
            spec.collapsed.add (n.key());
    }

    if (const auto* selected = tree.find (selectedId))
        spec.selectedKey = selected->key();

    // batches that are still waiting to be replayed
    for (const auto& b : replay)
        spec.batches.push_back (b);

    return spec;
}

void Engine::storeState()
{
    auto spec = specFromTree();
    if (pendingSpec.has_value() && pendingSpec->authoritative)
        spec = *pendingSpec;

    juce::ValueTree state ("Engine");
    state.setProperty ("sampleBpm", sampleBpmOverride, nullptr);
    state.setProperty ("selected", spec.selectedKey, nullptr);
    state.setProperty ("collapsed", spec.collapsed.joinIntoString ("|"), nullptr);

    for (const auto& b : spec.batches)
    {
        juce::ValueTree batch ("Kill");
        batch.setProperty ("parent", b.parentKey, nullptr);
        batch.setProperty ("seeds", seedsToString (b.seeds), nullptr);
        batch.setProperty ("strength", b.strength, nullptr);
        state.appendChild (batch, nullptr);
    }

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
        Spec spec;
        spec.selectedKey = copy["selected"].toString();
        spec.collapsed = juce::StringArray::fromTokens (copy["collapsed"].toString(), "|", {});
        spec.collapsed.removeEmptyStrings();

        for (const auto& child : copy)
        {
            if (! child.hasType ("Kill"))
                continue;
            Batch b;
            b.parentKey = child["parent"].toString();
            b.seeds = seedsFromString (child["seeds"].toString());
            b.strength = (float) (double) child.getProperty ("strength", 0.5);
            spec.batches.push_back (b);
        }

        e.sampleBpmOverride = (double) copy.getProperty ("sampleBpm", 0.0);
        spec.authoritative = true;
        e.pendingSpec = spec;
        e.storeState();

        if (! sampleWillReload && e.hasAnalysis)
        {
            e.applyOverrideTempo();
            e.startGeneration();
        }
    });
}

} // namespace digga
