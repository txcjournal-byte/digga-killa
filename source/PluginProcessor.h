#pragma once

#include "core/Engine.h"
#include "core/JobQueue.h"
#include "core/SampleStore.h"
#include "playback/ClipPlayer.h"
#include "playback/SamplePlayer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace digga
{

namespace ParamIDs
{
    inline constexpr auto volume = "volume";
}

/** TrapWin Sampler: drop a song, get its loops and one-shots. */
class TrapWinProcessor : public juce::AudioProcessor,
                         private juce::Timer
{
public:
    TrapWinProcessor();
    ~TrapWinProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    SampleStore& getSampleStore() noexcept { return sampleStore; }
    SamplePlayer& getPlayer() noexcept { return player; }
    ClipPlayer& getClipPlayer() noexcept { return clipPlayer; }
    Engine& getEngine() noexcept { return engine; }

    double getHostBpm() const noexcept { return hostBpm.load(); }

    /** Editor width the user last chose (0 = never resized); saved with the project. */
    int getEditorWidth() const noexcept { return editorWidth.load(); }
    void setEditorWidth (int width) noexcept { editorWidth.store (width); }

    /** Drag-to-DAW format: WAV (false) or MIDI (true); saved with the project. */
    bool isDragAsMidi() const noexcept { return dragAsMidi.load(); }
    void setDragAsMidi (bool midi) noexcept { dragAsMidi.store (midi); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* volumeParam = nullptr;
    juce::SmoothedValue<float> outputGain;

    SamplePlayer player;
    ClipPlayer clipPlayer;
    JobQueue jobs;
    Engine engine;
    SampleStore sampleStore;

    std::atomic<double> hostBpm { 0.0 };
    std::atomic<int> editorWidth { 0 };
    std::atomic<bool> dragAsMidi { false };
    double pendingHostBpm = 0.0;
    int hostBpmStableTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapWinProcessor)
};

} // namespace digga
