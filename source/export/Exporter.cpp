#include "export/Exporter.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace digga::exporter
{

juce::File getExportFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("DiggaKilla");
}

juce::String makeFileName (const juce::String& tag, double bpm, const juce::String& key)
{
    auto name = "DiggaKilla_" + tag + "_" + juce::String (juce::roundToInt (bpm)) + "bpm";
    if (key.isNotEmpty())
        name << "_" << key;
    return name.removeCharacters ("\\/:*?\"<>|");
}

juce::AudioBuffer<float> render (const Request& request)
{
    if (request.audio == nullptr || request.audio->getNumSamples() == 0)
        return {};

    juce::AudioBuffer<float> clip (*request.audio);
    if (request.reverse)
        clip.reverse (0, clip.getNumSamples());

    if (dsp::FxChain::isNeutral (request.fx) || request.fx.mix < 0.001f)
        return clip;

    constexpr int block = 512;
    const int length = clip.getNumSamples();
    const int channels = clip.getNumChannels();

    dsp::FxChain chain;
    chain.prepare (request.sampleRate, block, channels);

    // loops: run twice so tails wrap into the start; shots: let tails ring
    const bool hasTail = request.fx.reverb > 0.001f || request.fx.delay > 0.001f;
    const int passes = request.isLoop ? 2 : 1;
    const int tail = request.isLoop ? 0 : (hasTail ? (int) (3.0 * request.sampleRate) : 0);
    const int extra = (int) (0.5 * request.sampleRate); // room for pitch latency
    const int total = length * passes + tail + extra;

    juce::AudioBuffer<float> io (channels, total);
    io.clear();
    for (int p = 0; p < passes; ++p)
        for (int ch = 0; ch < channels; ++ch)
            io.copyFrom (ch, p * length, clip, ch, 0, length);

    juce::AudioBuffer<float> chunk (channels, block);
    int latency = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        for (int ch = 0; ch < channels; ++ch)
            chunk.copyFrom (ch, 0, io, ch, pos, n);
        chain.process (chunk, n, request.fx);
        latency = chain.getPitchLatency();
        for (int ch = 0; ch < channels; ++ch)
            io.copyFrom (ch, pos, chunk, ch, 0, n);
    }

    const int start = (request.isLoop ? length : 0) + latency;
    int outLength = request.isLoop ? length : length + tail;
    outLength = juce::jmin (outLength, total - start);

    juce::AudioBuffer<float> out (channels, juce::jmax (1, outLength));
    for (int ch = 0; ch < channels; ++ch)
        out.copyFrom (ch, 0, io, ch, start, outLength);

    if (! request.isLoop)
    {
        // trim silent tail, keep a short fade
        int end = outLength;
        const float threshold = 0.001f;
        while (end > length && out.getMagnitude (end - 256 > 0 ? end - 256 : 0, juce::jmin (256, end)) < threshold)
            end -= 256;
        end = juce::jmax (1, juce::jmin (outLength, end));
        out.setSize (channels, end, true);
        audio::fadeOut (out, end, juce::jmin (end / 4, (int) (0.01 * request.sampleRate)));
    }

    // effects can push past full scale
    const float peak = audio::peak (out);
    if (peak > 0.98f)
        out.applyGain (0.98f / peak);
    return out;
}

juce::File renderToTempFile (const Request& request)
{
    const auto audio = render (request);
    if (audio.getNumSamples() == 0)
        return {};

    const auto folder = getExportFolder();
    folder.createDirectory();
    auto file = folder.getChildFile (request.fileName + ".wav");
    // a DAW may still hold an earlier drag of the same clip open: never fail, pick a new name
    if (file.exists() && ! file.deleteFile())
        file = file.getNonexistentSibling (false);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return {};

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (request.sampleRate)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (24);
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream, options));
    if (writer == nullptr)
        return {};

    writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    writer.reset();
    return file;
}

} // namespace digga::exporter
