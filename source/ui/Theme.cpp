#include "ui/Theme.h"

#include "BinaryData.h"

namespace digga::theme
{

Typefaces::Typefaces()
    : display  (juce::Typeface::createSystemTypefaceFor (BinaryData::AntonRegular_ttf,
                                                         BinaryData::AntonRegular_ttfSize)),
      mono     (juce::Typeface::createSystemTypefaceFor (BinaryData::CourierPrimeRegular_ttf,
                                                         BinaryData::CourierPrimeRegular_ttfSize)),
      monoBold (juce::Typeface::createSystemTypefaceFor (BinaryData::CourierPrimeBold_ttf,
                                                         BinaryData::CourierPrimeBold_ttfSize))
{
}

juce::Font display (float height)
{
    const juce::SharedResourcePointer<Typefaces> faces;
    return juce::Font (juce::FontOptions (faces->display).withHeight (height));
}

juce::Font mono (float height, bool bold)
{
    const juce::SharedResourcePointer<Typefaces> faces;
    return juce::Font (juce::FontOptions (bold ? faces->monoBold : faces->mono).withHeight (height));
}

juce::Path textPath (const juce::String& text, const juce::Font& font,
                     juce::Rectangle<float> area, bool stretch, juce::Justification justification)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);

    juce::Path path;
    glyphs.createPath (path);

    if (! path.isEmpty())
        path.applyTransform (path.getTransformToScaleToFit (area, ! stretch, justification));

    return path;
}

void addGrunge (juce::Graphics& g, const juce::Path& clip, juce::Colour speckColour,
                int seed, float density)
{
    const auto bounds = clip.getBounds();
    if (bounds.isEmpty())
        return;

    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (clip);

    juce::Random random (seed);
    const int numSpecks = (int) (bounds.getWidth() * bounds.getHeight() * density * 0.01f);

    for (int i = 0; i < numSpecks; ++i)
    {
        const float x = bounds.getX() + random.nextFloat() * bounds.getWidth();
        const float y = bounds.getY() + random.nextFloat() * bounds.getHeight();
        const float r = 0.3f + std::pow (random.nextFloat(), 3.0f) * 2.2f;
        g.setColour (speckColour.withMultipliedAlpha (0.45f + 0.55f * random.nextFloat()));
        g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f * (0.6f + 0.8f * random.nextFloat()));
    }

    // a few scratches
    for (int i = 0; i < numSpecks / 60; ++i)
    {
        const float x = bounds.getX() + random.nextFloat() * bounds.getWidth();
        const float y = bounds.getY() + random.nextFloat() * bounds.getHeight();
        const float angle = random.nextFloat() * juce::MathConstants<float>::twoPi;
        const float length = 3.0f + random.nextFloat() * 14.0f;
        g.setColour (speckColour.withMultipliedAlpha (0.35f + 0.4f * random.nextFloat()));
        g.drawLine (x, y, x + std::cos (angle) * length, y + std::sin (angle) * length,
                    0.4f + random.nextFloat() * 0.6f);
    }
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> area, juce::Justification justification)
{
    const auto font = display (100.0f);

    juce::GlyphArrangement trap, vst;
    trap.addLineOfText (font, "Trap", 0.0f, 0.0f);
    const float trapWidth = trap.getBoundingBox (0, -1, true).getRight() + 1.0f;
    vst.addLineOfText (font, "VST", trapWidth, 0.0f);

    juce::Path trapPath, vstPath;
    trap.createPath (trapPath);
    vst.createPath (vstPath);

    juce::Path all (trapPath);
    all.addPath (vstPath);
    const auto transform = all.getTransformToScaleToFit (area, true, justification);
    trapPath.applyTransform (transform);
    vstPath.applyTransform (transform);

    g.setColour (ink);
    g.fillPath (trapPath);
    g.setColour (red);
    g.fillPath (vstPath);
}

} // namespace digga::theme
