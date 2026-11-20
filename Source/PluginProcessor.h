#pragma once

#include "DSP/FilterEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

/** The ParaEQ processor.

    Stereo/mono VST3 effect: input gain -> 24-band EQ -> output gain, with a
    click-free global bypass. Every control is a host-automatable parameter,
    saved with the project. Processing is double precision throughout.
*/
class ParaEQAudioProcessor final : public juce::AudioProcessor
{
public:
    ParaEQAudioProcessor();
    ~ParaEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    /** Undo bookkeeping for editor actions. JUCE copies parameter changes into
        the undoable state tree lazily (on a timer), so an action could end up
        in the wrong undo step. These flush pending changes first, so every
        edit lands in the step it belongs to. Message thread only. */
    void flushUndo()                            { apvts.copyState(); }
    void beginUndoStep (const juce::String& name) { flushUndo(); undoManager.beginNewTransaction (name); }

    /** Band solo ("audition"): plays only the region a band works on, through
        a band-pass at the band's frequency. -1 = no solo. Not automatable and
        not saved; it is a listening aid set from the editor. */
    void setSoloBand (int bandIndex) noexcept { soloBand.store (bandIndex); }
    int getSoloBand() const noexcept { return soloBand.load(); }

    /** Current sample rate, or 48 kHz before the host has prepared us (used by
        the editor to draw response curves). */
    double getCurrentSampleRateOrDefault() const noexcept
    {
        const double sr = getSampleRate();
        return sr > 0.0 ? sr : 48000.0;
    }

private:
    /** Shared double-precision path. */
    void processDouble (juce::AudioBuffer<double>& buffer);

    /** Reads the band parameters and hands them to the engine. */
    void updateEngine() noexcept;

    struct BandParams
    {
        std::atomic<float>* used = nullptr;
        std::atomic<float>* on = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* slope = nullptr;
        std::atomic<float>* mode = nullptr;
    };

    void updateSolo() noexcept;

    juce::UndoManager undoManager { 30000, 100 };
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* inputGainDb  = nullptr;
    std::atomic<float>* outputGainDb = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;
    std::array<BandParams, peq::FilterEngine::numBands> bandParams {};

    peq::FilterEngine engine;
    bool engineIdle = false;   // true while fully bypassed (EQ not running)

    std::atomic<int> soloBand { -1 };
    peq::EQBand soloFilter;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.0 }, outGain { 1.0 };
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> wet { 1.0 };

    juce::AudioBuffer<double> doubleBuffer;   // preallocated for the float path
    juce::AudioBuffer<double> dryBuffer;      // preallocated for click-free bypass

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQAudioProcessor)
};
