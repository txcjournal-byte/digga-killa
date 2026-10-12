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

    /** Largest semi-bold mono height (<= maxH) at which `text` fits `width`. */
    float fitMono (const juce::String& text, float width, float maxH, float kerning)
    {
        float h = maxH;
        while (h > 6.0f && juce::GlyphArrangement::getStringWidth (theme::mono (h, true).withExtraKerningFactor (kerning), text) > width)
            h -= 0.25f;
        return h;
    }

    void drawFitted (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification j,
                     juce::Colour colour, float maxH, float kerning = 0.16f)
    {
        const auto upper = text.toUpperCase();
        const float h = fitMono (upper, area.getWidth(), maxH, kerning);
        g.setColour (colour);
        g.setFont (theme::mono (h, true).withExtraKerningFactor (h < maxH ? kerning * 0.5f : kerning));
        g.drawText (upper, area, j, false);
    }

    juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

    // ==========================================================================
    /** Small rotary: chrome cap, blue value arc, label above, value below.
        Drag up/down (shift = fine), double-click resets, mouse wheel. */
    class Knob : public juce::Component, private juce::Timer
    {
    public:
        Knob (juce::String labelText, float lo, float hi, float defaultValue, bool logScale, float step,
              std::function<juce::String (float)> formatter)
            : label (std::move (labelText)), minimum (lo), maximum (hi), def (defaultValue),
              logarithmic (logScale), interval (step), format (std::move (formatter))
        {
            setRepaintsOnMouseActivity (true);
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        }

        void setValue (float v)
        {
            v = snap (juce::jlimit (minimum, maximum, v));
            if (! juce::approximatelyEqual (v, value))
            {
                value = v;
                repaint();
            }
        }

        void setActive (bool a)
        {
            if (a != active)
            {
                active = a;
                repaint();
            }
        }

        std::function<void (float)> onValue;   // live, while dragging
        std::function<void()> onCommit;        // gesture ended

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            auto r = getLocalBounds().toFloat();
            const float labelH = juce::jlimit (10.0f, 13.0f, r.getHeight() * 0.16f);
            const auto labelArea = r.removeFromTop (labelH);
            const auto valueArea = r.removeFromBottom (labelH + 2.0f);
            const bool hot = isMouseOverOrDragging() && isEnabled();

            g.setColour (active ? text2 : muted);
            g.setFont (mono (labelH * 0.78f, true).withExtraKerningFactor (0.12f));
            g.drawText (label, labelArea, juce::Justification::centred, false);

            g.setColour (hot ? accentL : (active ? text : muted));
            g.setFont (mono (labelH * 0.82f, false));
            g.drawText (format (value), valueArea, juce::Justification::centred, false);

            const float dia = juce::jmin (r.getWidth() - 4.0f, r.getHeight() - 2.0f);
            if (dia < 12.0f)
                return;
            const auto c = r.getCentre();
            const float radius = dia * 0.5f;
            const float angle = arcStart + (arcEnd - arcStart) * toNorm (value);

            // track + value arc
            const float arcR = radius - 2.0f, arcW = juce::jmax (2.0f, dia * 0.07f);
            juce::Path track;
            track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, arcStart, arcEnd, true);
            g.setColour (juce::Colour (0xff151c2a));
            g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path arc;
            arc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, arcStart, angle, true);
            if (active)
            {
                g.setColour (accent.withAlpha (0.28f));
                g.strokePath (arc, juce::PathStrokeType (arcW * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (active ? (hot ? accentL : accentL.interpolatedWith (accent, 0.35f)) : muted.withAlpha (0.55f));
            g.strokePath (arc, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

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
                g.setColour (well.withAlpha (0.45f));
                g.fillEllipse (cap);
            }

            // pointer
            const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
            g.setColour (juce::Colour (0xff0a0f18));
            g.drawLine ({ c + dir * (capR * 0.25f), c + dir * (capR * 0.9f) }, juce::jmax (1.6f, dia * 0.05f));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragStartNorm = toNorm (value);
            dragStartY = e.position.y;
            dragged = false;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const float sens = e.mods.isShiftDown() ? 900.0f : 180.0f;
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
            float n = toNorm (value) + delta;
            if (interval > 0.0f && std::abs (fromNorm (n) - value) < interval)   // stepped: at least one step
                n = toNorm (value + (delta > 0.0f ? interval : -interval));
            if (change (fromNorm (juce::jlimit (0.0f, 1.0f, n))))
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
            v = snap (juce::jlimit (minimum, maximum, v));
            if (juce::approximatelyEqual (v, value))
                return false;
            value = v;
            repaint();
            if (onValue)
                onValue (value);
            return true;
        }

        float snap (float v) const
        {
            return interval > 0.0f ? minimum + interval * std::round ((v - minimum) / interval) : v;
        }

        float toNorm (float v) const
        {
            if (logarithmic)
                return std::log (v / minimum) / std::log (maximum / minimum);
            return (v - minimum) / (maximum - minimum);
        }

        float fromNorm (float n) const
        {
            if (logarithmic)
                return minimum * std::pow (maximum / minimum, n);
            return minimum + (maximum - minimum) * n;
        }

        juce::String label;
        float minimum, maximum, def;
        bool logarithmic;
        float interval;
        std::function<juce::String (float)> format;
        float value = 0.0f;
        bool active = true, dragged = false;
        float dragStartNorm = 0.0f, dragStartY = 0.0f;
    };

    // ==========================================================================
    /** Segmented pill selector (filter type, drive type, rates...). */
    class Pills : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Pills (juce::StringArray itemNames, std::vector<int> itemValues = {})
            : items (std::move (itemNames)), values (std::move (itemValues))
        {
            if (values.empty())
                for (int i = 0; i < items.size(); ++i)
                    values.push_back (i);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setRepaintsOnMouseActivity (true);
        }

        void setSelected (int v)    { if (v != selected) { selected = v; repaint(); } }
        void setActive (bool a)     { if (a != active) { active = a; repaint(); } }
        int getSelected() const     { return selected; }

        std::function<void (int)> onSelect;

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (well);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (line);
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

            float fontH = juce::jlimit (7.5f, 9.5f, r.getHeight() * 0.52f);
            for (auto& item : items)
                fontH = juce::jmin (fontH, fitMono (item, segment (0).getWidth() - 5.0f, fontH, 0.06f));
            for (int i = 0; i < items.size(); ++i)
            {
                const auto s = segment (i);
                const bool sel = values[(size_t) i] == selected;
                const bool hover = isMouseOver() && s.contains (getMouseXYRelative().toFloat());
                if (sel)
                {
                    const auto p = s.reduced (1.5f);
                    if (active)
                    {
                        juce::ColourGradient grad (juce::Colour (0xfff4f6fb), 0.0f, p.getY(), juce::Colour (0xffd5dbe6), 0.0f, p.getBottom(), false);
                        grad.addColour (0.5, juce::Colour (0xffb9c1cf));
                        g.setGradientFill (grad);
                    }
                    else
                    {
                        g.setColour (juce::Colour (0xff1c2433));
                    }
                    g.fillRoundedRectangle (p, p.getHeight() * 0.5f);
                }
                g.setColour (sel ? (active ? well : text2) : (hover ? text : muted));
                g.setFont (mono (fontH, true).withExtraKerningFactor (0.06f));
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
            const auto r = getLocalBounds().toFloat().reduced (1.0f, 0.5f);
            const float w = r.getWidth() / (float) juce::jmax (1, items.size());
            return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
        }

        juce::StringArray items;
        std::vector<int> values;
        int selected = 0;
        bool active = true;
    };

    // ==========================================================================
    /** Round power switch, glowing blue when on. */
    class Power : public juce::Component
    {
    public:
        Power() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setRepaintsOnMouseActivity (true); }

        void setOn (bool o) { if (o != on) { on = o; repaint(); } }
        std::function<void()> onToggle;

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            const auto b = getLocalBounds().toFloat();
            const float d = juce::jmin (b.getWidth(), b.getHeight()) - 2.0f;
            const auto r = juce::Rectangle<float> (d, d).withCentre (b.getCentre());

            if (on)
            {
                juce::Path disc;
                disc.addEllipse (r);
                juce::DropShadow (accent.withAlpha (0.9f), (int) (d * 0.9f), {}).drawForPath (g, disc);
                juce::ColourGradient grad (accentL, r.getCentreX(), r.getY(), juce::Colour (0xff3f6fd0), r.getCentreX(), r.getBottom(), false);
                g.setGradientFill (grad);
                g.fillEllipse (r);
            }
            else
            {
                g.setColour (well);
                g.fillEllipse (r);
                g.setColour (isMouseOver() ? accent.withAlpha (0.6f) : line.withAlpha (0.5f));
                g.drawEllipse (r.reduced (0.5f), 1.0f);
            }

            // power glyph
            const auto c = r.getCentre();
            const float gr = d * 0.24f;
            juce::Path glyph;
            glyph.addCentredArc (c.x, c.y, gr, gr, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
            glyph.startNewSubPath (c.x, c.y - gr * 1.25f);
            glyph.lineTo (c.x, c.y - gr * 0.2f);
            g.setColour (on ? well : (isMouseOver() ? text2 : muted));
            g.strokePath (glyph, juce::PathStrokeType (juce::jmax (1.2f, d * 0.1f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (onToggle)
                onToggle();
        }

    private:
        bool on = false;
    };

    // ==========================================================================
    /** Small text toggle (delay ping-pong). */
    class Toggle : public juce::Component
    {
    public:
        explicit Toggle (juce::String t) : text (std::move (t)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void setOn (bool o)     { if (o != on) { on = o; repaint(); } }
        void setActive (bool a) { if (a != active) { active = a; repaint(); } }
        std::function<void()> onToggle;

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (on && active ? accent.withAlpha (0.22f) : well);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (on ? (active ? accentL : text2) : line);
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            g.setFont (mono (juce::jlimit (7.0f, 9.0f, r.getHeight() * 0.6f), true).withExtraKerningFactor (0.08f));
            g.drawText (text, r, juce::Justification::centred, false);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (onToggle)
                onToggle();
        }

    private:
        juce::String text;
        bool on = false, active = true;
    };

    // ==========================================================================
    /** The gate's 16 steps (2 rows of 8); click toggles, drag paints. */
    class StepGrid : public juce::Component
    {
    public:
        StepGrid() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void setPattern (juce::uint16 p) { if (p != pattern) { pattern = p; repaint(); } }
        void setActive (bool a)          { if (a != active) { active = a; repaint(); } }
        juce::uint16 getPattern() const  { return pattern; }

        std::function<void (juce::uint16)> onPattern;   // live
        std::function<void()> onCommit;

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            for (int i = 0; i < 16; ++i)
            {
                const auto r = cell (i);
                const bool on = ((pattern >> i) & 1) != 0;
                const float rad = juce::jmin (3.0f, r.getHeight() * 0.25f);
                if (on)
                {
                    if (active)
                    {
                        juce::Path p;
                        p.addRoundedRectangle (r, rad);
                        juce::DropShadow (accent.withAlpha (0.55f), 6, {}).drawForPath (g, p);
                        g.setGradientFill (juce::ColourGradient (accentL, 0.0f, r.getY(), juce::Colour (0xff4a78d6), 0.0f, r.getBottom(), false));
                        g.fillPath (p);
                    }
                    else
                    {
                        g.setColour (juce::Colour (0xff3a465c));
                        g.fillRoundedRectangle (r, rad);
                    }
                }
                else
                {
                    g.setColour (well);
                    g.fillRoundedRectangle (r, rad);
                    g.setColour (i % 4 == 0 ? line.withMultipliedAlpha (2.0f) : line);
                    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = cellAt (e.position);
            if (i < 0)
                return;
            paintValue = ((pattern >> i) & 1) == 0;
            changed = false;
            set (i);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const int i = cellAt (e.position);
            if (i >= 0)
                set (i);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (changed && onCommit)
                onCommit();
            changed = false;
        }

    private:
        void set (int i)
        {
            const auto bit = (juce::uint16) (1u << i);
            const auto p = (juce::uint16) (paintValue ? (pattern | bit) : (pattern & ~bit));
            if (p == pattern)
                return;
            pattern = p;
            changed = true;
            repaint();
            if (onPattern)
                onPattern (pattern);
        }

        juce::Rectangle<float> cell (int i) const
        {
            const auto b = getLocalBounds().toFloat();
            const float gap = 2.0f, groupGap = 4.0f;
            const float w = (b.getWidth() - 7.0f * gap - groupGap) / 8.0f;
            const float h = (b.getHeight() - 4.0f) / 2.0f;
            const int col = i % 8, row = i / 8;
            const float x = b.getX() + (float) col * (w + gap) + (col >= 4 ? groupGap : 0.0f);
            return { x, b.getY() + (float) row * (h + 4.0f), w, h };
        }

        int cellAt (juce::Point<float> p) const
        {
            for (int i = 0; i < 16; ++i)
                if (cell (i).expanded (1.0f, 2.0f).contains (p))
                    return i;
            return -1;
        }

        juce::uint16 pattern = 0;
        bool active = true, paintValue = true, changed = false;
    };

    // ==========================================================================
    /** KILL: red liquid-chrome slab. */
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
            auto r = getLocalBounds().toFloat().reduced (6.0f);
            if (down)
                r = r.reduced (1.0f).translated (0.0f, 1.0f);
            const float rad = juce::jmin (14.0f, r.getHeight() * 0.3f);
            juce::Path slab;
            slab.addRoundedRectangle (r, rad);

            juce::DropShadow (red.withAlpha (over ? 0.75f : 0.5f), over ? 22 : 16, {}).drawForPath (g, slab);

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
            const auto textArea = r.reduced (r.getWidth() * 0.17f, r.getHeight() * 0.24f);
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

    /** Chrome pill text button ("RESET FX"). */
    class PillButton : public juce::Button
    {
    public:
        explicit PillButton (const juce::String& name) : juce::Button (name) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace theme;
            const auto r = getLocalBounds().toFloat().reduced (3.0f);
            g.setColour (over ? juce::Colour (0xff141c2b) : well);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (over ? accent.withAlpha (0.7f) : line.withMultipliedAlpha (1.6f));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            drawMonoLabel (g, getName(), r.translated (0.0f, down ? 1.0f : 0.0f), juce::Justification::centred,
                           over ? theme::text : theme::text2, juce::jlimit (8.0f, 10.0f, r.getHeight() * 0.48f));
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
            g.setColour (card.withAlpha (0.78f));
            g.fillRoundedRectangle (b, 20.0f);
            if (message.isEmpty())
                return;
            const auto f = mono (11.0f, true).withExtraKerningFactor (0.16f);
            const float w = juce::jmin (b.getWidth() - 40.0f, juce::GlyphArrangement::getStringWidth (f, message.toUpperCase()) + 44.0f);
            const auto pill = juce::Rectangle<float> (w, 30.0f).withCentre (b.getCentre());
            g.setColour (well.withAlpha (0.95f));
            g.fillRoundedRectangle (pill, 15.0f);
            g.setColour (line.withMultipliedAlpha (1.8f));
            g.drawRoundedRectangle (pill, 15.0f, 1.0f);
            drawMonoLabel (g, message, pill, juce::Justification::centred, text2, 11.0f);
        }
    };

    // ==========================================================================
    // module screens (small visual readouts of the settings)
    enum Kind { kFilter, kDrive, kCrush, kGate, kTape, kDelay, kReverb, kWidth };

    float shapeVis (int type, float u)
    {
        if (type == 1) { const float t = std::tanh (u); return std::tanh (u + 0.3f * t * t); }
        if (type == 2) return juce::jlimit (-1.0f, 1.0f, u);
        return std::tanh (u + 0.08f) - 0.0798298f;
    }

    void strokeCurve (juce::Graphics& g, const juce::Path& curve, juce::Rectangle<float> area, bool on, bool fill)
    {
        using namespace theme;
        if (fill)
        {
            juce::Path filled (curve);
            filled.lineTo (area.getRight(), area.getBottom());
            filled.lineTo (area.getX(), area.getBottom());
            filled.closeSubPath();
            g.setGradientFill (juce::ColourGradient ((on ? accent : muted).withAlpha (on ? 0.35f : 0.12f), 0.0f, area.getY(),
                                                     (on ? accent : muted).withAlpha (0.0f), 0.0f, area.getBottom(), false));
            g.fillPath (filled);
        }
        if (on)
        {
            g.setColour (accent.withAlpha (0.3f));
            g.strokePath (curve, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved));
        }
        g.setColour (on ? accentL : muted.withAlpha (0.6f));
        g.strokePath (curve, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
    }

    void drawScreen (juce::Graphics& g, int kind, juce::Rectangle<float> screen, const FxSettings& s, bool on)
    {
        using namespace theme;
        g.setColour (well);
        g.fillRoundedRectangle (screen, 6.0f);

        const juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (screen.toNearestInt());

        // faint grid
        g.setColour (line.withMultipliedAlpha (0.45f));
        for (int i = 1; i < 4; ++i)
            g.fillRect (screen.getX() + screen.getWidth() * (float) i / 4.0f, screen.getY(), 1.0f, screen.getHeight());
        g.fillRect (screen.getX(), screen.getCentreY(), screen.getWidth(), 1.0f);

        const auto a = screen.reduced (4.0f, 4.0f);
        const int steps = juce::jmax (8, (int) a.getWidth());
        juce::Path p;

        switch (kind)
        {
            case kFilter:
            {
                const float q = juce::jlimit (0.5f, 10.0f, s.filterReso), k = 1.0f / q;
                for (int i = 0; i <= steps; ++i)
                {
                    const float t = (float) i / (float) steps;
                    const float w = 20.0f * std::pow (1000.0f, t) / s.filterFreq;
                    const float den = std::sqrt ((1.0f - w * w) * (1.0f - w * w) + (w * k) * (w * k));
                    float mag = s.filterType == 1 ? w * w / den : (s.filterType == 2 ? w * juce::jmin (1.0f, 2.0f * k) / den : 1.0f / den);
                    const float db = juce::jlimit (-30.0f, 20.0f, juce::Decibels::gainToDecibels (mag, -60.0f));
                    const float y = juce::jmap (db, -30.0f, 20.0f, a.getBottom(), a.getY());
                    i == 0 ? p.startNewSubPath (a.getX() + t * a.getWidth(), y) : p.lineTo (a.getX() + t * a.getWidth(), y);
                }
                strokeCurve (g, p, screen, on, true);
                break;
            }
            case kDrive:
            {
                const float pre = juce::Decibels::decibelsToGain (s.drive * 30.0f) * 1.2f;
                float peak = 1.0e-3f;
                for (int i = 0; i <= steps; ++i)
                    peak = juce::jmax (peak, std::abs (shapeVis (s.driveType, (-1.0f + 2.0f * (float) i / (float) steps) * pre)));
                g.setColour (line);
                g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f);
                for (int i = 0; i <= steps; ++i)
                {
                    const float x = -1.0f + 2.0f * (float) i / (float) steps;
                    const float wet = shapeVis (s.driveType, x * pre) / peak;
                    const float y = x + (wet - x) * s.driveMix;
                    const auto pt = juce::Point<float> (a.getX() + (float) i / (float) steps * a.getWidth(), a.getCentreY() - y * a.getHeight() * 0.48f);
                    i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
                }
                strokeCurve (g, p, screen, on, false);
                break;
            }
            case kCrush:
            {
                const float levels = std::pow (2.0f, s.crushBits - 1.0f);
                const float hold = 1.0f + s.crushRate * s.crushRate * 39.0f;
                float held = 0.0f, phase = 1.0f;
                for (int i = 0; i <= steps; ++i)
                {
                    const float t = (float) i / (float) steps;
                    if ((phase += 1.0f / hold) >= 1.0f)
                    {
                        phase -= 1.0f;
                        held = std::round (std::sin (t * juce::MathConstants<float>::twoPi * 1.5f) * 0.9f * levels) / levels;
                    }
                    const auto pt = juce::Point<float> (a.getX() + t * a.getWidth(), a.getCentreY() - held * a.getHeight() * 0.5f);
                    i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
                }
                strokeCurve (g, p, screen, on, false);
                break;
            }
            case kTape:
            {
                const float region = 0.15f + 0.65f * (s.tapeStopBeats - 0.25f) / 3.75f;
                double phase = 0.0;
                for (int i = 0; i <= steps; ++i)
                {
                    const float t = (float) i / (float) steps;
                    const float x = juce::jmax (0.0f, (t - (1.0f - region)) / region);
                    const float speed = std::pow (1.0f - x, 1.35f);
                    phase += speed * 14.0 / steps;
                    const float amp = std::sqrt (juce::jmin (1.0f, speed * 8.0f));
                    const auto pt = juce::Point<float> (a.getX() + t * a.getWidth(),
                                                        a.getCentreY() - amp * 0.85f * a.getHeight() * 0.5f * (float) std::sin (phase * juce::MathConstants<double>::twoPi));
                    i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
                }
                strokeCurve (g, p, screen, on, false);
                break;
            }
            case kDelay:
            {
                static constexpr float beats[] { 1.0f, 0.5f, 0.75f, 0.25f, 1.5f };
                const float d = beats[juce::jlimit (0, 4, s.delayRate)] / 4.0f;   // window: 4 beats
                const float barW = juce::jmax (2.0f, a.getWidth() * 0.025f);
                float level = 1.0f;
                for (int k = 0; k < 64; ++k)
                {
                    const float x = a.getX() + (float) k * d * a.getWidth();
                    if (x > a.getRight() || level < 0.02f)
                        break;
                    const float h = level * a.getHeight() * 0.48f * (k == 0 ? 1.0f : 0.4f + 0.6f * s.delayMix);
                    const bool right = s.delayPingPong && (k % 2 == 0) && k > 0;
                    juce::Rectangle<float> bar (x, right ? a.getCentreY() : a.getCentreY() - h, barW, s.delayPingPong && k > 0 ? h : (k == 0 ? h * 2.0f : h * 2.0f));
                    if (! s.delayPingPong || k == 0)
                        bar.setY (a.getCentreY() - h);
                    g.setColour (k == 0 ? (on ? text2 : muted.withAlpha (0.6f)) : (on ? accentL.withAlpha (0.4f + 0.6f * level) : muted.withAlpha (0.5f * level + 0.1f)));
                    g.fillRoundedRectangle (bar, barW * 0.4f);
                    if (k > 0)
                        level *= juce::jmax (0.0f, s.delayFeedback);
                }
                break;
            }
            case kReverb:
            {
                const float pre = (0.008f + 0.03f * s.reverbSize) / 3.0f;
                const float len = 0.25f + 0.75f * s.reverbSize;
                juce::Random rnd (7);
                g.setColour (on ? text2 : muted.withAlpha (0.6f));
                g.fillRect (a.getX(), a.getY(), 2.0f, a.getHeight());
                for (int i = 0; i <= steps; i += 2)
                {
                    const float t = (float) i / (float) steps;
                    if (t < pre)
                        continue;
                    const float env = std::exp (-4.5f * (t - pre) / len) * (0.25f + 0.75f * juce::jmin (1.0f, (t - pre) * 30.0f));
                    const float bright = 1.0f - 0.6f * s.reverbDamp * t;
                    const float h = env * (0.4f + 0.6f * rnd.nextFloat()) * a.getHeight() * (0.35f + 0.65f * s.reverbMix);
                    g.setColour ((on ? accentL : muted).withAlpha (juce::jlimit (0.05f, 1.0f, (on ? 0.85f : 0.4f) * bright)));
                    g.fillRect (a.getX() + t * a.getWidth(), a.getCentreY() - h * 0.5f, 1.0f, h);
                }
                break;
            }
            case kWidth:
            {
                // goniometer: mono is a vertical line, wide spreads sideways
                const float ry = a.getHeight() * 0.5f;
                const float rx = juce::jmin (a.getWidth() * 0.48f, juce::jmax (0.6f, ry * 1.25f * s.width));
                g.setColour (line);
                g.drawLine (a.getCentreX() - ry, a.getBottom(), a.getCentreX() + ry, a.getY(), 1.0f);
                g.drawLine (a.getCentreX() - ry, a.getY(), a.getCentreX() + ry, a.getBottom(), 1.0f);
                p.addEllipse (juce::Rectangle<float> (rx * 2.0f, ry * 2.0f).withCentre (a.getCentre()));
                g.setColour ((on ? accent : muted).withAlpha (on ? 0.22f : 0.08f));
                g.fillPath (p);
                g.setColour (on ? accentL : muted.withAlpha (0.6f));
                g.strokePath (p, juce::PathStrokeType (1.4f));
                g.setFont (mono (8.0f, true));
                g.setColour (muted);
                g.drawText ("L", screen.withTrimmedLeft (5.0f), juce::Justification::centredLeft, false);
                g.drawText ("R", screen.withTrimmedRight (5.0f), juce::Justification::centredRight, false);
                break;
            }
            case kGate:
            default:
                break;
        }
    }
}

// ==============================================================================
struct FxRack::Parts
{
    struct Module
    {
        juce::String title;
        bool FxSettings::* on = nullptr;
        float weight = 1.0f;
        Kind kind = kFilter;
        Power power;
        std::vector<std::unique_ptr<Knob>> knobs;
        std::vector<float FxSettings::*> knobFields;
        std::unique_ptr<Pills> pills;
        int FxSettings::* pillField = nullptr;
        std::unique_ptr<Toggle> toggle;
        std::unique_ptr<StepGrid> grid;
        juce::Rectangle<float> panel, screen, infoRow;
    };

    std::array<Module, 8> modules;
    KillButton kill;
    Pills strength { { "PG", "R", "UNRATED" } };
    PillButton reset { "Reset FX" };
    Overlay overlay;
    juce::Rectangle<float> killColumn;
    juce::Random random;
};

FxRack::FxRack() : parts (std::make_unique<Parts>())
{
    using S = FxSettings;
    const S defaults;

    const auto freqText = [] (float v)
    {
        return v < 1000.0f ? juce::String (juce::roundToInt (v))
                           : juce::String (v / 1000.0f, v < 10000.0f ? 1 : 0) + "k";
    };
    const auto beatsText = [] (float v)
    {
        if (v < 0.3f) return juce::String ("1/4");
        if (v < 0.6f) return juce::String ("1/2");
        if (v < 0.8f) return juce::String ("3/4");
        return juce::String (v, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " BT";
    };

    auto setup = [&] (int index, const char* title, bool S::* on, float weight)
    {
        auto& m = parts->modules[(size_t) index];
        m.title = title;
        m.on = on;
        m.weight = weight;
        m.kind = (Kind) index;
        m.power.onToggle = [this, on]
        {
            settings.*on = ! (settings.*on);
            refresh();
            commit();
        };
        addAndMakeVisible (m.power);
        return &m;
    };

    auto addKnob = [&] (Parts::Module* m, const char* label, float S::* field, float lo, float hi, bool logScale, float step,
                        std::function<juce::String (float)> fmt)
    {
        auto k = std::make_unique<Knob> (label, lo, hi, defaults.*field, logScale, step, std::move (fmt));
        k->onValue = [this, field] (float v) { settings.*field = v; repaint(); };
        k->onCommit = [this] { commit(); };
        addAndMakeVisible (*k);
        m->knobs.push_back (std::move (k));
        m->knobFields.push_back (field);
    };

    auto addPills = [&] (Parts::Module* m, juce::StringArray names, std::vector<int> values, int S::* field)
    {
        m->pills = std::make_unique<Pills> (std::move (names), std::move (values));
        m->pillField = field;
        m->pills->onSelect = [this, field] (int v) { settings.*field = v; repaint(); commit(); };
        addAndMakeVisible (*m->pills);
    };

    auto* filter = setup (kFilter, "Filter", &S::filterOn, 2.0f);
    addPills (filter, { "LP", "HP", "BP" }, {}, &S::filterType);
    addKnob (filter, "FREQ", &S::filterFreq, 20.0f, 20000.0f, true, 0.0f, freqText);
    addKnob (filter, "RES", &S::filterReso, 0.5f, 10.0f, true, 0.0f, [] (float v) { return juce::String (v, 1); });

    auto* drive = setup (kDrive, "Drive", &S::driveOn, 2.0f);
    addPills (drive, { "TAPE", "TUBE", "HARD" }, {}, &S::driveType);
    addKnob (drive, "DRIVE", &S::drive, 0.0f, 1.0f, false, 0.0f, percent);
    addKnob (drive, "MIX", &S::driveMix, 0.0f, 1.0f, false, 0.0f, percent);

    auto* crush = setup (kCrush, "Crush", &S::crushOn, 2.0f);
    addKnob (crush, "BITS", &S::crushBits, 2.0f, 16.0f, false, 1.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " BIT"; });
    addKnob (crush, "RATE", &S::crushRate, 0.0f, 1.0f, false, 0.0f, percent);

    auto* gate = setup (kGate, "Gate", &S::gateOn, 2.9f);
    addPills (gate, { "1/4", "1/8", "1/16", "1/32" }, {}, &S::gateRate);
    addKnob (gate, "DEPTH", &S::gateDepth, 0.0f, 1.0f, false, 0.0f, percent);
    gate->grid = std::make_unique<StepGrid>();
    gate->grid->onPattern = [this] (juce::uint16 p) { settings.gatePattern = p; };
    gate->grid->onCommit = [this] { commit(); };
    addAndMakeVisible (*gate->grid);

    auto* tape = setup (kTape, "Tape Stop", &S::tapeStopOn, 1.55f);
    addKnob (tape, "LENGTH", &S::tapeStopBeats, 0.25f, 4.0f, false, 0.25f, beatsText);

    auto* delay = setup (kDelay, "Delay", &S::delayOn, 2.6f);
    addPills (delay, { "1/4D", "1/4", "1/8D", "1/8", "1/16" }, { 4, 0, 2, 1, 3 }, &S::delayRate);
    addKnob (delay, "FDBK", &S::delayFeedback, 0.0f, 0.95f, false, 0.0f, percent);
    addKnob (delay, "MIX", &S::delayMix, 0.0f, 1.0f, false, 0.0f, percent);
    delay->toggle = std::make_unique<Toggle> ("P-P");
    delay->toggle->onToggle = [this] { settings.delayPingPong = ! settings.delayPingPong; refresh(); commit(); };
    addAndMakeVisible (*delay->toggle);

    auto* reverb = setup (kReverb, "Reverb", &S::reverbOn, 2.45f);
    addKnob (reverb, "SIZE", &S::reverbSize, 0.0f, 1.0f, false, 0.0f, percent);
    addKnob (reverb, "DAMP", &S::reverbDamp, 0.0f, 1.0f, false, 0.0f, percent);
    addKnob (reverb, "MIX", &S::reverbMix, 0.0f, 1.0f, false, 0.0f, percent);

    auto* width = setup (kWidth, "Width", &S::widthOn, 1.35f);
    addKnob (width, "WIDTH", &S::width, 0.0f, 2.0f, false, 0.0f, percent);

    // KILL + strength + reset
    parts->strength.setSelected (1);
    parts->strength.setTooltip ("KILL strength: subtle, normal, extreme");
    parts->kill.onClick = [this]
    {
        settings = fx::randomise (parts->random, parts->strength.getSelected());
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
    addAndMakeVisible (parts->strength);
    addAndMakeVisible (parts->reset);

    addChildComponent (parts->overlay);
    parts->overlay.setVisible (! enabled);

    refresh();
    setSize (1100, 230);
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
    for (auto& m : parts->modules)
    {
        const bool on = settings.*m.on;
        m.power.setOn (on);
        for (size_t i = 0; i < m.knobs.size(); ++i)
        {
            m.knobs[i]->setValue (settings.*m.knobFields[i]);
            m.knobs[i]->setActive (on);
        }
        if (m.pills != nullptr)
        {
            m.pills->setSelected (settings.*m.pillField);
            m.pills->setActive (on);
        }
        if (m.toggle != nullptr)
        {
            m.toggle->setOn (settings.delayPingPong);
            m.toggle->setActive (on);
        }
        if (m.grid != nullptr)
        {
            m.grid->setPattern (settings.gatePattern);
            m.grid->setActive (on);
        }
    }
    repaint();
}

// ==============================================================================
void FxRack::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    drawCard (g, bounds, 20.0f);

    for (auto& m : parts->modules)
    {
        const bool on = settings.*m.on;
        const auto& p = m.panel;

        g.setGradientFill (juce::ColourGradient (panelTop, 0.0f, p.getY(), panelBot, 0.0f, p.getBottom(), false));
        g.fillRoundedRectangle (p, 10.0f);
        if (on)
        {
            // soft blue wash from the top and a lit border
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.13f), 0.0f, p.getY(), accent.withAlpha (0.0f), 0.0f, p.getY() + p.getHeight() * 0.6f, false));
            g.fillRoundedRectangle (p, 10.0f);
            g.setColour (accent.withAlpha (0.55f));
            g.drawRoundedRectangle (p.reduced (0.5f), 10.0f, 1.0f);
        }
        else
        {
            g.setColour (line);
            g.drawRoundedRectangle (p.reduced (0.5f), 10.0f, 1.0f);
        }

        // title next to the power switch
        const auto pb = m.power.getBounds().toFloat();
        auto titleArea = juce::Rectangle<float> (pb.getRight() + 5.0f, pb.getY() - 2.0f, p.getRight() - pb.getRight() - 9.0f, pb.getHeight() + 4.0f);
        if (m.toggle != nullptr)
            titleArea.setRight ((float) m.toggle->getX() - 2.0f);
        drawFitted (g, m.title, titleArea, juce::Justification::centredLeft, on ? text : text2, 11.0f);

        // modules without a selector show a small readout in that row
        if (! m.infoRow.isEmpty())
        {
            juce::String info;
            if (m.kind == kCrush)  info = juce::String (1.0f + settings.crushRate * settings.crushRate * 39.0f, 1) + "x DOWN";
            if (m.kind == kTape)   info = "TO A HALT";
            if (m.kind == kReverb) info = "PRE " + juce::String (juce::roundToInt (8.0f + 30.0f * settings.reverbSize)) + " MS";
            if (m.kind == kWidth)  info = settings.width < 0.05f ? "MONO" : "MID / SIDE";
            drawFitted (g, info, m.infoRow, juce::Justification::centred, on ? text2.withAlpha (0.8f) : muted.withAlpha (0.7f), 8.5f);
        }

        if (m.kind != kGate)
        {
            drawScreen (g, m.kind, m.screen, settings, on);
            g.setColour (line.withMultipliedAlpha (on ? 1.6f : 1.0f));
            g.drawRoundedRectangle (m.screen.reduced (0.5f), 6.0f, 1.0f);
        }
    }

    // KILL column
    const auto& k = parts->killColumn;
    g.setColour (well.withAlpha (0.6f));
    g.fillRoundedRectangle (k, 12.0f);
    g.setColour (red.withAlpha (0.22f));
    g.drawRoundedRectangle (k.reduced (0.5f), 12.0f, 1.0f);
    auto head = k.withHeight (24.0f).withTrimmedTop (6.0f);
    drawMonoLabel (g, "FX Rack", head, juce::Justification::centred, text2, 10.0f);
    const auto strengthLabel = parts->strength.getBounds().toFloat().translated (0.0f, -13.0f).withHeight (12.0f);
    drawMonoLabel (g, "Strength", strengthLabel, juce::Justification::centred, muted, 8.0f);
}

void FxRack::resized()
{
    auto area = getLocalBounds().toFloat().reduced (11.0f);
    const float gap = area.getWidth() < 1000.0f ? 5.0f : 7.0f;

    // KILL column on the right
    const float killW = juce::jlimit (124.0f, 168.0f, area.getWidth() * 0.145f);
    auto k = area.removeFromRight (killW);
    area.removeFromRight (gap + 2.0f);
    parts->killColumn = k;
    {
        auto c = k.reduced (8.0f, 6.0f);
        c.removeFromTop (16.0f);
        const auto resetArea = c.removeFromBottom (26.0f);
        c.removeFromBottom (6.0f);
        const auto strengthArea = c.removeFromBottom (20.0f);
        c.removeFromBottom (16.0f);
        parts->kill.setBounds (c.toNearestInt());
        parts->strength.setBounds (strengthArea.toNearestInt());
        parts->reset.setBounds (resetArea.reduced (8.0f, 0.0f).toNearestInt());
    }

    // module columns
    float totalWeight = 0.0f;
    for (auto& m : parts->modules)
        totalWeight += m.weight;
    const float unit = (area.getWidth() - gap * 7.0f) / totalWeight;
    const float h = area.getHeight();
    const float headerH = juce::jlimit (22.0f, 28.0f, h * 0.13f);
    const float screenH = juce::jlimit (24.0f, 44.0f, h * 0.2f);
    const float pillH = juce::jlimit (15.0f, 18.0f, h * 0.085f);

    float x = area.getX();
    for (auto& m : parts->modules)
    {
        const float w = unit * m.weight;
        m.panel = { x, area.getY(), w, h };
        x += w + gap;

        auto r = m.panel.reduced (6.0f, 5.0f);
        auto header = r.removeFromTop (headerH);
        const float ps = juce::jmin (16.0f, headerH - 6.0f);
        m.power.setBounds (juce::Rectangle<float> (ps + 2.0f, ps + 2.0f).withCentre ({ header.getX() + ps * 0.5f + 2.0f, header.getCentreY() }).toNearestInt());
        if (m.toggle != nullptr)
            m.toggle->setBounds (header.removeFromRight (30.0f).withSizeKeepingCentre (30.0f, 14.0f).toNearestInt());

        r.removeFromTop (2.0f);
        m.screen = r.removeFromTop (screenH);
        if (m.grid != nullptr)
        {
            // the gate's screen is its step grid (a bit taller)
            m.screen = m.screen.withHeight (screenH + 6.0f);
            r.removeFromTop (6.0f);
            m.grid->setBounds (m.screen.reduced (1.0f).toNearestInt());
        }
        r.removeFromTop (5.0f);
        auto pillRow = r.removeFromTop (pillH);
        if (m.pills != nullptr)
            m.pills->setBounds (pillRow.toNearestInt());
        m.infoRow = m.pills != nullptr ? juce::Rectangle<float>() : pillRow;
        r.removeFromTop (3.0f);

        // knob rows share one baseline across modules; the knob size is capped
        const auto n = (int) m.knobs.size();
        const float kh = juce::jmin (r.getHeight(), 88.0f);
        const float kw = juce::jmin (r.getWidth() / (float) juce::jmax (1, n), kh - 26.0f, 56.0f);
        const auto knobRow = r.withSizeKeepingCentre (kw * (float) n, kh);
        for (int i = 0; i < n; ++i)
            m.knobs[(size_t) i]->setBounds (juce::Rectangle<float> (knobRow.getX() + kw * (float) i, knobRow.getY(), kw, kh).toNearestInt());
    }

    parts->overlay.setBounds (getLocalBounds());
}

} // namespace digga
