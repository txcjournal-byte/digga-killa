#include "ui/FxRack.h"

#include <array>
#include <cmath>

namespace digga
{

namespace
{
    using fx::FxSettings;

    const juce::Colour red      { 0xffff3b4f };
    const juce::Colour panelTop { 0xff0d1420 };
    const juce::Colour panelBot { 0xff080c14 };

    constexpr float arcStart = -2.35f, arcEnd = 2.35f;

    juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    /** Largest semi-bold mono height (<= maxH) at which `text` fits `width`. */
    float fitMono (const juce::String& text, float width, float maxH, float kerning)
    {
        float h = maxH;
        while (h > 6.0f && juce::GlyphArrangement::getStringWidth (theme::mono (h, true).withExtraKerningFactor (kerning), text) > width)
            h -= 0.25f;
        return h;
    }

    void drawFitted (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification j,
                     juce::Colour colour, float maxH, float kerning = 0.14f, bool caps = true)
    {
        const auto upper = caps ? text.toUpperCase() : text;
        const float h = fitMono (upper, area.getWidth(), maxH, kerning);
        g.setColour (colour);
        g.setFont (theme::mono (h, true).withExtraKerningFactor (h < maxH ? kerning * 0.5f : kerning));
        g.drawText (upper, area, j, false);
    }

    // ---- FILTER knob: -1 low-pass (cutoff down) .. 0 off .. +1 high-pass (cutoff up)
    constexpr float lpTop = 18000.0f, lpBottom = 200.0f, hpBottom = 30.0f, hpTop = 5000.0f;

    float lpFreq (float a) { return lpTop * std::pow (lpBottom / lpTop, a); }
    float hpFreq (float a) { return hpBottom * std::pow (hpTop / hpBottom, a); }

    float filterKnobValue (const FxSettings& s)
    {
        if (! s.filterOn)
            return 0.0f;
        const float f = juce::jlimit (20.0f, 20000.0f, s.filterFreq);
        if (s.filterType == 0)
            return -juce::jlimit (0.02f, 1.0f, std::log (f / lpTop) / std::log (lpBottom / lpTop));
        return juce::jlimit (0.02f, 1.0f, std::log (f / hpBottom) / std::log (hpTop / hpBottom));
    }

    juce::String hzText (float f)
    {
        if (f < 1000.0f)
            return juce::String (juce::roundToInt (f / (f < 200.0f ? 1.0f : 10.0f)) * (f < 200.0f ? 1 : 10)) + " Hz";
        return juce::String (f / 1000.0f, f < 10000.0f ? 1 : 0) + " kHz";
    }

    // ==========================================================================
    /** Rotary: chrome cap, blue value arc, value text below. Unipolar knobs are
        "off" at the minimum, bipolar ones at the centre (with a detent).
        Drag up/down (shift = fine), double-click resets, mouse wheel. */
    class Knob : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        Knob (float lo, float hi, float defaultValue, bool isBipolar, std::function<juce::String (float)> formatter)
            : minimum (lo), maximum (hi), def (defaultValue), bipolar (isBipolar), format (std::move (formatter))
        {
            setRepaintsOnMouseActivity (true);
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        }

        void setValue (float v)
        {
            v = juce::jlimit (minimum, maximum, v);
            if (! juce::approximatelyEqual (v, value))
            {
                value = v;
                repaint();
            }
        }

        /** Small captions beside the dial's two ends (bipolar: "LP" / "HP"). */
        void setEndLabels (juce::String left, juce::String right) { leftLabel = std::move (left); rightLabel = std::move (right); }

        std::function<void (float)> onValue;   // live, while dragging
        std::function<void()> onCommit;        // gesture ended

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            auto r = getLocalBounds().toFloat();
            const float valueH = juce::jlimit (16.0f, 20.0f, r.getHeight() * 0.17f);
            const auto valueArea = r.removeFromBottom (valueH);
            r.removeFromBottom (3.0f);
            const bool hot = isMouseOverOrDragging() && isEnabled();
            const bool active = bipolar ? ! juce::approximatelyEqual (value, 0.0f) : value > minimum + 1.0e-4f;

            drawFitted (g, format (value), valueArea, juce::Justification::centred,
                        hot ? accentL : (active ? text : text2), juce::jlimit (13.0f, 15.0f, valueH * 0.78f), 0.06f, false);

            // room for the arc's glow on every side (and for the end labels under the arc's ends)
            const bool ends = leftLabel.isNotEmpty();
            const float dia = juce::jmin (r.getWidth() - 14.0f, r.getHeight() - (ends ? 14.0f : 8.0f), 82.0f);
            if (dia < 16.0f)
                return;
            const auto c = r.getCentre().translated (0.0f, ends ? -4.0f : 0.0f);
            const float radius = dia * 0.5f;
            const float angle = arcStart + (arcEnd - arcStart) * toNorm (value);

            if (ends)
            {
                const auto at = [&] (float side) { return juce::Point<float> (c.x + side * (radius * 0.72f + 4.0f), c.y + radius * 0.7f + 9.0f); };
                const bool lOn = active && value < 0.0f, rOn = active && value > 0.0f;
                g.setFont (mono (12.0f, true).withExtraKerningFactor (0.06f));
                g.setColour (lOn ? accentL : muted);
                g.drawText (leftLabel, juce::Rectangle<float> (28.0f, 16.0f).withCentre (at (-1.0f)), juce::Justification::centred, false);
                g.setColour (rOn ? accentL : muted);
                g.drawText (rightLabel, juce::Rectangle<float> (28.0f, 16.0f).withCentre (at (1.0f)), juce::Justification::centred, false);
            }

            // track + value arc (bipolar: from the top centre)
            const float arcR = radius - 2.0f, arcW = juce::jmax (3.0f, dia * 0.075f);
            juce::Path track;
            track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, arcStart, arcEnd, true);
            g.setColour (juce::Colour (0xff151c2a));
            g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (bipolar)
            {
                g.setColour (muted.withAlpha (0.7f));
                g.fillRect (juce::Rectangle<float> (2.0f, arcW + 3.0f).withCentre ({ c.x, c.y - arcR }));
            }

            if (active)
            {
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, bipolar ? juce::jmin (0.0f, angle) : arcStart,
                                   bipolar ? juce::jmax (0.0f, angle) : angle, true);
                g.setColour (accent.withAlpha (0.28f));
                g.strokePath (arc, juce::PathStrokeType (arcW * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (hot ? accentL : accentL.interpolatedWith (accent, 0.35f));
                g.strokePath (arc, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // chrome cap with a dark bezel
            const float capR = radius * 0.7f;
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (juce::Rectangle<float> (capR * 2.0f + 4.0f, capR * 2.0f + 4.0f).withCentre (c.translated (0.0f, 1.5f)));
            g.setColour (well);
            g.fillEllipse (juce::Rectangle<float> (capR * 2.0f + 3.0f, capR * 2.0f + 3.0f).withCentre (c));

            const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
            juce::ColourGradient chrome (juce::Colour (0xfff4f6fb), 0.0f, cap.getY(), juce::Colour (0xff5b6478), 0.0f, cap.getBottom(), false);
            chrome.addColour (0.45, juce::Colour (0xffc3cad8));
            chrome.addColour (0.55, juce::Colour (0xff8a93a7));
            g.setGradientFill (chrome);
            g.fillEllipse (cap);

            const auto face = cap.reduced (capR * 0.2f);
            juce::ColourGradient faceGrad (juce::Colour (0xff9aa3b6), 0.0f, face.getY(), juce::Colour (0xffeef2f8), 0.0f, face.getBottom(), false);
            g.setGradientFill (faceGrad);
            g.fillEllipse (face);
            if (! active)
            {
                g.setColour (well.withAlpha (0.35f));
                g.fillEllipse (cap);
            }

            // pointer
            const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
            g.setColour (juce::Colour (0xff0a0f18));
            g.drawLine ({ c + dir * (capR * 0.25f), c + dir * (capR * 0.9f) }, juce::jmax (2.0f, dia * 0.05f));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragStartNorm = toNorm (value);
            dragStartY = e.position.y;
            dragged = false;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const float sens = e.mods.isShiftDown() ? 900.0f : 200.0f;
            const float n = juce::jlimit (0.0f, 1.0f, dragStartNorm + (dragStartY - e.position.y) / sens);
            dragged = change (fromNorm (n)) || dragged;
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragged && onCommit)
                onCommit();
            dragged = false;
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (change (def) && onCommit)
                onCommit();
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            const float delta = (w.isReversed ? -w.deltaY : w.deltaY) * (e.mods.isShiftDown() ? 0.01f : 0.06f);
            if (change (fromNorm (juce::jlimit (0.0f, 1.0f, toNorm (value) + delta))))
                startTimer (350);   // report once the wheel rests
        }

    private:
        void timerCallback() override
        {
            stopTimer();
            if (onCommit)
                onCommit();
        }

        bool change (float v)
        {
            v = juce::jlimit (minimum, maximum, v);
            if (bipolar && std::abs (v) < 0.04f * (maximum - minimum))
                v = 0.0f;   // centre detent: off
            if (juce::approximatelyEqual (v, value))
                return false;
            value = v;
            repaint();
            if (onValue)
                onValue (value);
            return true;
        }

        float toNorm (float v) const   { return (v - minimum) / (maximum - minimum); }
        float fromNorm (float n) const { return minimum + (maximum - minimum) * n; }

        float minimum, maximum, def;
        bool bipolar;
        std::function<juce::String (float)> format;
        juce::String leftLabel, rightLabel;
        float value = 0.0f;
        bool dragged = false;
        float dragStartNorm = 0.0f, dragStartY = 0.0f;
    };

    // ==========================================================================
    /** Segmented pill selector with big, readable labels. The "off" item, when
        selected, is drawn quiet so only real choices light up in chrome. */
    class Pills : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Pills (juce::StringArray itemNames, std::vector<int> itemValues, int offItemValue = -1)
            : items (std::move (itemNames)), values (std::move (itemValues)), offValue (offItemValue)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setRepaintsOnMouseActivity (true);
        }

        void setSelected (int v)    { if (v != selected) { selected = v; repaint(); } }
        int getSelected() const     { return selected; }

        std::function<void (int)> onSelect;

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const float rad = r.getHeight() * 0.5f;
            g.setColour (well);
            g.fillRoundedRectangle (r, rad);
            g.setColour (line.withMultipliedAlpha (1.4f));
            g.drawRoundedRectangle (r, rad, 1.0f);

            float fontH = juce::jlimit (13.0f, 16.0f, r.getHeight() * 0.44f);
            for (auto& item : items)
                fontH = juce::jmin (fontH, juce::jmax (11.0f, fitMono (item, segment (0).getWidth() - 6.0f, fontH, 0.02f)));

            for (int i = 0; i < items.size(); ++i)
            {
                const auto s = segment (i);
                const bool sel = values[(size_t) i] == selected;
                const bool quiet = sel && selected == offValue;
                const bool hover = isMouseOver() && s.contains (getMouseXYRelative().toFloat());

                // hairline separators between unselected neighbours
                if (i > 0 && ! sel && values[(size_t) i - 1] != selected)
                {
                    g.setColour (line);
                    g.fillRect (juce::Rectangle<float> (1.0f, s.getHeight() * 0.4f).withCentre ({ s.getX(), s.getCentreY() }));
                }

                if (sel)
                {
                    const auto p = s.reduced (2.0f);
                    if (quiet)
                    {
                        g.setColour (juce::Colour (0xff1a2232));
                        g.fillRoundedRectangle (p, p.getHeight() * 0.5f);
                    }
                    else
                    {
                        juce::Path pill;
                        pill.addRoundedRectangle (p, p.getHeight() * 0.5f);
                        juce::DropShadow (glow.withAlpha (0.7f), 10, {}).drawForPath (g, pill);
                        juce::ColourGradient grad (juce::Colour (0xfff4f6fb), 0.0f, p.getY(), juce::Colour (0xffdfe4ec), 0.0f, p.getBottom(), false);
                        grad.addColour (0.48, juce::Colour (0xffc9d0dd));
                        grad.addColour (0.52, juce::Colour (0xffa9b2c3));
                        g.setGradientFill (grad);
                        g.fillPath (pill);
                    }
                }
                else if (hover)
                {
                    g.setColour (accent.withAlpha (0.12f));
                    g.fillRoundedRectangle (s.reduced (2.0f), (s.getHeight() - 4.0f) * 0.5f);
                }

                g.setColour (sel ? (quiet ? text2 : juce::Colour (0xff070b12)) : (hover ? text : text2));
                // the vulgar fractions (1/2, 1/4) are small glyphs: draw them bigger
                const bool fraction = items[i].containsAnyOf (utf8 ("\xc2\xbd\xc2\xbc"));
                g.setFont (mono (fraction ? fontH * 1.55f : fontH, true).withExtraKerningFactor (0.02f));
                g.drawText (items[i], s, juce::Justification::centred, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < items.size(); ++i)
                if (segment (i).contains (e.position) && values[(size_t) i] != selected)
                {
                    selected = values[(size_t) i];
                    repaint();
                    if (onSelect)
                        onSelect (selected);
                }
        }

    private:
        juce::Rectangle<float> segment (int i) const
        {
            const auto b = getLocalBounds().toFloat();
            const auto r = b.reduced (juce::jmin (6.0f, b.getHeight() * 0.16f), 0.5f);   // clear of the rounded ends
            const float w = r.getWidth() / (float) juce::jmax (1, items.size());
            return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
        }

        juce::StringArray items;
        std::vector<int> values;
        int offValue;
        int selected = 0;
    };

    // ==========================================================================
    /** KILL: small red liquid-chrome slab. */
    class KillButton : public juce::Button
    {
    public:
        KillButton() : juce::Button ("KILL")
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("KILL: a new random effect rack");
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace theme;
            auto r = getLocalBounds().toFloat().reduced (4.0f);
            if (down)
                r = r.reduced (1.0f).translated (0.0f, 1.0f);
            const float rad = juce::jmin (12.0f, r.getHeight() * 0.3f);
            juce::Path slab;
            slab.addRoundedRectangle (r, rad);

            juce::DropShadow (red.withAlpha (over ? 0.7f : 0.45f), over ? 16 : 12, {}).drawForPath (g, slab);

            juce::ColourGradient grad (juce::Colour (0xffffc2c8), 0.0f, r.getY(), juce::Colour (0xffff6a7a), 0.0f, r.getBottom(), false);
            grad.addColour (0.30, juce::Colour (0xffff7d8b));
            grad.addColour (0.48, juce::Colour (0xffe0233a));
            grad.addColour (0.52, juce::Colour (0xff5c0712));
            grad.addColour (0.60, juce::Colour (0xffb3172b));
            grad.addColour (0.85, juce::Colour (0xffff3b4f));
            g.setGradientFill (grad);
            g.fillPath (slab);

            // glossy top edge + hairline
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (1.0f), rad - 1.0f, 1.0f);
            g.setColour (juce::Colour (0xff3a0008).withAlpha (0.8f));
            g.drawRoundedRectangle (r, rad, 1.0f);
            if (over)
            {
                g.setColour (juce::Colours::white.withAlpha (down ? 0.0f : 0.08f));
                g.fillPath (slab);
            }

            // label: chrome letters with a dark cut
            const auto textArea = r.reduced (r.getWidth() * 0.16f, r.getHeight() * 0.27f);
            auto glyphs = textPath ("KILL", display (r.getHeight()), textArea, juce::Justification::centred);
            juce::Path shadow (glyphs);
            shadow.applyTransform (juce::AffineTransform::translation (0.0f, 1.5f));
            g.setColour (juce::Colour (0xff2a0006).withAlpha (0.85f));
            g.fillPath (shadow);
            const auto gb = glyphs.getBounds();
            juce::ColourGradient letters (juce::Colours::white, 0.0f, gb.getY(), juce::Colour (0xffffd9dd), 0.0f, gb.getBottom(), false);
            letters.addColour (0.5, juce::Colour (0xfff2f4f8));
            letters.addColour (0.56, juce::Colour (0xffc9ced8));
            g.setGradientFill (letters);
            g.fillPath (glyphs);
        }
    };

    /** Quiet pill text button (RESET); a red dot marks older effects that are on
        but not shown in the strip (RESET clears them). */
    class PillButton : public juce::Button
    {
    public:
        explicit PillButton (const juce::String& name) : juce::Button (name) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        bool flagged = false;

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace theme;
            const auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (over ? juce::Colour (0xff141c2b) : well);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (over ? accent.withAlpha (0.7f) : line.withMultipliedAlpha (1.6f));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            drawFitted (g, getName(), r.reduced (8.0f, 0.0f).translated (0.0f, down ? 1.0f : 0.0f), juce::Justification::centred,
                        over ? theme::text : theme::text2, 12.0f, 0.14f);
            if (flagged)
            {
                g.setColour (red);
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getRight() - 9.0f, r.getY() + 7.0f }));
            }
        }
    };

    /** Dims the rack and shows why it is disabled. */
    class Overlay : public juce::Component
    {
    public:
        juce::String message;

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            const auto b = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (card.withAlpha (0.8f));
            g.fillRoundedRectangle (b, 16.0f);
            if (message.isEmpty())
                return;
            const auto f = mono (12.0f, true).withExtraKerningFactor (0.16f);
            const float w = juce::jmin (b.getWidth() - 40.0f, juce::GlyphArrangement::getStringWidth (f, message.toUpperCase()) + 44.0f);
            const auto pill = juce::Rectangle<float> (w, 32.0f).withCentre (b.getCentre());
            g.setColour (well.withAlpha (0.95f));
            g.fillRoundedRectangle (pill, 16.0f);
            g.setColour (line.withMultipliedAlpha (1.8f));
            g.drawRoundedRectangle (pill, 16.0f, 1.0f);
            drawMonoLabel (g, message, pill, juce::Justification::centred, text2, 12.0f);
        }
    };

    enum Module { mTime, mRepeat, mFilter, mDelay, mReverb, numModules };

    bool hiddenModulesOn (const FxSettings& s)
    {
        return s.driveOn || s.crushOn || s.gateOn || s.tapeStopOn || s.widthOn;
    }

    juce::String timeCaption (int mode)
    {
        switch (mode)
        {
            case 1:  return "Half speed  -12 st";
            case 2:  return "Quarter speed  -24 st";
            case 3:  return "Double speed  +12 st";
            default: return "Normal speed";
        }
    }

    juce::String repeatCaption (int rate)
    {
        switch (rate)
        {
            case 1:  return "1/2 note, every bar";
            case 2:  return "1/4 note, every 2 beats";
            case 3:  return "1/8 note, 2x per beat";
            case 4:  return "1/16 note, 4x per beat";
            case 5:  return "1/32 note, 8x per beat";
            default: return "No stutter";
        }
    }
}

// ==============================================================================
struct FxRack::Parts
{
    Pills time   { { "OFF", utf8 ("\xc2\xbd"), utf8 ("\xc2\xbc"), utf8 ("2\xc3\x97") }, { 0, 1, 2, 3 }, 0 };
    Pills repeat { { "OFF", "1/2", "1/4", "1/8", "1/16", "1/32" }, { 0, 1, 2, 3, 4, 5 }, 0 };
    Pills delayRate { { "1/8", "1/4" }, { 1, 0 } };
    std::unique_ptr<Knob> filter, delayMix, reverbMix;
    KillButton kill;
    PillButton reset { "Reset" };
    Overlay overlay;

    std::array<juce::Rectangle<float>, numModules> panels;
    juce::Rectangle<float> timeCaption, repeatCaption, rateLabel, side;
    juce::Random random;
};

FxRack::FxRack() : parts (std::make_unique<Parts>())
{
    using S = FxSettings;
    const S defaults;

    // HALFTIME / SPEED
    parts->time.setTooltip ("Tape speed: half, quarter or double (pitch follows). Loops keep their length.");
    parts->time.onSelect = [this] (int v) { settings.timeMode = v; refresh(); commit(); };
    addAndMakeVisible (parts->time);

    // REPEAT
    parts->repeat.setTooltip ("Stutter: repeats the first slice of every beat (or bar) on this grid");
    parts->repeat.onSelect = [this] (int v) { settings.repeatRate = v; refresh(); commit(); };
    addAndMakeVisible (parts->repeat);

    // FILTER: one bipolar knob onto filterOn / filterType / filterFreq
    parts->filter = std::make_unique<Knob> (-1.0f, 1.0f, 0.0f, true, [this] (float v)
    {
        if (juce::approximatelyEqual (v, 0.0f))
            return juce::String ("OFF");
        const juce::String type = v < 0.0f ? "LP " : (settings.filterType == 2 ? "BP " : "HP ");
        return type + hzText (v < 0.0f ? lpFreq (-v) : hpFreq (v));
    });
    parts->filter->setEndLabels ("LP", "HP");
    parts->filter->setTooltip ("Filter: turn left for low-pass, right for high-pass; centre is off");
    parts->filter->onValue = [this, defaults] (float v)
    {
        settings.filterOn = ! juce::approximatelyEqual (v, 0.0f);
        if (! settings.filterOn)
        {
            settings.filterType = defaults.filterType;
            settings.filterFreq = defaults.filterFreq;
        }
        else
        {
            if (v < 0.0f || settings.filterType != 2)
                settings.filterType = v < 0.0f ? 0 : 1;
            settings.filterFreq = v < 0.0f ? lpFreq (-v) : hpFreq (v);
        }
        repaint();
    };

    // DELAY: mix (0 = off) + rate
    const auto mixText = [] (float v)
    {
        return v <= 0.0f ? juce::String ("OFF") : "MIX " + juce::String (juce::roundToInt (v * 100.0f)) + "%";
    };
    parts->delayMix = std::make_unique<Knob> (0.0f, 1.0f, 0.0f, false, mixText);
    parts->delayMix->setTooltip ("Delay mix (0 = off)");
    parts->delayMix->onValue = [this, defaults] (float v)
    {
        settings.delayOn = v > 0.0f;
        settings.delayMix = settings.delayOn ? v : defaults.delayMix;
        if (settings.delayRate != 0)   // the strip offers 1/8 and 1/4 only; off, 1/8 is stored as the default
            settings.delayRate = settings.delayOn ? 1 : defaults.delayRate;
        repaint();
    };
    parts->delayRate.setTooltip ("Delay time: 1/8 or 1/4 note");
    parts->delayRate.onSelect = [this, defaults] (int v)
    {
        settings.delayRate = v == 1 && ! settings.delayOn ? defaults.delayRate : v;
        refresh();
        commit();
    };
    addAndMakeVisible (parts->delayRate);

    // REVERB: mix (0 = off), size fixed
    parts->reverbMix = std::make_unique<Knob> (0.0f, 1.0f, 0.0f, false, mixText);
    parts->reverbMix->setTooltip ("Reverb mix (0 = off)");
    parts->reverbMix->onValue = [this, defaults] (float v)
    {
        settings.reverbOn = v > 0.0f;
        settings.reverbMix = settings.reverbOn ? v : defaults.reverbMix;
        repaint();
    };

    for (auto* k : { parts->filter.get(), parts->delayMix.get(), parts->reverbMix.get() })
    {
        k->onCommit = [this] { refresh(); commit(); };
        addAndMakeVisible (*k);
    }

    // KILL + reset
    parts->kill.onClick = [this]
    {
        // a press always changes something
        auto next = fx::randomise (parts->random, 1);
        for (int tries = 0; tries < 16 && next == settings; ++tries)
            next = fx::randomise (parts->random, 1);
        settings = next;
        refresh();
        commit();
    };
    parts->reset.onClick = [this]
    {
        settings = {};
        refresh();
        commit();
    };
    addAndMakeVisible (parts->kill);
    addAndMakeVisible (parts->reset);

    addChildComponent (parts->overlay);
    parts->overlay.setVisible (! enabled);

    refresh();
    setSize (1100, 190);
}

FxRack::~FxRack() = default;

void FxRack::setSettings (const fx::FxSettings& s)
{
    settings = s;
    refresh();
}

void FxRack::setEnabled (bool e, const juce::String& m)
{
    enabled = e;
    message = m;
    parts->overlay.message = m;
    parts->overlay.setVisible (! e);
    parts->overlay.repaint();
}

void FxRack::commit()
{
    if (onChange)
        onChange (settings);
}

void FxRack::refresh()
{
    parts->time.setSelected (settings.timeMode);
    parts->repeat.setSelected (settings.repeatRate);
    parts->filter->setValue (filterKnobValue (settings));
    parts->delayMix->setValue (settings.delayOn ? juce::jmax (0.01f, settings.delayMix) : 0.0f);
    // off: show the 1/8 the delay starts on (an older project's dotted rate shows no pill while on)
    parts->delayRate.setSelected (! settings.delayOn && settings.delayRate != 0 ? 1 : settings.delayRate);
    parts->reverbMix->setValue (settings.reverbOn ? juce::jmax (0.01f, settings.reverbMix) : 0.0f);

    const bool hidden = hiddenModulesOn (settings);
    parts->reset.flagged = hidden;
    parts->reset.setTooltip (hidden ? "Older effects (drive, crush, gate, tape stop or width) are on too: RESET clears everything"
                                    : "Reset: all effects off");
    parts->reset.repaint();
    repaint();
}

// ==============================================================================
void FxRack::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    drawCard (g, bounds, 16.0f);

    static constexpr const char* titles[numModules] { "Halftime", "Repeat", "Filter", "Delay", "Reverb" };
    const bool on[numModules] { settings.timeMode != 0, settings.repeatRate != 0, settings.filterOn, settings.delayOn, settings.reverbOn };

    for (int i = 0; i < numModules; ++i)
    {
        const auto& p = parts->panels[(size_t) i];
        g.setGradientFill (juce::ColourGradient (panelTop, 0.0f, p.getY(), panelBot, 0.0f, p.getBottom(), false));
        g.fillRoundedRectangle (p, 12.0f);
        if (on[i])
        {
            // soft blue wash from the top and a lit border
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.14f), 0.0f, p.getY(), accent.withAlpha (0.0f), 0.0f, p.getY() + p.getHeight() * 0.6f, false));
            g.fillRoundedRectangle (p, 12.0f);
            g.setColour (accent.withAlpha (0.6f));
            g.drawRoundedRectangle (p.reduced (0.5f), 12.0f, 1.0f);
        }
        else
        {
            g.setColour (line);
            g.drawRoundedRectangle (p.reduced (0.5f), 12.0f, 1.0f);
        }

        // status LED + title
        const float titleH = juce::jlimit (16.0f, 20.0f, p.getHeight() * 0.11f);
        auto head = juce::Rectangle<float> (p.getX() + 12.0f, p.getY() + 9.0f, p.getWidth() - 24.0f, titleH);
        const auto led = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ head.getX() + 3.5f, head.getCentreY() });
        if (on[i])
        {
            juce::Path dot;
            dot.addEllipse (led);
            juce::DropShadow (accent, 8, {}).drawForPath (g, dot);
            g.setColour (accentL);
            g.fillPath (dot);
        }
        else
        {
            g.setColour (muted.withAlpha (0.6f));
            g.drawEllipse (led.reduced (0.5f), 1.0f);
        }
        head.removeFromLeft (15.0f);
        drawFitted (g, titles[i], head, juce::Justification::centredLeft, on[i] ? text : text2, 13.0f);
    }

    drawFitted (g, timeCaption (settings.timeMode), parts->timeCaption, juce::Justification::centred,
                settings.timeMode != 0 ? accentL : muted, 12.0f, 0.06f);
    drawFitted (g, repeatCaption (settings.repeatRate), parts->repeatCaption, juce::Justification::centred,
                settings.repeatRate != 0 ? accentL : muted, 12.0f, 0.06f);
    drawFitted (g, "Rate", parts->rateLabel, juce::Justification::centred, muted, 12.0f);

    // KILL column
    const auto& k = parts->side;
    g.setColour (well.withAlpha (0.6f));
    g.fillRoundedRectangle (k, 12.0f);
    g.setColour (red.withAlpha (0.22f));
    g.drawRoundedRectangle (k.reduced (0.5f), 12.0f, 1.0f);
}

void FxRack::resized()
{
    const auto bounds = getLocalBounds().toFloat();
    const float pad = juce::jlimit (8.0f, 11.0f, bounds.getHeight() * 0.055f);
    auto area = bounds.reduced (pad);
    const float gap = area.getWidth() < 1000.0f ? 6.0f : 8.0f;

    // KILL / RESET column on the right
    parts->side = area.removeFromRight (juce::jlimit (88.0f, 118.0f, area.getWidth() * 0.105f));
    area.removeFromRight (gap);
    {
        auto c = parts->side.reduced (7.0f, 8.0f);
        const auto resetArea = c.removeFromBottom (30.0f);
        c.removeFromBottom (8.0f);
        parts->kill.setBounds (c.withSizeKeepingCentre (c.getWidth(), juce::jmin (c.getHeight(), 84.0f)).toNearestInt());
        parts->reset.setBounds (resetArea.toNearestInt());
    }

    // module panels
    static constexpr float weights[numModules] { 1.65f, 2.65f, 1.4f, 1.85f, 1.1f };
    float total = 0.0f;
    for (auto w : weights)
        total += w;
    const float unit = (area.getWidth() - gap * (float) (numModules - 1)) / total;
    float x = area.getX();
    for (int i = 0; i < numModules; ++i)
    {
        parts->panels[(size_t) i] = { x, area.getY(), unit * weights[i], area.getHeight() };
        x += unit * weights[i] + gap;
    }

    const auto content = [] (juce::Rectangle<float> p)
    {
        const float titleH = juce::jlimit (16.0f, 20.0f, p.getHeight() * 0.11f);
        return p.reduced (12.0f, 0.0f).withTrimmedTop (9.0f + titleH + 4.0f).withTrimmedBottom (9.0f);
    };

    // pill modules: pills centred with a caption below
    const auto pillBlock = [&] (Pills& pills, juce::Rectangle<float> panel, juce::Rectangle<float>& caption)
    {
        const auto c = content (panel);
        const float pillH = juce::jlimit (30.0f, 40.0f, c.getHeight() * 0.36f);
        const float captionH = 16.0f, spacing = juce::jlimit (6.0f, 12.0f, c.getHeight() * 0.08f);
        const float top = c.getY() + juce::jmax (0.0f, (c.getHeight() - pillH - spacing - captionH) * 0.5f);
        pills.setBounds (juce::Rectangle<float> (c.getX(), top, c.getWidth(), pillH).toNearestInt());
        caption = { c.getX(), top + pillH + spacing, c.getWidth(), captionH };
    };
    pillBlock (parts->time, parts->panels[(size_t) mTime], parts->timeCaption);
    pillBlock (parts->repeat, parts->panels[(size_t) mRepeat], parts->repeatCaption);

    parts->filter->setBounds (content (parts->panels[(size_t) mFilter]).toNearestInt());
    parts->reverbMix->setBounds (content (parts->panels[(size_t) mReverb]).toNearestInt());

    // delay: mix knob left, rate pills right
    {
        auto c = content (parts->panels[(size_t) mDelay]);
        auto right = c.removeFromRight (juce::jmax (64.0f, c.getWidth() * 0.44f));
        c.removeFromRight (4.0f);
        parts->delayMix->setBounds (c.toNearestInt());
        const float pillH = juce::jlimit (28.0f, 34.0f, c.getHeight() * 0.3f);
        const float top = right.getCentreY() - (16.0f + 6.0f + pillH) * 0.5f;
        parts->rateLabel = { right.getX(), top, right.getWidth(), 16.0f };
        parts->delayRate.setBounds (juce::Rectangle<float> (right.getX(), top + 22.0f, right.getWidth(), pillH).toNearestInt());
    }

    parts->overlay.setBounds (getLocalBounds());
}

} // namespace digga
