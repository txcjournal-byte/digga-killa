#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace digga
{

namespace
{
    const juce::Identifier stateType { "TrapWinSamplerState" };
    const juce::Identifier samplePathProperty { "samplePath" };
}

TrapWinProcessor::TrapWinProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout()),
      engine (jobs, clipPlayer),
      sampleStore (jobs, [this] (std::unique_ptr<PlaybackSample> s)
      {
          engine.sourceReady (s->audio, s->sampleRate);
          player.setSample (std::move (s));
      })
{
    volumeParam = parameters.getRawParameterValue (ParamIDs::volume);
    startTimerHz (10);
}

TrapWinProcessor::~TrapWinProcessor()
{
    stopTimer();
    jobs.cancelAll();
}

juce::AudioProcessorValueTreeState::ParameterLayout TrapWinProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::volume, 1 }, "Volume", NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

void TrapWinProcessor::prepareToPlay (double sampleRate, int)
{
    player.prepare (sampleRate);
    clipPlayer.prepare (sampleRate);
    outputGain.reset (sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (volumeParam->load()));
    sampleStore.setTargetSampleRate (sampleRate);
}

bool TrapWinProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void TrapWinProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
    clipPlayer.beginBlock (transport, false);

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

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (volumeParam->load()));
    if (outputGain.isSmoothing())
        outputGain.applyGain (buffer, numSamples);
    else
        buffer.applyGain (outputGain.getTargetValue());
}

void TrapWinProcessor::timerCallback()
{
    player.collectGarbage();
    clipPlayer.collectGarbage();

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

void TrapWinProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root (stateType);
    root.setProperty ("version", 2, nullptr);
    root.setProperty (samplePathProperty, sampleStore.getInfo().file.getFullPathName(), nullptr);
    root.setProperty ("editorWidth", editorWidth.load(), nullptr);
    root.setProperty ("dragAsMidi", dragAsMidi.load(), nullptr);
    root.addChild (parameters.copyState(), -1, nullptr);
    root.addChild (engine.getState(), -1, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void TrapWinProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* TrapWinProcessor::createEditor()
{
    return new TrapWinEditor (*this);
}

} // namespace digga

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new digga::TrapWinProcessor();
}
