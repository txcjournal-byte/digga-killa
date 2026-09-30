#pragma once

#include "core/SampleStore.h"
#include "ui/TrackRow.h"

namespace digga
{

/** The round record label in the middle: drop zone for the source sample,
    load status and a preview button for the loaded file. */
class RecordLabel : public juce::Component,
                    public juce::FileDragAndDropTarget
{
public:
    RecordLabel();

    /** `detail` is shown under the file name while working ("CUTTING LOOPS"). */
    void setStatus (SampleStore::Status status, const SampleStore::Info& info, const juce::String& detail = {});
    void setPlayback (bool isPlaying, float progress);

    std::function<void (const juce::File&)> onFileChosen;
    std::function<void()> onPlayToggled;

    // Component
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    juce::Rectangle<float> getDisc() const;
    juce::Point<float> toLocal (juce::Point<float> designPoint) const;
    juce::Rectangle<float> toLocal (juce::Rectangle<float> designArea) const;
    void openFileChooser();

    SampleStore::Status status = SampleStore::Status::empty;
    SampleStore::Info info;
    juce::String detail;
    bool dragOver = false;
    bool playing = false;
    float progress = 0.0f;

    PlayButton playButton;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace digga
