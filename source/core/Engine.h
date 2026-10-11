#pragma once

#include "analysis/Structure.h"
#include "core/Edit.h"
#include "core/JobQueue.h"
#include "playback/ClipPlayer.h"

#include <juce_events/juce_events.h>

#include <map>

namespace digga
{

enum class ClipKind { loop, shot };

/** One result: a loop cut from a song section, or a one-shot. */
struct SampleItem
{
    int id = 0;
    ClipKind kind = ClipKind::loop;
    juce::String key;              // stable across sessions: "L3", "S5"
    juce::String name;             // "Drop", "Kick In", "808 F#1", "Note C#5"
    juce::String category;         // shots: "Kick", "Snare", ...
    int bars = 0;                  // loops
    double sourceSeconds = 0.0;    // where in the song it came from

    AudioPtr original;             // as generated
    AudioPtr edited;               // after the editor's settings (== original when untouched)
    std::vector<float> originalPeaks, peaks;
    EditSettings edit;

    juce::String detail (double sampleRate) const;   // "16 bars · from 0:42"
    juce::String fileTag() const;                    // "Drop_16bars", "808_F#1"
    double lengthSeconds (double sampleRate) const;
};

/** Owns the analysis and the results, and runs analysis, generation and
    edit rendering on the JobQueue. Message-thread only except where noted;
    listeners hear about changes through ChangeBroadcaster. */
class Engine : public juce::ChangeBroadcaster
{
public:
    enum class Phase { empty, analysing, generating, ready };

    Engine (JobQueue& jobs, ClipPlayer& player);
    ~Engine() override;

    // ---- any thread
    void sourceReady (AudioPtr audio, double sampleRate);
    juce::ValueTree getState() const;
    void setState (const juce::ValueTree& state, bool sampleWillReload);

    // ---- message thread
    void newSampleChosen();
    void setHostBpm (double bpm);
    void setSampleBpm (double bpm);
    void select (int id);
    void setEdit (int id, const EditSettings& settings);

    int getSelected() const noexcept { return selectedId; }
    const std::vector<SampleItem>& getItems() const noexcept { return items; }
    const SampleItem* find (int id) const;
    Phase getPhase() const noexcept { return phase; }
    const analysis::Analysis* getAnalysis() const noexcept { return hasAnalysis ? &analysisResult : nullptr; }
    double getProjectBpm() const noexcept;
    double getSampleRate() const noexcept { return sampleRate; }
    juce::String getStatusDetail() const { return statusDetail; }

private:
    void startAnalysis();
    void startGeneration();
    void renderEditFor (int id);
    void applyOverrideTempo();
    void publishBank();
    void storeState();

    JobQueue& jobs;
    JobQueue editJobs;
    ClipPlayer& player;

    AudioPtr source, monoSource;
    double sampleRate = 44100.0;
    analysis::Analysis analysisResult;
    std::shared_ptr<const midi::Posteriors> posteriors;
    std::shared_ptr<const std::vector<midi::Note>> notes;
    bool hasAnalysis = false;
    double sampleBpmOverride = 0.0;
    double hostBpm = 0.0;
    double generatedBpm = 0.0;

    std::vector<SampleItem> items;
    std::map<juce::String, EditSettings> pendingEdits;   // restored edits waiting for their sample
    juce::String pendingSelectedKey;
    std::map<int, juce::uint32> editTokens;
    int selectedId = -1, nextId = 1;
    Phase phase = Phase::empty;
    juce::String statusDetail;
    juce::uint32 generation = 0;

    mutable juce::CriticalSection stateLock;
    juce::ValueTree savedState { "Engine" };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Engine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};

} // namespace digga
