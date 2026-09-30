#pragma once

#include "core/JobQueue.h"
#include "core/SampleStore.h"
#include "playback/SamplePlayer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace digga
{

namespace ParamIDs
{
    inline constexpr auto reverb       = "reverb";
    inline constexpr auto delay        = "delay";
    inline constexpr auto distortion   = "distortion";
    inline constexpr auto filter       = "filter";
    inline constexpr auto pitch        = "pitch";
    inline constexpr auto mix          = "mix";
    inline constexpr auto reverse      = "reverse";
    inline constexpr auto killStrength = "killStrength";
}

class DiggaKillaProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    DiggaKillaProcessor();
    ~DiggaKillaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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

    /** Tempo reported by the host, or 0 if the host doesn't provide one. */
    double getHostBpm() const noexcept { return hostBpm.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void timerCallback() override;

    juce::AudioProcessorValueTreeState parameters;
    SamplePlayer player;
    JobQueue jobs;
    SampleStore sampleStore;
    std::atomic<double> hostBpm { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiggaKillaProcessor)
};

} // namespace digga
