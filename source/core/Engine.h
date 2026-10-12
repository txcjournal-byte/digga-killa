#pragma once

#include "analysis/Structure.h"
#include "core/Edit.h"
#include "core/JobQueue.h"
#include "loops/LoopGenerator.h"
#include "oneshots/OneShotExtractor.h"
#include "playback/ClipPlayer.h"
#include "separation/Stems.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
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
    juce::String group;            // browser tab: "Loops", "Drums", "Bass", "Melody", "Vocals"
    std::shared_ptr<const loops::LoopPlan> plan;            // loops: where it comes from
    std::array<float, stems::numStems> baseMix { 1.0f, 1.0f, 1.0f, 1.0f };   // loops: stems in `original`
    int bars = 0;                  // loops
    double sourceSeconds = 0.0;    // where in the song it came from

    AudioPtr original;             // as generated
    AudioPtr edited;               // after the editor's settings (== original when untouched)
    AudioPtr dry;                  // the same edit without the effect rack (== edited without FX)
    std::vector<float> originalPeaks, peaks;
    EditSettings edit;

    bool canMixStems() const noexcept { return plan != nullptr; }
    std::array<float, stems::numStems> stemMix() const noexcept { return edit.hasStemMix() ? edit.stems : baseMix; }

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
    float getProgress() const noexcept { return progress; }

    /** The separated song (null without the stem model or before analysis). */
    std::shared_ptr<const stems::SongStems> getStems() const noexcept { return songStems; }
    const std::vector<float>& getOverview() const noexcept { return overview; }
    const std::vector<analysis::Section>& getSections() const noexcept { return sections; }
    double getSourceSeconds() const noexcept { return source != nullptr ? source->getNumSamples() / sampleRate : 0.0; }
    AudioPtr getSource() const noexcept { return source; }
    bool hasStemModel() const;

    /** For audio-to-MIDI: builds a loop's pitched part only (melody + 808, no
        drums or vocals, or the vocals alone for a vocal-only mix) with the same
        crop, tempo and pitch as the clip. Safe to call on any thread once
        returned; empty when the clip has nothing to separate (one-shots,
        no stems), meaning: transcribe the clip itself. */
    std::function<AudioPtr()> pitchedSource (int id) const;

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
    std::shared_ptr<const stems::SongStems> songStems;
    std::shared_ptr<const std::vector<oneshots::Shot>> shots;
    std::vector<float> overview;
    std::vector<analysis::Section> sections;
    float progress = 0.0f;
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
    std::shared_ptr<std::atomic<juce::uint32>> latestGeneration = std::make_shared<std::atomic<juce::uint32>> (0u);   // read by workers

    mutable juce::CriticalSection stateLock;
    juce::ValueTree savedState { "Engine" };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Engine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};

} // namespace digga
