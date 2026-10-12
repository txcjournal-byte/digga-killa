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

    // Open as a window inside the DAW, never over all of it. Some hosts (FL Studio
    // on a scaled Windows display) apply the display scale on top of the logical
    // size, so the first guess is deliberately modest (about 55 % of the width and
    // 62 % of the height); fitToScreen() then checks the real on-screen size.
    auto area = juce::Rectangle<int> (1920, 1040);
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        area = display->userArea;

    const int fitWidth = juce::jmin ((int) (area.getWidth() * 0.55), (int) (area.getHeight() * 0.62 * aspect),
                                     MainView::logicalWidth);
    // a size the user chose is kept, as long as it still fits this screen
    const int wanted = savedWidth > 0 ? juce::jmin (savedWidth, (int) (area.getHeight() * 0.80 * aspect), area.getWidth() - 80)
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

void TrapWinEditor::parentHierarchyChanged()
{
    if (fittedToScreen || getPeer() == nullptr)
        return;

    fittedToScreen = true;
    juce::Component::SafePointer<TrapWinEditor> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->fitToScreen(); });
}

// Shrinks the window if, as actually shown by the host (including any scaling
// the host adds), it is taller or wider than the screen it is on.
void TrapWinEditor::fitToScreen()
{
    const auto onScreen = getScreenBounds();
    if (onScreen.isEmpty())
        return;

    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (onScreen);
    if (display == nullptr)
        return;

    const auto area = display->userArea;
    const double ratio = juce::jmin (area.getWidth() * 0.80 / onScreen.getWidth(),
                                     area.getHeight() * 0.80 / onScreen.getHeight());
    if (ratio >= 1.0)
        return;

    constexpr double aspect = (double) MainView::logicalWidth / (double) MainView::logicalHeight;
    const int width = juce::jmax (MainView::logicalWidth * 6 / 10, (int) (getWidth() * ratio));
    setSize (width, juce::roundToInt (width / aspect));
}

void TrapWinEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::logicalWidth;
    view.setTransform (juce::AffineTransform::scale (scale));

    if (sizeReady)
        processor.setEditorWidth (getWidth());
}

} // namespace digga
