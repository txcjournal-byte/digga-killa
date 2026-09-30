#pragma once

#include <juce_dsp/juce_dsp.h>

#include <memory>

namespace signalsmith::stretch { template <typename, class> struct SignalsmithStretch; }

namespace digga::dsp
{

struct FxParams
{
    float reverb = 0.0f;       // 0..1
    float delay = 0.0f;        // 0..1, tempo-synced dotted 1/8
    float distortion = 0.0f;   // 0..1
    float filter = 0.0f;       // -1 = low-pass .. 0 = off .. +1 = high-pass
    float pitch = 0.0f;        // semitones, -12..12
    float mix = 1.0f;          // dry/wet of the effect chain
    double bpm = 120.0;
};

/** PITCH -> [DISTORTION -> FILTER -> DELAY -> REVERB] with MIX as the
    dry/wet of the bracketed part. Used on the audio thread for playback
    and offline for drag-and-drop export, so both sound the same.
    prepare() allocates; process() never does. */
class FxChain
{
public:
    FxChain();
    ~FxChain();

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, int numSamples, const FxParams& params) noexcept;

    /** Delay the PITCH stage adds while it is active. */
    int getPitchLatency() const noexcept;

    static bool isNeutral (const FxParams& p) noexcept;

private:
    void processPitch (juce::AudioBuffer<float>& buffer, int numSamples, float semitones) noexcept;

    double sampleRate = 44100.0;
    int channels = 2, maxBlock = 512;

    std::unique_ptr<signalsmith::stretch::SignalsmithStretch<float, void>> pitcher;
    juce::AudioBuffer<float> pitchOut, dry;
    bool pitchActive = false;

    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoff;
    int filterMode = 0;   // -1 LP, 0 off, 1 HP

    juce::AudioBuffer<float> delayLine;
    int delayWrite = 0;
    float delayDamp[2] {};
    juce::SmoothedValue<float> delayTime;

    juce::dsp::Reverb reverb;
    juce::SmoothedValue<float> drive, mixAmount, delayAmount;
};

} // namespace digga::dsp
