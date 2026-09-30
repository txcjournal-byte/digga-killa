#include "ui/TempoDisplay.h"

namespace digga
{

namespace
{
    juce::String bpmText (double bpm)
    {
        if (bpm <= 0.0)
            return "---";
        return juce::String (bpm, juce::exactlyEqual (bpm, std::round (bpm)) ? 0 : 1);
    }
}

void SkinButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    if (! isEnabled() || ! (highlighted || down))
        return;

    g.setColour (theme::ink.withAlpha (down ? 0.16f : 0.08f));
    g.fillRect (getLocalBounds().reduced (2));
}

TempoDisplay::TempoDisplay()
{
    setMouseCursor (juce::MouseCursor::IBeamCursor);
    setTooltip ("Click to type the sample tempo");
    doubleButton.setTooltip ("Detected tempo x2");
    halveButton.setTooltip ("Detected tempo / 2");
    setSample (0.0, {});
    setProjectBpm (0.0);
}

TempoDisplay::~TempoDisplay() = default;

void TempoDisplay::setSample (double bpm, const juce::String& key)
{
    sampleBpm = bpm;
    auto text = "SAMPLE " + bpmText (bpm) + " BPM";
    if (key.isNotEmpty())
        text << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << key;

    if (text != sampleText)
    {
        sampleText = text;
        repaint();
    }
}

void TempoDisplay::setProjectBpm (double bpm)
{
    const auto text = "PROJECT " + bpmText (bpm) + " BPM";
    if (text != projectText)
    {
        projectText = text;
        repaint();
    }
}

void TempoDisplay::paint (juce::Graphics& g)
{
    const auto font = theme::condensed (16.5f);
    const juce::String arrow (juce::CharPointer_UTF8 (" \xe2\x86\x92 "));
    const auto full = sampleText + arrow + projectText;

    g.setFont (font);
    g.setColour (theme::ink);
    g.drawFittedText (full, getLocalBounds(), juce::Justification::centredRight, 1, 0.8f);
}

} // namespace digga

namespace digga
{

void TempoDisplay::mouseDown (const juce::MouseEvent&)
{
    if (sampleBpm <= 0.0 || editor != nullptr)
        return;

    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (theme::condensed (17.0f, true));
    editor->setJustification (juce::Justification::centredRight);
    editor->setInputRestrictions (6, "0123456789.");
    editor->setColour (juce::TextEditor::backgroundColourId, theme::paperLight);
    editor->setColour (juce::TextEditor::textColourId, theme::ink);
    editor->setColour (juce::TextEditor::outlineColourId, theme::red);
    editor->setColour (juce::TextEditor::focusedOutlineColourId, theme::red);
    editor->setColour (juce::TextEditor::highlightColourId, theme::red.withAlpha (0.3f));
    editor->setColour (juce::CaretComponent::caretColourId, theme::red);
    editor->setText (juce::String (sampleBpm, juce::exactlyEqual (sampleBpm, std::round (sampleBpm)) ? 0 : 1), false);
    editor->onReturnKey = [this] { finishEditing (true); };
    editor->onEscapeKey = [this] { finishEditing (false); };
    editor->onFocusLost = [this] { finishEditing (true); };
    addAndMakeVisible (*editor);
    editor->setBounds (getLocalBounds().removeFromRight (90).reduced (0, 1));
    editor->selectAll();
    editor->grabKeyboardFocus();
}

void TempoDisplay::finishEditing (bool commit)
{
    if (editor == nullptr || ! editor->isVisible())
        return;

    const auto value = editor->getText().getDoubleValue();
    editor->setVisible (false);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TempoDisplay> (this)]
    {
        if (safe != nullptr)
            safe->editor = nullptr;
    });

    if (commit && value >= 40.0 && value <= 250.0 && onBpmTyped)
        onBpmTyped (value);
}

} // namespace digga
