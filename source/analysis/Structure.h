#pragma once

#include "analysis/Analyzer.h"
#include "midi/BasicPitch.h"
#include "separation/Stems.h"

#include <vector>

namespace digga::analysis
{

/** What is playing in one bar of the source. All values 0..1 relative to
    the loudest / busiest bar of the song. */
struct BarProfile
{
    int start = 0, end = 0;     // samples
    float energy = 0.0f;        // overall level
    float kick = 0.0f;          // low-end hits on the beat grid
    float hats = 0.0f;          // high-frequency hits
    float bass = 0.0f;          // sustained low end (808 / bass)
    float melody = 0.0f;        // pitched notes (Basic Pitch) / melody stem
    float vocals = 0.0f;        // vocal stem
    std::array<float, stems::numStems> stems {};   // level of each stem (0 when the song has none)
    std::array<float, 12> chroma {};
};

/** A musical section: starts where something actually changes (kick comes in,
    the melody starts, the drop hits, the beat breaks down). */
struct Section
{
    int startBar = 0, numBars = 0;
    juce::String name;          // "Intro", "Kick In", "Melody", "Verse", "Hook", "Drop", "Break", "Outro" ...
    bool drums = false, melodic = false, vocal = false;
};

struct Structure
{
    std::vector<BarProfile> bars;
    std::vector<Section> sections;
    double barSamples = 0.0;
};

/** Bar-by-bar profile of the song plus its sections. `notes` are the Basic
    Pitch posteriors of the same audio (melody detection). With `stemAudio`
    the profile is read straight from the separated stems (what plays is
    then exact, not guessed from the mix). */
Structure analyseStructure (const juce::AudioBuffer<float>& source, double sampleRate,
                            const Analysis& analysis, const midi::Posteriors& notes,
                            const stems::SongStems* stemAudio = nullptr);

} // namespace digga::analysis
