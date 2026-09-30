#pragma once

#include "core/AudioTools.h"
#include "core/LockFree.h"

#include <array>
#include <atomic>
#include <memory>

namespace digga
{

/** Audio ready to be played by the audio thread: always stereo, already at
    the host sample rate. */
struct PlaybackSample
{
    AudioPtr audio;          // stereo
    double sampleRate = 0.0;
};

/** Previews the source sample (record label play button). New samples are
    handed over lock-free; the audio thread never allocates or frees. */
class SamplePlayer
{
public:
    SamplePlayer() = default;

    // ---- any non-audio thread
    void setSample (std::unique_ptr<PlaybackSample> sample) { handoff.publish (std::move (sample)); }
    void collectGarbage() { handoff.collectGarbage(); }
    void requestStart() noexcept   { startRequested.store (true); }
    void requestStop() noexcept    { stopRequested.store (true); }

    bool isPlaying() const noexcept    { return playingFlag.load(); }
    float getProgress() const noexcept { return progress.load(); }

    // ---- audio thread
    void prepare (double sampleRate) noexcept;
    /** Call once at the start of every block, before MIDI is handled. */
    void beginBlock() noexcept;
    void trigger() noexcept;
    void stop() noexcept;
    void render (juce::AudioBuffer<float>& output, int startSample, int numSamples) noexcept;

private:
    LockFreeHandoff<PlaybackSample> handoff;

    std::atomic<bool> startRequested { false }, stopRequested { false };
    std::atomic<bool> playingFlag { false };
    std::atomic<float> progress { 0.0f };

    double currentRate = 44100.0;
    juce::int64 position = 0;
    bool playing = false;
    int fadeLength = 256;
    int fadeInRemaining = 0;
    int fadeOutRemaining = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplePlayer)
};

} // namespace digga
