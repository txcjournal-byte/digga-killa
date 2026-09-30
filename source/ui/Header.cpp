#include "ui/Header.h"

namespace digga
{

// ---------------------------------------------------------------------------
TempoDisplay::TempoDisplay()
{
    doubleButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97" "2")));
    halveButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\xb7" "2")));
    doubleButton.setTooltip ("Detected tempo x2");
    halveButton.setTooltip ("Detected tempo /2");

    for (auto* b : { &doubleButton, &halveButton })
    {
        b->setEnabled (false);   // enabled once tempo detection exists (phase 2)
        addAndMakeVisible (*b);
    }
}

void TempoDisplay::setSampleText (const juce::String& text)
{
    if (text != sampleText)
    {
        sampleText = text;
        repaint();
    }
}

void TempoDisplay::setProjectBpm (double bpm)
{
    const auto text = bpm > 0.0 ? "PROJECT " + juce::String (bpm, juce::exactlyEqual (bpm, std::floor (bpm)) ? 0 : 1) + " BPM"
                                : juce::String ("PROJECT --- BPM");
    if (text != projectText)
    {
        projectText = text;
        repaint();
    }
}

void TempoDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    area.removeFromBottom (40.0f);

    const auto font = theme::mono (19.0f, true);
    const float lineHeight = area.getHeight() / 2.0f;

    g.setFont (font);
    g.setColour (theme::ink);
    g.drawText (sampleText, area.removeFromTop (lineHeight), juce::Justification::centredRight, false);

    auto projectLine = area.removeFromTop (lineHeight);
    g.drawText (projectText, projectLine, juce::Justification::centredRight, false);

    const float textWidth = juce::GlyphArrangement::getStringWidth (font, projectText);
    g.setColour (theme::red);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")),
                projectLine.withRight (projectLine.getRight() - textWidth - 8.0f),
                juce::Justification::centredRight, false);
}

void TempoDisplay::resized()
{
    auto buttons = getLocalBounds().removeFromBottom (32);
    halveButton.setBounds (buttons.removeFromRight (46));
    buttons.removeFromRight (8);
    doubleButton.setBounds (buttons.removeFromRight (46));
}

// ---------------------------------------------------------------------------
Header::Header()
{
    setBufferedToImage (true);
    addAndMakeVisible (tempo);
}

void Header::paint (juce::Graphics& g)
{
    theme::drawLogo (g, logoArea);

    // heavy condensed poster title; the stroke fattens Anton towards the
    // blackletter weight of the reference, the grunge wears the print down
    const auto title = theme::textPath ("DIGGA KILLA", theme::display (200.0f), titleArea, true);
    g.setColour (theme::ink);
    g.fillPath (title);
    g.strokePath (title, juce::PathStrokeType (3.0f, juce::PathStrokeType::mitered));

    juce::Path worn (title);
    juce::PathStrokeType (3.0f).createStrokedPath (worn, title);
    worn.addPath (title);
    theme::addGrunge (g, worn, theme::paper, 1337, 1.0f);
}

void Header::resized()
{
    auto bounds = getLocalBounds();
    logoArea = { 16.0f, 34.0f, 170.0f, 40.0f };
    titleArea = bounds.toFloat().withSizeKeepingCentre (690.0f, 162.0f).translated (-6.0f, 6.0f);
    tempo.setBounds (bounds.getRight() - 300, 30, 290, 110);
}

} // namespace digga
