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

        /** Concatenates the bars (crossfading non-adjacent joins), then
            appends what naturally follows the last bar as crossfade tail. */
        juce::AudioBuffer<float> gather (const std::vector<int>& seq, int& tailLength) const
        {
            const int channels = source.getNumChannels();
            const int xfade = juce::jmax (16, (int) (0.004 * sampleRate));

            int total = 0;
            for (int b : seq)
                total += bars[(size_t) b].end - bars[(size_t) b].start;

            const auto& lastBar = bars[(size_t) seq.back()];
            tailLength = juce::jmin ((int) (0.05 * sampleRate), (lastBar.end - lastBar.start) / 4);

            juce::AudioBuffer<float> out (channels, total + tailLength);
            out.clear();

            int pos = 0;
            for (size_t i = 0; i < seq.size(); ++i)
            {
                const auto& bar = bars[(size_t) seq[i]];
                const int len = bar.end - bar.start;
                for (int ch = 0; ch < channels; ++ch)
                    out.copyFrom (ch, pos, source, ch, bar.start, len);

                if (i > 0 && seq[i] != seq[i - 1] + 1)
                    blendContinuation (out, pos, bars[(size_t) seq[i - 1]].end, xfade);

                pos += len;
            }

            // tail: the audio that really follows the last bar, else the loop start
            const int after = lastBar.end;
            for (int ch = 0; ch < channels; ++ch)
            {
                if (after + tailLength <= source.getNumSamples())
                    out.copyFrom (ch, pos, source, ch, after, tailLength);
                else
                    out.copyFrom (ch, pos, source, ch, bars[(size_t) seq.front()].start, tailLength);
            }
            return out;
        }

    private:
        /** At a jump, fade the previous bar's continuation out under the new bar. */
        void blendContinuation (juce::AudioBuffer<float>& out, int joinPos, int continuationStart, int length) const
        {
            if (continuationStart + length > source.getNumSamples())
            {
                audio::fadeIn (out, joinPos, length);
                return;
            }

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
            {
                auto* d = out.getWritePointer (ch, joinPos);
                const auto* c = source.getReadPointer (ch, continuationStart);
                for (int i = 0; i < length; ++i)
                {
                    const float t = ((float) i + 0.5f) / (float) length;
                    d[i] = d[i] * std::sin (t * juce::MathConstants<float>::halfPi)
                         + c[i] * std::cos (t * juce::MathConstants<float>::halfPi);
                }
            }
        }

        const juce::AudioBuffer<float>& source;
        double sampleRate;
        std::vector<Bar> bars;
    };

    juce::AudioBuffer<float> render (const Planner& planner, const std::vector<int>& seq, int bars,
                                     double sampleRate, double sourceBpm, double projectBpm)
    {
        int tail = 0;
        const auto gathered = planner.gather (seq, tail);
        const int body = gathered.getNumSamples() - tail;

        const int target = barsToSamples (bars, projectBpm, sampleRate);
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

        audio::normalise (loop, 0.89f);   // -1 dBFS
        return loop;
    }
}

std::vector<GeneratedLoop> generate (const juce::AudioBuffer<float>& source, double sampleRate,
                                     const analysis::Analysis& analysis, double projectBpm)
{
    std::vector<GeneratedLoop> loops;
    if (source.getNumSamples() == 0 || analysis.bpm <= 0.0 || projectBpm <= 0.0)
        return loops;

    const Planner planner (source, sampleRate, analysis);

    const auto a1 = planner.best (8, {}, 0.0f);
    const auto a2 = planner.best (16, a1.bars, 0.15f);
    std::vector<int> used (a1.bars);
    used.insert (used.end(), a2.bars.begin(), a2.bars.end());
    const auto b1 = planner.best (8, used, 0.5f);
    used.insert (used.end(), b1.bars.begin(), b1.bars.end());
    const auto b2 = planner.best (16, used, 0.35f);

    const std::pair<const char*, const Candidate*> plan[] = { { "A1", &a1 }, { "A2", &a2 }, { "B1", &b1 }, { "B2", &b2 } };

    for (const auto& [slot, candidate] : plan)
    {
        GeneratedLoop loop;
        loop.slot = slot;
        loop.bars = (int) candidate->bars.size();
        loop.sourceBars = candidate->bars;
        loop.audio = render (planner, candidate->bars, loop.bars, sampleRate, analysis.bpm, projectBpm);
        loops.push_back (std::move (loop));
    }

    return loops;
}

} // namespace digga::loops
