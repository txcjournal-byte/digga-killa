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

    openedWithoutSavedSize = p.getEditorWidth() <= 0;
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

// Sizes the window from how big it really is on screen (including any scaling
// the host adds): on first open it fills about 88 % of the screen height, as big
// and readable as possible while still fully visible; a size the user chose is
// only ever shrunk, and only if it no longer fits.
void TrapWinEditor::fitToScreen()
{
    const auto onScreen = getScreenBounds();
    if (onScreen.isEmpty())
        return;

    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (onScreen);
    if (display == nullptr)
        return;

    const auto area = display->userArea;
    const double ratio = juce::jmin (area.getWidth() * 0.94 / onScreen.getWidth(),
                                     area.getHeight() * 0.88 / onScreen.getHeight());
    const bool userSized = processor.getEditorWidth() > 0 && ! openedWithoutSavedSize;
    if (std::abs (ratio - 1.0) < 0.03 || (userSized && ratio >= 1.0))
        return;

    constexpr double aspect = (double) MainView::logicalWidth / (double) MainView::logicalHeight;
    const int width = juce::jlimit (MainView::logicalWidth * 6 / 10, MainView::logicalWidth * 2, (int) (getWidth() * ratio));
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
