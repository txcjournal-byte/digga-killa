#include "ui/Theme.h"

#include "BinaryData.h"

namespace digga::theme
{

Typefaces::Typefaces()
    : display  (juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoDisplayBlackItalic_ttf, BinaryData::ArchivoDisplayBlackItalic_ttfSize)),
      bold     (juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoExtraBold_ttf, BinaryData::ArchivoExtraBold_ttfSize)),
      body     (juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoSemiBold_ttf, BinaryData::ArchivoSemiBold_ttfSize)),
      mono     (juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize)),
      monoBold (juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoSemiBold_ttf, BinaryData::IBMPlexMonoSemiBold_ttfSize))
{
}

namespace
{
    juce::Font make (juce::Typeface::Ptr Typefaces::* face, float height)
    {
        const juce::SharedResourcePointer<Typefaces> faces;
        return juce::Font (juce::FontOptions ((*faces).*face).withHeight (height));
    }
}

juce::Font display (float h)          { return make (&Typefaces::display, h); }
juce::Font bold (float h)             { return make (&Typefaces::bold, h); }
juce::Font body (float h)             { return make (&Typefaces::body, h); }
juce::Font mono (float h, bool semi)  { return make (semi ? &Typefaces::monoBold : &Typefaces::mono, h); }

juce::Image logo()
{
    return juce::ImageCache::getFromMemory (BinaryData::trapwin_logo_png, BinaryData::trapwin_logo_pngSize);
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> area, juce::RectanglePlacement placement)
{
    const juce::Graphics::ScopedSaveState state (g);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (logo(), area, placement);
}

void drawMonoLabel (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area,
                    juce::Justification j, juce::Colour colour, float height)
{
    g.setColour (colour);
    g.setFont (mono (height, true).withExtraKerningFactor (0.16f));
    g.drawText (s.toUpperCase(), area, j, false);
}

juce::Path textPath (const juce::String& s, const juce::Font& font, juce::Rectangle<float> area, juce::Justification j)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, s, 0.0f, 0.0f);
    juce::Path p;
    glyphs.createPath (p);
    if (! p.isEmpty())
        p.applyTransform (p.getTransformToScaleToFit (area, true, j));
    return p;
}

void fillChrome (juce::Graphics& g, const juce::Path& path, bool withGlow)
{
    const auto b = path.getBounds();
    if (b.isEmpty())
        return;

    if (withGlow)
    {
        // soft blue drop-shadow glow
        juce::DropShadow (glow.withAlpha (0.55f), (int) juce::jmax (6.0f, b.getHeight() * 0.35f), {}).drawForPath (g, path);
    }

    juce::ColourGradient chrome (juce::Colour (0xff9aa3b6), 0.0f, b.getY(), juce::Colour (0xfff7f9fd), 0.0f, b.getBottom(), false);
    chrome.addColour (0.32, juce::Colour (0xfff5f7fc));
    chrome.addColour (0.47, juce::Colour (0xff9099ad));
    chrome.addColour (0.50, juce::Colour (0xff151b29));
    chrome.addColour (0.57, juce::Colour (0xff6f7a90));
    chrome.addColour (0.82, juce::Colour (0xffdfe5ef));
    g.setGradientFill (chrome);
    g.fillPath (path);
}

void drawChromePill (juce::Graphics& g, juce::Rectangle<float> r, bool highlighted, bool down)
{
    const float radius = r.getHeight() * 0.5f;
    juce::Path pill;
    pill.addRoundedRectangle (r, radius);

    juce::DropShadow (glow.withAlpha (highlighted ? 0.6f : 0.35f), 14, {}).drawForPath (g, pill);

    juce::ColourGradient grad (juce::Colour (0xfff4f6fb), 0.0f, r.getY(), juce::Colour (0xffe6eaf2), 0.0f, r.getBottom(), false);
    grad.addColour (0.48, juce::Colour (0xffc9d0dd));
    grad.addColour (0.52, juce::Colour (0xff8e98ab));
    g.setGradientFill (grad);
    g.fillPath (pill);

    if (down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.15f));
        g.fillPath (pill);
    }
}

void drawCard (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    juce::DropShadow (juce::Colour (0x294676c4), 40, {}).drawForPath (g, shape);
    g.setColour (card);
    g.fillPath (shape);
    g.setColour (line);
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

void drawWaveform (juce::Graphics& g, const std::vector<float>& peaks, juce::Rectangle<float> area,
                   juce::Colour top, juce::Colour bottom)
{
    if (peaks.empty() || area.isEmpty())
        return;

    const auto n = (int) peaks.size();
    const float step = area.getWidth() / (float) juce::jmax (1, n - 1);
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;

    juce::Path wave;
    wave.startNewSubPath (area.getX(), mid);
    for (int i = 0; i < n; ++i)
        wave.lineTo (area.getX() + (float) i * step, mid - juce::jmax (0.5f, peaks[(size_t) i] * half));
    for (int i = n - 1; i >= 0; --i)
        wave.lineTo (area.getX() + (float) i * step, mid + juce::jmax (0.5f, peaks[(size_t) i] * half));
    wave.closeSubPath();

    g.setGradientFill (juce::ColourGradient (top, 0.0f, area.getY(), bottom, 0.0f, area.getBottom(), false));
    g.fillPath (wave);
}

} // namespace digga::theme
