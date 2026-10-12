#include "ui/WaveformEditor.h"

namespace digga
{

// Contract stub: the waveform agent replaces this file.
WaveformEditor::WaveformEditor() = default;
WaveformEditor::~WaveformEditor() = default;
void WaveformEditor::setAudio (AudioPtr a, double sr) { audio = std::move (a); sampleRate = sr; repaint(); }
void WaveformEditor::setGrid (double b, bool loop) { bpm = b; isLoop = loop; repaint(); }
void WaveformEditor::setRange (double s, double e) { rangeStart = s; rangeEnd = e; repaint(); }
void WaveformEditor::setLoopStart (double f) { loopStart = f; repaint(); }
void WaveformEditor::setPlayhead (bool p, double f) { playing = p; playhead = f; repaint(); }
void WaveformEditor::setSnap (Snap s) { snap = s; }
void WaveformEditor::setAccent (juce::Colour c) { accent = c; repaint(); }
void WaveformEditor::zoomIn() {}
void WaveformEditor::zoomOut() {}
void WaveformEditor::zoomToFit() {}
void WaveformEditor::zoomToSelection() {}
void WaveformEditor::paint (juce::Graphics& g) { g.fillAll (theme::well); }
void WaveformEditor::resized() {}

} // namespace digga
