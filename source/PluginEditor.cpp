#include "PluginEditor.h"

namespace digga
{

TrapWinEditor::TrapWinEditor (TrapWinProcessor& p)
    : AudioProcessorEditor (p), processor (p), view (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);
    view.setBounds (0, 0, MainView::logicalWidth, MainView::logicalHeight);

    constexpr double aspect = (double) MainView::logicalWidth / (double) MainView::logicalHeight;
    setResizable (true, true);
    setResizeLimits (MainView::logicalWidth * 6 / 10, MainView::logicalHeight * 6 / 10,
                     MainView::logicalWidth * 2, MainView::logicalHeight * 2);
    if (auto* sizeConstrainer = getConstrainer())
        sizeConstrainer->setFixedAspectRatio (aspect);

    const int width = initialWidth (p.getEditorWidth());
    setSize (width, juce::roundToInt (width / aspect));
    sizeReady = true;
}

int TrapWinEditor::initialWidth (int savedWidth)
{
    constexpr double aspect = (double) MainView::logicalWidth / (double) MainView::logicalHeight;
    constexpr int minWidth = MainView::logicalWidth * 6 / 10;

    // Open as a window inside the DAW, never over all of it: about 70 % of the
    // screen width and 80 % of its height (laptop 1366x768 -> ~950 px wide,
    // full HD -> ~1340 px), never bigger than 115 % of the design size.
    // Windows display scaling is already applied to userArea.
    auto area = juce::Rectangle<int> (1920, 1040);
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        area = display->userArea;

    const int fitWidth = juce::jmin ((int) (area.getWidth() * 0.70), (int) (area.getHeight() * 0.80 * aspect),
                                     MainView::logicalWidth * 115 / 100);
    // a size the user chose is kept, as long as it still fits this screen
    const int wanted = savedWidth > 0 ? juce::jmin (savedWidth, (int) (area.getHeight() * 0.92 * aspect), area.getWidth() - 40)
                                      : fitWidth;
    return juce::jmax (minWidth, wanted);
}

TrapWinEditor::~TrapWinEditor()
{
    setLookAndFeel (nullptr);
}

void TrapWinEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
}

void TrapWinEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::logicalWidth;
    view.setTransform (juce::AffineTransform::scale (scale));

    if (sizeReady)
        processor.setEditorWidth (getWidth());
}

} // namespace digga
