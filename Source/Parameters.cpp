#include "Parameters.h"

namespace peq::params
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dbAttributes = AudioParameterFloatAttributes()
                            .withLabel ("dB")
                            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { inputGain, version }, "Input Gain",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                                       dbAttributes));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { outputGain, version }, "Output Gain",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                                       dbAttributes));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));

    return layout;
}

} // namespace peq::params
