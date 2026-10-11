#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace digga::theme
{

// ---- trapwin.com "Chrome" design tokens (assets/trapwin.css)
inline const juce::Colour bg       { 0xff04060a };
inline const juce::Colour card     { 0xff0a0f18 };
inline const juce::Colour card2    { 0xff0b111c };
inline const juce::Colour well     { 0xff05070b };
inline const juce::Colour text     { 0xffe6ecf7 };
inline const juce::Colour text2    { 0xffa9b6cc };
inline const juce::Colour muted    { 0xff8794ab };
inline const juce::Colour accent   { 0xff7fa6e8 };
inline const juce::Colour accentL  { 0xff9fc3ff };
inline const juce::Colour glow     { 0x804676c4 };
inline const juce::Colour line     { 0x2e9fc3ff };   // rgba(159,195,255,.18)

/** Keeps the embedded typefaces alive (hold via SharedResourcePointer). */
struct Typefaces
{
    Typefaces();
    juce::Typeface::Ptr display;   // Archivo Black Italic, expanded (web headings)
    juce::Typeface::Ptr bold;      // Archivo ExtraBold
    juce::Typeface::Ptr body;      // Archivo SemiBold
    juce::Typeface::Ptr mono;      // IBM Plex Mono Medium
    juce::Typeface::Ptr monoBold;  // IBM Plex Mono SemiBold
};

juce::Font display (float height);
juce::Font bold (float height);
juce::Font body (float height);
juce::Font mono (float height, bool semiBold = false);

/** The TrapWin logo (chrome TRAPWIN, stars, "AUDIO · UNDERGROUND"), as on
    the other TrapWin plugins; transparent background. */
juce::Image logo();

/** Draws the logo fitted into an area, keeping its proportions. */
void drawLogo (juce::Graphics&, juce::Rectangle<float> area,
               juce::RectanglePlacement placement = juce::RectanglePlacement::centred);

/** Upper-case mono label with the web's wide letter spacing. */
void drawMonoLabel (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                    juce::Justification, juce::Colour colour, float height = 11.0f);

/** Glyph outline of a single line of text fitted into an area. */
juce::Path textPath (const juce::String& text, const juce::Font& font, juce::Rectangle<float> area,
                     juce::Justification justification = juce::Justification::centredLeft);

/** Fills a path with the web's liquid-chrome gradient (+ blue glow). */
void fillChrome (juce::Graphics&, const juce::Path& path, bool withGlow = true);

/** Chrome pill button background (".tw-btn"). */
void drawChromePill (juce::Graphics&, juce::Rectangle<float> area, bool highlighted, bool down);

/** Dark rounded card with hairline border and soft blue glow (".tw-glow"). */
void drawCard (juce::Graphics&, juce::Rectangle<float> area, float radius = 20.0f);

/** Waveform from 0..1 peaks, mirrored around the centre. */
void drawWaveform (juce::Graphics&, const std::vector<float>& peaks, juce::Rectangle<float> area,
                   juce::Colour top, juce::Colour bottom);

} // namespace digga::theme
