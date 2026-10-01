#include "midi/BasicPitch.h"

#include "BinaryData.h"

#include <juce_dsp/juce_dsp.h>

#include <map>
#include <thread>

namespace digga::midi
{

namespace
{
    // ---- model constants (basic_pitch/constants.py)
    constexpr int modelRate = 22050;
    constexpr int fftHop = 256;
    constexpr int windowSamples = 2 * modelRate - fftHop;      // 43844
    constexpr int framesPerWindow = 172;
    constexpr int overlapFrames = 30;
    constexpr int overlapSamples = overlapFrames * fftHop;      // 7680
    constexpr int hopSamples = windowSamples - overlapSamples;  // 36164
    constexpr int cqtBins = 309, octaveBins = 36, octaves = 9, kernel = 256;
    constexpr int contourBins = 264, noteBins = 88;
    constexpr int midiOffset = 21;

    // ---- decoding defaults (basic_pitch/inference.py)
    constexpr float onsetThreshold = 0.5f;
    constexpr float frameThreshold = 0.3f;
    constexpr int minNoteFrames = 11;        // round(127.7 ms * 22050 / 256 / 1000)
    constexpr int energyTolerance = 11;

    struct Tensor4
    {
        int o = 0, c = 0, h = 0, w = 0;
        std::vector<float> data;
        float at (int oo, int cc, int hh, int ww) const noexcept { return data[(size_t) (((oo * c + cc) * h + hh) * w + ww)]; }
    };

    struct Conv
    {
        Tensor4 weight;
        std::vector<float> bias;
        int strideH = 1, strideW = 1, padTop = 0, padLeft = 0, padBottom = 0, padRight = 0;
    };

    /** (C, H, W) feature map. */
    struct Map
    {
        int c = 0, h = 0, w = 0;
        std::vector<float> data;

        Map() = default;
        Map (int cc, int hh, int ww) : c (cc), h (hh), w (ww), data ((size_t) (cc * hh * ww), 0.0f) {}
        float* plane (int ch) noexcept { return data.data() + (size_t) ch * (size_t) (h * w); }
        const float* plane (int ch) const noexcept { return data.data() + (size_t) ch * (size_t) (h * w); }
    };

    Map conv2d (const Map& in, const Conv& cv)
    {
        const auto& wt = cv.weight;
        const int outH = (in.h + cv.padTop + cv.padBottom - wt.h) / cv.strideH + 1;
        const int outW = (in.w + cv.padLeft + cv.padRight - wt.w) / cv.strideW + 1;
        Map out (wt.o, outH, outW);

        for (int o = 0; o < wt.o; ++o)
        {
            float* dst = out.plane (o);
            std::fill (dst, dst + outH * outW, cv.bias[(size_t) o]);

            for (int c = 0; c < wt.c; ++c)
            {
                const float* src = in.plane (c);
                for (int i = 0; i < wt.h; ++i)
                {
                    for (int j = 0; j < wt.w; ++j)
                    {
                        const float k = wt.at (o, c, i, j);
                        // output columns whose input column is inside the image
                        const int colOffset = j - cv.padLeft;
                        const int woMin = colOffset >= 0 ? 0 : (-colOffset + cv.strideW - 1) / cv.strideW;
                        const int woMax = juce::jmin (outW, (in.w - 1 - colOffset) / cv.strideW + 1);

                        for (int ho = 0; ho < outH; ++ho)
                        {
                            const int row = ho * cv.strideH + i - cv.padTop;
                            if (row < 0 || row >= in.h)
                                continue;
                            const float* s = src + row * in.w + colOffset;
                            float* d = dst + ho * outW;
                            if (cv.strideW == 1)
                                for (int wo = woMin; wo < woMax; ++wo)
                                    d[wo] += k * s[wo];
                            else
                                for (int wo = woMin; wo < woMax; ++wo)
                                    d[wo] += k * s[wo * cv.strideW];
                        }
                    }
                }
            }
        }
        return out;
    }

    void relu (Map& m) noexcept     { for (auto& v : m.data) v = juce::jmax (0.0f, v); }
    void sigmoid (Map& m) noexcept  { for (auto& v : m.data) v = 1.0f / (1.0f + std::exp (-v)); }

    class Model
    {
    public:
        static const Model& get()
        {
            static const Model model;
            return model;
        }

        void run (const float* x, float* noteOut, float* onsetOut, float* contourOut) const
        {
            // ---------------- CQT (nnAudio CQT2010v2: octave-wise kernels + 2x decimation)
            std::vector<double> power ((size_t) (framesPerWindow * cqtBins), 0.0);
            std::vector<float> signal (x, x + windowSamples), padded, next;
            int stride = kernel;

            for (int octave = 0; octave < octaves; ++octave)
            {
                // reflect padding by 128 on both sides
                const int n = (int) signal.size();
                padded.resize ((size_t) n + 256);
                for (int i = 0; i < 128; ++i)
                {
                    padded[(size_t) i] = signal[(size_t) (128 - i)];
                    padded[(size_t) (n + 128 + i)] = signal[(size_t) (n - 2 - i)];
                }
                std::copy (signal.begin(), signal.end(), padded.begin() + 128);

                // octave k lands in channels [324 - 36 (k + 1), 324 - 36 k); we keep the top 309
                const int channelBase = (octaves - 1 - octave) * octaveBins - 15;
                for (int t = 0; t < framesPerWindow; ++t)
                {
                    const float* seg = padded.data() + (size_t) (t * stride);
                    for (int b = 0; b < octaveBins; ++b)
                    {
                        const int channel = channelBase + b;
                        if (channel < 0)
                            continue;
                        const float* kr = cqtReal.data() + (size_t) (b * kernel);
                        const float* ki = cqtImag.data() + (size_t) (b * kernel);
                        double re = 0.0, im = 0.0;
                        for (int k = 0; k < kernel; ++k)
                        {
                            re += (double) kr[k] * seg[k];
                            im += (double) ki[k] * seg[k];
                        }
                        const double s = cqtScale[(size_t) channel];
                        power[(size_t) (t * cqtBins + channel)] = (re * s) * (re * s) + (im * s) * (im * s);
                    }
                }

                if (octave + 1 < octaves)
                {
                    // anti-aliased 2x decimation (zero padding 127)
                    const int outLength = (n + 254 - kernel) / 2 + 1;
                    next.assign ((size_t) outLength, 0.0f);
                    for (int t = 0; t < outLength; ++t)
                    {
                        double acc = 0.0;
                        for (int k = 0; k < kernel; ++k)
                        {
                            const int idx = 2 * t + k - 127;
                            if (idx >= 0 && idx < n)
                                acc += (double) lowpass[(size_t) k] * signal[(size_t) idx];
                        }
                        next[(size_t) t] = (float) acc;
                    }
                    signal.swap (next);
                    stride /= 2;
                }
            }

            // ---------------- normalised log power + batch norm
            std::vector<float> logPower (power.size());
            float lo = std::numeric_limits<float>::max(), hi = -lo;
            for (size_t i = 0; i < power.size(); ++i)
            {
                logPower[i] = (float) (10.0 * std::log10 (power[i] + 1.0e-10));
                lo = juce::jmin (lo, logPower[i]);
            }
            for (auto& v : logPower)
            {
                v -= lo;
                hi = juce::jmax (hi, v);
            }
            for (auto& v : logPower)
                v = (hi > 0.0f ? v / hi : 0.0f) * bnMul + bnAdd;

            // ---------------- harmonic stacking -> (8, 172, 264)
            static constexpr int shifts[8] = { -36, 0, 36, 57, 72, 84, 93, 101 };
            Map stacked (8, framesPerWindow, contourBins);
            for (int h = 0; h < 8; ++h)
            {
                float* p = stacked.plane (h);
                for (int t = 0; t < framesPerWindow; ++t)
                    for (int f = 0; f < contourBins; ++f)
                    {
                        const int src = f + shifts[h];
                        p[t * contourBins + f] = src >= 0 && src < cqtBins ? logPower[(size_t) (t * cqtBins + src)] : 0.0f;
                    }
            }

            // ---------------- CNN
            auto onsetFeatures = conv2d (stacked, onset1);   relu (onsetFeatures);
            auto contourHidden = conv2d (stacked, contour1); relu (contourHidden);
            auto contour = conv2d (contourHidden, contour2); sigmoid (contour);
            auto noteHidden = conv2d (contour, note1);       relu (noteHidden);
            auto note = conv2d (noteHidden, note2);          sigmoid (note);

            Map joined (1 + onsetFeatures.c, note.h, note.w);
            std::copy (note.data.begin(), note.data.end(), joined.data.begin());
            std::copy (onsetFeatures.data.begin(), onsetFeatures.data.end(), joined.data.begin() + (long) note.data.size());
            auto onset = conv2d (joined, onset2);            sigmoid (onset);

            std::copy (note.data.begin(), note.data.end(), noteOut);
            std::copy (onset.data.begin(), onset.data.end(), onsetOut);
            if (contourOut != nullptr)
                std::copy (contour.data.begin(), contour.data.end(), contourOut);
        }

    private:
        Model()
        {
            std::map<std::string, std::vector<float>> tensors;
            juce::MemoryInputStream in (BinaryData::basic_pitch_bin, BinaryData::basic_pitch_binSize, false);
            char magic[4] {};
            in.read (magic, 4);
            jassert (std::string (magic, 4) == "BPW1");
            const int count = in.readInt();
            for (int i = 0; i < count; ++i)
            {
                const int nameLength = in.readInt();
                juce::MemoryBlock name;
                in.readIntoMemoryBlock (name, nameLength);
                const int values = in.readInt();
                std::vector<float> data ((size_t) values);
                in.read (data.data(), values * (int) sizeof (float));
                tensors[name.toString().toStdString()] = std::move (data);
            }

            cqtReal = tensors["cqt.real"];
            cqtImag = tensors["cqt.imag"];
            lowpass = tensors["cqt.lowpass"];
            for (auto v : tensors["cqt.scale"])
                cqtScale.push_back (v);
            bnMul = tensors["bn.mul"][0];
            bnAdd = tensors["bn.add"][0];

            auto conv = [&tensors] (const std::string& name, int o, int c, int h, int w,
                                    int sh, int sw, int pt, int pl, int pb, int pr)
            {
                Conv cv;
                cv.weight = { o, c, h, w, tensors[name + ".w"] };
                cv.bias = tensors[name + ".b"];
                cv.strideH = sh; cv.strideW = sw;
                cv.padTop = pt; cv.padLeft = pl; cv.padBottom = pb; cv.padRight = pr;
                jassert ((int) cv.weight.data.size() == o * c * h * w && (int) cv.bias.size() == o);
                return cv;
            };

            onset1   = conv ("onset1",   32, 8, 5, 5,   1, 3, 2, 1, 2, 1);
            contour1 = conv ("contour1",  8, 8, 3, 39,  1, 1, 1, 19, 1, 19);
            contour2 = conv ("contour2",  1, 8, 5, 5,   1, 1, 2, 2, 2, 2);
            note1    = conv ("note1",    32, 1, 7, 7,   1, 3, 3, 2, 3, 2);
            note2    = conv ("note2",     1, 32, 7, 3,  1, 1, 3, 1, 3, 1);
            onset2   = conv ("onset2",    1, 33, 3, 3,  1, 1, 1, 1, 1, 1);
        }

        std::vector<float> cqtReal, cqtImag, lowpass;
        std::vector<double> cqtScale;
        float bnMul = 1.0f, bnAdd = 0.0f;
        Conv onset1, contour1, contour2, note1, note2, onset2;
    };

    /** Mono, band-limited and resampled to 22.05 kHz. */
    std::vector<float> toModelRate (const juce::AudioBuffer<float>& audio, double sampleRate)
    {
        const int n = audio.getNumSamples();
        std::vector<float> mono ((size_t) n, 0.0f);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            juce::FloatVectorOperations::addWithMultiply (mono.data(), audio.getReadPointer (ch), 1.0f / (float) audio.getNumChannels(), n);

        if (std::abs (sampleRate - modelRate) < 1.0)
            return mono;

        if (sampleRate > modelRate)
        {
            // anti-alias low-pass at 10 kHz before decimating
            auto fir = juce::dsp::FilterDesign<float>::designFIRLowpassWindowMethod (
                10000.0f, sampleRate, 127, juce::dsp::WindowingFunction<float>::blackman);
            const auto& c = fir->coefficients;
            const int taps = (int) c.size(), half = taps / 2;
            std::vector<float> filtered ((size_t) n, 0.0f);
            for (int i = 0; i < n; ++i)
            {
                float acc = 0.0f;
                const int kFrom = juce::jmax (0, i + half - (n - 1)), kTo = juce::jmin (taps - 1, i + half);
                for (int k = kFrom; k <= kTo; ++k)
                    acc += c[(size_t) k] * mono[(size_t) (i + half - k)];
                filtered[(size_t) i] = acc;
            }
            mono.swap (filtered);
        }

        const double ratio = sampleRate / modelRate;
        const int outLength = (int) std::floor (n / ratio);
        std::vector<float> out ((size_t) outLength, 0.0f);
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, mono.data(), out.data(), outLength, n, 0);
        return out;
    }
}

void runModelWindow (const float* window, float* note, float* onset, float* contour)
{
    Model::get().run (window, note, onset, contour);
}

Posteriors computePosteriors (const float* mono, int numSamples)
{
    // basic_pitch.inference: pad half the overlap in front, windows of 43844
    // samples every 36164, drop 15 frames at both ends of every window
    const int padFront = overlapSamples / 2;
    const int total = numSamples + padFront;
    const int numWindows = juce::jmax (1, (total + hopSamples - 1) / hopSamples);
    const int keep = framesPerWindow - overlapFrames;   // 142
    const int trim = overlapFrames / 2;

    std::vector<std::vector<float>> notes ((size_t) numWindows), onsets ((size_t) numWindows);

    auto runWindow = [&] (int w)
    {
        std::vector<float> window ((size_t) windowSamples, 0.0f);
        for (int i = 0; i < windowSamples; ++i)
        {
            const int src = w * hopSamples + i - padFront;
            if (src >= 0 && src < numSamples)
                window[(size_t) i] = mono[src];
        }
        notes[(size_t) w].resize ((size_t) (framesPerWindow * noteBins));
        onsets[(size_t) w].resize ((size_t) (framesPerWindow * noteBins));
        Model::get().run (window.data(), notes[(size_t) w].data(), onsets[(size_t) w].data(), nullptr);
    };

    (void) Model::get();   // load weights once before going parallel
    const int threads = juce::jlimit (1, 8, (int) std::thread::hardware_concurrency());
    std::atomic<int> nextWindow { 0 };
    std::vector<std::thread> workers;
    for (int t = 0; t < juce::jmin (threads, numWindows); ++t)
        workers.emplace_back ([&]
        {
            for (int w = nextWindow++; w < numWindows; w = nextWindow++)
                runWindow (w);
        });
    for (auto& worker : workers)
        worker.join();

    Posteriors p;
    p.frames = (int) ((double) numSamples / hopSamples * keep);
    p.frames = juce::jmin (p.frames, numWindows * keep);
    p.note.assign ((size_t) (p.frames * noteBins), 0.0f);
    p.onset.assign ((size_t) (p.frames * noteBins), 0.0f);

    for (int f = 0; f < p.frames; ++f)
    {
        const int w = f / keep, local = f % keep + trim;
        std::copy_n (notes[(size_t) w].data() + local * noteBins, noteBins, p.note.data() + f * noteBins);
        std::copy_n (onsets[(size_t) w].data() + local * noteBins, noteBins, p.onset.data() + f * noteBins);
    }
    return p;
}

std::vector<Note> decodeNotes (const Posteriors& p)
{
    const int n = p.frames;
    std::vector<Note> result;
    if (n < 3)
        return result;

    const auto& frames = p.note;
    auto at = [] (int t, int f) { return (size_t) (t * noteBins + f); };

    // onsets inferred from jumps in the frame activations (get_infered_onsets)
    std::vector<float> onsets (p.onset);
    {
        std::vector<float> diff ((size_t) (n * noteBins), 0.0f);
        float maxDiff = 0.0f, maxOnset = 0.0f;
        for (auto v : p.onset)
            maxOnset = juce::jmax (maxOnset, v);
        for (int t = 2; t < n; ++t)
            for (int f = 0; f < noteBins; ++f)
            {
                const float d1 = frames[at (t, f)] - frames[at (t - 1, f)];
                const float d2 = frames[at (t, f)] - frames[at (t - 2, f)];
                const float d = juce::jmax (0.0f, juce::jmin (d1, d2));
                diff[at (t, f)] = d;
                maxDiff = juce::jmax (maxDiff, d);
            }
        if (maxDiff > 0.0f)
            for (size_t i = 0; i < onsets.size(); ++i)
                onsets[i] = juce::jmax (onsets[i], maxOnset * diff[i] / maxDiff);
    }

    std::vector<float> remaining (frames);
    std::vector<std::tuple<int, int, int, float>> events;   // start frame, end frame, pitch, amplitude

    auto meanFrames = [&] (int from, int to, int f)
    {
        double sum = 0.0;
        for (int t = from; t < to; ++t)
            sum += frames[at (t, f)];
        return (float) (sum / juce::jmax (1, to - from));
    };

    // onset peaks above threshold, newest first
    for (int t = n - 2; t >= 1; --t)
    {
        for (int f = noteBins - 1; f >= 0; --f)
        {
            const float o = onsets[at (t, f)];
            if (! (o > onsets[at (t - 1, f)] && o > onsets[at (t + 1, f)] && o >= onsetThreshold))
                continue;

            int i = t + 1, k = 0;
            while (i < n - 1 && k < energyTolerance)
            {
                k = remaining[at (i, f)] < frameThreshold ? k + 1 : 0;
                ++i;
            }
            i -= k;

            if (i - t <= minNoteFrames)
                continue;

            for (int tt = t; tt < i; ++tt)
            {
                remaining[at (tt, f)] = 0.0f;
                if (f < noteBins - 1) remaining[at (tt, f + 1)] = 0.0f;
                if (f > 0)            remaining[at (tt, f - 1)] = 0.0f;
            }
            events.emplace_back (t, i, f + midiOffset, meanFrames (t, i, f));
        }
    }

    // "melodia trick": follow leftover energy both ways from its peak
    for (;;)
    {
        size_t best = 0;
        for (size_t idx = 1; idx < remaining.size(); ++idx)
            if (remaining[idx] > remaining[best])
                best = idx;
        if (remaining[best] <= frameThreshold)
            break;

        const int mid = (int) best / noteBins, f = (int) best % noteBins;
        remaining[best] = 0.0f;

        auto clearAround = [&] (int t)
        {
            remaining[at (t, f)] = 0.0f;
            if (f < noteBins - 1) remaining[at (t, f + 1)] = 0.0f;
            if (f > 0)            remaining[at (t, f - 1)] = 0.0f;
        };

        int i = mid + 1, k = 0;
        while (i < n - 1 && k < energyTolerance)
        {
            k = remaining[at (i, f)] < frameThreshold ? k + 1 : 0;
            clearAround (i);
            ++i;
        }
        const int end = i - 1 - k;

        i = mid - 1;
        k = 0;
        while (i > 0 && k < energyTolerance)
        {
            k = remaining[at (i, f)] < frameThreshold ? k + 1 : 0;
            clearAround (i);
            --i;
        }
        const int start = i + 1 + k;

        if (end - start <= minNoteFrames)
            continue;
        events.emplace_back (start, end, f + midiOffset, meanFrames (start, end, f));
    }

    // frame index -> seconds of the original signal
    const int keep = framesPerWindow - overlapFrames, trim = overlapFrames / 2;
    auto toSeconds = [&] (int frame)
    {
        const int w = frame / keep, local = frame % keep + trim;
        return ((double) w * hopSamples + (double) local * fftHop - overlapSamples / 2) / modelRate;
    };

    for (const auto& [start, end, pitch, amplitude] : events)
        result.push_back ({ juce::jmax (0.0, toSeconds (start)), toSeconds (end), pitch, amplitude });

    std::sort (result.begin(), result.end(), [] (const Note& a, const Note& b)
    {
        return a.startSeconds < b.startSeconds || (juce::exactlyEqual (a.startSeconds, b.startSeconds) && a.pitch < b.pitch);
    });
    return result;
}

std::vector<Note> transcribe (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    if (audio.getNumSamples() == 0 || sampleRate <= 0.0)
        return {};
    const auto mono = toModelRate (audio, sampleRate);
    return decodeNotes (computePosteriors (mono.data(), (int) mono.size()));
}

} // namespace digga::midi
