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
      sampleStore (jobs, [this] (std::unique_ptr<PlaybackSample> s) { player.setSample (std::move (s)); })
{
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

void DiggaKillaProcessor::prepareToPlay (double sampleRate, int)
{
    player.prepare (sampleRate);
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

    if (auto* host = getPlayHead())
        if (const auto position = host->getPosition())
            if (const auto bpm = position->getBpm())
                hostBpm.store (*bpm);

    player.beginBlock();

    const int numSamples = buffer.getNumSamples();
    int rendered = 0;

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const int eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (eventTime > rendered)
        {
            player.render (buffer, rendered, eventTime - rendered);
            rendered = eventTime;
        }

        if (message.isNoteOn())
            player.trigger();
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            player.stop();
    }

    if (rendered < numSamples)
        player.render (buffer, rendered, numSamples - rendered);
}

void DiggaKillaProcessor::timerCallback()
{
    player.collectGarbage();
}

void DiggaKillaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root (stateType);
    root.setProperty ("version", stateVersion, nullptr);
    root.setProperty (samplePathProperty, sampleStore.getInfo().file.getFullPathName(), nullptr);
    root.addChild (parameters.copyState(), -1, nullptr);

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

    const auto params = root.getChildWithName (parameters.state.getType());
    if (params.isValid())
        parameters.replaceState (params);

    const auto path = root[samplePathProperty].toString();
    if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
    {
        const juce::File file (path);
        if (file != sampleStore.getInfo().file)
            sampleStore.loadFile (file);
    }
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
