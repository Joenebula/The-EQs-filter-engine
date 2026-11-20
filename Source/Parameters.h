#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Host-automatable parameter IDs and layout.

    Parameter IDs are saved in Cubase projects and presets, so never rename an
    ID once released; add new ones instead. Per-band parameters arrive in
    Phase 3.
*/
namespace peq::params
{
    inline constexpr const char* inputGain  = "input_gain";
    inline constexpr const char* outputGain = "output_gain";
    inline constexpr const char* bypass     = "bypass";

    /** Version hint for every parameter, required by JUCE for VST3 hosts. */
    inline constexpr int version = 1;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
