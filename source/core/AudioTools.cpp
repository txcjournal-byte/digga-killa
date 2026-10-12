#include "core/AudioTools.h"

#include <map>

namespace digga::audio
{

std::vector<float> computePeaks (const juce::AudioBuffer<float>& buffer, int numPoints)
{
    std::vector<float> peaks ((size_t) juce::jmax (1, numPoints), 0.0f);
    const int n = buffer.getNumSamples();
    if (n == 0)
        return peaks;

    for (int p = 0; p < numPoints; ++p)
    {
        const int start = (int) ((juce::int64) p * n / numPoints);
        const int end = juce::jmax (start + 1, (int) ((juce::int64) (p + 1) * n / numPoints));
        float m = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (buffer.getReadPointer (ch, start), end - start);
            m = juce::jmax (m, std::abs (range.getStart()), std::abs (range.getEnd()));
        }
        peaks[(size_t) p] = m;
    }

    const float maxPeak = *std::max_element (peaks.begin(), peaks.end());
    if (maxPeak > 0.0f)
        for (auto& v : peaks)
            v = std::pow (v / maxPeak, 0.75f);

    return peaks;
}

juce::AudioBuffer<float> toMono (const juce::AudioBuffer<float>& buffer)
{
    juce::AudioBuffer<float> mono (1, buffer.getNumSamples());
    mono.clear();
    const int channels = buffer.getNumChannels();
    for (int ch = 0; ch < channels; ++ch)
        mono.addFrom (0, 0, buffer, ch, 0, buffer.getNumSamples(), 1.0f / (float) channels);
    return mono;
}

void fadeIn (juce::AudioBuffer<float>& buffer, int start, int length)
{
    length = juce::jmin (length, buffer.getNumSamples() - start);
    if (length > 0)
        buffer.applyGainRamp (start, length, 0.0f, 1.0f);
}

void fadeOut (juce::AudioBuffer<float>& buffer, int endExclusive, int length)
{
    endExclusive = juce::jmin (endExclusive, buffer.getNumSamples());
    length = juce::jmin (length, endExclusive);
    if (length > 0)
        buffer.applyGainRamp (endExclusive - length, length, 1.0f, 0.0f);
}

float peak (const juce::AudioBuffer<float>& buffer)
{
    return buffer.getNumSamples() > 0 ? buffer.getMagnitude (0, buffer.getNumSamples()) : 0.0f;
}

void normalise (juce::AudioBuffer<float>& buffer, float targetPeak)
{
    const float p = peak (buffer);
    if (p > 1.0e-5f)
        buffer.applyGain (targetPeak / p);
}

void crossfadeInto (juce::AudioBuffer<float>& dest, int destStart,
                    const juce::AudioBuffer<float>& incoming, int incomingStart, int length)
{
    length = juce::jmin (length, dest.getNumSamples() - destStart, incoming.getNumSamples() - incomingStart);
    const int channels = juce::jmin (dest.getNumChannels(), incoming.getNumChannels());

    for (int i = 0; i < length; ++i)
    {
        const float t = ((float) i + 0.5f) / (float) length;
        const float gIn = std::sin (t * juce::MathConstants<float>::halfPi);
        const float gOut = std::cos (t * juce::MathConstants<float>::halfPi);
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* d = dest.getWritePointer (ch);
            d[destStart + i] = d[destStart + i] * gOut + incoming.getSample (ch, incomingStart + i) * gIn;
        }
    }
}

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source, int newLength)
{
    juce::AudioBuffer<float> out (source.getNumChannels(), juce::jmax (1, newLength));
    const int n = source.getNumSamples();
    const double step = n > 1 && newLength > 1 ? (double) (n - 1) / (double) (newLength - 1) : 0.0;

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        const float* in = source.getReadPointer (ch);
        float* o = out.getWritePointer (ch);
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const double pos = (double) i * step;
            const int i0 = juce::jmin ((int) pos, n - 1);
            const int i1 = juce::jmin (i0 + 1, n - 1);
            const float frac = (float) (pos - (double) i0);
            o[i] = n > 0 ? in[i0] + (in[i1] - in[i0]) * frac : 0.0f;
        }
    }
    return out;
}

juce::String formatDuration (double seconds)
{
    const int total = juce::jmax (1, juce::roundToInt (seconds));
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}


std::vector<int> detectChops (const juce::AudioBuffer<float>& buffer, double sampleRate, double bpm, int maxChops)
{
    const int length = buffer.getNumSamples();
    std::vector<int> chops { 0 };
    if (length <= 0 || sampleRate <= 0.0 || buffer.getNumChannels() == 0)
        return chops;

    // energy envelope, ~5.8 ms hops at 44.1 kHz
    const int hop = juce::jmax (64, (int) (sampleRate * 0.0058));
    const int frames = length / hop;
    if (frames < 4)
        return chops;

    std::vector<float> db ((size_t) frames);
    const int channels = buffer.getNumChannels();
    for (int f = 0; f < frames; ++f)
    {
        double sum = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* x = buffer.getReadPointer (ch) + f * hop;
            for (int i = 0; i < hop; ++i)
                sum += (double) x[i] * x[i];
        }
        db[(size_t) f] = juce::Decibels::gainToDecibels ((float) std::sqrt (sum / (hop * channels)), -100.0f);
    }

    float loudest = -100.0f;
    for (auto v : db)
        loudest = juce::jmax (loudest, v);
    const float floor = loudest - 30.0f;

    // onset strength: rise against the quietest of the previous ~35 ms
    struct Candidate { int sample; float strength; };
    std::vector<Candidate> candidates;
    const int look = 6;
    for (int f = 1; f < frames; ++f)
    {
        float before = db[(size_t) f - 1];
        for (int k = 2; k <= look && f - k >= 0; ++k)
            before = juce::jmin (before, db[(size_t) (f - k)]);
        const float rise = db[(size_t) f] - before;
        const bool peak = f + 1 >= frames || db[(size_t) f] >= db[(size_t) f + 1];
        if (rise > 6.0f && db[(size_t) f] > floor && peak)
            candidates.push_back ({ f * hop, rise });
    }

    // snap to the 1/16 grid and keep the strongest hit per grid step
    const double step = bpm > 0.0 ? sampleRate * 60.0 / bpm / 4.0 : sampleRate * 0.1;
    std::map<int, float> bySlot;
    for (const auto& c : candidates)
    {
        const int slot = (int) std::round (c.sample / step);
        auto& best = bySlot[slot];
        best = juce::jmax (best, c.strength);
    }

    // too many: keep only the strongest, then re-sort in time
    std::vector<std::pair<int, float>> slots (bySlot.begin(), bySlot.end());
    if ((int) slots.size() > maxChops - 1)
    {
        std::sort (slots.begin(), slots.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
        slots.resize ((size_t) (maxChops - 1));
        std::sort (slots.begin(), slots.end());
    }

    for (const auto& s : slots)
    {
        const int position = (int) std::round (s.first * step);
        if (position > 0 && position < length - hop && position > chops.back() + hop)
            chops.push_back (position);
    }
    return chops;
}

} // namespace digga::audio
