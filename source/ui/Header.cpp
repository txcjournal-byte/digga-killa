#include "ui/Header.h"

namespace digga
{

using namespace theme;

namespace
{
    juce::String bpmText (double bpm)
    {
        if (bpm <= 0.0)
            return "---";
        return juce::String (bpm, juce::exactlyEqual (bpm, std::round (bpm)) ? 0 : 1);
    }
}

Header::Header()
{
    doubleButton.setTooltip ("Sample tempo x2 (detected half time)");
    halveButton.setTooltip ("Sample tempo / 2 (detected double time)");
    doubleButton.onClick = [this] { if (onDouble) onDouble(); };
    halveButton.onClick = [this] { if (onHalve) onHalve(); };
    for (auto* c : std::initializer_list<juce::Component*> { &doubleButton, &halveButton, &format })
        addAndMakeVisible (c);
    setTempoFixEnabled (false);
}

Header::~Header() = default;

void Header::setSample (double bpm, const juce::String& newKey)
{
    if (! juce::exactlyEqual (bpm, sampleBpm) || newKey != key)
    {
        sampleBpm = bpm;
        key = newKey;
        repaint();
    }
}

void Header::setProjectBpm (double bpm)
{
    if (! juce::exactlyEqual (bpm, projectBpm))
    {
        projectBpm = bpm;
        repaint();
    }
}

void Header::setTempoFixEnabled (bool enabled)
{
    doubleButton.setEnabled (enabled);
    halveButton.setEnabled (enabled);
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (28, 0);
    wordmarkArea = r.removeFromLeft (420).toFloat();

    format.setBounds (r.removeFromRight (210).withSizeKeepingCentre (210, 34));
    r.removeFromRight (16);
    halveButton.setBounds (r.removeFromRight (52).withSizeKeepingCentre (52, 34));
    r.removeFromRight (8);
    doubleButton.setBounds (r.removeFromRight (52).withSizeKeepingCentre (52, 34));
    r.removeFromRight (18);
    tempoArea = r.toFloat();
}

void Header::paint (juce::Graphics& g)
{
    // the TrapWin logo exactly as on the other plugins, product pill beside it
    const auto logoArea = wordmarkArea.withSizeKeepingCentre (wordmarkArea.getWidth(), 80.0f).withWidth (220.0f);
    drawLogo (g, logoArea, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);

    const auto pill = juce::Rectangle<float> (logoArea.getRight() + 12.0f, logoArea.getCentreY() - 13.0f, 112.0f, 26.0f);
    g.setColour (juce::Colour (0xffe6eaf2));
    g.fillRoundedRectangle (pill, 13.0f);
    g.setColour (well);
    g.setFont (mono (12.0f, true).withExtraKerningFactor (0.18f));
    g.drawText ("SAMPLER", pill, juce::Justification::centred, false);

    // tempo readout: two lines, right aligned
    auto t = tempoArea.withSizeKeepingCentre (tempoArea.getWidth(), 40.0f);
    const auto top = t.removeFromTop (20.0f), bottom = t;
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    const auto sampleText = "SAMPLE " + bpmText (sampleBpm) + " BPM" + (key.isNotEmpty() ? dot + key : juce::String());
    g.setFont (mono (13.0f, true).withExtraKerningFactor (0.08f));
    g.setColour (text);
    g.drawText (sampleText, top, juce::Justification::centredRight, false);
    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), sampleText);
    sampleBpmArea = top.withLeft (top.getRight() - w);

    g.setColour (accentL);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 PROJECT ")) + bpmText (projectBpm) + " BPM",
                bottom, juce::Justification::centredRight, false);
}

void Header::mouseDown (const juce::MouseEvent& e)
{
    if (sampleBpm <= 0.0 || editor != nullptr || ! sampleBpmArea.contains (e.position))
        return;

    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (mono (14.0f, true));
    editor->setJustification (juce::Justification::centredRight);
    editor->setInputRestrictions (6, "0123456789.");
    editor->setText (bpmText (sampleBpm), false);
    editor->onReturnKey = [this] { finishEditing (true); };
    editor->onEscapeKey = [this] { finishEditing (false); };
    editor->onFocusLost = [this] { finishEditing (true); };
    addAndMakeVisible (*editor);
    editor->setBounds (sampleBpmArea.withWidth (90.0f).withX (sampleBpmArea.getRight() - 90.0f).toNearestInt().expanded (0, 3));
    editor->selectAll();
    editor->grabKeyboardFocus();
}

void Header::finishEditing (bool commit)
{
    if (editor == nullptr || ! editor->isVisible())
        return;

    const auto value = editor->getText().getDoubleValue();
    editor->setVisible (false);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Header> (this)]
    {
        if (safe != nullptr)
            safe->editor = nullptr;
    });

    if (commit && value >= 40.0 && value <= 250.0 && onBpmTyped)
        onBpmTyped (value);
}

} // namespace digga
