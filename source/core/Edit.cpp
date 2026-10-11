#include "core/Edit.h"

#include "core/AudioTools.h"
#include "dsp/Stretcher.h"

namespace digga
{

bool EditSettings::isDefault() const noexcept
{
    return *this == EditSettings {};
}

bool EditSettings::operator== (const EditSettings& o) const noexcept
{
    return juce::approximatelyEqual (start, o.start) && juce::approximatelyEqual (end, o.end)
        && juce::approximatelyEqual (fadeInMs, o.fadeInMs) && juce::approximatelyEqual (fadeOutMs, o.fadeOutMs)
        && juce::approximatelyEqual (gainDb, o.gainDb) && juce::approximatelyEqual (pitch, o.pitch)
        && reverse == o.reverse;
}

juce::ValueTree EditSettings::toValueTree (const juce::String& key) const
{
    juce::ValueTree t ("Edit");
    t.setProperty ("key", key, nullptr);
    t.setProperty ("start", start, nullptr);
    t.setProperty ("end", end, nullptr);
    t.setProperty ("fadeIn", fadeInMs, nullptr);
    t.setProperty ("fadeOut", fadeOutMs, nullptr);
    t.setProperty ("gain", gainDb, nullptr);
    t.setProperty ("pitch", pitch, nullptr);
    t.setProperty ("reverse", reverse, nullptr);
    return t;
}

EditSettings EditSettings::fromValueTree (const juce::ValueTree& t)
{
    EditSettings e;
    e.start = juce::jlimit (0.0, 1.0, (double) t.getProperty ("start", 0.0));
    e.end = juce::jlimit (e.start, 1.0, (double) t.getProperty ("end", 1.0));
    e.fadeInMs = (float) (double) t.getProperty ("fadeIn", 0.0);
    e.fadeOutMs = (float) (double) t.getProperty ("fadeOut", 0.0);
    e.gainDb = (float) (double) t.getProperty ("gain", 0.0);
    e.pitch = (float) (double) t.getProperty ("pitch", 0.0);
    e.reverse = (bool) t.getProperty ("reverse", false);
    return e;
}

juce::AudioBuffer<float> renderEdit (const juce::AudioBuffer<float>& original, const EditSettings& edit, double sampleRate)
{
    const int n = original.getNumSamples();
    const int from = juce::jlimit (0, n, (int) std::lround (edit.start * n));
    const int to = juce::jlimit (from + 1, juce::jmax (from + 1, n), (int) std::lround (edit.end * n));
    const int len = juce::jmax (1, juce::jmin (to, n) - from);

    juce::AudioBuffer<float> out (original.getNumChannels(), len);
    for (int ch = 0; ch < original.getNumChannels(); ++ch)
        out.copyFrom (ch, 0, original, ch, from, len);

    if (std::abs (edit.pitch) >= 0.01f)
        out = dsp::pitchShift (out, edit.pitch, sampleRate);

    if (edit.reverse)
        out.reverse (0, out.getNumSamples());

    audio::fadeIn (out, 0, (int) (edit.fadeInMs * 0.001 * sampleRate));
    audio::fadeOut (out, out.getNumSamples(), (int) (edit.fadeOutMs * 0.001 * sampleRate));

    if (std::abs (edit.gainDb) > 0.01f)
        out.applyGain (juce::Decibels::decibelsToGain (edit.gainDb));

    return out;
}

} // namespace digga
