#include "analysis/Structure.h"

#include "core/AudioTools.h"

namespace digga::analysis
{

namespace
{
    float normalised (float value, float maxValue) { return maxValue > 1.0e-9f ? juce::jlimit (0.0f, 1.0f, value / maxValue) : 0.0f; }

    /** Sum of an onset curve around each beat of the bar. */
    float onBeat (const std::vector<float>& curve, const OnsetEnvelope& env, double barStartSec, double beatSec)
    {
        float sum = 0.0f;
        for (int b = 0; b < 4; ++b)
            for (int half = 0; half < 2; ++half)
            {
                const double centre = env.secondsToFrame (barStartSec + (b + 0.5 * half) * beatSec);
                float best = 0.0f;
                for (int k = -2; k <= 2; ++k)
                    best = juce::jmax (best, sampleAt (curve, centre + k));
                sum += half == 0 ? best : 0.5f * best;
            }
        return sum;
    }

    float distance (const BarProfile& a, const BarProfile& b)
    {
        float d = 1.6f * std::abs (a.kick - b.kick) + 1.2f * std::abs (a.melody - b.melody)
                + 1.0f * std::abs (a.energy - b.energy) + 0.8f * std::abs (a.hats - b.hats)
                + 0.8f * std::abs (a.bass - b.bass);
        d += 1.2f * std::abs (a.vocals - b.vocals);
        d += 0.6f * (1.0f - cosineSimilarity (a.chroma.data(), b.chroma.data(), 12));
        return d;
    }
}

Structure analyseStructure (const juce::AudioBuffer<float>& source, double sampleRate,
                            const Analysis& a, const midi::Posteriors& notes,
                            const stems::SongStems* stemAudio)
{
    Structure s;
    if (a.bpm <= 0.0 || source.getNumSamples() == 0 || a.onsets == nullptr)
        return s;

    const auto mono = audio::toMono (source);
    const double barSec = a.barSeconds(), beatSec = barSec / 4.0;
    s.barSamples = barSec * sampleRate;
    const auto& env = *a.onsets;

    // high-band flux for hats: difference between full and low flux is mostly mids, so
    // estimate highs from the full flux minus low flux, weighted by bar-level centroid later
    for (int i = 0;; ++i)
    {
        const double startSec = a.downbeatSeconds + i * barSec;
        const int start = (int) std::lround (startSec * sampleRate);
        const int end = (int) std::lround ((startSec + barSec) * sampleRate);
        if (end > source.getNumSamples())
            break;

        BarProfile bar;
        bar.start = start;
        bar.end = end;

        const auto f = describe (mono.getReadPointer (0, start), end - start, sampleRate);
        bar.energy = f.rms;
        bar.bass = f.rms * f.lowRatio;
        bar.chroma = f.chroma;
        bar.kick = onBeat (env.lowFlux, env, startSec, beatSec);

        // hats: busy full-band flux in bright bars
        float flux = 0.0f;
        const int f0 = juce::jmax (0, (int) env.secondsToFrame (startSec)), f1 = (int) env.secondsToFrame (startSec + barSec);
        for (int k = f0; k < f1 && k < (int) env.flux.size(); ++k)
            flux += juce::jmax (0.0f, env.flux[(size_t) k] - env.lowFlux[(size_t) k]);
        bar.hats = flux * juce::jlimit (0.0f, 1.0f, f.centroid / 5000.0f);

        // melody: how much of the bar has confident pitched notes
        if (notes.frames > 0)
        {
            int active = 0, total = 0;
            for (int fr = 0; fr < notes.frames; ++fr)
            {
                const double t = midi::frameToSeconds (fr);
                if (t < startSec || t >= startSec + barSec)
                    continue;
                ++total;
                float best = 0.0f;
                // ignore the lowest octave (808 / kick fundamentals live there)
                for (int p = 12; p < 88; ++p)
                    best = juce::jmax (best, notes.note[(size_t) (fr * 88 + p)]);
                active += best > 0.35f ? 1 : 0;
            }
            bar.melody = total > 0 ? (float) active / (float) total : 0.0f;
        }
        if (stemAudio != nullptr)
            for (int st = 0; st < stems::numStems; ++st)
            {
                std::array<float, stems::numStems> solo {};
                solo[(size_t) st] = 1.0f;
                const auto b = stemAudio->mix (start, end - start, solo);
                float sum = 0.0f;
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    sum += b.getRMSLevel (ch, 0, b.getNumSamples());
                bar.stems[(size_t) st] = sum / (float) juce::jmax (1, b.getNumChannels());
            }
        s.bars.push_back (bar);
    }

    if (s.bars.empty())
        return s;

    // relative scales
    float maxE = 0, maxK = 0, maxH = 0, maxB = 0;
    for (const auto& b : s.bars)
    {
        maxE = juce::jmax (maxE, b.energy); maxK = juce::jmax (maxK, b.kick);
        maxH = juce::jmax (maxH, b.hats);   maxB = juce::jmax (maxB, b.bass);
    }
    if (stemAudio != nullptr)
    {
        // each stem relative to its loudest bar; a stem that never gets near the
        // mix level is leakage, not a part of the song
        float loudestMix = 0.0f;
        std::array<float, stems::numStems> loudest {};
        for (const auto& b : s.bars)
        {
            loudestMix = juce::jmax (loudestMix, b.energy);
            for (int st = 0; st < stems::numStems; ++st)
                loudest[(size_t) st] = juce::jmax (loudest[(size_t) st], b.stems[(size_t) st]);
        }
        for (auto& b : s.bars)
            for (int st = 0; st < stems::numStems; ++st)
                b.stems[(size_t) st] = loudest[(size_t) st] > 0.12f * loudestMix ? normalised (b.stems[(size_t) st], loudest[(size_t) st]) : 0.0f;
    }

    for (auto& b : s.bars)
    {
        b.energy = normalised (b.energy, maxE);
        b.kick = normalised (b.kick, maxK);
        b.hats = normalised (b.hats, maxH);
        b.bass = normalised (b.bass, maxB);
        if (stemAudio != nullptr)
        {
            b.kick = b.hats = b.stems[stems::drums];
            b.bass = b.stems[stems::bass];
            b.melody = b.stems[stems::other];
            b.vocals = b.stems[stems::vocals];
        }
    }

    const int n = (int) s.bars.size();
    // a kick needs real low end on the beat; pitched attacks alone also make low flux
    const bool fromStems = stemAudio != nullptr;
    auto hasDrums = [&] (int i)
    {
        const auto& b = s.bars[(size_t) i];
        if (fromStems)
            return b.stems[stems::drums] > 0.3f;
        return b.hats > 0.45f || (b.kick > 0.35f && b.bass > 0.12f);
    };
    auto hasMelody = [&] (int i) { return s.bars[(size_t) i].melody > (fromStems ? 0.3f : 0.25f); };
    auto hasVocals = [&] (int i) { return s.bars[(size_t) i].vocals > 0.3f; };

    // ---- boundaries: novelty between the two bars before and the two bars after
    std::vector<float> novelty ((size_t) n, 0.0f);
    for (int i = 1; i < n; ++i)
    {
        float d = 0.0f;
        int pairs = 0;
        for (int x = juce::jmax (0, i - 2); x < i; ++x)
            for (int y = i; y < juce::jmin (n, i + 2); ++y)
            {
                d += distance (s.bars[(size_t) x], s.bars[(size_t) y]);
                ++pairs;
            }
        novelty[(size_t) i] = pairs > 0 ? d / (float) pairs : 0.0f;
    }

    float mean = 0.0f;
    for (auto v : novelty) mean += v / (float) n;

    std::vector<int> starts { 0 };
    for (int i = 1; i < n; ++i)
    {
        const bool peak = novelty[(size_t) i] >= novelty[(size_t) i - 1] && (i + 1 >= n || novelty[(size_t) i] >= novelty[(size_t) i + 1]);
        const bool event = (hasDrums (i) && ! hasDrums (i - 1)) || (hasMelody (i) && ! hasMelody (i - 1))
                        || (hasVocals (i) && ! hasVocals (i - 1) && i + 1 < n && hasVocals (i + 1))
                        || (! hasDrums (i) && hasDrums (i - 1) && i + 1 < n && ! hasDrums (i + 1));
        if ((peak && novelty[(size_t) i] > mean * 1.6f) || (event && novelty[(size_t) i] > mean * 1.1f))
            if (i - starts.back() >= 2)   // sections are at least two bars
                starts.push_back (i);
    }

    // ---- name each section by what changed at its start and what plays in it
    for (size_t k = 0; k < starts.size(); ++k)
    {
        Section sec;
        sec.startBar = starts[k];
        sec.numBars = (k + 1 < starts.size() ? starts[k + 1] : n) - sec.startBar;

        int drumBars = 0, melodyBars = 0, vocalBars = 0;
        float energy = 0.0f;
        for (int i = sec.startBar; i < sec.startBar + sec.numBars; ++i)
        {
            drumBars += hasDrums (i) ? 1 : 0;
            melodyBars += hasMelody (i) ? 1 : 0;
            vocalBars += hasVocals (i) ? 1 : 0;
            energy += s.bars[(size_t) i].energy;
        }
        energy /= (float) sec.numBars;
        sec.drums = drumBars * 2 > sec.numBars;
        sec.melodic = melodyBars * 2 > sec.numBars;
        sec.vocal = vocalBars * 2 > sec.numBars;

        const bool first = k == 0, last = k + 1 == starts.size();
        const bool prevDrums = ! first && s.sections.back().drums;
        const bool prevMelodic = ! first && s.sections.back().melodic;

        if (first && ! (sec.drums && (sec.melodic || sec.vocal)))
            sec.name = "Intro";
        else if (last && ! first && energy < 0.5f)
            sec.name = "Outro";
        else if (sec.drums && ! prevDrums && energy > 0.6f && (sec.melodic || sec.vocal))
            sec.name = "Drop";
        else if (sec.vocal && sec.drums)
            sec.name = energy > 0.75f ? "Hook" : "Verse";
        else if (sec.drums && ! prevDrums)
            sec.name = "Kick In";
        else if (! sec.drums && prevDrums)
            sec.name = sec.melodic ? "Break" : "Breakdown";
        else if (sec.melodic && ! prevMelodic)
            sec.name = "Melody";
        else if (sec.drums && sec.melodic)
            sec.name = energy > 0.75f ? "Hook" : "Full";
        else if (sec.drums)
            sec.name = k + 1 == starts.size() ? "Outro" : "Drums";
        else
            sec.name = sec.melodic ? "Melody" : "Ambient";

        s.sections.push_back (sec);
    }

    return s;
}

} // namespace digga::analysis
