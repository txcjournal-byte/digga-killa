#include "ui/PaperBackground.h"

namespace digga
{

void PaperBackground::draw (juce::Graphics& g, juce::Rectangle<int> area)
{
    const float scale = juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    if (cache.isNull() || area != cachedArea || std::abs (scale - cachedScale) > 0.05f)
    {
        cache = render (area.getWidth(), area.getHeight(), scale);
        cachedScale = scale;
        cachedArea = area;
    }

    g.drawImage (cache, area.toFloat());
}

juce::Image PaperBackground::render (int width, int height, float scale)
{
    using namespace theme;

    juce::Image image (juce::Image::RGB, juce::roundToInt ((float) width * scale),
                       juce::roundToInt ((float) height * scale), true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto bounds = juce::Rectangle<float> ((float) width, (float) height);
    const float w = bounds.getWidth(), h = bounds.getHeight();
    juce::Random random (0xd1994);

    // dark surround so the worn edges read as torn card
    g.fillAll (juce::Colour (0xff2a2724));

    // card with slightly irregular edges
    juce::Path card;
    {
        const float inset = 7.0f;
        const int steps = 60;
        auto jitter = [&random] { return (random.nextFloat() - 0.5f) * 3.0f; };
        card.startNewSubPath (inset, inset);
        for (int i = 1; i <= steps; ++i) card.lineTo (inset + (w - 2 * inset) * (float) i / steps, inset + jitter());
        for (int i = 1; i <= steps; ++i) card.lineTo (w - inset + jitter(), inset + (h - 2 * inset) * (float) i / steps);
        for (int i = 1; i <= steps; ++i) card.lineTo (w - inset - (w - 2 * inset) * (float) i / steps, h - inset + jitter());
        for (int i = 1; i <= steps; ++i) card.lineTo (inset + jitter(), h - inset - (h - 2 * inset) * (float) i / steps);
        card.closeSubPath();
    }

    g.setColour (paper);
    g.fillPath (card);

    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (card);

        // large soft blotches of uneven ageing
        for (int i = 0; i < 40; ++i)
        {
            const float cx = random.nextFloat() * w, cy = random.nextFloat() * h;
            const float r = 40.0f + random.nextFloat() * 180.0f;
            const auto tone = random.nextBool() ? paperLight : paperDark;
            juce::ColourGradient blotch (tone.withAlpha (0.25f), cx, cy, tone.withAlpha (0.0f), cx + r, cy, true);
            g.setGradientFill (blotch);
            g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        }

        // fibres and dirt specks
        for (int i = 0; i < 9000; ++i)
        {
            const float x = random.nextFloat() * w, y = random.nextFloat() * h;
            const float r = 0.25f + std::pow (random.nextFloat(), 4.0f) * 1.8f;
            g.setColour (juce::Colour (0xff3b3226).withAlpha (0.04f + random.nextFloat() * 0.14f));
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
        }

        for (int i = 0; i < 500; ++i)
        {
            const float x = random.nextFloat() * w, y = random.nextFloat() * h;
            const float a = random.nextFloat() * juce::MathConstants<float>::twoPi;
            const float l = 2.0f + random.nextFloat() * 8.0f;
            g.setColour (juce::Colour (0xff5b4f3c).withAlpha (0.05f + random.nextFloat() * 0.08f));
            g.drawLine (x, y, x + std::cos (a) * l, y + std::sin (a) * l, 0.5f);
        }

        // creases
        for (int i = 0; i < 3; ++i)
        {
            const bool horizontal = random.nextBool();
            const float p = (0.2f + random.nextFloat() * 0.6f) * (horizontal ? h : w);
            juce::Path crease;
            crease.startNewSubPath (horizontal ? 0.0f : p, horizontal ? p : 0.0f);
            crease.quadraticTo (horizontal ? w * 0.5f : p + 8.0f, horizontal ? p + 6.0f : h * 0.5f,
                                horizontal ? w : p - 4.0f, horizontal ? p - 3.0f : h);
            g.setColour (paperDark.withAlpha (0.35f));
            g.strokePath (crease, juce::PathStrokeType (1.6f));
            g.setColour (paperLight.withAlpha (0.5f));
            g.strokePath (crease, juce::PathStrokeType (0.8f), juce::AffineTransform::translation (0.0f, 1.2f));
        }

        // edge wear: darkened, dirty border
        const float edge = 38.0f;
        auto edgeGradient = [&] (juce::Point<float> from, juce::Point<float> to)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6f5f45).withAlpha (0.35f), from,
                                                     juce::Colour (0xff6f5f45).withAlpha (0.0f), to, false));
        };
        edgeGradient ({ 0, 0 }, { 0, edge });          g.fillRect (0.0f, 0.0f, w, edge);
        edgeGradient ({ 0, h }, { 0, h - edge });      g.fillRect (0.0f, h - edge, w, edge);
        edgeGradient ({ 0, 0 }, { edge, 0 });          g.fillRect (0.0f, 0.0f, edge, h);
        edgeGradient ({ w, 0 }, { w - edge, 0 });      g.fillRect (w - edge, 0.0f, edge, h);

        // chipped corners and edge specks
        for (int i = 0; i < 1400; ++i)
        {
            const int side = random.nextInt (4);
            const float t = random.nextFloat();
            const float d = std::pow (random.nextFloat(), 2.5f) * 22.0f;
            const float x = side == 0 ? t * w : side == 1 ? w - d : side == 2 ? t * w : d;
            const float y = side == 0 ? d : side == 1 ? t * h : side == 2 ? h - d : t * h;
            const float r = 0.4f + random.nextFloat() * 1.8f;
            g.setColour (juce::Colour (0xff3a3025).withAlpha (0.1f + random.nextFloat() * 0.3f));
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
        }
    }

    g.setColour (juce::Colour (0xff4a4034).withAlpha (0.6f));
    g.strokePath (card, juce::PathStrokeType (1.0f));

    return image;
}

} // namespace digga
