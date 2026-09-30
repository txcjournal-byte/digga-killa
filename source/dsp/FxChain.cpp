#include "dsp/FxChain.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

namespace digga::dsp
{

FxChain::FxChain() = default;
FxChain::~FxChain() = default;

bool FxChain::isNeutral (const FxParams& p) noexcept
{
    return p.reverb < 0.001f && p.delay < 0.001f && p.distortion < 0.001f
        && std::abs (p.filter) < 0.02f && std::abs (p.pitch) < 0.5f;
}

void FxChain::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    maxBlock = juce::jmax (1, maxBlockSize);
    channels = juce::jmax (1, numChannels);

    pitcher = std::make_unique<signalsmith::stretch::SignalsmithStretch<float, void>> (0x5eed);
    pitcher->presetCheaper (channels, (float) sampleRate);
    pitchOut.setSize (channels, maxBlock);
    dry.setSize (channels, maxBlock);

    filter.prepare ({ sampleRate, (juce::uint32) maxBlock, (juce::uint32) channels });
    filter.setResonance (0.9f);
    cutoff.reset (sampleRate, 0.05);
    cutoff.setCurrentAndTargetValue (1000.0f);

    delayLine.setSize (channels, (int) (sampleRate * 2.5) + 1);
    delayTime.reset (sampleRate, 0.2);

    reverb.prepare ({ sampleRate, (juce::uint32) maxBlock, (juce::uint32) channels });

    for (auto* s : { &drive, &mixAmount, &delayAmount })
        s->reset (sampleRate, 0.03);

    reset();
}

void FxChain::reset()
{
    if (pitcher != nullptr)
        pitcher->reset();
    pitchActive = false;
    filter.reset();
    delayLine.clear();
    delayWrite = 0;
    delayDamp[0] = delayDamp[1] = 0.0f;
    reverb.reset();
}

int FxChain::getPitchLatency() const noexcept
{
    return pitcher != nullptr && pitchActive ? pitcher->inputLatency() + pitcher->outputLatency() : 0;
}

void FxChain::processPitch (juce::AudioBuffer<float>& buffer, int numSamples, float semitones) noexcept
{
    const bool wanted = std::abs (semitones) >= 0.5f;
    if (! wanted)
    {
        pitchActive = false;
        return;
    }

    if (! pitchActive)
    {
        pitcher->reset();
        pitchActive = true;
    }

    pitcher->setTransposeSemitones (semitones);

    const float* in[8] {};
    float* out[8] {};
    const int n = juce::jmin (channels, buffer.getNumChannels(), 8);
    for (int ch = 0; ch < n; ++ch)
    {
        in[ch] = buffer.getReadPointer (ch);
        out[ch] = pitchOut.getWritePointer (ch);
    }
    pitcher->process (in, numSamples, out, numSamples);
    for (int ch = 0; ch < n; ++ch)
        buffer.copyFrom (ch, 0, pitchOut, ch, 0, numSamples);
}

void FxChain::process (juce::AudioBuffer<float>& buffer, int numSamples, const FxParams& p) noexcept
{
    numSamples = juce::jmin (numSamples, maxBlock);
    const int n = juce::jmin (channels, buffer.getNumChannels());
    if (numSamples <= 0 || n <= 0 || pitcher == nullptr)
        return;

    processPitch (buffer, numSamples, p.pitch);

    const bool wetChain = p.reverb > 0.001f || p.delay > 0.001f || p.distortion > 0.001f || std::abs (p.filter) > 0.02f
                       || delayAmount.isSmoothing() || drive.isSmoothing();
    drive.setTargetValue (p.distortion);
    mixAmount.setTargetValue (p.mix);
    delayAmount.setTargetValue (p.delay);

    if (! wetChain)
    {
        // nothing in the wet path: keep reverb / delay tails ringing out
        drive.skip (numSamples);
        mixAmount.skip (numSamples);
        delayAmount.skip (numSamples);
    }

    for (int ch = 0; ch < n; ++ch)
        dry.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // ---- distortion
    if (drive.getTargetValue() > 0.001f || drive.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float amount = drive.getNextValue();
            const float gainIn = 1.0f + amount * 30.0f;
            const float gainOut = 1.0f / (1.0f + amount * 1.6f);
            const float blend = juce::jmin (1.0f, amount * 4.0f);
            for (int ch = 0; ch < n; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                const float x = d[i];
                d[i] = x + (std::tanh (x * gainIn) * gainOut - x) * blend;
            }
        }
    }

    // ---- filter: one knob, left = low-pass, right = high-pass
    const int mode = p.filter < -0.02f ? -1 : p.filter > 0.02f ? 1 : 0;
    if (mode != 0)
    {
        if (mode != filterMode)
        {
            filter.setType (mode < 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                                     : juce::dsp::StateVariableTPTFilterType::highpass);
            filter.reset();
            cutoff.setCurrentAndTargetValue (mode < 0 ? 20000.0f : 20.0f);
        }
        const float amount = std::abs (p.filter);
        cutoff.setTargetValue (mode < 0 ? 20000.0f * std::pow (180.0f / 20000.0f, amount)
                                        : 20.0f * std::pow (6000.0f / 20.0f, amount));

        for (int i = 0; i < numSamples; ++i)
        {
            if ((i & 15) == 0)
                filter.setCutoffFrequency (juce::jmin (cutoff.getNextValue(), (float) sampleRate * 0.45f));
            else
                cutoff.getNextValue();
            for (int ch = 0; ch < n; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                d[i] = filter.processSample (ch, d[i]);
            }
        }
    }
    filterMode = mode;

    // ---- tempo-synced delay (dotted eighth), damped feedback
    const int delayLength = delayLine.getNumSamples();
    if (p.delay > 0.001f || delayAmount.isSmoothing() || delayAmount.getCurrentValue() > 0.001f)
    {
        const double bpm = p.bpm > 20.0 ? p.bpm : 120.0;
        delayTime.setTargetValue ((float) juce::jmin ((double) delayLength - 2.0, 0.75 * 60.0 / bpm * sampleRate));

        for (int i = 0; i < numSamples; ++i)
        {
            const float amount = delayAmount.getNextValue();
            const float time = delayTime.getNextValue();
            const float feedback = 0.2f + 0.5f * amount;
            const float wet = 0.55f * amount;

            float readPos = (float) delayWrite - time;
            if (readPos < 0.0f)
                readPos += (float) delayLength;
            const int r0 = (int) readPos;
            const int r1 = (r0 + 1) % delayLength;
            const float frac = readPos - (float) r0;

            for (int ch = 0; ch < n; ++ch)
            {
                auto* line = delayLine.getWritePointer (ch);
                auto* d = buffer.getWritePointer (ch);
                const float delayed = line[r0] + (line[r1] - line[r0]) * frac;
                auto& damp = delayDamp[juce::jmin (ch, 1)];
                damp += 0.35f * (delayed - damp);                     // darker repeats
                line[delayWrite] = d[i] + damp * feedback;
                d[i] += delayed * wet;
            }
            delayWrite = (delayWrite + 1) % delayLength;
        }
    }

    // ---- reverb
    if (p.reverb > 0.001f)
    {
        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = 0.55f + 0.4f * p.reverb;
        rp.damping = 0.45f;
        rp.wetLevel = 0.5f * p.reverb;
        rp.dryLevel = 1.0f - 0.25f * p.reverb;
        rp.width = 1.0f;
        reverb.setParameters (rp);

        auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, (size_t) n)
                                                           .getSubBlock (0, (size_t) numSamples);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // ---- mix
    if (mixAmount.getTargetValue() < 0.999f || mixAmount.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = mixAmount.getNextValue();
            for (int ch = 0; ch < n; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                d[i] = dry.getSample (ch, i) + (d[i] - dry.getSample (ch, i)) * m;
            }
        }
    }
}

} // namespace digga::dsp
