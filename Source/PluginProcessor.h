#pragma once

#include "DSP/FilterEngine.h"
#include "DSP/LinearPhase.h"
#include "DSP/EQMatch.h"
#include "DSP/SampleFifo.h"
#include "PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

/** The ParaEQ processor.

    Stereo/mono VST3 effect: input gain -> 24-band EQ -> output gain, with a
    click-free global bypass. Every control is a host-automatable parameter,
    saved with the project. Processing is double precision throughout.
*/
class ParaEQAudioProcessor final : public juce::AudioProcessor,
                                   private juce::AsyncUpdater,
                                   private juce::Timer
{
public:
    ParaEQAudioProcessor();
    ~ParaEQAudioProcessor() override { stopTimer(); cancelPendingUpdate(); }

    /** Total latency right now: look-ahead plus linear phase (samples). */
    int getCurrentLatency() const noexcept
    {
        return getLookaheadLatency() + (linearActive.load() ? fir.latencySamples() : 0);
    }
    bool isLinearPhaseActive() const noexcept { return linearActive.load(); }
    /** Designs and installs the linear-phase kernel now if the curve changed
        (message thread; normally done by a timer, called by tests). Returns
        true if a new kernel was handed to the convolver. */
    bool updateLinearPhaseKernel();

    /** Latency added by look-ahead right now (samples; 0 when off). */
    int getLookaheadLatency() const noexcept { return lookaheadActive.load() ? lookaheadSamples : 0; }
    /** Reports a pending latency change to the host now instead of on the
        next message loop pass (message thread; used by tests). */
    void flushLatencyUpdate() { handleUpdateNowIfNeeded(); }

    /** EQ Match (1.6). While learning, the audio thread sends this track's
        input and the sidechain (the reference) to the matcher, which a
        message-thread timer feeds. Message thread only. */
    void startMatchLearning();
    void stopMatchLearning() noexcept { matchLearning.store (false); }
    bool isMatchLearning() const noexcept { return matchLearning.load(); }
    /** Moves the audio collected so far into the matcher (normally the timer;
        called by tests). */
    void pumpMatch();
    const peq::EQMatch& getMatch() const noexcept { return match; }
    /** Fits up to maxBands bands to `amount` (0..1) of what was learned and
        writes them into unused bands, as one undo step. Returns how many
        bands were added (0 if nothing was learned or no band is free). */
    int applyMatch (int maxBands, double amount);
    /** True when the host has switched the sidechain input on. */
    bool isSidechainActive() const;

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
    static constexpr int currentStateVersion = 3;
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

    /** Delta listen (1.7): play only what the EQ changes (its output minus
        its input), to hear exactly what a cut removes or a boost adds. A
        listening aid set from the editor: not automatable, not saved. */
    void setDeltaListen (bool on) noexcept { deltaListen.store (on); }
    bool getDeltaListen() const noexcept { return deltaListen.load(); }
    int getSoloBand() const noexcept { return soloBand.load(); }

    /** Spectrum analyser feeds: mono audio before the EQ (after input gain)
        and after the output gain: (L+R)/2 by default, or L, R or (L-R)/2
        (see setAnalyserChannel). Filled only while the editor asks. */
    peq::SampleFifo& getPreFifo() noexcept  { return preFifo; }
    peq::SampleFifo& getPostFifo() noexcept { return postFifo; }
    /** The side-chain input, mono, while the analyser is wanted (1.8). */
    peq::SampleFifo& getSidechainFifo() noexcept { return sidechainFifo; }
    void setAnalyserWanted (bool wanted) noexcept { analyserWanted.store (wanted); }
    /** Which part of the stereo signal the pre/post feeds carry (2.1).
        Mono input feeds its one channel for all but Side (silence). */
    enum class AnalyserChannel { stereo = 0, left, right, side };
    void setAnalyserChannel (AnalyserChannel c) noexcept { analyserChannel.store ((int) c); }
    AnalyserChannel getAnalyserChannel() const noexcept { return (AnalyserChannel) analyserChannel.load(); }

    /** Gain change (dB) each band's dynamic EQ is applying now, and the
        output peak level per channel (linear, held until read). For the
        editor's activity display and meters; written by the audio thread. */
    float getDynamicGainDb (int band) const noexcept
    {
        return (band >= 0 && band < peq::FilterEngine::numBands) ? dynamicGainDb[(size_t) band].load (std::memory_order_relaxed) : 0.0f;
    }
    /** The Auto Gain correction (dB) for the current curve, whether or not
        Auto Gain is switched on (for the editor's display). */
    float getAutoGainCorrectionDb() const noexcept { return autoGainDb.load (std::memory_order_relaxed); }

    /** Returns the peak since the last call and resets it (message thread). */
    /** Learn threshold (2.4): the highest detector level (dBFS) band
        `band` reached since the last call, or -200 if none. */
    float takeDetectorPeakDb (int band) noexcept
    {
        return (band >= 0 && band < peq::FilterEngine::numBands) ? detectorPeakDb[(size_t) band].exchange (-200.0f) : -200.0f;
    }

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
        std::atomic<float>* ratio = nullptr;
        std::atomic<float>* sc = nullptr;
    };

    void updateSolo() noexcept;
    /** Output gain plus the Auto Gain correction when it is on (dB). */
    double outputGainTargetDb() const noexcept;

    juce::UndoManager undoManager { 30000, 100 };
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts, &undoManager };
    int activeSlot = 0;
    juce::ValueTree otherSlot;   // the inactive A/B settings ("Params" tree); invalid = same as active
    std::atomic<float>* inputGainDb  = nullptr;
    std::atomic<float>* outputGainDb = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;
    std::atomic<float>* analogMatch = nullptr;
    std::atomic<float>* gainScale = nullptr;
    std::atomic<float>* autoGainParam = nullptr;
    std::atomic<float> autoGainDb { 0.0f };
    // Settings the Auto Gain correction was last worked out for.
    std::array<peq::BandParameters, peq::FilterEngine::numBands> autoGainBands {};
    std::array<peq::ChannelMode, peq::FilterEngine::numBands> autoGainModes {};
    double autoGainSampleRate = 0.0;
    std::array<BandParams, peq::FilterEngine::numBands> bandParams {};

    peq::FilterEngine engine;
    bool engineIdle = false;   // true while fully bypassed (EQ not running)

    void pushToAnalyser (peq::SampleFifo& fifo, const juce::AudioBuffer<double>& buffer, int numChannels) noexcept;

    peq::SampleFifo preFifo, postFifo, sidechainFifo;
    std::vector<float> analyserScratch;
    std::atomic<bool> analyserWanted { false };
    std::atomic<int> analyserChannel { 0 };

    std::array<std::atomic<float>, peq::FilterEngine::numBands> dynamicGainDb {};
    std::array<std::atomic<float>, 2> outputPeak {};
    std::array<std::atomic<float>, peq::FilterEngine::numBands> detectorPeakDb {};
    void publishMeters (const juce::AudioBuffer<double>& buffer, int numChannels) noexcept;

    std::atomic<int> soloBand { -1 };
    peq::EQBand soloFilter;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.0 }, outGain { 1.0 };
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> wet { 1.0 };
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> deltaMix { 0.0 };
    std::atomic<bool> deltaListen { false };
    std::vector<double> inGainTrace;           // per-sample input gain (for delta listen)

    juce::AudioBuffer<double> doubleBuffer;   // preallocated for the float path
    juce::AudioBuffer<double> dryBuffer;      // preallocated for click-free bypass

    // Look-ahead (1.4): the main channels run through a fixed delay while the
    // dynamic detectors hear the undelayed input.
    void handleAsyncUpdate() override;        // reports the new latency (message thread)
    void timerCallback() override
    {
        updateLinearPhaseKernel();
        pumpMatch();
    }

    peq::EQMatch match;
    peq::SampleFifo matchSourceFifo, matchReferenceFifo;
    std::vector<float> matchScratch, matchDrain;
    std::atomic<bool> matchLearning { false };
    double matchRate = 0.0;
    void pushToMatch (const juce::AudioBuffer<double>& buffer, int numChannels,
                      const double* const* sidechain, int numSidechain) noexcept;
    void delayDry (int numChannels, int numSamples) noexcept;

    // Linear phase (1.5): bands that are Stereo, static (not dynamic) and on
    // go into one linear-phase FIR after the IIR engine; the rest stay IIR.
    bool isLinearBand (int band) const noexcept;
    peq::BandParameters bandParametersFor (int band) const noexcept;
    std::atomic<float>* linearParam = nullptr;
    std::atomic<bool> linearActive { false };
    std::atomic<bool> kernelReady { false };  // a kernel for the current prepare was installed
    peq::LinearPhaseFIR fir;
    std::vector<double> kernel;               // message thread scratch
    std::array<peq::BandParameters, peq::FilterEngine::numBands> kernelBands {};
    bool kernelDesigned = false;
    double preparedRate = 0.0;
    juce::AudioBuffer<double> dryDelay;       // dry path delayed like the FIR
    int dryDelayPos = 0;
    void applyLookaheadDelay (juce::AudioBuffer<double>& buffer, int numChannels) noexcept;
    std::atomic<float>* lookaheadParam = nullptr;
    std::atomic<bool> lookaheadActive { false };
    int lookaheadSamples = 0;
    int delayPos = 0;
    juce::AudioBuffer<double> delayLines;     // 2 x lookaheadSamples
    juce::AudioBuffer<double> aheadBuffer;    // undelayed input: L, R, then mid, side

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQAudioProcessor)
};
