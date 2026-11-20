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

private:
    /** Shared double-precision path. */
    void processDouble (juce::AudioBuffer<double>& buffer);

    /** Reads the band parameters and hands them to the engine. */
    void updateEngine() noexcept;

    struct BandParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* slope = nullptr;
        std::atomic<float>* mode = nullptr;
    };

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* inputGainDb  = nullptr;
    std::atomic<float>* outputGainDb = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;
    std::array<BandParams, peq::FilterEngine::numBands> bandParams {};

    peq::FilterEngine engine;
    bool engineIdle = false;   // true while fully bypassed (EQ not running)

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.0 }, outGain { 1.0 };
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> wet { 1.0 };

    juce::AudioBuffer<double> doubleBuffer;   // preallocated for the float path
    juce::AudioBuffer<double> dryBuffer;      // preallocated for click-free bypass

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQAudioProcessor)
};
