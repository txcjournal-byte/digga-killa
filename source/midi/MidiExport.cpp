#include "midi/MidiExport.h"

#include "export/Exporter.h"

namespace digga::midi
{

juce::MidiFile makeMidiFile (const std::vector<Note>& notes, const ExportOptions& options)
{
    constexpr int ppq = 960;
    const double bpm = options.bpm > 0.0 ? options.bpm : 120.0;
    auto toTicks = [&] (double seconds) { return std::round (seconds * bpm / 60.0 * ppq); };

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    track.addEvent (juce::MidiMessage::textMetaEvent (3, "Digga Killa"), 0.0);

    const double length = options.lengthSeconds > 0.0 ? options.lengthSeconds : 0.0;

    for (const auto& n : notes)
    {
        double start = n.startSeconds, end = n.endSeconds;
        if (options.reverse && length > 0.0)
        {
            start = length - n.endSeconds;
            end = length - n.startSeconds;
        }
        if (length > 0.0)
        {
            start = juce::jlimit (0.0, length, start);
            end = juce::jlimit (0.0, length, end);
        }
        if (end - start < 0.01)
            continue;

        const int pitch = juce::jlimit (0, 127, n.pitch + options.transpose);
        const auto velocity = (juce::uint8) juce::jlimit (1, 127, (int) std::round (n.amplitude * 127.0f));
        track.addEvent (juce::MidiMessage::noteOn (1, pitch, velocity), toTicks (start));
        track.addEvent (juce::MidiMessage::noteOff (1, pitch), toTicks (end));
    }

    if (length > 0.0)
        track.addEvent (juce::MidiMessage::endOfTrack(), toTicks (length));

    track.sort();
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);
    file.addTrack (track);
    return file;
}

juce::File writeToTempFile (const std::vector<Note>& notes, const ExportOptions& options, const juce::String& fileName)
{
    const auto folder = exporter::getExportFolder();
    folder.createDirectory();
    auto file = folder.getChildFile (fileName + ".mid");
    if (file.exists() && ! file.deleteFile())
        file = file.getNonexistentSibling (false);

    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! makeMidiFile (notes, options).writeTo (out))
        return {};
    out.flush();
    return file;
}

std::shared_future<TranscriptionCache::NotesPtr> TranscriptionCache::start (const AudioPtr& audio, double sampleRate)
{
    // caller holds the lock
    for (auto it = entries.begin(); it != entries.end();)
        it = it->second.audio.expired() ? entries.erase (it) : std::next (it);

    auto& entry = entries[audio.get()];
    if (entry.result.valid() && ! entry.audio.expired())
        return entry.result;

    entry.audio = audio;
    entry.result = std::async (std::launch::async, [audio, sampleRate]
    {
        return std::make_shared<const std::vector<Note>> (transcribe (*audio, sampleRate));
    }).share();
    return entry.result;
}

void TranscriptionCache::prefetch (const AudioPtr& audio, double sampleRate)
{
    if (audio == nullptr)
        return;
    const std::lock_guard<std::mutex> sl (lock);
    start (audio, sampleRate);
}

TranscriptionCache::NotesPtr TranscriptionCache::get (const AudioPtr& audio, double sampleRate)
{
    if (audio == nullptr)
        return {};

    std::shared_future<NotesPtr> result;
    {
        const std::lock_guard<std::mutex> sl (lock);
        result = start (audio, sampleRate);
    }
    return result.get();
}

} // namespace digga::midi
