#pragma once

#include "DSP/FilterEngine.h"
#include "DSP/SampleFifo.h"
#include "PresetManager.h"

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

    /** Saved state: the parameter tree as XML plus a "stateVersion" number
        (absent = 1, Phases 1-5; 2 = Phase 6 onwards, with dynamic EQ). */
    static constexpr int currentStateVersion = 2;
    static constexpr const char* stateVersionId = "stateVersion";

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

    PresetManager& getPresets() noexcept { return presets; }

    /** A/B compare: two complete sets of settings. The host parameters always
        hold the active one; switching stores them and recalls the other. Both
        are saved with the project. Message thread only. */
    int getActiveSlot() const noexcept { return activeSlot; }   // 0 = A, 1 = B
    void switchAB();
    /** Copies the active settings into the other slot (A to B or B to A). */
    void copyActiveToOther();

    /** Band solo ("audition"): plays only the region a band works on, through
        a band-pass at the band's frequency. -1 = no solo. Not automatable and
        not saved; it is a listening aid set from the editor. */
    void setSoloBand (int bandIndex) noexcept { soloBand.store (bandIndex); }
    int getSoloBand() const noexcept { return soloBand.load(); }

    /** Spectrum analyser feeds: mono (L+R)/2 audio before the EQ (after input
        gain) and after the output gain. Filled only while the editor asks. */
    peq::SampleFifo& getPreFifo() noexcept  { return preFifo; }
    peq::SampleFifo& getPostFifo() noexcept { return postFifo; }
    void setAnalyserWanted (bool wanted) noexcept { analyserWanted.store (wanted); }

    /** Gain change (dB) each band's dynamic EQ is applying now, and the
        output peak level per channel (linear, held until read). For the
        editor's activity display and meters; written by the audio thread. */
    float getDynamicGainDb (int band) const noexcept
    {
        return (band >= 0 && band < peq::FilterEngine::numBands) ? dynamicGainDb[(size_t) band].load (std::memory_order_relaxed) : 0.0f;
    }
    /** Returns the peak since the last call and resets it (message thread). */
    float takeOutputPeak (int channel) noexcept
    {
        return (channel >= 0 && channel < 2) ? outputPeak[(size_t) channel].exchange (0.0f) : 0.0f;
    }

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
        std::atomic<float>* dyn = nullptr;
        std::atomic<float>* thr = nullptr;
        std::atomic<float>* range = nullptr;
        std::atomic<float>* att = nullptr;
        std::atomic<float>* rel = nullptr;
    };

    void updateSolo() noexcept;

    juce::UndoManager undoManager { 30000, 100 };
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts, &undoManager };
    int activeSlot = 0;
    juce::ValueTree otherSlot;   // the inactive A/B settings ("Params" tree); invalid = same as active
    std::atomic<float>* inputGainDb  = nullptr;
    std::atomic<float>* outputGainDb = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;
    std::array<BandParams, peq::FilterEngine::numBands> bandParams {};

    peq::FilterEngine engine;
    bool engineIdle = false;   // true while fully bypassed (EQ not running)

    void pushToAnalyser (peq::SampleFifo& fifo, const juce::AudioBuffer<double>& buffer, int numChannels) noexcept;

    peq::SampleFifo preFifo, postFifo;
    std::vector<float> analyserScratch;
    std::atomic<bool> analyserWanted { false };

    std::array<std::atomic<float>, peq::FilterEngine::numBands> dynamicGainDb {};
    std::array<std::atomic<float>, 2> outputPeak {};
    void publishMeters (const juce::AudioBuffer<double>& buffer, int numChannels) noexcept;

    std::atomic<int> soloBand { -1 };
    peq::EQBand soloFilter;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.0 }, outGain { 1.0 };
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> wet { 1.0 };

    juce::AudioBuffer<double> doubleBuffer;   // preallocated for the float path
    juce::AudioBuffer<double> dryBuffer;      // preallocated for click-free bypass

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQAudioProcessor)
};
