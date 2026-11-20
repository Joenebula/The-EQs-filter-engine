#pragma once

#include "DSP/FilterEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** Host-automatable parameter IDs and layout.

    Parameter IDs are saved in Cubase projects and presets, so never rename an
    ID once released; add new ones instead.

    Per band (n = 01..24): bNN_on, bNN_type, bNN_freq, bNN_gain, bNN_q,
    bNN_slope, bNN_mode. Choice parameters store an index, so only append new
    choices at the end of each list.
*/
namespace peq::params
{
    inline constexpr const char* inputGain  = "input_gain";
    inline constexpr const char* outputGain = "output_gain";
    inline constexpr const char* bypass     = "bypass";

    inline constexpr int numBands = FilterEngine::numBands;

    enum class BandField { on, type, freq, gain, q, slope, mode };

    /** e.g. bandID (0, BandField::freq) == "b01_freq". */
    juce::String bandID (int bandIndex, BandField field);

    /** Default frequency of each band (log-spaced, so new bands spread out). */
    float defaultFrequency (int bandIndex);

    /** Version hint for every parameter, required by JUCE for VST3 hosts. */
    inline constexpr int version = 1;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
