#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <memory>

namespace digga
{

/** Audio ready to be played by the audio thread: always stereo, already at
    the host sample rate. */
struct PlaybackSample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;
};

/** Single-producer / single-consumer queue of raw pointers, lock-free. */
template <typename T, int Capacity>
class PointerFifo
{
public:
    bool push (T* item) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 <= 0)
            return false;
        items[(size_t) scope.startIndex1] = item;
        return true;
    }

    bool pop (T*& item) noexcept
    {
        const auto scope = fifo.read (1);
        if (scope.blockSize1 <= 0)
            return false;
        item = items[(size_t) scope.startIndex1];
        return true;
    }

    int getFreeSpace() const noexcept { return fifo.getFreeSpace(); }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T*, (size_t) Capacity> items {};
};

/** Plays one PlaybackSample. New samples are handed over without locks: any
    non-audio thread calls setSample(), the audio thread picks the pointer up,
    and the replaced one goes into a trash queue that collectGarbage() empties
    on the message thread. The audio thread never allocates or frees. */
class SamplePlayer
{
public:
    SamplePlayer() = default;
    ~SamplePlayer();

    // ---- any non-audio thread
    void setSample (std::unique_ptr<PlaybackSample> sample);
    void collectGarbage();
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
    std::atomic<PlaybackSample*> pending { nullptr };
    PlaybackSample* active = nullptr;           // audio thread only
    PointerFifo<PlaybackSample, 32> trash;

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
