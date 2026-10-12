#pragma once

#include "core/AudioTools.h"
#include "midi/BasicPitch.h"

#include <vector>

namespace digga::oneshots
{

struct Shot
{
    juce::String name;          // "Kick 1", "808 F#1", "Note C#5", "Chord F#m"
    juce::String category;      // "Kick", "808", "Snare", "Hat", "Open Hat", "Perc", "Chord", "Note", "Vocal"
    double sourceSeconds = 0.0; // where it was found
    juce::AudioBuffer<float> audio;
};

/** Finds usable one-shots in a song, never at random:
    - drum hits are classified (kick, 808, snare, hat, open hat, perc) and the
      cleanest ones are kept: quiet before the attack, nothing else hitting
      inside the sound, full decay;
    - melodic notes and chords come from the Basic Pitch transcription: a
      note that rings on its own (or a chord struck together) with little
      drum activity around it, cut from its attack to the end of its release.
    Results are normalised, faded and grouped by category. */
std::vector<Shot> extract (const juce::AudioBuffer<float>& source, double sampleRate,
                           const std::vector<midi::Note>& notes);

/** The separated song (all buffers the length and rate of the mix). */
struct StemSet
{
    const juce::AudioBuffer<float>* drums = nullptr;
    const juce::AudioBuffer<float>* bass = nullptr;
    const juce::AudioBuffer<float>* melody = nullptr;
    const juce::AudioBuffer<float>* vocals = nullptr;
    bool hasDrums = true, hasBass = true, hasMelody = true, hasVocals = true;
};

/** One-shots from stems: every sound is cut from its own stem, so nothing
    else plays underneath. Drums (kick, snare, hats, open hats, percs) from
    the drum stem, 808s with their note from the bass stem, single notes and
    chords from the melody stem (`melodyNotes`: Basic Pitch on that stem),
    vocal chops (phrases between breaths) from the vocal stem. */
std::vector<Shot> extractFromStems (const StemSet& stems, double sampleRate,
                                    const std::vector<midi::Note>& melodyNotes);

} // namespace digga::oneshots
