#include "separation/Stems.h"

#include <juce_dsp/juce_dsp.h>

#include <Eigen/Core>

#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

namespace digga::stems
{

const char* stemName (int stem)
{
    switch (stem)
    {
        case vocals: return "Vocals";
        case drums:  return "Drums";
        case bass:   return "Bass";
        case other:  return "Melody";
        default:     return "";
    }
}

namespace
{
    using RowMatrix = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    using ConstMap = Eigen::Map<const RowMatrix>;
    using StridedMap = Eigen::Map<RowMatrix, 0, Eigen::OuterStride<>>;

    constexpr int kernel = 5, taps = kernel * kernel;
    constexpr int encoderChannels[7] = { 2, 16, 32, 64, 128, 256, 512 };
    constexpr int decoderIn[6] = { 512, 512, 256, 128, 64, 32 };
    constexpr int decoderOut[6] = { 256, 128, 64, 32, 16, 1 };

    float halfToFloat (uint16_t h)
    {
        const uint32_t sign = (uint32_t) (h & 0x8000u) << 16;
        uint32_t exponent = (h >> 10) & 0x1fu;
        uint32_t mantissa = h & 0x3ffu;
        uint32_t bits;

        if (exponent == 0)
        {
            if (mantissa == 0)
                bits = sign;
            else
            {
                exponent = 127 - 15 + 1;
                while ((mantissa & 0x400u) == 0) { mantissa <<= 1; --exponent; }
                bits = sign | (exponent << 23) | ((mantissa & 0x3ffu) << 13);
            }
        }
        else if (exponent == 31)
            bits = sign | 0x7f800000u | (mantissa << 13);
        else
            bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);

        float f;
        std::memcpy (&f, &bits, sizeof (f));
        return f;
    }

    inline float elu (float x) noexcept { return x > 0.0f ? x : std::expm1 (x); }

    /** [channels][height][width], row-major. */
    struct Tensor
    {
        int channels = 0, height = 0, width = 0;
        std::vector<float> data;

        void resize (int c, int h, int w)
        {
            channels = c; height = h; width = w;
            data.assign ((size_t) c * (size_t) h * (size_t) w, 0.0f);
        }

        int plane() const noexcept { return height * width; }
        float* channel (int c) noexcept { return data.data() + (size_t) c * (size_t) plane(); }
        const float* channel (int c) const noexcept { return data.data() + (size_t) c * (size_t) plane(); }
    };
}

//==============================================================================
struct Separator::Model
{
    struct Layer
    {
        int in = 0, out = 0;
        std::vector<float> weights;   // encoder: [out][in*25], decoder: [in][out*25]
        std::vector<float> bias, scale, shift;
    };

    struct Net
    {
        Layer encoder[6], decoder[6];
        std::vector<float> finalWeights;   // [2][4][4]
        float finalBias[2] {};
    };

    Net nets[numStems];
};

namespace
{
    /** Conv2D 5x5, stride 2, TF "same" padding (1 before, 2 after on even sizes). */
    void convDown (const Tensor& in, Tensor& out, const std::vector<float>& weights,
                   const std::vector<float>& bias, int outChannels, std::vector<float>& col)
    {
        const int H = in.height, W = in.width, Ho = H / 2, Wo = W / 2, C = in.channels;
        out.resize (outChannels, Ho, Wo);

        const int rowsPerBlock = juce::jmax (1, (1 << 21) / juce::jmax (1, C * taps * Wo));
        const ConstMap w (weights.data(), outChannels, C * taps);

        for (int y0 = 0; y0 < Ho; y0 += rowsPerBlock)
        {
            const int rows = juce::jmin (rowsPerBlock, Ho - y0), n = rows * Wo;
            col.resize ((size_t) C * taps * (size_t) n);

            for (int c = 0; c < C; ++c)
            {
                const float* src = in.channel (c);
                for (int ki = 0; ki < kernel; ++ki)
                    for (int kj = 0; kj < kernel; ++kj)
                    {
                        float* dst = col.data() + (size_t) ((c * kernel + ki) * kernel + kj) * (size_t) n;
                        for (int r = 0; r < rows; ++r)
                        {
                            const int y = 2 * (y0 + r) + ki - 1;
                            float* d = dst + r * Wo;
                            if (y < 0 || y >= H) { std::fill (d, d + Wo, 0.0f); continue; }
                            const float* s = src + (size_t) y * (size_t) W;
                            for (int x = 0; x < Wo; ++x)
                            {
                                const int xx = 2 * x + kj - 1;
                                d[x] = (xx >= 0 && xx < W) ? s[xx] : 0.0f;
                            }
                        }
                    }
            }

            StridedMap o (out.data.data() + (size_t) y0 * (size_t) Wo, outChannels, n, Eigen::OuterStride<> (Ho * Wo));
            o.noalias() = w * ConstMap (col.data(), C * taps, n);
        }

        for (int c = 0; c < outChannels; ++c)
        {
            float* d = out.channel (c);
            for (int i = 0; i < out.plane(); ++i)
                d[i] += bias[(size_t) c];
        }
    }

    /** Conv2DTranspose 5x5, stride 2, "same" (adjoint of convDown). */
    void convUp (const Tensor& in, Tensor& out, const std::vector<float>& weights,
                 const std::vector<float>& bias, int outChannels, std::vector<float>& col)
    {
        const int H = in.height, W = in.width, Ho = 2 * H, Wo = 2 * W, C = in.channels;
        out.resize (outChannels, Ho, Wo);

        const int rowsPerBlock = juce::jmax (1, (1 << 21) / juce::jmax (1, outChannels * taps * W));
        const ConstMap w (weights.data(), C, outChannels * taps);   // [in][out*25]

        for (int i0 = 0; i0 < H; i0 += rowsPerBlock)
        {
            const int rows = juce::jmin (rowsPerBlock, H - i0), n = rows * W;
            col.resize ((size_t) outChannels * taps * (size_t) n);

            Eigen::Map<RowMatrix> t (col.data(), outChannels * taps, n);
            const Eigen::Map<const RowMatrix, 0, Eigen::OuterStride<>> x (in.data.data() + (size_t) i0 * (size_t) W, C, n,
                                                                         Eigen::OuterStride<> (H * W));
            t.noalias() = w.transpose() * x;

            for (int o = 0; o < outChannels; ++o)
            {
                float* dst = out.channel (o);
                for (int ki = 0; ki < kernel; ++ki)
                    for (int kj = 0; kj < kernel; ++kj)
                    {
                        const float* s = col.data() + (size_t) ((o * kernel + ki) * kernel + kj) * (size_t) n;
                        for (int r = 0; r < rows; ++r)
                        {
                            const int y = 2 * (i0 + r) + ki - 1;
                            if (y < 0 || y >= Ho)
                                continue;
                            float* d = dst + (size_t) y * (size_t) Wo;
                            const float* sr = s + r * W;
                            for (int j = 0; j < W; ++j)
                            {
                                const int xx = 2 * j + kj - 1;
                                if (xx >= 0 && xx < Wo)
                                    d[xx] += sr[j];
                            }
                        }
                    }
            }
        }

        for (int c = 0; c < outChannels; ++c)
        {
            float* d = out.channel (c);
            for (int i = 0; i < out.plane(); ++i)
                d[i] += bias[(size_t) c];
        }
    }

    struct Workspace
    {
        Tensor convs[6], act, up, merge;
        std::vector<float> col;
    };

    void runNet (const Separator::Model::Net& net, const Tensor& input, float* output, Workspace& ws)
    {
        // encoder: raw conv outputs feed the skip connections, BN + ELU feed the next layer
        const Tensor* h = &input;
        for (int j = 0; j < 6; ++j)
        {
            const auto& l = net.encoder[j];
            convDown (*h, ws.convs[j], l.weights, l.bias, l.out, ws.col);

            if (j < 5)
            {
                ws.act.resize (l.out, ws.convs[j].height, ws.convs[j].width);
                for (int c = 0; c < l.out; ++c)
                {
                    const float* s = ws.convs[j].channel (c);
                    float* d = ws.act.channel (c);
                    const float sc = l.scale[(size_t) c], sh = l.shift[(size_t) c];
                    for (int i = 0; i < ws.act.plane(); ++i)
                        d[i] = elu (s[i] * sc + sh);
                }
                std::swap (ws.act, ws.merge);
                h = &ws.merge;
            }
        }

        // decoder: transposed conv -> ELU -> BN, concatenated with the matching encoder conv
        h = &ws.convs[5];
        for (int j = 0; j < 6; ++j)
        {
            const auto& l = net.decoder[j];
            convUp (*h, ws.up, l.weights, l.bias, l.out, ws.col);

            for (int c = 0; c < l.out; ++c)
            {
                float* d = ws.up.channel (c);
                const float sc = l.scale[(size_t) c], sh = l.shift[(size_t) c];
                for (int i = 0; i < ws.up.plane(); ++i)
                    d[i] = elu (d[i]) * sc + sh;
            }

            if (j < 5)
            {
                const Tensor& skip = ws.convs[4 - j];
                ws.merge.resize (skip.channels + l.out, skip.height, skip.width);
                std::memcpy (ws.merge.data.data(), skip.data.data(), skip.data.size() * sizeof (float));
                std::memcpy (ws.merge.data.data() + skip.data.size(), ws.up.data.data(), ws.up.data.size() * sizeof (float));
                h = &ws.merge;
            }
        }

        // 4x4 conv, dilation 2, "same" (3 before, 3 after) -> sigmoid mask, times the input
        const int H = input.height, W = input.width;
        const float* u = ws.up.channel (0);
        for (int o = 0; o < 2; ++o)
        {
            const float* wk = net.finalWeights.data() + o * 16;
            const float* x = input.channel (o);
            float* dst = output + (size_t) o * (size_t) H * (size_t) W;

            for (int y = 0; y < H; ++y)
                for (int xx = 0; xx < W; ++xx)
                {
                    float acc = net.finalBias[o];
                    for (int ki = 0; ki < 4; ++ki)
                    {
                        const int yy = y + 2 * ki - 3;
                        if (yy < 0 || yy >= H)
                            continue;
                        const float* row = u + (size_t) yy * (size_t) W;
                        for (int kj = 0; kj < 4; ++kj)
                        {
                            const int xc = xx + 2 * kj - 3;
                            if (xc >= 0 && xc < W)
                                acc += wk[ki * 4 + kj] * row[xc];
                        }
                    }
                    const float mask = 1.0f / (1.0f + std::exp (-acc));
                    dst[(size_t) y * (size_t) W + (size_t) xx] = mask * x[(size_t) y * (size_t) W + (size_t) xx];
                }
        }
    }
}

//==============================================================================
void CompactAudio::assign (const juce::AudioBuffer<float>& audio)
{
    channels = audio.getNumChannels();
    length = audio.getNumSamples();
    float peak = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
        peak = juce::jmax (peak, audio.getMagnitude (ch, 0, length));
    scale = peak > 1.0e-9f ? peak / 32767.0f : 1.0f;

    data.resize ((size_t) channels * (size_t) length);
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* src = audio.getReadPointer (ch);
        for (int i = 0; i < length; ++i)
            data[(size_t) i * (size_t) channels + (size_t) ch] = (int16_t) juce::jlimit (-32767, 32767, (int) std::lround (src[i] / scale));
    }
}

void CompactAudio::addTo (juce::AudioBuffer<float>& dst, int dstStart, int srcStart, int numSamples, float gain) const
{
    if (gain <= 0.0f || channels == 0)
        return;
    const float g = gain * scale;
    const int from = juce::jmax (0, -srcStart), to = juce::jmin (numSamples, length - srcStart);
    for (int ch = 0; ch < dst.getNumChannels(); ++ch)
    {
        const int src = juce::jmin (ch, channels - 1);
        float* d = dst.getWritePointer (ch, dstStart);
        for (int i = from; i < to; ++i)
            d[i] += g * (float) data[(size_t) (srcStart + i) * (size_t) channels + (size_t) src];
    }
}

juce::AudioBuffer<float> SongStems::mix (int start, int numSamples, const std::array<float, numStems>& gains) const
{
    juce::AudioBuffer<float> out (channels, juce::jmax (0, numSamples));
    out.clear();
    for (int s = 0; s < numStems; ++s)
        stems[(size_t) s].addTo (out, 0, start, numSamples, gains[(size_t) s]);
    return out;
}

Separator::Separator() = default;
Separator::~Separator() = default;

juce::File Separator::findModelFile()
{
    const auto name = juce::String ("spleeter4.bin");
    const auto binary = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto dir = binary.getParentDirectory();

    const juce::File candidates[] = {
        dir.getSiblingFile ("Resources").getChildFile (name),                       // <bundle>/Contents/Resources (mac, Windows VST3)
        dir.getChildFile (name),                                                    // next to the standalone .exe
        dir.getChildFile ("Resources").getChildFile (name),
        juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
            .getChildFile ("TrapWin").getChildFile ("TrapWin Sampler").getChildFile (name),
       #if JUCE_MAC
        juce::File ("/Library/Application Support/TrapWin/TrapWin Sampler").getChildFile (name),
       #endif
    };

    for (const auto& f : candidates)
        if (f.existsAsFile())
            return f;

    return {};
}

bool Separator::load (const juce::File& modelFile)
{
    model.reset();

    juce::FileInputStream in (modelFile);
    if (! in.openedOk())
        return false;

    char magic[4] {};
    if (in.read (magic, 4) != 4 || std::memcmp (magic, "SPL4", 4) != 0 || in.readInt() != 1)
        return false;

    auto m = std::make_unique<Model>();
    std::vector<uint16_t> half;

    auto readHalf = [&] (std::vector<float>& dst, size_t count)
    {
        half.resize (count);
        if (in.read (half.data(), (int) (count * sizeof (uint16_t))) != (int) (count * sizeof (uint16_t)))
            return false;
        dst.resize (count);
        for (size_t i = 0; i < count; ++i)
            dst[i] = halfToFloat (juce::ByteOrder::swapIfBigEndian (half[i]));
        return true;
    };

    auto readFloats = [&] (std::vector<float>& dst, size_t count)
    {
        dst.resize (count);
        return in.read (dst.data(), (int) (count * sizeof (float))) == (int) (count * sizeof (float));
    };

    for (auto& net : m->nets)
    {
        for (int j = 0; j < 6; ++j)
        {
            auto& l = net.encoder[j];
            l.in = encoderChannels[j];
            l.out = encoderChannels[j + 1];
            if (! (readHalf (l.weights, (size_t) l.out * (size_t) l.in * taps)
                   && readFloats (l.bias, (size_t) l.out) && readFloats (l.scale, (size_t) l.out) && readFloats (l.shift, (size_t) l.out)))
                return false;
        }

        for (int j = 0; j < 6; ++j)
        {
            auto& l = net.decoder[j];
            l.in = decoderIn[j];
            l.out = decoderOut[j];
            if (! (readHalf (l.weights, (size_t) l.in * (size_t) l.out * taps)
                   && readFloats (l.bias, (size_t) l.out) && readFloats (l.scale, (size_t) l.out) && readFloats (l.shift, (size_t) l.out)))
                return false;
        }

        std::vector<float> finalBias;
        if (! (readHalf (net.finalWeights, 32) && readFloats (finalBias, 2)))
            return false;
        net.finalBias[0] = finalBias[0];
        net.finalBias[1] = finalBias[1];
    }

    model = std::move (m);
    return true;
}

bool Separator::isLoaded() const noexcept { return model != nullptr; }

void Separator::runNetwork (int stem, const float* input, float* output) const
{
    jassert (model != nullptr);
    Tensor in;
    in.resize (2, segmentFrames, modelBins);
    std::memcpy (in.data.data(), input, in.data.size() * sizeof (float));
    Workspace ws;
    runNet (model->nets[stem], in, output, ws);
}

//==============================================================================
bool Separator::separate (const juce::AudioBuffer<float>& audio, double sampleRate,
                          StemBuffers& out, const Progress& progress) const
{
    if (model == nullptr || audio.getNumSamples() == 0)
        return false;

    const int numIn = audio.getNumSamples(), inChannels = audio.getNumChannels();

    // ---- stereo at 44.1 kHz
    const double ratio = sampleRate / modelRate;
    const int N = (int) std::ceil ((double) numIn / ratio);
    juce::AudioBuffer<float> x (2, N);
    for (int ch = 0; ch < 2; ++ch)
    {
        const int src = juce::jmin (ch, inChannels - 1);
        if (std::abs (ratio - 1.0) < 1.0e-9)
            x.copyFrom (ch, 0, audio, src, 0, numIn);
        else
        {
            juce::Interpolators::Lagrange interpolator;
            interpolator.process (ratio, audio.getReadPointer (src), x.getWritePointer (ch), N, numIn, 0);
        }
    }

    // ---- framing: frame t covers [t*hop - fftSize/2, t*hop + fftSize/2)
    const int half = fftSize / 2, numBins = fftSize / 2 + 1;
    const int numFrames = N / hopSize + 2;
    const int numSegments = (numFrames + segmentFrames - 1) / segmentFrames;

    std::vector<float> window ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);

    juce::AudioBuffer<float> stemAudio[numStems];
    for (auto& s : stemAudio)
        s.setSize (2, N + fftSize, false, true, false);
    std::vector<float> windowSum ((size_t) (N + fftSize), 0.0f);

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> fftBuffer ((size_t) fftSize * 2);

    // complex spectra of one segment: [ch][frame][bin] interleaved re/im
    std::vector<float> spectrum ((size_t) 2 * segmentFrames * (size_t) numBins * 2);
    Tensor magnitudes;
    magnitudes.resize (2, segmentFrames, modelBins);
    std::vector<float> estimates[numStems];
    for (auto& e : estimates)
        e.resize ((size_t) 2 * segmentFrames * modelBins);
    Workspace workspaces[numStems];

    for (int seg = 0; seg < numSegments; ++seg)
    {
        const int firstFrame = seg * segmentFrames;
        const int frames = juce::jmin (segmentFrames, numFrames - firstFrame);

        // ---- forward STFT
        std::fill (magnitudes.data.begin(), magnitudes.data.end(), 0.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = x.getReadPointer (ch);
            for (int f = 0; f < frames; ++f)
            {
                const int start = (firstFrame + f) * hopSize - half;
                std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
                for (int i = 0; i < fftSize; ++i)
                {
                    const int idx = start + i;
                    if (idx >= 0 && idx < N)
                        fftBuffer[(size_t) i] = src[idx] * window[(size_t) i];
                }
                fft.performRealOnlyForwardTransform (fftBuffer.data(), true);

                float* spec = spectrum.data() + ((size_t) ch * segmentFrames + (size_t) f) * (size_t) numBins * 2;
                std::memcpy (spec, fftBuffer.data(), (size_t) numBins * 2 * sizeof (float));

                float* mag = magnitudes.channel (ch) + (size_t) f * modelBins;
                for (int b = 0; b < modelBins; ++b)
                    mag[b] = std::hypot (spec[2 * b], spec[2 * b + 1]);
            }
        }

        // ---- the four U-Nets in parallel
        {
            std::vector<std::thread> threads;
            for (int s = 1; s < numStems; ++s)
                threads.emplace_back ([&, s] { runNet (model->nets[s], magnitudes, estimates[s].data(), workspaces[s]); });
            runNet (model->nets[0], magnitudes, estimates[0].data(), workspaces[0]);
            for (auto& t : threads)
                t.join();
        }

        // ---- ratio masks, inverse STFT, overlap-add
        constexpr float eps = 1.0e-10f;
        constexpr int extensionBins = 128;
        std::vector<float> masks ((size_t) numStems * (size_t) numBins);

        for (int ch = 0; ch < 2; ++ch)
            for (int f = 0; f < frames; ++f)
            {
                const size_t row = ((size_t) ch * segmentFrames + (size_t) f) * modelBins;

                for (int b = 0; b < modelBins; ++b)
                {
                    float sum = eps;
                    for (int s = 0; s < numStems; ++s)
                        sum += estimates[s][row + (size_t) b] * estimates[s][row + (size_t) b];
                    for (int s = 0; s < numStems; ++s)
                        masks[(size_t) s * numBins + (size_t) b] = (estimates[s][row + (size_t) b] * estimates[s][row + (size_t) b] + eps / (float) numStems) / sum;
                }

                for (int s = 0; s < numStems; ++s)
                {
                    float* m = masks.data() + (size_t) s * numBins;
                    float avg = 0.0f;
                    for (int b = modelBins - extensionBins; b < modelBins; ++b)
                        avg += m[b];
                    avg /= (float) extensionBins;
                    for (int b = modelBins; b < numBins; ++b)
                        m[b] = avg;
                }

                const float* spec = spectrum.data() + ((size_t) ch * segmentFrames + (size_t) f) * (size_t) numBins * 2;
                const int start = (firstFrame + f) * hopSize - half;

                for (int s = 0; s < numStems; ++s)
                {
                    const float* m = masks.data() + (size_t) s * numBins;
                    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
                    for (int b = 0; b < numBins; ++b)
                    {
                        fftBuffer[(size_t) (2 * b)] = spec[2 * b] * m[b];
                        fftBuffer[(size_t) (2 * b + 1)] = spec[2 * b + 1] * m[b];
                    }
                    fft.performRealOnlyInverseTransform (fftBuffer.data());

                    float* dst = stemAudio[s].getWritePointer (ch);
                    for (int i = 0; i < fftSize; ++i)
                    {
                        const int idx = start + i;
                        if (idx >= 0 && idx < N + fftSize)
                            dst[idx] += fftBuffer[(size_t) i] * window[(size_t) i];
                    }
                }

                if (ch == 0)
                    for (int i = 0; i < fftSize; ++i)
                    {
                        const int idx = start + i;
                        if (idx >= 0 && idx < N + fftSize)
                            windowSum[(size_t) idx] += window[(size_t) i] * window[(size_t) i];
                    }
            }

        if (progress && ! progress ((float) (seg + 1) / (float) numSegments))
            return false;
    }

    // ---- normalise, back to the input rate / layout
    const double backRatio = modelRate / sampleRate;
    for (int s = 0; s < numStems; ++s)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = stemAudio[s].getWritePointer (ch);
            for (int i = 0; i < N; ++i)
                d[i] = windowSum[(size_t) i] > 1.0e-6f ? d[i] / windowSum[(size_t) i] : 0.0f;
        }

        out[(size_t) s].setSize (inChannels, numIn, false, true, false);
        for (int ch = 0; ch < inChannels; ++ch)
        {
            const int src = juce::jmin (ch, 1);
            if (inChannels == 1)
            {
                // mono input: average back to one channel
                juce::AudioBuffer<float> mono (1, N);
                mono.copyFrom (0, 0, stemAudio[s], 0, 0, N);
                mono.addFrom (0, 0, stemAudio[s], 1, 0, N);
                mono.applyGain (0.5f);
                stemAudio[s].copyFrom (0, 0, mono, 0, 0, N);
            }

            if (std::abs (backRatio - 1.0) < 1.0e-9)
                out[(size_t) s].copyFrom (ch, 0, stemAudio[s], src, 0, numIn);
            else
            {
                juce::Interpolators::Lagrange interpolator;
                interpolator.process (backRatio, stemAudio[s].getReadPointer (src), out[(size_t) s].getWritePointer (ch), numIn, N, 0);
            }
        }
    }

    return true;
}

} // namespace digga::stems
