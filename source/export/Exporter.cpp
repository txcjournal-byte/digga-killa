#include "export/Exporter.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace digga::exporter
{

juce::File getExportFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TrapWin Sample");
}

juce::String makeFileName (const juce::String& tag, double bpm, const juce::String& key)
{
    auto name = "TrapWin_" + tag + "_" + juce::String (juce::roundToInt (bpm)) + "bpm";
    if (key.isNotEmpty())
        name << "_" << key;
    return name.removeCharacters ("\\/:*?\"<>|");
}

juce::File writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& fileName)
{
    if (audio.getNumSamples() == 0)
        return {};

    const auto folder = getExportFolder();
    folder.createDirectory();
    auto file = folder.getChildFile (fileName + ".wav");
    if (file.exists() && ! file.deleteFile())
        file = file.getNonexistentSibling (false);

    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return {};

    // keep inter-sample peaks out of the red
    juce::AudioBuffer<float> safe (audio);
    const float peak = safe.getMagnitude (0, safe.getNumSamples());
    if (peak > 0.99f)
        safe.applyGain (0.99f / peak);

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (safe.getNumChannels())
                             .withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return {};

    writer->writeFromAudioSampleBuffer (safe, 0, safe.getNumSamples());
    writer.reset();
    return file;
}

} // namespace digga::exporter
