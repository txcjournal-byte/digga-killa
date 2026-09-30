#include "ui/RecordLabel.h"

namespace digga
{

namespace
{
    juce::String formatTime (double seconds)
    {
        const int total = juce::roundToInt (seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }
}

RecordLabel::RecordLabel()
{
    playButton.onClick = [this] { if (onPlayToggled) onPlayToggled(); };
    playButton.setTooltip ("Preview the source sample");
    addChildComponent (playButton);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

// Geometry of the printed record in docs/design.png (absolute design pixels).
namespace
{
    constexpr float discCentreX = 739.0f, discCentreY = 440.0f, discRadius = 212.0f, labelRadius = 205.0f;
    const juce::Rectangle<float> dropSampleArea { 612.0f, 332.0f, 260.0f, 140.0f };
    const juce::Rectangle<float> titleArea      { 620.0f, 338.0f, 238.0f, 58.0f };
    constexpr float infoY = 414.0f, playY = 458.0f;
}

juce::Rectangle<float> RecordLabel::getDisc() const
{
    return juce::Rectangle<float> (discRadius * 2.0f, discRadius * 2.0f).withCentre (toLocal (juce::Point<float> (discCentreX, discCentreY)));
}

juce::Point<float> RecordLabel::toLocal (juce::Point<float> designPoint) const
{
    return designPoint - getPosition().toFloat();
}

juce::Rectangle<float> RecordLabel::toLocal (juce::Rectangle<float> designArea) const
{
    return designArea - getPosition().toFloat();
}

bool RecordLabel::hitTest (int x, int y)
{
    return getDisc().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= discRadius;
}

void RecordLabel::setStatus (SampleStore::Status newStatus, const SampleStore::Info& newInfo, const juce::String& newDetail)
{
    status = newStatus;
    info = newInfo;
    detail = newDetail;
    playButton.setVisible (status == SampleStore::Status::ready);
    repaint();
}

void RecordLabel::setPlayback (bool isPlaying, float newProgress)
{
    if (isPlaying != playing || std::abs (newProgress - progress) > 0.001f)
    {
        playing = isPlaying;
        progress = newProgress;
        playButton.setPlaying (playing);
        repaint();
    }
}

void RecordLabel::resized()
{
    playButton.setBounds (juce::Rectangle<int> (34, 34).withCentre (toLocal (juce::Point<float> (discCentreX, playY)).toInt()));
}

void RecordLabel::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto centre = toLocal (juce::Point<float> (discCentreX, discCentreY));

    if (dragOver)
    {
        g.setColour (red);
        g.drawEllipse (juce::Rectangle<float> (labelRadius * 2.0f, labelRadius * 2.0f).withCentre (centre), 3.0f);
    }

    if (playing && progress > 0.0f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, discRadius + 5.0f, discRadius + 5.0f, 0.0f, 0.0f,
                           progress * juce::MathConstants<float>::twoPi, true);
        g.setColour (red);
        g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto stampText = [&] (const juce::String& text, juce::Rectangle<float> area, juce::Colour colour)
    {
        g.setColour (colour);
        g.fillPath (textPath (text, display (100.0f, 0.8f), toLocal (area), false));
    };

    auto smallText = [&] (const juce::String& text, float y, juce::Colour colour)
    {
        g.setColour (colour);
        g.setFont (condensed (18.0f, true));
        g.drawFittedText (text, toLocal (juce::Rectangle<float> (300.0f, 22.0f).withCentre ({ discCentreX, y })).toNearestInt(),
                          juce::Justification::centred, 1, 0.8f);
    };

    switch (status)
    {
        case SampleStore::Status::empty:
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            if (dragOver)
            {
                g.setColour (red);
                g.drawImage (skinDropSample(), toLocal (dropSampleArea), juce::RectanglePlacement::stretchToFit, true);
            }
            else
            {
                g.drawImage (skinDropSample(), toLocal (dropSampleArea), juce::RectanglePlacement::stretchToFit);
            }
            break;

        case SampleStore::Status::loading:
            stampText ("DIGGING...", titleArea, ink);
            smallText (info.file.getFileName(), infoY, inkSoft);
            if (detail.isNotEmpty())
                smallText (detail, infoY + 26.0f, red);
            break;

        case SampleStore::Status::error:
            stampText ("NO DICE", titleArea, red);
            smallText (info.error.toUpperCase(), infoY, red);
            break;

        case SampleStore::Status::ready:
        {
            const auto name = info.file.getFileNameWithoutExtension().toUpperCase();
            stampText (name.length() > 16 ? name.substring (0, 15) + "." : name, titleArea, dragOver ? red : ink);

            const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
            smallText (formatTime (info.lengthSeconds) + dot
                           + juce::String (info.fileSampleRate / 1000.0, 1) + " kHz" + dot
                           + (info.numChannels == 1 ? "MONO" : "STEREO"),
                       infoY, inkSoft);
            break;
        }
    }
}

void RecordLabel::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && status != SampleStore::Status::loading)
        openFileChooser();
}

void RecordLabel::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sample", juce::File(), SampleStore::getFileWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::Component::SafePointer<RecordLabel> (this)] (const juce::FileChooser& fc)
                          {
                              if (safeThis == nullptr)
                                  return;
                              const auto file = fc.getResult();
                              if (file.existsAsFile() && safeThis->onFileChosen)
                                  safeThis->onFileChosen (file);
                          });
}

bool RecordLabel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (SampleStore::isSupportedFile (juce::File (path)))
            return true;
    return false;
}

void RecordLabel::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void RecordLabel::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void RecordLabel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();

    for (const auto& path : files)
    {
        const juce::File file (path);
        if (SampleStore::isSupportedFile (file))
        {
            if (onFileChosen)
                onFileChosen (file);
            return;
        }
    }
}

} // namespace digga
