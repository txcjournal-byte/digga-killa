#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace digga::stems
{

/** Stems in model order. */
enum Stem
{
    vocals = 0,
    drums,
    bass,
    other,   // melody: keys, synths, guitars, pads ...
    numStems
};

const char* stemName (int stem);

using StemBuffers = std::array<juce::AudioBuffer<float>, numStems>;

/** Called with 0..1 progress; return false to cancel. */
using Progress = std::function<bool (float)>;

/** Source separation into vocals / drums / bass / other with the Spleeter
    4-stem model (Deezer, MIT, https://github.com/deezer/spleeter),
    re-implemented natively (Eigen GEMMs, no ML runtime). The weights are
    exported by tools/export_spleeter.py into spleeter4.bin, which ships in the
    plugin bundle's Resources folder.

    Like Spleeter: 4096 / 1024 STFT at 44.1 kHz, one U-Net per stem on
    512-frame segments of the magnitude spectrogram, ratio masks (exponent 2)
    applied to the complex spectrogram. Above 11 kHz (outside the model's
    1024 bins) each stem keeps the average mask of its top bins, so hats and
    air stay in the drums instead of disappearing. */
class Separator
{
public:
    Separator();
    ~Separator();

    /** Looks next to the plugin / app binary (bundle Resources) and in the
        shared TrapWin data folder. Returns a non-existing file if not found. */
    static juce::File findModelFile();

    bool load (const juce::File& modelFile);
    bool isLoaded() const noexcept;

    /** Splits stereo (or mono) audio at any sample rate. The stems have the
        input's rate, length and channel count and sum back to the input.
        Returns false when cancelled or no model is loaded. Thread-safe
        (const); uses up to four worker threads. */
    bool separate (const juce::AudioBuffer<float>& audio, double sampleRate,
                   StemBuffers& out, const Progress& progress = {}) const;

    /** One U-Net on one segment (testing): input / output [2][512][1024]
        magnitudes, output = estimated stem magnitudes. */
    void runNetwork (int stem, const float* input, float* output) const;

    static constexpr int segmentFrames = 512, modelBins = 1024, fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder, hopSize = 1024;
    static constexpr double modelRate = 44100.0;

    struct Model;

private:
    std::unique_ptr<Model> model;
};

} // namespace digga::stems
