#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Phase 1 scaffold processor.

    What it does now: stereo/mono VST3 effect with input gain, output gain and
    bypass (all automatable, smoothed, saved with the project), processing
    internally in double precision. The EQ bands are wired in Phase 3.
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

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* inputGainDb  = nullptr;
    std::atomic<float>* outputGainDb = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.0 }, outGain { 1.0 };
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> wet { 1.0 };

    juce::AudioBuffer<double> doubleBuffer;   // preallocated for the float path
    juce::AudioBuffer<double> dryBuffer;      // preallocated for click-free bypass

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQAudioProcessor)
};
