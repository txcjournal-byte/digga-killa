#pragma once

#include "ui/FormatSwitch.h"

namespace digga
{

/** Top bar: TRAPWIN chrome wordmark + SAMPLE pill, tempo / key readout
    (click the sample BPM to type it), x2 / /2 and the WAV | MIDI switch. */
class Header : public juce::Component
{
public:
    Header();
    ~Header() override;

    void setSample (double bpm, const juce::String& key);
    void setProjectBpm (double bpm);
    void setTempoFixEnabled (bool enabled);

    std::function<void (double)> onBpmTyped;
    std::function<void()> onDouble, onHalve;

    FormatSwitch format;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void finishEditing (bool commit);

    juce::TextButton doubleButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x97" "2")) };
    juce::TextButton halveButton { juce::String (juce::CharPointer_UTF8 ("\xc3\xb7" "2")) };
    std::unique_ptr<juce::TextEditor> editor;

    double sampleBpm = 0.0, projectBpm = 0.0;
    juce::String key;
    juce::Rectangle<float> wordmarkArea, tempoArea, sampleBpmArea;
};

} // namespace digga
