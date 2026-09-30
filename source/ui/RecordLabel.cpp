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

juce::Rectangle<float> RecordLabel::getDisc() const
{
    const auto bounds = getLocalBounds().toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
    return bounds.withSizeKeepingCentre (size, size);
}

bool RecordLabel::hitTest (int x, int y)
{
    return getDisc().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= getDisc().getWidth() * 0.5f;
}

void RecordLabel::setStatus (SampleStore::Status newStatus, const SampleStore::Info& newInfo)
{
    status = newStatus;
    info = newInfo;
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
    const auto disc = getDisc();
    const float labelR = disc.getWidth() * 0.5f * 0.88f;
    playButton.setBounds (juce::Rectangle<int> (40, 40).withCentre (
        disc.getCentre().translated (0.0f, labelR * 0.3f).toInt()));
}

void RecordLabel::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto disc = getDisc();
    const auto centre = disc.getCentre();
    const float r = disc.getWidth() * 0.5f;

    // grooves peeking out around the label
    for (int i = 0; i < 14; ++i)
    {
        const float rr = r - 2.0f - (float) i * 1.6f;
        g.setColour (ink.withAlpha (i == 0 ? 0.9f : 0.05f + 0.05f * (float) (i % 3)));
        g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (centre), i == 0 ? 3.0f : 0.8f);
    }

    const float labelR = r * 0.88f;
    const auto label = juce::Rectangle<float> (labelR * 2.0f, labelR * 2.0f).withCentre (centre);

    g.setColour (paperLight.withAlpha (0.55f));
    g.fillEllipse (label);

    juce::Path labelPath;
    labelPath.addEllipse (label);
    theme::addGrunge (g, labelPath, juce::Colour (0xff6b5c43).withAlpha (0.25f), 77, 0.25f);

    g.setColour ((dragOver ? red : ink).withAlpha (0.85f));
    g.drawEllipse (label, dragOver ? 3.0f : 1.6f);

    // playback progress around the label edge
    if (playing && progress > 0.0f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, labelR + 5.0f, labelR + 5.0f, 0.0f, 0.0f,
                           progress * juce::MathConstants<float>::twoPi, true);
        g.setColour (red);
        g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // wordmark
    drawLogo (g, juce::Rectangle<float> (labelR * 0.62f, labelR * 0.14f)
                     .withCentre (centre.translated (0.0f, -labelR * 0.62f)),
              juce::Justification::centred);

    // spindle hole
    const float holeR = r * 0.065f;
    g.setColour (ink);
    g.fillEllipse (juce::Rectangle<float> (holeR * 2.0f, holeR * 2.0f).withCentre (centre.translated (0.0f, labelR * 0.56f)));

    // main message
    auto message = juce::Rectangle<float> (labelR * 1.3f, labelR * 0.8f).withCentre (centre.translated (0.0f, -labelR * 0.1f));

    auto stampText = [&] (const juce::String& line1, const juce::String& line2, juce::Colour colour)
    {
        auto top = message.withHeight (message.getHeight() * 0.5f).reduced (0.0f, 4.0f);
        auto bottom = top.translated (0.0f, message.getHeight() * 0.5f);
        juce::Path text = textPath (line1, display (100.0f), top, false);
        text.addPath (textPath (line2, display (100.0f), bottom, false));
        g.setColour (colour);
        g.fillPath (text);
        g.strokePath (text, juce::PathStrokeType (1.5f));
        addGrunge (g, text, paper, 99, 1.2f);
    };

    auto smallText = [&] (const juce::String& text, float y, juce::Colour colour, bool bold)
    {
        g.setColour (colour);
        g.setFont (mono (16.0f, bold));
        g.drawFittedText (text, juce::Rectangle<float> (labelR * 1.4f, 22.0f).withCentre (centre.translated (0.0f, y)).toNearestInt(),
                          juce::Justification::centred, 1, 0.7f);
    };

    switch (status)
    {
        case SampleStore::Status::empty:
            stampText ("DROP", "SAMPLE", dragOver ? red : ink);
            break;

        case SampleStore::Status::loading:
            stampText ("DIGGING", "...", ink);
            smallText (info.file.getFileName(), labelR * 0.34f, inkSoft, false);
            break;

        case SampleStore::Status::error:
            stampText ("NO", "DICE", red);
            smallText (info.error.toUpperCase(), labelR * 0.34f, red, true);
            break;

        case SampleStore::Status::ready:
        {
            message = message.withSizeKeepingCentre (message.getWidth(), message.getHeight() * 0.5f)
                             .translated (0.0f, -labelR * 0.14f);
            const auto name = info.file.getFileNameWithoutExtension().toUpperCase();
            const auto text = textPath (name.length() > 18 ? name.substring (0, 17) + "." : name,
                                        display (100.0f), message, false);
            g.setColour (dragOver ? red : ink);
            g.fillPath (text);
            addGrunge (g, text, paper, 12, 1.0f);

            const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
            smallText (formatTime (info.lengthSeconds) + dot
                           + juce::String (info.fileSampleRate / 1000.0, 1) + " kHz" + dot
                           + (info.numChannels == 1 ? "MONO" : "STEREO"),
                       labelR * 0.06f, inkSoft, true);
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
