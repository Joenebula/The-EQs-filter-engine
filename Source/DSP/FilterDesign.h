#pragma once

#include "Biquad.h"

#include <array>

namespace peq
{

/** The filter shapes an EQ band can take. The numeric values are stored in
    plugin state, so only append new types at the end. */
enum class FilterType : int
{
    Bell = 0,
    LowShelf,
    HighShelf,
    LowPass,
    HighPass,
    Notch,
    BandPass,
    NumTypes
};

/** Parameter limits shared by the DSP, the plugin parameters and the GUI. */
namespace limits
{
    constexpr double minFrequency = 10.0;
    constexpr double maxFrequency = 30000.0;
    constexpr double minGainDb    = -30.0;
    constexpr double maxGainDb    = 30.0;
    constexpr double minQ         = 0.025;
    constexpr double maxQ         = 40.0;
    constexpr double defaultQ     = 0.70710678118654752; // 1/sqrt(2)

    /** Frequencies are clamped to this fraction of the sample rate so the
        bilinear transform never reaches Nyquist. At 44.1 kHz this caps a band at
        21.6 kHz; the full 30 kHz range is available from 88.2 kHz upwards. */
    constexpr double maxFrequencyFractionOfSampleRate = 0.49;

    /** Supported slopes in dB/octave. Index into this array is what gets stored. */
    constexpr std::array<int, 8> slopesDbPerOct { 6, 12, 18, 24, 36, 48, 72, 96 };
}

/** Plain description of one band. No JUCE types, so the DSP stays GUI- and
    framework-independent. */
struct BandParameters
{
    FilterType type     = FilterType::Bell;
    double frequencyHz  = 1000.0;
    double gainDb       = 0.0;
    double q            = limits::defaultQ;
    int slopeDbPerOct   = 12;
    bool enabled        = true;
};

/** A cascade of up to 8 biquads (enough for a 96 dB/oct Butterworth). */
struct CascadeCoefficients
{
    static constexpr int maxSections = 8;

    std::array<BiquadCoefficients, maxSections> sections {};
    int numSections = 0;

    /** Complex response of the whole cascade at frequency f. */
    std::complex<double> response (double frequencyHz, double sampleRate) const noexcept;

    /** Linear magnitude at frequency f. */
    double magnitude (double frequencyHz, double sampleRate) const noexcept;

    /** Magnitude in dB at frequency f (floored at -300 dB). */
    double magnitudeDb (double frequencyHz, double sampleRate) const noexcept;
};

namespace FilterDesign
{
    /** Number of biquad sections a given band will use. Depends only on type
        and slope; a change of this value is a "structural" change. */
    int numSectionsFor (FilterType type, int slopeDbPerOct) noexcept;

    /** Snaps an arbitrary slope to the nearest supported slope. Shelves only
        support 6 and 12 dB/oct; bell and notch ignore slope. */
    int sanitiseSlope (FilterType type, int slopeDbPerOct) noexcept;

    /** Computes the coefficients for a band. Out-of-range values are clamped.
        Always produces a stable cascade. */
    void design (const BandParameters& params, double sampleRate, CascadeCoefficients& out) noexcept;

    /** Butterworth section Q values for an order-N low/high-pass, as used by
        design(). Exposed for tests. Returns the number of 2nd-order sections and
        writes their Qs into qs; hasFirstOrder is set for odd N. */
    int butterworthQs (int order, std::array<double, CascadeCoefficients::maxSections>& qs, bool& hasFirstOrder) noexcept;

    // Individual section designs (bilinear transform, pre-warped at fc).
    BiquadCoefficients bell          (double fc, double fs, double gainDb, double q) noexcept;
    BiquadCoefficients lowShelf2     (double fc, double fs, double gainDb, double q) noexcept;
    BiquadCoefficients highShelf2    (double fc, double fs, double gainDb, double q) noexcept;
    BiquadCoefficients lowShelf1     (double fc, double fs, double gainDb) noexcept;
    BiquadCoefficients highShelf1    (double fc, double fs, double gainDb) noexcept;
    BiquadCoefficients lowPass2      (double fc, double fs, double q) noexcept;
    BiquadCoefficients highPass2     (double fc, double fs, double q) noexcept;
    BiquadCoefficients lowPass1      (double fc, double fs) noexcept;
    BiquadCoefficients highPass1     (double fc, double fs) noexcept;
    BiquadCoefficients notch         (double fc, double fs, double q) noexcept;
    BiquadCoefficients bandPass      (double fc, double fs, double q) noexcept;
}

} // namespace peq
