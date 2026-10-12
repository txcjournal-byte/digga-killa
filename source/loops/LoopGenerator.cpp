#include "loops/LoopGenerator.h"

#include "dsp/Stretcher.h"

namespace digga::loops
{

namespace
{
    constexpr int featureSize = 15;

    struct Bar
    {
        int start = 0, end = 0;
        analysis::SpectralFeatures features;
        std::array<float, featureSize> vector {};
        float energy = 0.0f;   // rms relative to loudest bar
    };

    struct Candidate
    {
        std::vector<int> bars;
        float score = 0.0f;
    };

    class Planner
    {
    public:
        Planner (const juce::AudioBuffer<float>& src, double sr, const analysis::Analysis& a)
            : source (src), sampleRate (sr)
        {
            const auto mono = audio::toMono (source);
            const double barLength = a.barSeconds() * sampleRate;
            const double first = a.downbeatSeconds * sampleRate;
            const int length = source.getNumSamples();

            for (int i = 0;; ++i)
            {
                const int start = (int) std::lround (first + i * barLength);
                const int end = (int) std::lround (first + (i + 1) * barLength);
                if (end > length)
                    break;
                bars.push_back (Bar { start, end, {}, {}, 0.0f });
            }

            if (bars.empty())   // shorter than a bar: use what there is
                bars.push_back (Bar { 0, juce::jmax (1, length), {}, {}, 0.0f });

            float loudest = 1.0e-6f;
            for (auto& bar : bars)
            {
                bar.features = analysis::describe (mono.getReadPointer (0, bar.start), bar.end - bar.start, sampleRate);
                loudest = juce::jmax (loudest, bar.features.rms);
            }

            for (auto& bar : bars)
            {
                const auto& f = bar.features;
                bar.energy = f.rms / loudest;
                for (int k = 0; k < 12; ++k)
                    bar.vector[(size_t) k] = f.chroma[(size_t) k];
                bar.vector[12] = bar.energy * 0.5f;
                bar.vector[13] = juce::jlimit (0.0f, 1.0f, f.centroid / 6000.0f) * 0.5f;
                bar.vector[14] = f.lowRatio * 0.5f;
            }
        }

        int numBars() const noexcept { return (int) bars.size(); }

        float similarity (int a, int b) const
        {
            if (! juce::isPositiveAndBelow (a, numBars()) || ! juce::isPositiveAndBelow (b, numBars()))
                return -1.0f;
            return analysis::cosineSimilarity (bars[(size_t) a].vector.data(), bars[(size_t) b].vector.data(), featureSize);
        }

        /** How naturally playback can jump from after `last` back to `first`. */
        float transition (int last, int first) const
        {
            if (last + 1 == first)
                return 1.0f;
            // the bar that would really follow `last` should sound like `first`,
            // and `last` should sound like the bar before `first`
            float total = 0.0f;
            int count = 0;
            if (const auto s = similarity (last + 1, first); s >= 0.0f) { total += s; ++count; }
            if (const auto s = similarity (last, first - 1); s >= 0.0f) { total += s; ++count; }
            if (count == 0)
                return juce::jmax (0.0f, similarity (last, first)) * 0.85f;
            return total / (float) count;
        }

        float sectionScore (const std::vector<int>& seq) const
        {
            float energy = 0, melodic = 0, silentPenalty = 0, mean = 0;
            for (int b : seq)
            {
                const auto& bar = bars[(size_t) b];
                energy += bar.energy;
                melodic += bar.features.tonalness * (1.0f - bar.features.flatness);
                if (bar.energy < 0.12f)
                    silentPenalty += 0.5f;
                mean += bar.energy;
            }
            const float n = (float) seq.size();
            energy /= n; melodic /= n; mean /= n;

            float variance = 0;
            for (int b : seq)
                variance += juce::square (bars[(size_t) b].energy - mean);
            const float consistency = 1.0f - juce::jmin (1.0f, std::sqrt (variance / n) * 2.5f);

            float joins = 0.0f;
            for (size_t i = 1; i < seq.size(); ++i)
                joins += transition (seq[i - 1], seq[i]);
            joins /= juce::jmax (1.0f, n - 1.0f);

            const float loopability = transition (seq.back(), seq.front());
            float phrase = 0.0f;
            if (seq.front() % 4 == 0) phrase += 0.06f;
            if (seq.front() % 8 == 0) phrase += 0.04f;

            return 0.25f * energy + 0.2f * melodic + 0.35f * loopability + 0.1f * consistency
                 + 0.1f * joins + phrase - silentPenalty / n;
        }

        /** Best bar sequence of `length` bars; `avoid` bars cost `overlapCost` each. */
        Candidate best (int length, const std::vector<int>& avoid, float overlapCost) const
        {
            auto overlap = [&avoid] (const std::vector<int>& seq)
            {
                int shared = 0;
                for (int b : seq)
                    if (std::find (avoid.begin(), avoid.end(), b) != avoid.end())
                        ++shared;
                return (float) shared / (float) seq.size();
            };

            Candidate result;
            result.score = -1.0e9f;

            // contiguous run of `run` bars, tiled with the best-fitting runs
            int run = 1;
            while (run * 2 <= juce::jmin (length, numBars()))
                run *= 2;

            std::vector<std::vector<int>> runs;
            for (int s = 0; s + run <= numBars(); ++s)
            {
                std::vector<int> r;
                for (int i = 0; i < run; ++i)
                    r.push_back (s + i);
                runs.push_back (std::move (r));
            }

            for (const auto& firstRun : runs)
            {
                std::vector<int> seq (firstRun);
                while ((int) seq.size() < length)
                {
                    // append the run that continues best (may repeat)
                    const std::vector<int>* bestNext = &firstRun;
                    float bestJoin = -1.0e9f;
                    for (const auto& next : runs)
                    {
                        const float join = transition (seq.back(), next.front())
                                         + 0.5f * transition (next.back(), seq.front())
                                         + (next.front() == seq.back() + 1 ? 0.3f : 0.0f);
                        if (join > bestJoin)
                        {
                            bestJoin = join;
                            bestNext = &next;
                        }
                    }
                    seq.insert (seq.end(), bestNext->begin(), bestNext->end());
                }
                seq.resize ((size_t) length);

                const float score = sectionScore (seq) - overlapCost * overlap (seq);
                if (score > result.score)
                    result = { seq, score };
            }
            return result;
        }

        std::pair<int, int> range (int bar) const { return { bars[(size_t) bar].start, bars[(size_t) bar].end }; }

    private:
        const juce::AudioBuffer<float>& source;
        double sampleRate;
        std::vector<Bar> bars;
    };

    /** At a jump, fade the previous bar's continuation out under the new bar. */
    void blendContinuation (juce::AudioBuffer<float>& out, int joinPos, const juce::AudioBuffer<float>& region,
                            int continuationStart, int length)
    {
        if (continuationStart < 0 || continuationStart + length > region.getNumSamples())
        {
            audio::fadeIn (out, joinPos, length);
            return;
        }

        for (int ch = 0; ch < out.getNumChannels(); ++ch)
        {
            auto* d = out.getWritePointer (ch, joinPos);
            const auto* c = region.getReadPointer (juce::jmin (ch, region.getNumChannels() - 1), continuationStart);
            for (int i = 0; i < length; ++i)
            {
                const float t = ((float) i + 0.5f) / (float) length;
                d[i] = d[i] * std::sin (t * juce::MathConstants<float>::halfPi)
                     + c[i] * std::cos (t * juce::MathConstants<float>::halfPi);
            }
        }
    }

    /** Concatenates the bars (crossfading non-adjacent joins), then appends
        what naturally follows the last bar as crossfade tail. */
    juce::AudioBuffer<float> gather (const LoopPlan& plan, const RegionSource& source, int sourceLength,
                                     double sampleRate, int& tailLength)
    {
        const int xfade = juce::jmax (16, (int) (0.004 * sampleRate));
        const auto& last = plan.barRanges.back();
        tailLength = juce::jmin ((int) (0.05 * sampleRate), (last.second - last.first) / 4);

        // one read covering every bar, continuation and tail
        int lo = sourceLength, hi = 0, total = 0;
        for (const auto& [start, end] : plan.barRanges)
        {
            lo = juce::jmin (lo, start);
            hi = juce::jmax (hi, end + juce::jmax (xfade, tailLength));
            total += end - start;
        }
        hi = juce::jmin (hi, sourceLength);
        const auto region = source (lo, hi - lo);
        const int channels = region.getNumChannels();

        juce::AudioBuffer<float> out (channels, total + tailLength);
        out.clear();

        int pos = 0;
        for (size_t i = 0; i < plan.barRanges.size(); ++i)
        {
            const auto [start, end] = plan.barRanges[i];
            for (int ch = 0; ch < channels; ++ch)
                out.copyFrom (ch, pos, region, ch, start - lo, end - start);

            if (i > 0 && plan.barRanges[i - 1].second != start)
                blendContinuation (out, pos, region, plan.barRanges[i - 1].second - lo, xfade);

            pos += end - start;
        }

        // tail: the audio that really follows the last bar, else the loop start
        const int after = last.second;
        const int tailFrom = after + tailLength <= hi ? after - lo : plan.barRanges.front().first - lo;
        for (int ch = 0; ch < channels; ++ch)
            out.copyFrom (ch, pos, region, ch, tailFrom, tailLength);
        return out;
    }
}

juce::AudioBuffer<float> renderLoop (const LoopPlan& plan, const RegionSource& source, int sourceLength,
                                     double sampleRate, double sourceBpm, double projectBpm)
{
    int tail = 0;
    const auto gathered = gather (plan, source, sourceLength, sampleRate, tail);
    const int body = gathered.getNumSamples() - tail;

    const int target = barsToSamples (plan.bars, projectBpm, sampleRate);
    const double ratio = (double) target / (double) juce::jmax (1, body);
    const int tailOut = juce::jmax (1, (int) std::lround (tail * ratio));

    auto stretched = juce::approximatelyEqual (sourceBpm, projectBpm) && body == target
                         ? gathered
                         : dsp::stretch (gathered, target + tailOut, 0.0, sampleRate);

    // seamless loop: the tail (what follows the end) fades into the start
    const int xfade = juce::jmin (tailOut, (int) (0.02 * sampleRate));
    juce::AudioBuffer<float> loop (stretched.getNumChannels(), target);
    for (int ch = 0; ch < loop.getNumChannels(); ++ch)
        loop.copyFrom (ch, 0, stretched, ch, 0, target);

    for (int ch = 0; ch < loop.getNumChannels(); ++ch)
    {
        auto* d = loop.getWritePointer (ch);
        const auto* t = stretched.getReadPointer (ch, target);
        for (int i = 0; i < xfade; ++i)
        {
            const float x = ((float) i + 0.5f) / (float) xfade;
            d[i] = d[i] * std::sin (x * juce::MathConstants<float>::halfPi)
                 + t[i] * std::cos (x * juce::MathConstants<float>::halfPi);
        }
    }

    if (audio::peak (loop) > 1.0e-4f)
        audio::normalise (loop, 0.89f);   // -1 dBFS
    return loop;
}

juce::AudioBuffer<float> renderLoop (const LoopPlan& plan, const juce::AudioBuffer<float>& source,
                                     double sampleRate, double sourceBpm, double projectBpm)
{
    return renderLoop (plan, [&source] (int start, int length)
    {
        juce::AudioBuffer<float> region (source.getNumChannels(), length);
        for (int ch = 0; ch < source.getNumChannels(); ++ch)
            region.copyFrom (ch, 0, source, ch, start, length);
        return region;
    }, source.getNumSamples(), sampleRate, sourceBpm, projectBpm);
}

std::vector<LoopPlan> planSectionLoops (const juce::AudioBuffer<float>& source, double sampleRate,
                                        const analysis::Analysis& analysis,
                                        const analysis::Structure& structure)
{
    std::vector<LoopPlan> loops;
    if (source.getNumSamples() == 0 || analysis.bpm <= 0.0)
        return loops;

    const Planner planner (source, sampleRate, analysis);
    const int numBars = planner.numBars();
    constexpr size_t maxLoops = 16;

    struct Plan { juce::String name; int start; int bars; };
    std::vector<Plan> plans;

    // does this stretch of bars sound like a loop we already have (repeated chorus)?
    auto isRepeat = [&] (int start, int bars)
    {
        for (const auto& p : plans)
        {
            if (p.bars != bars)
                continue;
            float sim = 0.0f;
            for (int i = 0; i < bars; ++i)
                sim += planner.similarity (p.start + i, start + i);
            if (sim / (float) bars > 0.985f)
                return true;
        }
        return false;
    };

    for (const auto& section : structure.sections)
    {
        int bar = section.startBar;
        const int end = section.startBar + section.numBars;
        bool first = true;

        while (plans.size() < maxLoops)
        {
            const int left = end - bar;
            int length = left >= 16 ? 16 : 8;
            if (! first && left < 8)
                break;                      // the tail of a section: covered by the next start
            if (bar + length > numBars)
                length = 8;
            if (bar + length > numBars)
                break;

            if (! isRepeat (bar, length))
                plans.push_back ({ section.name, bar, length });

            first = false;
            bar += length;
            if (bar >= end)
                break;
        }
    }

    auto makePlan = [&] (const juce::String& name, const std::vector<int>& seq, int length)
    {
        LoopPlan plan;
        plan.name = name;
        plan.bars = length;
        plan.sourceBars = seq;
        plan.startBar = seq.empty() ? 0 : seq.front();
        plan.sourceSeconds = analysis.downbeatSeconds + plan.startBar * analysis.barSeconds();
        for (int b : seq)
            plan.barRanges.push_back (planner.range (b));
        return plan;
    };

    // short material (a loop, not a song): fall back to the best-sounding phrase
    if (plans.empty())
    {
        for (int length : { 8, 16 })
        {
            const auto best = planner.best (length, {}, 0.0f);
            if (! best.bars.empty())
                loops.push_back (makePlan ("Loop", best.bars, length));
        }
        return loops;
    }

    for (const auto& p : plans)
    {
        std::vector<int> seq;
        for (int i = 0; i < p.bars; ++i)
            seq.push_back (p.start + i);
        loops.push_back (makePlan (p.name, seq, p.bars));
    }

    return loops;
}

} // namespace digga::loops
