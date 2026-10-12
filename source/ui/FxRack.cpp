#include "ui/FxRack.h"

namespace digga
{

// Contract stub: the FX agent replaces this file.
FxRack::FxRack() = default;
FxRack::~FxRack() = default;
void FxRack::setSettings (const fx::FxSettings& s) { settings = s; repaint(); }
void FxRack::setEnabled (bool e, const juce::String& m) { enabled = e; message = m; repaint(); }
void FxRack::paint (juce::Graphics& g) { theme::drawCard (g, getLocalBounds().toFloat().reduced (1.0f)); }
void FxRack::resized() {}

} // namespace digga
