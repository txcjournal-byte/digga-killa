#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace digga::midi
{

/** One transcribed note. */
struct Note
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int pitch = 60;            // MIDI note number
    float amplitude = 0.5f;    // 0..1
};

/** Polyphonic audio-to-MIDI with the Basic Pitch model (Spotify, Apache-2.0,
    https://github.com/spotify/basic-pitch), re-implemented natively: no ML
    runtime needed. The weights ship in assets/models/basic_pitch.bin.
    Thread-safe; heavy (a few hundred ms per 2 s of audio, run off the
    audio thread). */
std::vector<Note> transcribe (const juce::AudioBuffer<float>& audio, double sampleRate);

/** Model posteriors for 22.05 kHz mono audio: frames x 88 note / onset
    activations (exposed for testing). */
struct Posteriors
{
    int frames = 0;
    std::vector<float> note, onset;
};

Posteriors computePosteriors (const float* mono22k, int numSamples);

/** Basic Pitch's note decoding (onsets, frame tracking, "melodia trick").
    Times are seconds of the 22.05 kHz input. */
std::vector<Note> decodeNotes (const Posteriors& posteriors);

/** Raw model output for one 43844-sample window (testing). */
void runModelWindow (const float* window, float* note172x88, float* onset172x88, float* contour172x264);

} // namespace digga::midi
