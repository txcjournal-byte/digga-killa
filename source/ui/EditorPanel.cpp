#include "ui/EditorPanel.h"

namespace digga
{

using namespace theme;

// ---------------------------------------------------------------------------- drag pill
DragPill::DragPill()
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag into FL Studio, Ableton, Logic... (WAV or MIDI, see top right)");
}

void DragPill::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    juce::Graphics::ScopedSaveState s (g);
    g.setOpacity (isEnabled() ? 1.0f : 0.35f);
    drawChromePill (g, r, hover, false);
    g.setColour (well);
    for (int col = 0; col < 2; ++col)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse (r.getX() + 20.0f + (float) col * 6.0f, r.getCentreY() + (float) (row - 1) * 6.0f - 1.6f, 3.2f, 3.2f);
    g.setFont (display (14.0f).withExtraKerningFactor (0.04f));
    g.drawText ("DRAG TO DAW", r.withTrimmedLeft (30.0f), juce::Justification::centred, false);
}

void DragPill::mouseDrag (const juce::MouseEvent& e)
{
    if (! started && isEnabled() && e.getDistanceFromDragStart() > 4)
    {
        started = true;
        if (onDragOut)
            onDragOut();
    }
}

// ---------------------------------------------------------------------------- editor
EditorPanel::EditorPanel()
{
    play.onClick = [this] { if (onPlay && id >= 0) onPlay (id); };
    reverse.setClickingTogglesState (true);
    reverse.onClick = [this] { edit.reverse = reverse.getToggleState(); commit(); };
    reset.onClick = [this] { edit = {}; syncControls(); commit(); };
    drag.onDragOut = [this] { if (onDragOut && id >= 0) onDragOut (id); };

    for (auto* c : std::initializer_list<juce::Component*> { &play, &reverse, &reset, &drag })
        addAndMakeVisible (c);

    auto ms = [] (double v) { return v < 1.0 ? juce::String ("off") : juce::String (juce::roundToInt (v)) + " ms"; };
    sliders[0].label = "Fade in";  sliders[0].format = ms;
    sliders[1].label = "Fade out"; sliders[1].format = ms;
    sliders[2].label = "Gain";     sliders[2].format = [] (double v) { return (v > 0.04 ? "+" : "") + juce::String (v, 1) + " dB"; };
    sliders[3].label = "Pitch";    sliders[3].format = [] (double v) { return (v > 0.5 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; };

    const std::array<juce::Range<double>, 4> ranges { juce::Range<double> (0.0, 2000.0), { 0.0, 2000.0 }, { -24.0, 12.0 }, { -12.0, 12.0 } };
    const std::array<double, 4> steps { 1.0, 1.0, 0.1, 1.0 };

    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& s = sliders[i].slider;
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (ranges[i], steps[i]);
        if (i < 2)
            s.setSkewFactorFromMidPoint (300.0);
        s.setDoubleClickReturnValue (true, 0.0);
        s.onValueChange = [this, i]
        {
            auto& sl = sliders[i].slider;
            switch (i)
            {
                case 0: edit.fadeInMs = (float) sl.getValue(); break;
                case 1: edit.fadeOutMs = (float) sl.getValue(); break;
                case 2: edit.gainDb = (float) sl.getValue(); break;
                default: edit.pitch = (float) sl.getValue(); break;
            }
            repaint();
            if (! sl.isMouseButtonDown())
                commit();
        };
        s.onDragEnd = [this] { commit(); };
        addAndMakeVisible (s);
    }

    setItem (nullptr, 44100.0, 120.0);
}

void EditorPanel::setItem (const SampleItem* item, double rate, double)
{
    const int newId = item != nullptr ? item->id : -1;
    const bool sameItem = newId == id;

    id = newId;
    sampleRate = rate;
    if (item != nullptr)
    {
        isLoop = item->kind == ClipKind::loop;
        bars = item->bars;
        title = item->name;
        detail = item->detail (rate);
        peaks = item->originalPeaks;
        originalSeconds = item->original != nullptr ? item->original->getNumSamples() / rate : 0.0;
        if (! sameItem || dragging == 0)
            edit = item->edit;
    }
    else
    {
        title = {};
        peaks.clear();
        edit = {};
    }

    for (auto* c : std::initializer_list<juce::Component*> { &play, &reverse, &reset, &drag })
        c->setEnabled (item != nullptr);
    for (auto& s : sliders)
        s.slider.setEnabled (item != nullptr);
    syncControls();
    repaint();
}

void EditorPanel::syncControls()
{
    sliders[0].slider.setValue (edit.fadeInMs, juce::dontSendNotification);
    sliders[1].slider.setValue (edit.fadeOutMs, juce::dontSendNotification);
    sliders[2].slider.setValue (edit.gainDb, juce::dontSendNotification);
    sliders[3].slider.setValue (edit.pitch, juce::dontSendNotification);
    reverse.setToggleState (edit.reverse, juce::dontSendNotification);
}

void EditorPanel::commit()
{
    if (onEdit && id >= 0)
        onEdit (id, edit);
}

void EditorPanel::setPlayback (bool isPlaying, float p)
{
    if (isPlaying != playing || std::abs (p - progress) > 0.001f)
    {
        playing = isPlaying;
        progress = p;
        play.setButtonText (playing ? "Stop" : "Play");
        play.getProperties().set ("chrome", playing);
        repaint();
    }
}

void EditorPanel::resized()
{
    auto r = getLocalBounds().reduced (26, 20);

    auto top = r.removeFromTop (44);
    drag.setBounds (top.removeFromRight (190).withSizeKeepingCentre (190, 40));
    top.removeFromRight (12);
    reset.setBounds (top.removeFromRight (86).withSizeKeepingCentre (86, 34));
    top.removeFromRight (8);
    reverse.setBounds (top.removeFromRight (106).withSizeKeepingCentre (106, 34));
    top.removeFromRight (8);
    play.setBounds (top.removeFromRight (86).withSizeKeepingCentre (86, 34));

    r.removeFromTop (12);
    auto controls = r.removeFromBottom (46);
    r.removeFromBottom (12);
    waveArea = r;

    const int w = controls.getWidth() / (int) sliders.size();
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto cell = controls.removeFromLeft (w).reduced (10, 0);
        sliders[i].slider.setBounds (cell.removeFromBottom (24));
    }
}

float EditorPanel::toX (double f) const   { return (float) waveArea.getX() + (float) f * (float) waveArea.getWidth(); }
double EditorPanel::toFraction (float x) const { return juce::jlimit (0.0, 1.0, (double) (x - (float) waveArea.getX()) / juce::jmax (1, waveArea.getWidth())); }

double EditorPanel::snap (double f) const
{
    if (! isLoop || bars <= 0)
        return f;
    const double beats = bars * 4.0;
    return std::round (f * beats) / beats;
}

void EditorPanel::paint (juce::Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat().reduced (1.0f));

    auto header = getLocalBounds().reduced (26, 20).removeFromTop (44).toFloat();
    if (id < 0)
    {
        drawMonoLabel (g, "Editor", header.removeFromTop (16.0f), juce::Justification::topLeft, accent, 11.0f);
        g.setColour (text2);
        g.setFont (body (16.0f));
        g.drawText ("Pick a loop or one-shot to edit it here.", header, juce::Justification::centredLeft, false);
    }
    else
    {
        auto label = header.removeFromLeft (header.getWidth() - 520.0f);
        fillChrome (g, textPath (title.toUpperCase(), display (30.0f), label.removeFromTop (28.0f).withWidth (juce::jmin (label.getWidth(), 18.0f * (float) title.length() + 40.0f))), true);
        const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
        const double seconds = (edit.end - edit.start) * originalSeconds;
        drawMonoLabel (g, detail + dot + "edit " + juce::String (seconds, 2) + " s", label, juce::Justification::topLeft, muted, 10.5f);
    }

    // waveform well
    const auto wa = waveArea.toFloat();
    g.setColour (well);
    g.fillRoundedRectangle (wa, 14.0f);
    g.setColour (line);
    g.drawRoundedRectangle (wa.reduced (0.5f), 14.0f, 1.0f);

    if (id < 0)
        return;

    // beat / bar grid
    if (isLoop && bars > 0)
        for (int b = 1; b < bars * 4; ++b)
        {
            const float x = toX ((double) b / (bars * 4.0));
            g.setColour (accentL.withAlpha (b % 4 == 0 ? 0.16f : 0.06f));
            g.drawVerticalLine ((int) x, wa.getY() + 6.0f, wa.getBottom() - 6.0f);
        }

    const auto inner = wa.reduced (0.0f, 12.0f);
    drawWaveform (g, peaks, inner, muted.withAlpha (0.45f), muted.withAlpha (0.25f));

    // the kept part, bright
    const float x0 = toX (edit.start), x1 = toX (edit.end);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<float> (x0, wa.getY(), x1 - x0, wa.getHeight()).toNearestInt());
        drawWaveform (g, peaks, inner, accentL, juce::Colour (0xff4f78c4));

        // fades as ramps
        const float fadeIn = (float) (edit.fadeInMs * 0.001 / juce::jmax (0.001, originalSeconds)) * wa.getWidth();
        const float fadeOut = (float) (edit.fadeOutMs * 0.001 / juce::jmax (0.001, originalSeconds)) * wa.getWidth();
        g.setColour (text.withAlpha (0.7f));
        if (fadeIn > 1.0f)
            g.drawLine (x0, wa.getBottom() - 6.0f, juce::jmin (x1, x0 + fadeIn), wa.getY() + 6.0f, 1.5f);
        if (fadeOut > 1.0f)
            g.drawLine (juce::jmax (x0, x1 - fadeOut), wa.getY() + 6.0f, x1, wa.getBottom() - 6.0f, 1.5f);
    }

    // crop handles
    for (float x : { x0, x1 })
    {
        g.setColour (accentL);
        g.fillRect (x - 1.0f, wa.getY(), 2.0f, wa.getHeight());
        g.fillRoundedRectangle (x - 5.0f, wa.getY() + wa.getHeight() * 0.5f - 14.0f, 10.0f, 28.0f, 4.0f);
    }

    // playhead (inside the kept part)
    if (playing)
    {
        const float pos = edit.reverse ? 1.0f - progress : progress;
        const float x = x0 + pos * (x1 - x0);
        g.setColour (text);
        g.fillRect (x - 0.75f, wa.getY() + 4.0f, 1.5f, wa.getHeight() - 8.0f);
    }

    if (std::abs (edit.pitch) > 0.01f || edit.reverse || std::abs (edit.gainDb) > 0.01f)
    {
        juce::StringArray tags;
        if (edit.reverse) tags.add ("REVERSED");
        if (std::abs (edit.pitch) > 0.01f) tags.add (sliders[3].format (edit.pitch));
        if (std::abs (edit.gainDb) > 0.01f) tags.add (sliders[2].format (edit.gainDb));
        drawMonoLabel (g, tags.joinIntoString ("  "), wa.reduced (14.0f, 8.0f).removeFromTop (14.0f), juce::Justification::topRight, accentL, 10.0f);
    }

    // slider captions
    for (auto& s : sliders)
    {
        const auto b = s.slider.getBounds().toFloat().translated (0.0f, -20.0f).withHeight (18.0f);
        drawMonoLabel (g, s.label, b, juce::Justification::centredLeft, muted, 10.5f);
        g.setColour (text);
        g.setFont (mono (11.5f, true));
        g.drawText (s.format (s.slider.getValue()), b, juce::Justification::centredRight, false);
    }
}

void EditorPanel::mouseMove (const juce::MouseEvent& e)
{
    const bool nearHandle = id >= 0 && waveArea.contains (e.getPosition())
                         && (std::abs (e.position.x - toX (edit.start)) < 9.0f || std::abs (e.position.x - toX (edit.end)) < 9.0f);
    setMouseCursor (nearHandle ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void EditorPanel::mouseDown (const juce::MouseEvent& e)
{
    dragging = 0;
    if (id < 0 || ! waveArea.contains (e.getPosition()))
        return;
    const float ds = std::abs (e.position.x - toX (edit.start)), de = std::abs (e.position.x - toX (edit.end));
    if (juce::jmin (ds, de) < 12.0f)
        dragging = ds <= de ? 1 : 2;
}

void EditorPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == 0)
        return;

    const double minLength = isLoop && bars > 0 ? 1.0 / (bars * 4.0) : 0.02 / juce::jmax (0.02, originalSeconds);
    const double f = snap (toFraction (e.position.x));
    if (dragging == 1)
        edit.start = juce::jlimit (0.0, edit.end - minLength, f);
    else
        edit.end = juce::jlimit (edit.start + minLength, 1.0, f);
    repaint();
}

void EditorPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragging != 0)
    {
        dragging = 0;
        commit();
    }
}

} // namespace digga
