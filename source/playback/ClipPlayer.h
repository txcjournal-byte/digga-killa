#pragma once

#include "core/AudioTools.h"
#include "core/LockFree.h"

#include <array>

namespace digga
{

struct ClipEntry
{
    int id = 0;
    AudioPtr audio;
    bool isLoop = false;
    double beats = 0.0;   // loop length in beats at project tempo
};

/** Immutable set of playable clips handed to the audio thread. */
struct ClipBank
{
    std::vector<ClipEntry> entries;   // sorted by id

    const ClipEntry* find (int id) const noexcept
    {
        const auto it = std::lower_bound (entries.begin(), entries.end(), id,
                                          [] (const ClipEntry& e, int value) { return e.id < value; });
        return it != entries.end() && it->id == id ? &*it : nullptr;
    }
};

struct Transport
{
    bool playing = false;
    double ppq = 0.0;
    double bpm = 120.0;
};

/** Plays generated clips: row previews (loops locked to the host beat) and
    MIDI from the piano roll for the selected clip — one-shots chromatic
    around note 60 (C3 = original pitch), loops started by any note.
    Realtime-safe: no locks, no allocation, no freeing on the audio thread. */
class ClipPlayer
{
public:
    // ---- message thread
    void setBank (std::unique_ptr<ClipBank> newBank) { bank.publish (std::move (newBank)); }
    void collectGarbage() { bank.collectGarbage(); }
    void preview (int id)             { commands.push ({ Command::preview, id }); }
    void stopPreview()                { commands.push ({ Command::stopPreview, -1 }); }
    void setSelected (int id) noexcept { selected.store (id); }
    int getSelected() const noexcept   { return selected.load(); }
    int getPreviewId() const noexcept  { return previewId.load(); }
    float getPreviewProgress() const noexcept { return previewProgress.load(); }

    // ---- audio thread
    void prepare (double sampleRate);
    void beginBlock (const Transport& transport, bool reverse) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void allNotesOff() noexcept;
    void render (juce::AudioBuffer<float>& output, int startSample, int numSamples) noexcept;

private:
    struct Command
    {
        enum Type { preview, stopPreview } type = preview;
        int id = -1;
    };

    struct Voice
    {
        const ClipEntry* clip = nullptr;
        int clipId = -1;
        double position = 0.0;
        double rate = 1.0;
        float gain = 1.0f;
        int note = -1;
        bool active = false, loop = false, isPreview = false;
        int fadeIn = 0, fadeOut = -1;
        juce::uint32 order = 0;
    };

    Voice& allocateVoice() noexcept;
    void startVoice (Voice&, const ClipEntry&, bool isPreview, int note, double rate, float gain, double startPosition) noexcept;
    void release (Voice&) noexcept;

    LockFreeHandoff<ClipBank> bank;
    SpscQueue<Command, 64> commands;
    std::array<Voice, 24> voices {};

    std::atomic<int> selected { -1 }, previewId { -1 };
    std::atomic<float> previewProgress { 0.0f };

    double sampleRate = 44100.0;
    int fadeLength = 128;
    bool reversed = false;
    Transport transport;
    juce::uint32 voiceCounter = 0;
};

} // namespace digga
