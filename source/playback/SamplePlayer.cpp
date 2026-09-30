#include "playback/SamplePlayer.h"

namespace digga
{

SamplePlayer::~SamplePlayer()
{
    collectGarbage();
    delete pending.exchange (nullptr);
    delete active;
}

void SamplePlayer::setSample (std::unique_ptr<PlaybackSample> sample)
{
    // If the audio thread has not picked up the previous one yet, it never
    // will, so it is safe to delete here.
    delete pending.exchange (sample.release());
}

void SamplePlayer::collectGarbage()
{
    PlaybackSample* item = nullptr;
    while (trash.pop (item))
        delete item;
}

void SamplePlayer::prepare (double sampleRate) noexcept
{
    currentRate = sampleRate;
    fadeLength = juce::jmax (16, (int) (sampleRate * 0.005));
    playing = false;
    playingFlag.store (false);
}

void SamplePlayer::trigger() noexcept
{
    if (active == nullptr || active->audio.getNumSamples() == 0)
        return;

    position = 0;
    playing = true;
    fadeInRemaining = fadeLength;
    fadeOutRemaining = -1;
}

void SamplePlayer::stop() noexcept
{
    if (playing && fadeOutRemaining < 0)
        fadeOutRemaining = fadeLength;
}

void SamplePlayer::beginBlock() noexcept
{
    // Only swap when the old sample can be retired without freeing it here.
    if (pending.load() != nullptr && trash.getFreeSpace() > 0)
    {
        if (auto* incoming = pending.exchange (nullptr))
        {
            if (active != nullptr)
                trash.push (active);

            active = incoming;
            playing = false;
            position = 0;
        }
    }

    if (stopRequested.exchange (false))
        stop();

    if (startRequested.exchange (false))
        trigger();
}

void SamplePlayer::render (juce::AudioBuffer<float>& output, int startSample, int numSamples) noexcept
{
    // A sample prepared for a different rate is being rebuilt in the
    // background; stay silent rather than play it at the wrong pitch.
    if (playing && active != nullptr && ! juce::approximatelyEqual (active->sampleRate, currentRate))
        playing = false;

    if (playing && active != nullptr)
    {
        const auto total = (juce::int64) active->audio.getNumSamples();
        const float* srcL = active->audio.getReadPointer (0);
        const float* srcR = active->audio.getReadPointer (1);
        const int numOut = output.getNumChannels();
        float* outL = numOut > 0 ? output.getWritePointer (0) : nullptr;
        float* outR = numOut > 1 ? output.getWritePointer (1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            if (position >= total || fadeOutRemaining == 0)
            {
                playing = false;
                break;
            }

            float gain = 1.0f;

            if (fadeInRemaining > 0)
                gain *= 1.0f - (float) fadeInRemaining-- / (float) fadeLength;

            if (fadeOutRemaining > 0)
                gain *= (float) fadeOutRemaining-- / (float) fadeLength;

            const float l = srcL[position] * gain;
            const float r = srcR[position] * gain;
            ++position;

            if (outR != nullptr)
            {
                outL[startSample + i] += l;
                outR[startSample + i] += r;
            }
            else if (outL != nullptr)
            {
                outL[startSample + i] += 0.5f * (l + r);
            }
        }
    }

    playingFlag.store (playing);

    const auto total = active != nullptr ? active->audio.getNumSamples() : 0;
    progress.store (playing && total > 0 ? (float) position / (float) total : 0.0f);
}

} // namespace digga
