#include "core/SampleStore.h"

namespace digga
{

namespace
{
    constexpr double maxLengthSeconds = 10.0 * 60.0;

    std::unique_ptr<PlaybackSample> makePlayback (const juce::AudioBuffer<float>& source,
                                                  double sourceRate, double targetRate)
    {
        auto result = std::make_unique<PlaybackSample>();
        result->sampleRate = targetRate;

        const double ratio = sourceRate / targetRate;
        const int numIn = source.getNumSamples();
        const int numOut = (int) std::ceil ((double) numIn / ratio);
        auto audio = std::make_shared<juce::AudioBuffer<float>> (2, numOut);

        for (int ch = 0; ch < 2; ++ch)
        {
            if (juce::approximatelyEqual (sourceRate, targetRate))
            {
                audio->copyFrom (ch, 0, source, ch, 0, numIn);
                continue;
            }
            juce::Interpolators::WindowedSinc interpolator;
            interpolator.process (ratio, source.getReadPointer (ch), audio->getWritePointer (ch), numOut, numIn, 0);
        }

        result->audio = std::move (audio);
        return result;
    }
}

SampleStore::SampleStore (JobQueue& jobQueue, ReadyCallback onPlaybackReady)
    : jobs (jobQueue), onReady (std::move (onPlaybackReady))
{
    formats.registerBasicFormats();
}

bool SampleStore::isSupportedFile (const juce::File& file)
{
    return file.existsAsFile()
        && file.hasFileExtension ("wav;wave;aif;aiff;aifc;mp3;flac");
}

juce::String SampleStore::getFileWildcard()
{
    return "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.mp3;*.flac";
}

SampleStore::Info SampleStore::getInfo() const
{
    const juce::ScopedLock sl (lock);
    return info;
}

void SampleStore::loadFile (const juce::File& file)
{
    const auto generation = ++currentGeneration;

    {
        const juce::ScopedLock sl (lock);
        info = {};
        info.file = file;
    }

    if (! isSupportedFile (file))
    {
        fail (generation, file.existsAsFile() ? "Unsupported file type" : "File not found");
        return;
    }

    status.store (Status::loading);
    sendChangeMessage();

    jobs.add ([this, file, generation] { runLoad (file, generation); });
}

void SampleStore::setTargetSampleRate (double newRate)
{
    if (newRate <= 0.0 || juce::approximatelyEqual (targetRate.exchange (newRate), newRate))
        return;

    bool hasOriginal = false;
    {
        const juce::ScopedLock sl (lock);
        hasOriginal = original != nullptr;
    }

    if (hasOriginal && status.load() == Status::ready)
    {
        const auto generation = ++currentGeneration;
        jobs.add ([this, generation] { runPrepare (generation); });
    }
}

void SampleStore::fail (juce::uint32 generation, const juce::String& message)
{
    if (isSuperseded (generation))
        return;

    {
        const juce::ScopedLock sl (lock);
        info.error = message;
    }

    status.store (Status::error);
    sendChangeMessage();
}

void SampleStore::runLoad (const juce::File& file, juce::uint32 generation)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return fail (generation, "Can't read this file");

    const auto length = reader->lengthInSamples;

    if (length <= 0 || reader->sampleRate <= 0.0 || reader->numChannels == 0)
        return fail (generation, "File is empty");

    if ((double) length / reader->sampleRate > maxLengthSeconds)
        return fail (generation, "File is longer than 10 minutes");

    auto buffer = std::make_shared<juce::AudioBuffer<float>> (2, (int) length);
    buffer->clear();

    if (! reader->read (buffer.get(), 0, (int) length, 0, true, true))
        return fail (generation, "Error while decoding");

    if (reader->numChannels == 1)
        buffer->copyFrom (1, 0, *buffer, 0, 0, (int) length);

    if (isSuperseded (generation))
        return;

    {
        const juce::ScopedLock sl (lock);
        original = buffer;
        originalRate = reader->sampleRate;
        info.lengthSeconds = (double) length / reader->sampleRate;
        info.fileSampleRate = reader->sampleRate;
        info.numChannels = (int) reader->numChannels;
    }

    runPrepare (generation);
}

void SampleStore::runPrepare (juce::uint32 generation)
{
    std::shared_ptr<const juce::AudioBuffer<float>> source;
    double sourceRate = 0.0;

    {
        const juce::ScopedLock sl (lock);
        source = original;
        sourceRate = originalRate;
    }

    if (source == nullptr || isSuperseded (generation))
        return;

    auto playback = makePlayback (*source, sourceRate, targetRate.load());

    if (isSuperseded (generation))
        return;

    if (onReady)
        onReady (std::move (playback));

    status.store (Status::ready);
    sendChangeMessage();
}

} // namespace digga
