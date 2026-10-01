#pragma once

#include "core/AudioTools.h"
#include "midi/BasicPitch.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <future>
#include <map>
#include <mutex>

namespace digga::midi
{

struct ExportOptions
{
    double bpm = 140.0;             // tempo the clip plays at
    double lengthSeconds = 0.0;     // clip length (MIDI clip ends here)
    int transpose = 0;              // PITCH knob, semitones
    bool reverse = false;           // REVERSE switch
};

/** Standard MIDI file (type 1, 960 PPQ) with tempo, 4/4 and the notes. */
juce::MidiFile makeMidiFile (const std::vector<Note>& notes, const ExportOptions& options);

/** Writes "<fileName>.mid" next to the WAV exports and returns it. */
juce::File writeToTempFile (const std::vector<Note>& notes, const ExportOptions& options, const juce::String& fileName);

/** Transcriptions per clip, computed in the background and reused. */
class TranscriptionCache
{
public:
    using NotesPtr = std::shared_ptr<const std::vector<Note>>;

    /** Starts transcribing in the background if not done / running yet. */
    void prefetch (const AudioPtr& audio, double sampleRate);

    /** Result for this clip, computing it now if needed (blocks). */
    NotesPtr get (const AudioPtr& audio, double sampleRate);

private:
    struct Entry
    {
        std::weak_ptr<const juce::AudioBuffer<float>> audio;
        std::shared_future<NotesPtr> result;
    };

    std::shared_future<NotesPtr> start (const AudioPtr& audio, double sampleRate);

    std::mutex lock;
    std::map<const void*, Entry> entries;
};

} // namespace digga::midi
