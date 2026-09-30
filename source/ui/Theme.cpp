#include "ui/Theme.h"

#include "BinaryData.h"

namespace digga::theme
{

Typefaces::Typefaces()
    : display  (juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoBlackRegular_ttf,
                                                         BinaryData::ArchivoBlackRegular_ttfSize)),
      condensed (juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedSemiBold_ttf,
                                                          BinaryData::BarlowCondensedSemiBold_ttfSize)),
      condensedBold (juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedBold_ttf,
                                                              BinaryData::BarlowCondensedBold_ttfSize)),
      mono     (juce::Typeface::createSystemTypefaceFor (BinaryData::CourierPrimeRegular_ttf,
                                                         BinaryData::CourierPrimeRegular_ttfSize)),
      monoBold (juce::Typeface::createSystemTypefaceFor (BinaryData::CourierPrimeBold_ttf,
                                                         BinaryData::CourierPrimeBold_ttfSize))
{
}

juce::Font display (float height, float horizontalScale)
{
    const juce::SharedResourcePointer<Typefaces> faces;
    return juce::Font (juce::FontOptions (faces->display).withHeight (height)
                                                         .withHorizontalScale (horizontalScale));
}

juce::Font condensed (float height, bool bold)
{
    const juce::SharedResourcePointer<Typefaces> faces;
    return juce::Font (juce::FontOptions (bold ? faces->condensedBold : faces->condensed).withHeight (height));
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

juce::Image skinBackground()
{
    return juce::ImageCache::getFromMemory (BinaryData::background_png, BinaryData::background_pngSize);
}

juce::Image skinDropSample()
{
    return juce::ImageCache::getFromMemory (BinaryData::drop_sample_png, BinaryData::drop_sample_pngSize);
}

juce::Image skinKillStamp()
{
    return juce::ImageCache::getFromMemory (BinaryData::kill_stamp_png, BinaryData::kill_stamp_pngSize);
}

} // namespace digga::theme
