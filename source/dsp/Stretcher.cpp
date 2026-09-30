#include "dsp/Stretcher.h"

#include "core/AudioTools.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

namespace digga::dsp
{

juce::AudioBuffer<float> stretch (const juce::AudioBuffer<float>& input, int outputLength,
                                  double semitones, double sampleRate)
{
    const int channels = input.getNumChannels();
    const int inLength = input.getNumSamples();
    juce::AudioBuffer<float> output (channels, juce::jmax (1, outputLength));
    output.clear();

    if (inLength == 0 || outputLength <= 0)
        return output;

    if (inLength == outputLength && std::abs (semitones) < 1.0e-3)
    {
        output.makeCopyOf (input);
        return output;
    }

    signalsmith::stretch::SignalsmithStretch<float> stretcher (0x5eed);
    stretcher.presetDefault (channels, (float) sampleRate);
    stretcher.setTransposeSemitones ((float) semitones);

    std::vector<const float*> in ((size_t) channels);
    std::vector<float*> out ((size_t) channels);
    for (int ch = 0; ch < channels; ++ch)
    {
        in[(size_t) ch] = input.getReadPointer (ch);
        out[(size_t) ch] = output.getWritePointer (ch);
    }

    const int minLength = 2 * stretcher.blockSamples() + 2 * stretcher.intervalSamples();

    if (inLength < minLength)
    {
        // too short for the phase vocoder: pad with silence, process, trim
        juce::AudioBuffer<float> padded (channels, minLength);
        padded.clear();
        for (int ch = 0; ch < channels; ++ch)
            padded.copyFrom (ch, 0, input, ch, 0, inLength);

        const int paddedOut = (int) std::lround ((double) minLength * outputLength / inLength);
        const auto processed = stretch (padded, paddedOut, semitones, sampleRate);
        for (int ch = 0; ch < channels; ++ch)
            output.copyFrom (ch, 0, processed, ch, 0, juce::jmin (outputLength, processed.getNumSamples()));
        return output;
    }

    if (! stretcher.exact (in.data(), inLength, out.data(), outputLength))
        output = audio::resample (input, outputLength);

    return output;
}

} // namespace digga::dsp
