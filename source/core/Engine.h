#pragma once

#include "analysis/Analyzer.h"
#include "core/JobQueue.h"
#include "core/ResultTree.h"
#include "playback/ClipPlayer.h"

#include <juce_events/juce_events.h>

#include <array>
#include <optional>
#include <set>

namespace digga
{

/** Owns the analysis and the result tree, and runs analysis, loop/one-shot
    generation and KILL on the JobQueue. Everything except the members marked
    "any thread" is message-thread only; listeners hear about changes through
    ChangeBroadcaster. */
class Engine : public juce::ChangeBroadcaster
{
public:
    enum class Phase { empty, analysing, generating, ready };

    Engine (JobQueue& jobs, ClipPlayer& player);
    ~Engine() override;

    // ---- any thread
    /** A (re)decoded source at the host rate. */
    void sourceReady (AudioPtr audio, double sampleRate);
    /** Snapshot for the host's project file. */
    juce::ValueTree getState() const;
    /** Restores tempo override, KILL tree (by seeds), selection and folding.
        If the sample is about to be (re)loaded, the restore waits for it. */
    void setState (const juce::ValueTree& state, bool sampleWillReload);

    // ---- message thread
    void newSampleChosen();
    void setHostBpm (double bpm);
    void setSampleBpm (double bpm);
    void kill (int nodeId, float strength);
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    void undo();
    void setExpanded (int nodeId, bool expanded);
    void select (int nodeId);

    int getSelected() const noexcept { return selectedId; }
    const ResultTree& getTree() const noexcept { return tree; }
    Phase getPhase() const noexcept { return phase; }
    bool isBusy (int nodeId) const { return busy.count (nodeId) > 0; }
    const analysis::Analysis* getAnalysis() const noexcept { return hasAnalysis ? &analysisResult : nullptr; }
    double getProjectBpm() const noexcept;   // host tempo, or the sample's when there is no host
    double getHostBpm() const noexcept { return hostBpm; }
    double getSampleRate() const noexcept { return sampleRate; }

    static constexpr int variationsPerKill = 6;

private:
    struct Batch
    {
        juce::String parentKey;
        std::array<juce::uint32, variationsPerKill> seeds {};
        float strength = 0.5f;
    };

    struct Spec
    {
        std::vector<Batch> batches;
        juce::StringArray collapsed;
        juce::String selectedKey;
        bool authoritative = false;   // restored from a project: replaces the current tree
    };

    void startAnalysis();
    void startGeneration();
    void startKill (int nodeId, const std::array<juce::uint32, variationsPerKill>& seeds, float strength, bool replaying);
    void pumpReplay();
    void publishBank();
    void storeState();
    Spec specFromTree() const;
    void applyOverrideTempo();

    JobQueue& jobs;
    ClipPlayer& player;

    AudioPtr source, monoSource;
    double sampleRate = 44100.0;
    analysis::Analysis analysisResult;
    bool hasAnalysis = false;
    double sampleBpmOverride = 0.0;
    double hostBpm = 0.0;
    double generatedBpm = 0.0;

    ResultTree tree;
    std::vector<ResultTree> undoStack;
    std::set<int> busy;
    std::vector<Batch> replay;
    std::optional<Spec> pendingSpec;
    int selectedId = -1;
    Phase phase = Phase::empty;
    juce::uint32 generation = 0;

    mutable juce::CriticalSection stateLock;
    juce::ValueTree savedState { "Engine" };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Engine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};

} // namespace digga
