#pragma once

#include "core/AudioTools.h"

#include <vector>

namespace digga::oneshots
{

/** Finds onsets, cuts each hit from its attack to the end of its decay and
    returns up to `maxShots` hits that differ the most from each other
    (spectral shape, noisiness, low end, length). Results are normalised,
    faded out and ordered from low to bright. */
std::vector<juce::AudioBuffer<float>> extract (const juce::AudioBuffer<float>& source, double sampleRate,
                                               int maxShots = 8);

} // namespace digga::oneshots
