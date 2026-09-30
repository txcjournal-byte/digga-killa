#include "PluginEditor.h"

namespace digga
{

DiggaKillaEditor::DiggaKillaEditor (DiggaKillaProcessor& p)
    : AudioProcessorEditor (p), view (p)
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

    setSize (1152, 768);
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
}

} // namespace digga
