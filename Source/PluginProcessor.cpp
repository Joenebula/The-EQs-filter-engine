#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    constexpr double gainSmoothingSeconds = 0.02;
    constexpr double bypassFadeSeconds    = 0.01;
}

ParaEQAudioProcessor::ParaEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ParaEQState", peq::params::createLayout())
{
    inputGainDb  = apvts.getRawParameterValue (peq::params::inputGain);
    outputGainDb = apvts.getRawParameterValue (peq::params::outputGain);
    bypassParam  = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (peq::params::bypass));
    jassert (inputGainDb != nullptr && outputGainDb != nullptr && bypassParam != nullptr);
}

bool ParaEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ParaEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int channels = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
    const int block = juce::jmax (samplesPerBlock, 32);

    doubleBuffer.setSize (channels, block);
    dryBuffer.setSize (channels, block);

    inGain.reset (sampleRate, gainSmoothingSeconds);
    outGain.reset (sampleRate, gainSmoothingSeconds);
    wet.reset (sampleRate, bypassFadeSeconds);

    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((double) inputGainDb->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((double) outputGainDb->load()));
    wet.setCurrentAndTargetValue (bypassParam->get() ? 0.0 : 1.0);
}

void ParaEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = juce::jmin (buffer.getNumChannels(), doubleBuffer.getNumChannels());
    const int capacity = doubleBuffer.getNumSamples();

    // Process in chunks no larger than the preallocated buffer, so an
    // unexpectedly large host block never allocates on the audio thread.
    for (int start = 0; start < buffer.getNumSamples(); start += capacity)
    {
        const int n = juce::jmin (capacity, buffer.getNumSamples() - start);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* src = buffer.getReadPointer (ch, start);
            double* dst = doubleBuffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                dst[i] = (double) src[i];
        }

        juce::AudioBuffer<double> view (doubleBuffer.getArrayOfWritePointers(), numChannels, n);
        processDouble (view);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double* src = doubleBuffer.getReadPointer (ch);
            float* dst = buffer.getWritePointer (ch, start);
            for (int i = 0; i < n; ++i)
                dst[i] = (float) src[i];
        }
    }
}

void ParaEQAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int capacity = dryBuffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int start = 0; start < buffer.getNumSamples(); start += capacity)
    {
        const int n = juce::jmin (capacity, buffer.getNumSamples() - start);
        double* const* base = buffer.getArrayOfWritePointers();
        double* chans[8] {};
        const int nc = juce::jmin (numChannels, 8);
        for (int ch = 0; ch < nc; ++ch)
            chans[ch] = base[ch] + start;
        juce::AudioBuffer<double> view (chans, nc, n);
        processDouble (view);
    }
}

void ParaEQAudioProcessor::processDouble (juce::AudioBuffer<double>& buffer)
{
    const int numChannels = juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();

    for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    inGain.setTargetValue (juce::Decibels::decibelsToGain ((double) inputGainDb->load()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain ((double) outputGainDb->load()));
    wet.setTargetValue (bypassParam->get() ? 0.0 : 1.0);

    // Fully bypassed and settled: leave the audio untouched.
    if (! wet.isSmoothing() && wet.getTargetValue() <= 0.0)
    {
        inGain.skip (numSamples);
        outGain.skip (numSamples);
        return;
    }

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // Per-sample smoothed gain, identical on every channel.
    double* const* data = buffer.getArrayOfWritePointers();
    for (int i = 0; i < numSamples; ++i)
    {
        const double gIn  = inGain.getNextValue();
        // --- EQ processing goes here in Phase 3 (operates between the gains) ---
        const double gOut = outGain.getNextValue();
        const double w    = wet.getNextValue();
        const double g    = gIn * gOut;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double dry = dryBuffer.getSample (ch, i);
            const double processed = data[ch][i] * g;
            data[ch][i] = dry + w * (processed - dry);
        }
    }
}

juce::AudioProcessorEditor* ParaEQAudioProcessor::createEditor()
{
    return new ParaEQAudioProcessorEditor (*this);
}

void ParaEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ParaEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParaEQAudioProcessor();
}
