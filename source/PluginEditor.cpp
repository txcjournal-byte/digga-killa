#include "PluginEditor.h"

namespace digga
{

DiggaKillaEditor::DiggaKillaEditor (DiggaKillaProcessor& p)
    : AudioProcessorEditor (p), processor (p), view (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);
    view.setBounds (0, 0, MainView::logicalWidth, MainView::logicalHeight);

    constexpr double aspect = (double) MainView::logicalWidth / (double) MainView::logicalHeight;
    setResizable (true, true);
    setResizeLimits (MainView::logicalWidth / 2, MainView::logicalHeight / 2,
                     MainView::logicalWidth * 2, MainView::logicalHeight * 2);
    if (auto* sizeConstrainer = getConstrainer())
        sizeConstrainer->setFixedAspectRatio (aspect);

    const int width = initialWidth (p.getEditorWidth());
    setSize (width, juce::roundToInt (width / aspect));
    sizeReady = true;
}

int DiggaKillaEditor::initialWidth (int savedWidth)
{
    constexpr double aspect = (double) MainView::logicalWidth / (double) MainView::logicalHeight;
    constexpr int minWidth = MainView::logicalWidth / 2;

    // fit the screen the host opens us on, leaving room for the DAW's own
    // window chrome and toolbars (Windows display scaling is already applied)
    auto area = juce::Rectangle<int> (1920, 1040);
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        area = display->userArea;

    const int fitWidth = juce::jmin ((int) (area.getWidth() * 0.72), (int) (area.getHeight() * 0.72 * aspect));
    const int wanted = savedWidth > 0 ? savedWidth : juce::jmin (1152, fitWidth);
    return juce::jlimit (minWidth, juce::jmax (minWidth, area.getWidth()), wanted);
}

DiggaKillaEditor::~DiggaKillaEditor()
{
    setLookAndFeel (nullptr);
}

void DiggaKillaEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::paper);
}

void DiggaKillaEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::logicalWidth;
    view.setTransform (juce::AffineTransform::scale (scale));

    if (sizeReady)
        processor.setEditorWidth (getWidth());
}

} // namespace digga
