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
    // a transparency layer: the chrome gradient ignores setOpacity
    g.beginTransparencyLayer (isEnabled() ? 1.0f : 0.35f);
    drawChromePill (g, r, hover, false);
    g.setColour (well);
    for (int col = 0; col < 2; ++col)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse (r.getX() + 20.0f + (float) col * 6.0f, r.getCentreY() + (float) (row - 1) * 6.0f - 1.6f, 3.2f, 3.2f);
    g.setFont (display (14.0f).withExtraKerningFactor (0.04f));
    g.drawText ("DRAG TO DAW", r.withTrimmedLeft (30.0f), juce::Justification::centred, false);
    g.endTransparencyLayer();
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
namespace
{
    const char* snapName (WaveformEditor::Snap s)
    {
        switch (s)
        {
            case WaveformEditor::Snap::off:          return "Snap off";
            case WaveformEditor::Snap::zeroCrossing: return "Snap zero";
            case WaveformEditor::Snap::sixteenth:    return "Snap 1/16";
            case WaveformEditor::Snap::eighth:       return "Snap 1/8";
            case WaveformEditor::Snap::beat:         return "Snap beat";
            case WaveformEditor::Snap::bar:          return "Snap bar";
        }
        return "";
    }
}

EditorPanel::EditorPanel()
{
    play.onClick = [this] { if (onPlay && id >= 0) onPlay (id); };
    reverse.setClickingTogglesState (true);
    reverse.onClick = [this] { edit.reverse = reverse.getToggleState(); commit(); };
    reset.setTooltip ("Back to the sample as it was cut (crop, fades, pitch, effects, stem mix)");
    reset.onClick = [this] { edit = {}; syncControls(); commit(); };
    drag.onDragOut = [this] { if (onDragOut && id >= 0) onDragOut (id); };

    // view tabs
    waveTab.onClick = [this] { setView (View::wave); };
    midiTab.onClick = [this] { setView (View::midi); };
    fxTab.onClick = [this] { setView (View::fx); };
    midiTab.setTooltip ("Shred the sample into MIDI notes, edit them, drag them into your piano roll");
    fxTab.setTooltip ("Effect rack: filter, drive, crush, gate, tape stop, delay, reverb, width, KILL");

    // wave tools
    zoomOut.onClick = [this] { wave.zoomOut(); };
    zoomIn.onClick = [this] { wave.zoomIn(); };
    zoomFit.onClick = [this] { wave.zoomToFit(); };
    zoomSel.onClick = [this] { wave.zoomToSelection(); };
    zoomFit.setTooltip ("Show the whole sample");
    zoomSel.setTooltip ("Zoom to the kept part");
    snapButton.setTooltip ("Where markers snap (hold Alt while dragging to ignore it)");
    snapButton.onClick = [this]
    {
        juce::PopupMenu menu;
        using S = WaveformEditor::Snap;
        for (auto s : { S::off, S::zeroCrossing, S::sixteenth, S::eighth, S::beat, S::bar })
            menu.addItem (snapName (s), true, wave.getSnap() == s, [this, s] { wave.setSnap (s); updateSnapButton(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (snapButton));
    };

    // drag raw or with the effects
    dragRaw.setTooltip ("Drag the sample without the effect rack");
    dragFx.setTooltip ("Drag the sample with the effect rack");
    dragRaw.onClick = [this] { dragWithFx = false; syncControls(); };
    dragFx.onClick = [this] { dragWithFx = true; syncControls(); };

    for (auto* c : std::initializer_list<juce::Component*> { &play, &reverse, &reset, &drag, &waveTab, &midiTab, &fxTab,
                                                             &zoomOut, &zoomIn, &zoomFit, &zoomSel, &snapButton, &dragRaw, &dragFx,
                                                             &wave, &midiView, &fxRack })
        addAndMakeVisible (c);

    wave.onRangeChanged = [this] (double start, double end)
    {
        edit.start = start;
        edit.end = end;
        commit();
    };
    wave.onLoopStartChanged = [this] (double f) { edit.loopStart = f; commit(); };
    wave.onAudition = [this] (double f) { if (onAudition && id >= 0) onAudition (id, f); };

    fxRack.onChange = [this] (const fx::FxSettings& settings)
    {
        edit.fx = settings;
        syncControls();
        commit();
    };

    auto ms = [] (double v) { return v < 1.0 ? juce::String ("off") : juce::String (juce::roundToInt (v)) + " ms"; };
    sliders[0].label = "Fade in";  sliders[0].format = ms;
    sliders[1].label = "Fade out"; sliders[1].format = ms;
    sliders[2].label = "Gain";     sliders[2].format = [] (double v) { return (v > 0.04 ? "+" : "") + juce::String (v, 1) + " dB"; };
    sliders[3].label = "Pitch";    sliders[3].format = [] (double v) { return (v > 0.5 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; };

    const std::array<juce::Range<double>, 4> ranges { juce::Range<double> (0.0, 2000.0), { 0.0, 2000.0 }, { -24.0, 12.0 }, { -12.0, 12.0 } };
    const std::array<double, 4> steps { 1.0, 1.0, 0.1, 1.0 };

    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& sl = sliders[i].slider;
        sl.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sl.setRange (ranges[i], steps[i]);
        if (i < 2)
            sl.setSkewFactorFromMidPoint (300.0);
        sl.setDoubleClickReturnValue (true, 0.0);
        sl.onValueChange = [this, i]
        {
            auto& s = sliders[i].slider;
            switch (i)
            {
                case 0: edit.fadeInMs = (float) s.getValue(); break;
                case 1: edit.fadeOutMs = (float) s.getValue(); break;
                case 2: edit.gainDb = (float) s.getValue(); break;
                default: edit.pitch = (float) s.getValue(); break;
            }
            repaint();
            if (! s.isMouseButtonDown())
                commit();
        };
        sl.onDragEnd = [this] { commit(); };
        addAndMakeVisible (sl);
    }

    updateSnapButton();
    setItem (nullptr, 44100.0, 120.0);
    setView (View::wave);
}

EditorPanel::~EditorPanel() = default;

void EditorPanel::updateSnapButton()
{
    snapButton.setButtonText (snapName (wave.getSnap()));
}

void EditorPanel::setView (View v)
{
    view = v;
    waveTab.getProperties().set ("chrome", view == View::wave);
    midiTab.getProperties().set ("chrome", view == View::midi);
    fxTab.getProperties().set ("chrome", view == View::fx);
    for (auto* b : { &waveTab, &midiTab, &fxTab })
        b->repaint();
    layoutView();
    repaint();
    if (onViewChanged)
        onViewChanged (view);
}

void EditorPanel::setItem (const SampleItem* item, double rate, double newBpm)
{
    const int newId = item != nullptr ? item->id : -1;
    const bool sameItem = newId == id;

    id = newId;
    sampleRate = rate;
    bpm = newBpm;
    if (item != nullptr)
    {
        isLoop = item->kind == ClipKind::loop;
        title = item->name;
        detail = item->detail (rate);
        originalSeconds = item->original != nullptr ? item->original->getNumSamples() / rate : 0.0;
        edit = item->edit;

        if (item->original != shownAudio)
        {
            shownAudio = item->original;
            wave.setAudio (item->original, rate);
            wave.setGrid (isLoop ? newBpm : 0.0, isLoop);
            wave.setSnap (isLoop ? WaveformEditor::Snap::beat : WaveformEditor::Snap::zeroCrossing);
            updateSnapButton();
            if (! sameItem)
                wave.zoomToFit();
        }
        wave.setRange (edit.start, edit.end);
        wave.setLoopStart (edit.loopStart);
        fxRack.setSettings (edit.fx);
        fxRack.setEnabled (true);
    }
    else
    {
        title = {};
        edit = {};
        shownAudio = nullptr;
        wave.setAudio (nullptr, rate);
        fxRack.setSettings ({});
        fxRack.setEnabled (false, "Pick a loop or one-shot");
    }

    for (auto* c : std::initializer_list<juce::Component*> { &play, &reverse, &reset, &drag, &zoomOut, &zoomIn, &zoomFit,
                                                             &zoomSel, &snapButton, &dragRaw, &dragFx })
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
    fxTab.setButtonText (edit.fx.anyOn() ? juce::String (juce::CharPointer_UTF8 ("FX \xe2\x97\x8f")) : juce::String ("FX"));

    const bool fxOn = edit.fx.anyOn();
    dragRaw.getProperties().set ("chrome", ! dragWithFx || ! fxOn);
    dragFx.getProperties().set ("chrome", dragWithFx && fxOn);
    dragFx.setEnabled (id >= 0 && fxOn);
    dragRaw.repaint();
    dragFx.repaint();
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
        play.repaint();

        // playhead on the original: inside the kept part, after the loop start
        double pos = edit.reverse ? 1.0 - (double) p : (double) p;
        pos = std::fmod (pos + edit.loopStart, 1.0);
        wave.setPlayhead (playing, edit.start + pos * (edit.end - edit.start));
        midiView.setPlayhead (playing, p);
    }
}

void EditorPanel::resized()
{
    auto r = getLocalBounds().reduced (22, 16);

    headerArea = r.removeFromTop (44);
    auto top = headerArea;
    drag.setBounds (top.removeFromRight (190).withSizeKeepingCentre (190, 40));
    top.removeFromRight (8);
    dragFx.setBounds (top.removeFromRight (46).withSizeKeepingCentre (46, 30));
    top.removeFromRight (4);
    dragRaw.setBounds (top.removeFromRight (56).withSizeKeepingCentre (56, 30));
    top.removeFromRight (16);
    reset.setBounds (top.removeFromRight (80).withSizeKeepingCentre (80, 34));
    top.removeFromRight (8);
    reverse.setBounds (top.removeFromRight (100).withSizeKeepingCentre (100, 34));
    top.removeFromRight (8);
    play.setBounds (top.removeFromRight (80).withSizeKeepingCentre (80, 34));

    r.removeFromTop (10);
    auto tabs = r.removeFromTop (30);
    waveTab.setBounds (tabs.removeFromLeft (92));
    tabs.removeFromLeft (8);
    midiTab.setBounds (tabs.removeFromLeft (128));
    tabs.removeFromLeft (8);
    fxTab.setBounds (tabs.removeFromLeft (80));

    snapButton.setBounds (tabs.removeFromRight (118));
    tabs.removeFromRight (14);
    zoomSel.setBounds (tabs.removeFromRight (54));
    tabs.removeFromRight (6);
    zoomFit.setBounds (tabs.removeFromRight (54));
    tabs.removeFromRight (6);
    zoomIn.setBounds (tabs.removeFromRight (36));
    tabs.removeFromRight (6);
    zoomOut.setBounds (tabs.removeFromRight (36));

    r.removeFromTop (10);
    mainArea = r;
    layoutView();
}

void EditorPanel::layoutView()
{
    auto r = mainArea;
    const bool showWave = view == View::wave;
    for (auto* c : std::initializer_list<juce::Component*> { &zoomOut, &zoomIn, &zoomFit, &zoomSel, &snapButton })
        c->setVisible (showWave);
    for (auto& s : sliders)
        s.slider.setVisible (showWave);
    wave.setVisible (showWave);
    midiView.setVisible (view == View::midi);
    fxRack.setVisible (view == View::fx);

    if (showWave)
    {
        controlsArea = r.removeFromBottom (44);
        r.removeFromBottom (10);
        wave.setBounds (r);

        auto controls = controlsArea;
        const int w = controls.getWidth() / (int) sliders.size();
        for (size_t i = 0; i < sliders.size(); ++i)
        {
            auto cell = controls.removeFromLeft (w).reduced (10, 0);
            sliders[i].slider.setBounds (cell.removeFromBottom (24));
        }
    }
    else
    {
        controlsArea = {};
        midiView.setBounds (r);
        fxRack.setBounds (r);
    }
}

void EditorPanel::paint (juce::Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat().reduced (1.0f));

    auto header = headerArea.toFloat();
    if (id < 0)
    {
        drawMonoLabel (g, "Editor", header.removeFromTop (16.0f), juce::Justification::topLeft, accent, 11.0f);
        g.setColour (text2);
        g.setFont (body (16.0f));
        g.drawText ("Pick a loop or one-shot to edit it here.", header, juce::Justification::centredLeft, false);
    }
    else
    {
        auto label = header.removeFromLeft (juce::jmax (120.0f, header.getWidth() - 640.0f));
        fillChrome (g, textPath (title.toUpperCase(), display (28.0f),
                                 label.removeFromTop (27.0f).withWidth (juce::jmin (label.getWidth(), 17.0f * (float) title.length() + 40.0f))), true);
        const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
        const double seconds = (edit.end - edit.start) * originalSeconds;
        juce::String info = detail + dot + "edit " + juce::String (seconds, 2) + " s";
        if (edit.fx.anyOn())
            info << dot << "fx on";
        drawMonoLabel (g, info, label, juce::Justification::topLeft, muted, 10.0f);

        drawMonoLabel (g, "Drag", dragRaw.getBounds().toFloat().translated (-44.0f, 0.0f).withWidth (40.0f),
                       juce::Justification::centredRight, muted, 9.5f);
    }

    // slider captions (wave view)
    if (view == View::wave)
        for (auto& s : sliders)
        {
            const auto b = s.slider.getBounds().toFloat().translated (0.0f, -18.0f).withHeight (16.0f);
            drawMonoLabel (g, s.label, b, juce::Justification::centredLeft, muted, 10.0f);
            g.setColour (text);
            g.setFont (mono (11.0f, true));
            g.drawText (s.format (s.slider.getValue()), b, juce::Justification::centredRight, false);
        }
}

} // namespace digga
