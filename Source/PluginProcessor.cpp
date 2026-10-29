#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SolidClipAudioProcessor::SolidClipAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SolidClip", sc::createParameterLayout())
{
    bypassParam = apvts.getParameter (sc::ParamIDs::bypass);
}

bool SolidClipAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SolidClipAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, sc::readSettings (apvts));
    syncLatency();
}

void SolidClipAudioProcessor::reset()
{
    engine.reset();
}

void SolidClipAudioProcessor::syncLatency()
{
    const int latency = engine.getLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

void SolidClipAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() < 2)
        return;

    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples(),
                    sc::readSettings (apvts));

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    syncLatency();
}

void SolidClipAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Host bypass without our bypass parameter: still run the engine in bypass so the dry
    // signal stays latency-compensated.
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() < 2)
        return;

    auto settings = sc::readSettings (apvts);
    settings.bypass = true;
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples(), settings);
    syncLatency();
}

//==============================================================================
juce::AudioProcessorEditor* SolidClipAudioProcessor::createEditor()
{
    return new SolidClipAudioProcessorEditor (*this);
}

void SolidClipAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SolidClipAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
#ifndef SOLIDCLIP_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SolidClipAudioProcessor();
}
#endif
