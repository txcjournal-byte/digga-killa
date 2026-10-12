#include "ui/MidiView.h"

namespace digga
{

// Contract stub: the MIDI agent replaces this file.
MidiView::MidiView() = default;
MidiView::~MidiView() = default;
void MidiView::setNotes (std::vector<midi::Note> n, double l, double b) { notes = std::move (n); length = l; bpm = b; repaint(); }
void MidiView::setBusy (bool b) { busy = b; repaint(); }
void MidiView::setEmpty (const juce::String& m) { emptyMessage = m; notes.clear(); repaint(); }
void MidiView::setPlayhead (bool p, double f) { playing = p; playhead = f; repaint(); }
void MidiView::paint (juce::Graphics& g) { g.fillAll (theme::well); }
void MidiView::resized() {}

} // namespace digga
