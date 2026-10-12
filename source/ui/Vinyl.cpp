#include "ui/Vinyl.h"

namespace digga
{

using namespace theme;

Vinyl::Vinyl()
{
    play.onClick = [this] { if (onPlayToggled) onPlayToggled(); };
    play.setTooltip ("Preview the whole song");
    addChildComponent (play);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> Vinyl::recordArea() const
{
    auto r = getLocalBounds().toFloat().reduced (20.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight() - 82.0f);
    return juce::Rectangle<float> (size, size).withCentre ({ r.getCentreX(), r.getY() + size * 0.5f });
}

void Vinyl::setStatus (SampleStore::Status s, const SampleStore::Info& i, const juce::String& d)
{
    status = s;
    info = i;
    detail = d;
    busy = status == SampleStore::Status::loading;
    play.setVisible (status == SampleStore::Status::ready);
    if (busy || playing) startTimerHz (30); else stopTimer();
    repaint();
}

void Vinyl::setPlayback (bool isPlaying, float p)
{
    if (isPlaying != playing || std::abs (p - progress) > 0.002f)
    {
        playing = isPlaying;
        progress = p;
        play.setButtonText (playing ? "Stop" : "Play");
        play.getProperties().set ("chrome", playing);
        if (busy || playing) startTimerHz (30); else stopTimer();
        repaint();
    }
}

void Vinyl::timerCallback()
{
    angle += playing ? 0.06f : 0.12f;
    repaint();
}

void Vinyl::resized()
{
    play.setBounds (getLocalBounds().removeFromBottom (44).withSizeKeepingCentre (100, 30));
}

void Vinyl::paint (juce::Graphics& g)
{
    const auto rec = recordArea();
    const auto c = rec.getCentre();
    const float r = rec.getWidth() * 0.5f;

    // record body
    juce::Path disc;
    disc.addEllipse (rec);
    juce::DropShadow (dragOver ? accentL.withAlpha (0.7f) : glow.withAlpha (0.35f), dragOver ? 40 : 28, {}).drawForPath (g, disc);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141a26), c.x - r * 0.3f, c.y - r * 0.4f, juce::Colour (0xff020305), c.x + r, c.y + r, true));
    g.fillPath (disc);

    // grooves
    for (float rr = r * 0.97f; rr > r * 0.42f; rr -= 3.2f)
    {
        g.setColour (accentL.withAlpha (0.035f + 0.025f * std::sin (rr * 0.7f)));
        g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.8f);
    }

    // rotating light sheen: two soft, thin highlights across the grooves
    for (int k = 0; k < 2; ++k)
    {
        const float a = angle + (float) k * juce::MathConstants<float>::pi;
        for (int layer = 0; layer < 4; ++layer)
        {
            const float w = 0.05f + 0.06f * (float) layer;
            juce::Path wedge;
            wedge.addPieSegment (rec.reduced (3.0f), a - w, a + w, 0.42f);
            g.setColour (accentL.withAlpha (0.018f));
            g.fillPath (wedge);
        }
    }

    // label: dark centre with a chrome rim and the TrapWin logo
    const float lr = r * 0.54f;
    const auto labelArea = juce::Rectangle<float> (lr * 2.0f, lr * 2.0f).withCentre (c);
    juce::Path rim;
    rim.addEllipse (labelArea);
    fillChrome (g, rim, false);
    g.setGradientFill (juce::ColourGradient (card2.brighter (0.08f), c.x, c.y - lr, well, c.x, c.y + lr, false));
    g.fillEllipse (labelArea.reduced (lr * 0.06f));
    drawLogo (g, juce::Rectangle<float> (lr * 1.72f, lr * 0.66f).withCentre (c.translated (0.0f, -lr * 0.3f)));
    g.setColour (text2);
    g.setFont (mono (lr * 0.13f, true).withExtraKerningFactor (0.24f));
    g.drawText ("SAMPLER", juce::Rectangle<float> (lr * 1.4f, lr * 0.22f).withCentre (c.translated (0.0f, lr * 0.62f)), juce::Justification::centred, false);
    g.setColour (bg);
    g.fillEllipse (juce::Rectangle<float> (lr * 0.12f, lr * 0.12f).withCentre (c.translated (0.0f, lr * 0.3f)));

    // progress / busy ring
    if (playing || busy)
    {
        juce::Path arc;
        const float start = busy ? angle * 2.0f : 0.0f;
        const float span = busy ? 1.4f : progress * juce::MathConstants<float>::twoPi;
        arc.addCentredArc (c.x, c.y, r + 7.0f, r + 7.0f, 0.0f, start, start + span, true);
        juce::DropShadow (glow, 12, {}).drawForPath (g, arc);
        g.setColour (accentL);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // text block under the record
    auto textArea = getLocalBounds().toFloat().withTop (rec.getBottom() + 10.0f).reduced (14.0f, 0.0f);
    if (status == SampleStore::Status::ready)
        textArea.removeFromBottom (44.0f);

    auto title = textArea.removeFromTop (26.0f);
    auto sub = textArea.removeFromTop (18.0f);
    const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));

    switch (status)
    {
        case SampleStore::Status::empty:
            fillChrome (g, textPath ("DROP A SONG", display (30.0f), title, juce::Justification::centred), true);
            drawMonoLabel (g, "WAV" + dot + "MP3" + dot + "AIFF" + dot + "FLAC" + dot + "or click", sub, juce::Justification::centred, muted);
            break;

        case SampleStore::Status::loading:
            fillChrome (g, textPath ("ANALYSING", display (30.0f), title, juce::Justification::centred), true);
            drawMonoLabel (g, detail.isNotEmpty() ? detail : "Decoding " + info.file.getFileName(), sub, juce::Justification::centred, accentL);
            break;

        case SampleStore::Status::error:
            fillChrome (g, textPath ("CAN'T READ", display (30.0f), title, juce::Justification::centred), true);
            drawMonoLabel (g, info.error, sub, juce::Justification::centred, juce::Colour (0xffff7a7a));
            break;

        case SampleStore::Status::ready:
        {
            g.setColour (text);
            g.setFont (bold (19.0f));
            g.drawFittedText (info.file.getFileNameWithoutExtension(), title.toNearestInt(), juce::Justification::centred, 1, 0.8f);
            const int secs = juce::roundToInt (info.lengthSeconds);
            drawMonoLabel (g, juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2) + dot
                               + juce::String (info.fileSampleRate / 1000.0, 1) + " kHz" + dot + (info.numChannels == 1 ? "Mono" : "Stereo"),
                           sub, juce::Justification::centred, muted);
            break;
        }
    }
}

void Vinyl::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && status != SampleStore::Status::loading && recordArea().contains (e.position))
        browse();
}

void Vinyl::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a song or loop", juce::File(), SampleStore::getFileWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<Vinyl> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile() && safe->onFileChosen)
                                  safe->onFileChosen (fc.getResult());
                          });
}

bool Vinyl::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (SampleStore::isSupportedFile (juce::File (f)))
            return true;
    return false;
}

void Vinyl::fileDragEnter (const juce::StringArray&, int, int) { dragOver = true; repaint(); }
void Vinyl::fileDragExit (const juce::StringArray&)            { dragOver = false; repaint(); }

void Vinyl::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    for (const auto& f : files)
        if (SampleStore::isSupportedFile (juce::File (f)))
        {
            if (onFileChosen)
                onFileChosen (juce::File (f));
            return;
        }
}

} // namespace digga
