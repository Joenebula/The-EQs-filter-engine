#pragma once

#include "DSP/FilterEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** Host-automatable parameter IDs and layout.

    Parameter IDs are saved in Cubase projects and presets, so never rename an
    ID once released; add new ones instead.

    Per band (n = 01..24): bNN_used, bNN_on, bNN_type, bNN_freq, bNN_gain,
    bNN_q, bNN_slope, bNN_mode; dynamic EQ (Phase 6): bNN_dyn, bNN_thr,
    bNN_range, bNN_att, bNN_rel; added in 1.3: bNN_ratio, bNN_sc (detector
    listens to the sidechain input). Their defaults (2:1, own signal) are how
    earlier versions behaved, so older projects and presets sound the same.
    "used" means the band exists (shown in the
    graph); "on" is its enable/bypass switch. A band is heard only when both
    are set. Choice parameters store an index, so only append new
    choices at the end of each list.
*/
namespace peq::params
{
    inline constexpr const char* inputGain  = "input_gain";
    inline constexpr const char* outputGain = "output_gain";
    inline constexpr const char* bypass     = "bypass";
    /** Analog-matched filter designs (no high-frequency cramping). Added in
        1.1; projects saved before it load with it off so they sound the same. */
    inline constexpr const char* analogMatch = "analog_match";
    /** Scales every band's gain together, 0-200 % (100 % = as set). Added
        in 1.2; missing from older states and presets, so they get 100 %. */
    inline constexpr const char* gainScale   = "gain_scale";
    /** Auto Gain: output correction that keeps the level steady while you
        EQ (see DSP/AutoGain.h). Added in 1.4, default off. */
    inline constexpr const char* autoGain    = "auto_gain";

    inline constexpr int numBands = FilterEngine::numBands;

    enum class BandField { on, type, freq, gain, q, slope, mode, used, dyn, thr, range, att, rel, ratio, sc };

    /** e.g. bandID (0, BandField::freq) == "b01_freq". */
    juce::String bandID (int bandIndex, BandField field);

    /** Default frequency of each band (log-spaced, so new bands spread out). */
    float defaultFrequency (int bandIndex);

    /** Version hint for every parameter, required by JUCE for VST3 hosts. */
    inline constexpr int version = 1;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
