#pragma once

#include "core/SampleStore.h"
#include "ui/Theme.h"

namespace digga
{

/** Left card: a dark vinyl with a chrome TrapWin label. Drop a song or a
    loop on it (or click to browse); shows progress and previews the source. */
class Vinyl : public juce::Component,
              public juce::FileDragAndDropTarget,
              private juce::Timer
{
public:
    Vinyl();

    void setStatus (SampleStore::Status status, const SampleStore::Info& info, const juce::String& detail);
    void setPlayback (bool isPlaying, float progress);

    std::function<void (const juce::File&)> onFileChosen;
    std::function<void()> onPlayToggled;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> recordArea() const;
    void browse();

    SampleStore::Status status = SampleStore::Status::empty;
    SampleStore::Info info;
    juce::String detail;
    bool dragOver = false, playing = false, busy = false;
    float progress = 0.0f, angle = 0.0f;

    juce::TextButton play { "Play" };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace digga
