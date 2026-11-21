#include "PluginProcessor.h"
#include "DSP/AutoGain.h"
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
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          // Optional sidechain for dynamic bands (1.3). Off until the
                          // user routes a track to it in Cubase.
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "ParaEQState", peq::params::createLayout())
{
    inputGainDb  = apvts.getRawParameterValue (peq::params::inputGain);
    outputGainDb = apvts.getRawParameterValue (peq::params::outputGain);
    bypassParam  = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (peq::params::bypass));
    analogMatch  = apvts.getRawParameterValue (peq::params::analogMatch);
    gainScale    = apvts.getRawParameterValue (peq::params::gainScale);
    autoGainParam = apvts.getRawParameterValue (peq::params::autoGain);
    lookaheadParam = apvts.getRawParameterValue (peq::params::lookahead);
    jassert (lookaheadParam != nullptr);
    jassert (autoGainParam != nullptr);
    jassert (gainScale != nullptr);
    jassert (inputGainDb != nullptr && outputGainDb != nullptr && bypassParam != nullptr && analogMatch != nullptr);

    // Fixed size, allocated once: the editor may read these at any time, so
    // they are never resized while the plugin exists (~0.7 s at 192 kHz).
    preFifo.setCapacity (1 << 17);
    postFifo.setCapacity (1 << 17);

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
        bp.dyn   = get (BandField::dyn);
        bp.thr   = get (BandField::thr);
        bp.range = get (BandField::range);
        bp.att   = get (BandField::att);
        bp.rel   = get (BandField::rel);
        bp.ratio = get (BandField::ratio);
        bp.sc    = get (BandField::sc);
    }
}

void ParaEQAudioProcessor::updateEngine() noexcept
{
    const auto& slopes = peq::limits::slopesDbPerOct;
    const bool matched = analogMatch->load() >= 0.5f;
    const double scale = juce::jlimit (0.0, 2.0, (double) gainScale->load() / 100.0);
    std::array<peq::BandParameters, peq::FilterEngine::numBands> bandsNow {};
    std::array<peq::ChannelMode, peq::FilterEngine::numBands> modesNow {};

    for (int b = 0; b < peq::FilterEngine::numBands; ++b)
    {
        const auto& bp = bandParams[(size_t) b];
        peq::BandParameters p;
        p.enabled     = bp.used->load() >= 0.5f && bp.on->load() >= 0.5f;
        p.type        = (peq::FilterType) juce::jlimit (0, (int) peq::FilterType::NumTypes - 1,
                                                        juce::roundToInt (bp.type->load()));
        p.frequencyHz = (double) bp.freq->load();
        p.gainDb      = (double) bp.gain->load() * scale;
        p.q           = (double) bp.q->load();
        p.slopeDbPerOct = slopes[(size_t) juce::jlimit (0, (int) slopes.size() - 1, juce::roundToInt (bp.slope->load()))];
        p.analogMatched = matched;
        const auto mode = (peq::ChannelMode) juce::jlimit (0, (int) peq::ChannelMode::NumModes - 1,
                                                           juce::roundToInt (bp.mode->load()));
        engine.setBand (b, p, mode);
        bandsNow[(size_t) b] = p;
        modesNow[(size_t) b] = mode;

        peq::DynamicSettings d;
        d.enabled     = bp.dyn->load() >= 0.5f;
        d.thresholdDb = (double) bp.thr->load();
        d.rangeDb     = (double) bp.range->load();
        d.attackMs    = (double) bp.att->load();
        d.releaseMs   = (double) bp.rel->load();
        d.ratio       = (double) bp.ratio->load();
        d.externalSidechain = bp.sc->load() >= 0.5f;
        engine.setBandDynamics (b, d);
    }

    // Auto Gain: rework the correction only when the curve changed.
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    auto same = [] (const peq::BandParameters& a, const peq::BandParameters& b)
    {
        return a.enabled == b.enabled && a.type == b.type && a.slopeDbPerOct == b.slopeDbPerOct
            && a.analogMatched == b.analogMatched && juce::exactlyEqual (a.frequencyHz, b.frequencyHz)
            && juce::exactlyEqual (a.gainDb, b.gainDb) && juce::exactlyEqual (a.q, b.q);
    };
    bool changed = ! juce::exactlyEqual (sr, autoGainSampleRate);
    for (int b = 0; b < peq::FilterEngine::numBands && ! changed; ++b)
        changed = ! same (bandsNow[(size_t) b], autoGainBands[(size_t) b])
               || (bandsNow[(size_t) b].enabled && modesNow[(size_t) b] != autoGainModes[(size_t) b]);
    if (changed)
    {
        autoGainBands = bandsNow;
        autoGainModes = modesNow;
        autoGainSampleRate = sr;
        autoGainDb.store ((float) peq::autogain::correctionDb (bandsNow.data(), modesNow.data(),
                                                               peq::FilterEngine::numBands, sr),
                          std::memory_order_relaxed);
    }
}

double ParaEQAudioProcessor::outputGainTargetDb() const noexcept
{
    double db = (double) outputGainDb->load();
    if (autoGainParam->load() >= 0.5f)
        db += (double) autoGainDb.load (std::memory_order_relaxed);
    return db;
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
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // Sidechain: off, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void ParaEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int channels = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
    const int block = juce::jmax (samplesPerBlock, 32);

    doubleBuffer.setSize (channels, block);
    analyserScratch.assign ((size_t) block, 0.0f);
    dryBuffer.setSize (channels, block);

    lookaheadSamples = juce::jmax (1, juce::roundToInt (peq::params::lookaheadMs * 0.001 * sampleRate));
    delayLines.setSize (2, lookaheadSamples);
    delayLines.clear();
    delayPos = 0;
    aheadBuffer.setSize (4, block);
    aheadBuffer.clear();
    lookaheadActive = lookaheadParam->load() >= 0.5f;
    setLatencySamples (getLookaheadLatency());

    inGain.reset (sampleRate, gainSmoothingSeconds);
    outGain.reset (sampleRate, gainSmoothingSeconds);
    wet.reset (sampleRate, bypassFadeSeconds);

    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((double) inputGainDb->load()));
    wet.setCurrentAndTargetValue (bypassParam->get() ? 0.0 : 1.0);

    autoGainSampleRate = 0.0;   // force a fresh Auto Gain correction
    updateEngine();
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainTargetDb()));
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
    // The buffer holds the main channels first, then the sidechain's (when
    // the host has enabled that bus). Only the main channels are processed.
    const int mainChannels = juce::jmax (getMainBusNumInputChannels(), getMainBusNumOutputChannels());
    const int numChannels = juce::jmin (buffer.getNumChannels(), dryBuffer.getNumChannels(),
                                        mainChannels > 0 ? mainChannels : buffer.getNumChannels(), 2);
    const int numSamples  = buffer.getNumSamples();

    const double* sidechain[2] {};
    int numSidechain = 0;
    if (getBusCount (true) > 1)
        if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        {
            const int first = scBus->getChannelIndexInProcessBlockBuffer (0);
            const int count = juce::jmin (scBus->getNumberOfChannels(), 2);
            for (int k = 0; k < count && first + k < buffer.getNumChannels(); ++k)
                sidechain[numSidechain++] = buffer.getReadPointer (first + k);
        }

    // Look-ahead on/off: restart the delay and tell the host the new latency.
    const bool wantLookahead = lookaheadParam->load() >= 0.5f && lookaheadSamples > 0
                               && delayLines.getNumSamples() >= lookaheadSamples;
    if (wantLookahead != lookaheadActive.load())
    {
        lookaheadActive = wantLookahead;
        delayLines.clear();
        delayPos = 0;
        triggerAsyncUpdate();
    }
    const bool ahead = lookaheadActive.load() && numSamples <= aheadBuffer.getNumSamples();
    if (ahead)
        applyLookaheadDelay (buffer, numChannels);

    inGain.setTargetValue (juce::Decibels::decibelsToGain ((double) inputGainDb->load()));
    wet.setTargetValue (bypassParam->get() ? 0.0 : 1.0);
    updateEngine();
    outGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainTargetDb()));

    // Fully bypassed and settled: leave the audio untouched. The EQ keeps
    // receiving its settings (above) so it comes back in the right state.
    if (! wet.isSmoothing() && wet.getTargetValue() <= 0.0)
    {
        inGain.skip (numSamples);
        outGain.skip (numSamples);
        engineIdle = true;
        pushToAnalyser (preFifo, buffer, numChannels);
        pushToAnalyser (postFifo, buffer, numChannels);
        for (auto& g : dynamicGainDb)
            g.store (0.0f, std::memory_order_relaxed);
        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)   // sidechain / unused
            buffer.clear (ch, 0, numSamples);
        publishMeters (buffer, numChannels);
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
    double* const* aheadData = aheadBuffer.getArrayOfWritePointers();
    for (int i = 0; i < numSamples; ++i)
    {
        const double g = inGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            data[ch][i] *= g;
        if (ahead)
            for (int ch = 0; ch < numChannels; ++ch)
                aheadData[ch][i] *= g;
    }

    peq::FilterEngine::LookaheadInput lookahead;
    if (ahead)
    {
        lookahead.lr[0] = aheadData[0];
        lookahead.lr[1] = numChannels > 1 ? aheadData[1] : aheadData[0];
        if (numChannels > 1)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const double a = aheadData[0][i], b = aheadData[1][i];
                aheadData[2][i] = 0.5 * (a + b);
                aheadData[3][i] = 0.5 * (a - b);
            }
            lookahead.ms[0] = aheadData[2];
            lookahead.ms[1] = aheadData[3];
        }
        else
        {
            lookahead.ms[0] = lookahead.ms[1] = aheadData[0];
        }
    }

    pushToAnalyser (preFifo, buffer, numChannels);

    // 2. The EQ bands (then the solo/audition filter, normally off).
    engine.process (data, numChannels, numSamples, sidechain, numSidechain, ahead ? &lookahead : nullptr);
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

    pushToAnalyser (postFifo, buffer, numChannels);

    for (int b = 0; b < peq::FilterEngine::numBands; ++b)
        dynamicGainDb[(size_t) b].store ((float) engine.dynamicOffsetDb (b), std::memory_order_relaxed);
    for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)   // sidechain / unused
        buffer.clear (ch, 0, numSamples);
    publishMeters (buffer, numChannels);
}

void ParaEQAudioProcessor::applyLookaheadDelay (juce::AudioBuffer<double>& buffer, int numChannels) noexcept
{
    // Keeps the undelayed input in aheadBuffer and replaces the main
    // channels with the same audio lookaheadSamples later.
    const int n = buffer.getNumSamples();
    const int len = lookaheadSamples;
    double* const* data = buffer.getArrayOfWritePointers();
    double* const* line = delayLines.getArrayOfWritePointers();
    double* const* ahead = aheadBuffer.getArrayOfWritePointers();
    const int chans = juce::jmin (numChannels, 2);
    int pos = delayPos;
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < chans; ++ch)
        {
            const double x = data[ch][i];
            ahead[ch][i] = x;
            data[ch][i] = line[ch][pos];
            line[ch][pos] = x;
        }
        if (++pos >= len)
            pos = 0;
    }
    delayPos = pos;
}

void ParaEQAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (getLookaheadLatency());
}

void ParaEQAudioProcessor::publishMeters (const juce::AudioBuffer<double>& buffer, int numChannels) noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        // Mono: show the one channel on both meters.
        const int src = juce::jmin (ch, numChannels - 1);
        if (src < 0)
            return;
        double peak = 0.0;
        const double* d = buffer.getReadPointer (src);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs (d[i]));

        // Keep the highest value until the editor takes it.
        auto& slot = outputPeak[(size_t) ch];
        float prev = slot.load (std::memory_order_relaxed);
        while ((float) peak > prev && ! slot.compare_exchange_weak (prev, (float) peak, std::memory_order_relaxed)) {}
    }
}

void ParaEQAudioProcessor::pushToAnalyser (peq::SampleFifo& fifo, const juce::AudioBuffer<double>& buffer,
                                           int numChannels) noexcept
{
    if (! analyserWanted.load (std::memory_order_relaxed) || numChannels <= 0)
        return;

    const int n = juce::jmin (buffer.getNumSamples(), (int) analyserScratch.size());
    const double scale = 1.0 / (double) numChannels;
    for (int i = 0; i < n; ++i)
    {
        double sum = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
            sum += buffer.getSample (ch, i);
        analyserScratch[(size_t) i] = (float) (sum * scale);
    }
    fifo.push (analyserScratch.data(), (size_t) n);
}

juce::AudioProcessorEditor* ParaEQAudioProcessor::createEditor()
{
    return new ParaEQAudioProcessorEditor (*this);
}

namespace
{
    const juce::Identifier abTag { "ABCompare" };
    const juce::Identifier abActiveId { "active" };
}

void ParaEQAudioProcessor::switchAB()
{
    const auto current = presets.capture();
    beginUndoStep (activeSlot == 0 ? "Switch to B" : "Switch to A");
    if (otherSlot.isValid())
        presets.apply (otherSlot);
    otherSlot = current;
    activeSlot = 1 - activeSlot;
    flushUndo();
}

void ParaEQAudioProcessor::copyActiveToOther()
{
    otherSlot = presets.capture();
}

void ParaEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (stateVersionId, currentStateVersion, nullptr);

    juce::ValueTree ab (abTag);
    ab.setProperty (abActiveId, activeSlot, nullptr);
    if (otherSlot.isValid())
        ab.appendChild (otherSlot.createCopy(), nullptr);
    state.appendChild (ab, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ParaEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Anything that is not our own state (corrupt data, another plugin's
    // chunk) is ignored and the current settings are kept.
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    // Version 1 (Phases 1-5) had no version tag. Parameters added since then
    // are simply missing from old states; replaceState gives them their
    // defaults (dynamics off), so old projects sound exactly as before.
    // A state from a newer ParaEQ loads what this version understands.
    const int version = (int) tree.getProperty (stateVersionId, 1);
    tree.removeProperty (stateVersionId, nullptr);

    // Version 3 added analog-matched filters, on by default. Projects saved
    // before keep the classic design, so they sound exactly as they did.
    if (version < 3)
    {
        auto param = tree.getChildWithProperty ("id", peq::params::analogMatch);
        if (! param.isValid())
        {
            param = juce::ValueTree ("PARAM");
            param.setProperty ("id", peq::params::analogMatch, nullptr);
            tree.appendChild (param, nullptr);
        }
        param.setProperty ("value", 0.0f, nullptr);
    }

    // A/B compare: the inactive slot travels as a child; not part of the
    // parameter tree itself.
    activeSlot = 0;
    otherSlot = {};
    const auto ab = tree.getChildWithName (abTag);
    if (ab.isValid())
    {
        activeSlot = juce::jlimit (0, 1, (int) ab.getProperty (abActiveId, 0));
        if (ab.getNumChildren() > 0)
            otherSlot = ab.getChild (0).createCopy();
        tree.removeChild (ab, nullptr);
    }

    apvts.replaceState (tree);
    undoManager.clearUndoHistory();
    presets.resetModifiedReference();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParaEQAudioProcessor();
}
