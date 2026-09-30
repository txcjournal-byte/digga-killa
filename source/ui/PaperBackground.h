#pragma once

#include "ui/Theme.h"

namespace digga
{

/** Procedural worn "white label" sleeve paper. Rendered once into an image at
    the current physical scale and reused. */
class PaperBackground
{
public:
    void draw (juce::Graphics& g, juce::Rectangle<int> area);

private:
    static juce::Image render (int width, int height, float scale);

    juce::Image cache;
    float cachedScale = 0.0f;
    juce::Rectangle<int> cachedArea;
};

} // namespace digga
