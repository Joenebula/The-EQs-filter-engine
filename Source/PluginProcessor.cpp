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
      apvts (*this, &undoManager, "ParaEQState", peq::params::createLayout())
{
    inputGainDb  = apvts.getRawParameterValue (peq::params::inputGain);
    outputGainDb = apvts.getRawParameterValue (peq::params::outputGain);
    bypassParam  = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (peq::params::bypass));
    jassert (inputGainDb != nullptr && outputGainDb != nullptr && bypassParam != nullptr);

    using peq::params::BandField;
    for (int b = 0; b < peq::FilterEngine::numBands; ++b)
    {
        auto get = [this, b] (BandField f)
        {
            auto* v = apvts.getRawParameterValue (peq::params::bandID (b, f));
            jassert (v != nullptr);
            return v;
        };
        auto& bp = bandParams[(size_t) b];
        bp.used  = get (BandField::used);
        bp.on    = get (BandField::on);
        bp.type  = get (BandField::type);
        bp.freq  = get (BandField::freq);
        bp.gain  = get (BandField::gain);
        bp.q     = get (BandField::q);
        bp.slope = get (BandField::slope);
        bp.mode  = get (BandField::mode);
    }
}

void ParaEQAudioProcessor::updateEngine() noexcept
{
    const auto& slopes = peq::limits::slopesDbPerOct;

    for (int b = 0; b < peq::FilterEngine::numBands; ++b)
    {
        const auto& bp = bandParams[(size_t) b];
        peq::BandParameters p;
        p.enabled     = bp.used->load() >= 0.5f && bp.on->load() >= 0.5f;
        p.type        = (peq::FilterType) juce::jlimit (0, (int) peq::FilterType::NumTypes - 1,
                                                        juce::roundToInt (bp.type->load()));
        p.frequencyHz = (double) bp.freq->load();
        p.gainDb      = (double) bp.gain->load();
        p.q           = (double) bp.q->load();
        p.slopeDbPerOct = slopes[(size_t) juce::jlimit (0, (int) slopes.size() - 1, juce::roundToInt (bp.slope->load()))];
        const auto mode = (peq::ChannelMode) juce::jlimit (0, (int) peq::ChannelMode::NumModes - 1,
                                                           juce::roundToInt (bp.mode->load()));
        engine.setBand (b, p, mode);
    }
}

void ParaEQAudioProcessor::updateSolo() noexcept
{
    // Audition filter: a band-pass centred on the soloed band. For bells,
    // notches and band-passes it uses the band's own Q, so you hear exactly the
    // region being changed; for shelves and cuts a fixed, fairly wide Q.
    peq::BandParameters p;
    p.type = peq::FilterType::BandPass;
    p.slopeDbPerOct = 12;
    p.enabled = false;

    const int solo = soloBand.load();
    if (solo >= 0 && solo < peq::FilterEngine::numBands)
    {
        const auto& bp = bandParams[(size_t) solo];
        if (bp.used->load() >= 0.5f)
        {
            const auto type = (peq::FilterType) juce::roundToInt (bp.type->load());
            const bool narrow = type == peq::FilterType::Bell || type == peq::FilterType::Notch
                             || type == peq::FilterType::BandPass;
            p.frequencyHz = (double) bp.freq->load();
            p.q = narrow ? juce::jlimit (0.3, 40.0, (double) bp.q->load()) : 0.5;
            p.enabled = true;
        }
    }
    soloFilter.setTarget (p);
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

    updateEngine();
    engine.prepare (sampleRate);
    engine.snapToTargets();
    engineIdle = false;

    updateSolo();
    soloFilter.prepare (sampleRate);
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
    updateEngine();

    // Fully bypassed and settled: leave the audio untouched. The EQ keeps
    // receiving its settings (above) so it comes back in the right state.
    if (! wet.isSmoothing() && wet.getTargetValue() <= 0.0)
    {
        inGain.skip (numSamples);
        outGain.skip (numSamples);
        engineIdle = true;
        return;
    }

    if (engineIdle)
    {
        // Leaving bypass: start the bands exactly at their current settings
        // with clean filter memory (the bypass crossfade hides the restart).
        engine.snapToTargets();
        engineIdle = false;
    }

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    double* const* data = buffer.getArrayOfWritePointers();

    // 1. Input gain (per-sample smoothed, identical on every channel).
    for (int i = 0; i < numSamples; ++i)
    {
        const double g = inGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] *= g;
    }

    // 2. The EQ bands (then the solo/audition filter, normally off).
    engine.process (data, numChannels, numSamples);
    updateSolo();
    soloFilter.process (data, numChannels, numSamples);

    // 3. Output gain and the bypass crossfade.
    for (int i = 0; i < numSamples; ++i)
    {
        const double g = outGain.getNextValue();
        const double w = wet.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double dry = dryBuffer.getSample (ch, i);
            data[ch][i] = dry + w * (data[ch][i] * g - dry);
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
