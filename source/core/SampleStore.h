#pragma once

#include "core/JobQueue.h"
#include "playback/SamplePlayer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

namespace digga
{

/** Owns the user's source sample. Decoding and sample-rate conversion run on
    the JobQueue; listeners are told about status changes asynchronously on the
    message thread. */
class SampleStore : public juce::ChangeBroadcaster
{
public:
    enum class Status { empty, loading, ready, error };

    struct Info
    {
        juce::File file;
        double lengthSeconds = 0.0;
        double fileSampleRate = 0.0;
        int numChannels = 0;
        juce::String error;
    };

    using ReadyCallback = std::function<void (std::unique_ptr<PlaybackSample>)>;

    SampleStore (JobQueue& jobs, ReadyCallback onPlaybackReady);

    /** Thread-safe (never call from the audio thread). */
    void loadFile (const juce::File& file);
    void setTargetSampleRate (double newRate);

    Status getStatus() const noexcept { return status.load(); }
    Info getInfo() const;

    static bool isSupportedFile (const juce::File& file);
    static juce::String getFileWildcard();

private:
    void runLoad (const juce::File& file, juce::uint32 generation);
    void runPrepare (juce::uint32 generation);
    void fail (juce::uint32 generation, const juce::String& message);
    bool isSuperseded (juce::uint32 generation) const noexcept { return generation != currentGeneration.load(); }

    JobQueue& jobs;
    ReadyCallback onReady;
    juce::AudioFormatManager formats;

    std::atomic<juce::uint32> currentGeneration { 0 };
    std::atomic<double> targetRate { 44100.0 };
    std::atomic<Status> status { Status::empty };

    mutable juce::CriticalSection lock;
    Info info;
    std::shared_ptr<const juce::AudioBuffer<float>> original;
    double originalRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleStore)
};

} // namespace digga
