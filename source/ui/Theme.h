#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace digga::theme
{

// ---- palette: dirty white paper, black ink, one red accent (stamp ink)
inline const juce::Colour paper      { 0xffe8e1cf };
inline const juce::Colour paperLight { 0xfff1ebdc };
inline const juce::Colour paperDark  { 0xffcfc5ad };
inline const juce::Colour ink        { 0xff161513 };
inline const juce::Colour inkSoft    { 0xff45423c };
inline const juce::Colour muted      { 0xff938d80 };
inline const juce::Colour red        { 0xffc8202a };

/** Keeps the embedded typefaces alive; hold one of these (via
    juce::SharedResourcePointer) for as long as the UI exists. */
struct Typefaces
{
    Typefaces();

    juce::Typeface::Ptr display;   // Anton (OFL) – heavy condensed headlines
    juce::Typeface::Ptr mono;      // Courier Prime (OFL) – typewriter labels
    juce::Typeface::Ptr monoBold;
};

juce::Font display (float height);
juce::Font mono (float height, bool bold = false);

/** Glyphs of a single line of text as a path, fitted into an area. With
    stretch = true the proportions are ignored (poster-style lettering). */
juce::Path textPath (const juce::String& text, const juce::Font& font,
                     juce::Rectangle<float> area, bool stretch,
                     juce::Justification justification = juce::Justification::centred);

/** Knocks tiny paper-coloured specks and scratches out of whatever is inside
    the clip path — the worn print look. Deterministic for a given seed. */
void addGrunge (juce::Graphics& g, const juce::Path& clip, juce::Colour speckColour,
                int seed, float density);

/** The "TrapVST" wordmark: black "Trap", red "VST". */
void drawLogo (juce::Graphics& g, juce::Rectangle<float> area,
               juce::Justification justification = juce::Justification::centredLeft);

} // namespace digga::theme
