#include "fx/FxChain.h"

namespace digga::fx
{

// Contract stub: the FX agent replaces this file with the real implementation.
bool FxSettings::anyOn() const noexcept
{
    return filterOn || driveOn || crushOn || gateOn || tapeStopOn || delayOn || reverbOn || widthOn;
}

bool FxSettings::isDefault() const noexcept { return *this == FxSettings {}; }

juce::ValueTree FxSettings::toValueTree() const { return juce::ValueTree ("Fx"); }

FxSettings FxSettings::fromValueTree (const juce::ValueTree&) { return {}; }

bool FxSettings::operator== (const FxSettings& o) const noexcept
{
    return anyOn() == o.anyOn();
}

void process (juce::AudioBuffer<float>&, const FxSettings&, double, double, bool) {}

FxSettings randomise (juce::Random&, int) { return {}; }

} // namespace digga::fx
