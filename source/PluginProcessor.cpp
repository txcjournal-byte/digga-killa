#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace digga
{

namespace
{
    constexpr int stateVersion = 1;
    const juce::Identifier stateType { "DiggaKillaState" };
    const juce::Identifier samplePathProperty { "samplePath" };

    juce::String percent (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + " %"; }
}

DiggaKillaProcessor::DiggaKillaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout()),
      engine (jobs, clipPlayer),
      sampleStore (jobs, [this] (std::unique_ptr<PlaybackSample> s)
      {
          engine.sourceReady (s->audio, s->sampleRate);
          player.setSample (std::move (s));
      })
{
    reverbParam = parameters.getRawParameterValue (ParamIDs::reverb);
    delayParam = parameters.getRawParameterValue (ParamIDs::delay);
    distortionParam = parameters.getRawParameterValue (ParamIDs::distortion);
    filterParam = parameters.getRawParameterValue (ParamIDs::filter);
    pitchParam = parameters.getRawParameterValue (ParamIDs::pitch);
    mixParam = parameters.getRawParameterValue (ParamIDs::mix);
    reverseParam = parameters.getRawParameterValue (ParamIDs::reverse);

    fx.prepare (44100.0, maxBlockSize, 2);
    startTimerHz (10);
}

DiggaKillaProcessor::~DiggaKillaProcessor()
{
    stopTimer();
    jobs.cancelAll();
}

juce::AudioProcessorValueTreeState::ParameterLayout DiggaKillaProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const auto percentAttributes = AudioParameterFloatAttributes().withStringFromValueFunction (percent);

    auto addPercent = [&] (const char* id, const char* name, float defaultValue)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                           NormalisableRange<float> (0.0f, 1.0f), defaultValue,
                                                           percentAttributes));
    };

    addPercent (ParamIDs::reverb, "Reverb", 0.0f);
    addPercent (ParamIDs::delay, "Delay", 0.0f);
    addPercent (ParamIDs::distortion, "Distortion", 0.0f);

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::filter, 1 }, "Filter", NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (std::abs (v) < 0.01f) return String ("Off");
            return (v < 0.0f ? "LP " : "HP ") + String (roundToInt (std::abs (v) * 100.0f)) + " %";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::pitch, 1 }, "Pitch", NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("st")));

    addPercent (ParamIDs::mix, "Mix", 1.0f);

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::reverse, 1 }, "Reverse", false));

    addPercent (ParamIDs::killStrength, "Kill Strength", 0.5f);

    return layout;
}

dsp::FxParams DiggaKillaProcessor::getFxParams() const noexcept
{
    dsp::FxParams p;
    p.reverb = reverbParam->load();
    p.delay = delayParam->load();
    p.distortion = distortionParam->load();
    p.filter = filterParam->load();
    p.pitch = pitchParam->load();
    p.mix = mixParam->load();
    const double host = hostBpm.load();
    p.bpm = host > 0.0 ? host : fallbackBpm.load();
    return p;
}

void DiggaKillaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);
    maxBlockSize = juce::jmax (1, samplesPerBlock);
    player.prepare (sampleRate);
    clipPlayer.prepare (sampleRate);
    fx.prepare (sampleRate, maxBlockSize, juce::jmax (1, getTotalNumOutputChannels()));
    sampleStore.setTargetSampleRate (sampleRate);
}

void DiggaKillaProcessor::releaseResources() {}

bool DiggaKillaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void DiggaKillaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    Transport transport;
    transport.bpm = 0.0;
    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm())
                transport.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition())
                transport.ppq = *ppq;
            transport.playing = position->getIsPlaying();
        }
    }
    hostBpm.store (transport.bpm);

    player.beginBlock();
    clipPlayer.beginBlock (transport, reverseParam->load() > 0.5f);

    const bool clipsSelected = clipPlayer.getSelected() >= 0;
    const int numSamples = buffer.getNumSamples();
    int rendered = 0;

    auto renderUpTo = [&] (int end)
    {
        if (end > rendered)
        {
            player.render (buffer, rendered, end - rendered);
            clipPlayer.render (buffer, rendered, end - rendered);
            rendered = end;
        }
    };

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        renderUpTo (juce::jlimit (0, numSamples, metadata.samplePosition));

        if (message.isNoteOn())
        {
            if (clipsSelected)
                clipPlayer.noteOn (message.getNoteNumber(), message.getFloatVelocity());
            else
                player.trigger();
        }
        else if (message.isNoteOff())
        {
            clipPlayer.noteOff (message.getNoteNumber());
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            clipPlayer.allNotesOff();
            player.stop();
        }
    }
    renderUpTo (numSamples);

    // effects, in chunks of the prepared block size
    auto params = getFxParams();
    if (transport.bpm > 0.0)
        params.bpm = transport.bpm;

    for (int start = 0; start < numSamples; start += maxBlockSize)
    {
        const int n = juce::jmin (maxBlockSize, numSamples - start);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, n);
        fx.process (chunk, n, params);
    }
}

void DiggaKillaProcessor::timerCallback()
{
    player.collectGarbage();
    clipPlayer.collectGarbage();
    fallbackBpm.store (engine.getProjectBpm());

    // follow the project tempo once it has settled for half a second
    const double bpm = hostBpm.load();
    if (! juce::approximatelyEqual (bpm, pendingHostBpm))
    {
        pendingHostBpm = bpm;
        hostBpmStableTicks = 0;
    }
    else if (++hostBpmStableTicks == 5)
    {
        engine.setHostBpm (bpm);
    }
}

void DiggaKillaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root (stateType);
    root.setProperty ("version", stateVersion, nullptr);
    root.setProperty (samplePathProperty, sampleStore.getInfo().file.getFullPathName(), nullptr);
    root.setProperty ("editorWidth", editorWidth.load(), nullptr);
    root.setProperty ("dragAsMidi", dragAsMidi.load(), nullptr);
    root.addChild (parameters.copyState(), -1, nullptr);
    root.addChild (engine.getState(), -1, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void DiggaKillaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType (stateType))
        return;

    editorWidth.store ((int) root.getProperty ("editorWidth", 0));
    dragAsMidi.store ((bool) root.getProperty ("dragAsMidi", false));

    const auto params = root.getChildWithName (parameters.state.getType());
    if (params.isValid())
        parameters.replaceState (params);

    const auto path = root[samplePathProperty].toString();
    bool reload = false;
    if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
    {
        const juce::File file (path);
        reload = file != sampleStore.getInfo().file || sampleStore.getStatus() == SampleStore::Status::error;
        if (reload)
            sampleStore.loadFile (file);
    }

    engine.setState (root.getChildWithName ("Engine"), reload);
}

juce::AudioProcessorEditor* DiggaKillaProcessor::createEditor()
{
    return new DiggaKillaEditor (*this);
}

} // namespace digga

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new digga::DiggaKillaProcessor();
}
