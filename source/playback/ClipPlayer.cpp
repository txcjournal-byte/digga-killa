#include "playback/ClipPlayer.h"

namespace digga
{

void ClipPlayer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    fadeLength = juce::jmax (16, (int) (0.003 * sampleRate));
    for (auto& v : voices)
        v.active = false;
    previewId.store (-1);
}

ClipPlayer::Voice& ClipPlayer::allocateVoice() noexcept
{
    Voice* oldest = &voices[0];
    for (auto& v : voices)
    {
        if (! v.active)
            return v;
        if (v.order < oldest->order)
            oldest = &v;
    }
    return *oldest;
}

void ClipPlayer::startVoice (Voice& v, const ClipEntry& clip, bool isPreview, int note, double rate, float gain,
                             double startPosition) noexcept
{
    v.clip = &clip;
    v.clipId = clip.id;
    v.position = startPosition;
    v.rate = rate;
    v.gain = gain;
    v.note = note;
    v.active = true;
    v.loop = clip.isLoop;
    v.isPreview = isPreview;
    v.fadeIn = fadeLength;
    v.fadeOut = -1;
    v.order = ++voiceCounter;
}

void ClipPlayer::release (Voice& v) noexcept
{
    if (v.active && v.fadeOut < 0)
        v.fadeOut = fadeLength;
}

void ClipPlayer::beginBlock (const Transport& t, bool reverse) noexcept
{
    transport = t;
    reversed = reverse;

    if (bank.acquire())
    {
        const auto* current = bank.get();
        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            v.clip = current->find (v.clipId);
            if (v.clip == nullptr)
                v.active = false;
        }
    }

    const auto* current = bank.get();
    Command command;
    while (commands.pop (command))
    {
        for (auto& v : voices)
            if (v.isPreview)
                release (v);

        if (command.type != Command::preview || current == nullptr)
            continue;

        if (const auto* clip = current->find (command.id))
        {
            double start = 0.0;
            if (clip->isLoop && transport.playing && clip->beats > 0.0 && transport.bpm > 0.0)
            {
                // lock the loop to the host's bar position
                const double beatsIn = std::fmod (juce::jmax (0.0, transport.ppq), clip->beats);
                start = beatsIn * 60.0 / transport.bpm * sampleRate;
            }
            startVoice (allocateVoice(), *clip, true, -1, 1.0, 1.0f, start);
        }
    }
}

void ClipPlayer::noteOn (int note, float velocity) noexcept
{
    const auto* current = bank.get();
    if (current == nullptr)
        return;

    const auto* clip = current->find (selected.load());
    if (clip == nullptr)
        return;

    const float gain = std::pow (juce::jlimit (0.0f, 1.0f, velocity), 0.6f);

    if (clip->isLoop)
    {
        for (auto& v : voices)
            if (v.active && v.loop && ! v.isPreview)
                release (v);
        startVoice (allocateVoice(), *clip, false, note, 1.0, gain, 0.0);
    }
    else
    {
        const double rate = std::pow (2.0, (note - 60) / 12.0);
        startVoice (allocateVoice(), *clip, false, note, rate, gain, 0.0);
    }
}

void ClipPlayer::noteOff (int note) noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.isPreview && v.note == note && v.loop)
            release (v);
}

void ClipPlayer::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (! v.isPreview)
            release (v);
}

void ClipPlayer::render (juce::AudioBuffer<float>& output, int startSample, int numSamples) noexcept
{
    const int outChannels = output.getNumChannels();
    int activePreview = -1;
    float progress = 0.0f;

    for (auto& v : voices)
    {
        if (! v.active || v.clip == nullptr)
            continue;

        const auto& audio = *v.clip->audio;
        const int length = audio.getNumSamples();
        const int srcChannels = audio.getNumChannels();
        if (length == 0)
        {
            v.active = false;
            continue;
        }

        if (v.position >= length && v.loop)
            v.position = std::fmod (v.position, (double) length);

        for (int i = 0; i < numSamples; ++i)
        {
            if (v.position >= length)
            {
                if (! v.loop)
                {
                    v.active = false;
                    break;
                }
                v.position -= length;
            }

            if (v.fadeOut == 0)
            {
                v.active = false;
                break;
            }

            float gain = v.gain;
            if (v.fadeIn > 0)
                gain *= 1.0f - (float) v.fadeIn-- / (float) fadeLength;
            if (v.fadeOut > 0)
                gain *= (float) v.fadeOut-- / (float) fadeLength;

            const double readPos = reversed ? (double) length - 1.0 - v.position : v.position;
            const int i0 = juce::jlimit (0, length - 1, (int) readPos);
            int i1 = i0 + 1;
            if (i1 >= length)
                i1 = v.loop ? 0 : length - 1;
            const float frac = (float) (readPos - (double) i0);

            for (int ch = 0; ch < outChannels; ++ch)
            {
                const auto* src = audio.getReadPointer (juce::jmin (ch, srcChannels - 1));
                output.getWritePointer (ch)[startSample + i] += (src[i0] + (src[i1] - src[i0]) * frac) * gain;
            }

            v.position += v.rate;
        }

        if (v.active && v.isPreview && v.fadeOut < 0)
        {
            activePreview = v.clipId;
            progress = (float) (v.position / length);
        }
    }

    previewId.store (activePreview);
    previewProgress.store (progress);
}

} // namespace digga
