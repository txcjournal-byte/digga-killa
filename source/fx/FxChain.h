#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>

namespace digga::fx
{

/** The effect rack of one sample (taken from TrapWin Effector, sampler-sized).
    Applied offline after the editor's crop / pitch / reverse, so what you
    hear is what you drag. Modules run in this order. */
struct FxSettings
{
    // TIME (tape-style varispeed, pitch follows): 0 off, 1 half speed, 2 quarter speed, 3 double speed.
    // Loops keep their length (the first half / quarter gets stretched, or the loop plays twice);
    // one-shots get longer / shorter.
    int timeMode = 0;
    // REPEAT (beat-repeat stutter): 0 off, 1 1/2, 2 1/4, 3 1/8, 4 1/16, 5 1/32 note. In every segment
    // of max (1 beat, 2 slices) from the sample start, the first slice repeats to fill the segment.
    int repeatRate = 0;
    // FILTER: 0 low-pass, 1 high-pass, 2 band-pass
    bool filterOn = false;  int filterType = 0;  float filterFreq = 2000.0f;  float filterReso = 0.7f;
    // DRIVE: 0 tape, 1 tube, 2 hard clip; amount 0..1, mix 0..1
    bool driveOn = false;   int driveType = 0;   float drive = 0.4f;          float driveMix = 1.0f;
    // CRUSH: bit depth 2..16, sample-rate reduction 0..1 (0 = none)
    bool crushOn = false;   float crushBits = 8.0f;  float crushRate = 0.3f;
    // GATE / STUTTER sequencer: 16 steps (bit i = step i on), rate 0 1/4, 1 1/8, 2 1/16, 3 1/32; depth 0..1
    bool gateOn = false;    int gateRate = 2;    juce::uint16 gatePattern = 0xB6DB;  float gateDepth = 1.0f;
    // TAPE STOP at the end: length in beats 0.25..4
    bool tapeStopOn = false; float tapeStopBeats = 1.0f;
    // DELAY: 0 1/4, 1 1/8, 2 1/8 dotted, 3 1/16, 4 1/4 dotted; feedback 0..0.95, mix 0..1
    bool delayOn = false;   int delayRate = 2;   float delayFeedback = 0.4f;  float delayMix = 0.3f;  bool delayPingPong = true;
    // REVERB: size 0..1, damping 0..1, mix 0..1
    bool reverbOn = false;  float reverbSize = 0.6f;  float reverbDamp = 0.5f;  float reverbMix = 0.25f;
    // WIDTH: 0 mono .. 1 as is .. 2 extra wide
    bool widthOn = false;   float width = 1.5f;

    bool anyOn() const noexcept;
    bool isDefault() const noexcept;
    juce::ValueTree toValueTree() const;                       // type "Fx"
    static FxSettings fromValueTree (const juce::ValueTree&);   // missing tree -> defaults
    bool operator== (const FxSettings&) const noexcept;
    bool operator!= (const FxSettings& o) const noexcept { return ! (*this == o); }
};

/** Offline render of the rack into `audio` (in place, stereo or mono).
    `bpm` syncs repeat / delay / gate / tape stop; `loop` = true keeps the
    length (time) and makes delay and reverb tails wrap around to the start
    so the result still loops seamlessly (one-shots instead change length
    with the time module and get their tail appended). */
void process (juce::AudioBuffer<float>& audio, const FxSettings& settings, double sampleRate, double bpm, bool loop);

/** KILL: a new, musical random rack built from the simple modules the rack
    shows (time, repeat, filter, delay, reverb). strength 0 subtle .. 2 extreme. */
FxSettings randomise (juce::Random& random, int strength);

} // namespace digga::fx
