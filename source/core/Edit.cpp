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
        && reverse == o.reverse && juce::approximatelyEqual (loopStart, o.loopStart) && fx == o.fx
        && std::equal (stems.begin(), stems.end(), o.stems.begin(), [] (float a, float b) { return juce::approximatelyEqual (a, b); });
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
    if (loopStart > 0.0)
        t.setProperty ("loopStart", loopStart, nullptr);
    if (! fx.isDefault())
        t.appendChild (fx.toValueTree(), nullptr);
    if (hasStemMix())
        t.setProperty ("stems", juce::String (stems[0]) + " " + juce::String (stems[1]) + " "
                                + juce::String (stems[2]) + " " + juce::String (stems[3]), nullptr);
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
    e.loopStart = juce::jlimit (0.0, 0.999, (double) t.getProperty ("loopStart", 0.0));
    e.fx = fx::FxSettings::fromValueTree (t.getChildWithName ("Fx"));
    const auto mix = juce::StringArray::fromTokens (t["stems"].toString(), " ", "");
    if (mix.size() == 4)
        for (int i = 0; i < 4; ++i)
            e.stems[(size_t) i] = juce::jlimit (0.0f, 1.0f, mix[i].getFloatValue());
    return e;
}

juce::AudioBuffer<float> renderEdit (const juce::AudioBuffer<float>& original, const EditSettings& edit, double sampleRate,
                                     double bpm, bool isLoop, bool withFx)
{
    const int n = original.getNumSamples();
    const int from = juce::jlimit (0, n, (int) std::lround (edit.start * n));
    const int to = juce::jlimit (from + 1, juce::jmax (from + 1, n), (int) std::lround (edit.end * n));
    const int len = juce::jmax (1, juce::jmin (to, n) - from);

    juce::AudioBuffer<float> out (original.getNumChannels(), len);
    for (int ch = 0; ch < original.getNumChannels(); ++ch)
        out.copyFrom (ch, 0, original, ch, from, len);

    // loop start: play from that point and wrap around
    if (edit.loopStart > 0.0 && len > 1)
    {
        const int shift = juce::jlimit (0, len - 1, (int) std::lround (edit.loopStart * len));
        if (shift > 0)
        {
            juce::AudioBuffer<float> rotated (out.getNumChannels(), len);
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
            {
                rotated.copyFrom (ch, 0, out, ch, shift, len - shift);
                rotated.copyFrom (ch, len - shift, out, ch, 0, shift);
            }
            out = std::move (rotated);   // the old loop seam is seamless, so the wrap is too
        }
    }

    if (std::abs (edit.pitch) >= 0.01f)
        out = dsp::pitchShift (out, edit.pitch, sampleRate);

    if (edit.reverse)
        out.reverse (0, out.getNumSamples());

    if (withFx && edit.fx.anyOn())
        fx::process (out, edit.fx, sampleRate, bpm > 0.0 ? bpm : 120.0, isLoop);

    audio::fadeIn (out, 0, (int) (edit.fadeInMs * 0.001 * sampleRate));
    audio::fadeOut (out, out.getNumSamples(), (int) (edit.fadeOutMs * 0.001 * sampleRate));

    if (std::abs (edit.gainDb) > 0.01f)
        out.applyGain (juce::Decibels::decibelsToGain (edit.gainDb));

    return out;
}

} // namespace digga
