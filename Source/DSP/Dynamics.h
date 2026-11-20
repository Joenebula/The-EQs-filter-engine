#pragma once

#include "FilterDesign.h"

#include <algorithm>
#include <cmath>

namespace peq
{

/** Per-band dynamic EQ settings.

    A dynamic band listens to its own frequency region (through a detector
    filter, see dynamics::detectorFilter) and moves its gain away from the set
    gain while that region is louder than the threshold:

        offset = clamp (slope * softKnee (level - threshold), 0, |range|) * sign (range)

    A negative range turns the band down when the region gets loud (like a
    compressor for that region, e.g. de-essing); a positive range turns it up
    (upward expansion, e.g. bringing out a kick drum's attack).
*/
struct DynamicSettings
{
    bool enabled        = false;
    double thresholdDb  = -24.0;   // dBFS of the detected region (a sine reads its peak level)
    double rangeDb      = -6.0;    // most the gain may move, + or -
    double attackMs     = 10.0;
    double releaseMs    = 150.0;
};

namespace dynamics
{
    constexpr double minThresholdDb = -60.0, maxThresholdDb = 0.0;
    constexpr double minRangeDb = -30.0,     maxRangeDb = 30.0;
    constexpr double minAttackMs = 0.1,      maxAttackMs = 200.0;
    constexpr double minReleaseMs = 5.0,     maxReleaseMs = 2000.0;

    /** Gain change per dB over the threshold (0.5 = 1 dB per 2 dB, like 2:1). */
    constexpr double slope = 0.5;
    /** Soft knee width around the threshold, in dB. */
    constexpr double kneeDb = 6.0;

    /** Only types with a gain control can be dynamic. */
    inline bool typeSupportsDynamics (FilterType t) noexcept
    {
        return t == FilterType::Bell || t == FilterType::LowShelf || t == FilterType::HighShelf;
    }

    inline DynamicSettings sanitise (DynamicSettings s) noexcept
    {
        s.thresholdDb = std::clamp (s.thresholdDb, minThresholdDb, maxThresholdDb);
        s.rangeDb     = std::clamp (s.rangeDb, minRangeDb, maxRangeDb);
        s.attackMs    = std::clamp (s.attackMs, minAttackMs, maxAttackMs);
        s.releaseMs   = std::clamp (s.releaseMs, minReleaseMs, maxReleaseMs);
        return s;
    }

    /** dB over the threshold after a soft knee (0 well below, linear above). */
    inline double softOver (double overDb) noexcept
    {
        constexpr double half = kneeDb * 0.5;
        if (overDb <= -half) return 0.0;
        if (overDb >= half)  return overDb;
        const double x = overDb + half;
        return x * x / (2.0 * kneeDb);
    }

    /** The gain offset (dB) for a detected level (dBFS). */
    inline double gainOffset (double levelDb, const DynamicSettings& s) noexcept
    {
        const double amount = std::min (std::abs (s.rangeDb), slope * softOver (levelDb - s.thresholdDb));
        return s.rangeDb < 0.0 ? -amount : amount;
    }

    /** The detector ("sidechain") filter for a band: a band-pass with 0 dB
        peak at the band's frequency and Q for a bell, a low-pass for a low
        shelf, a high-pass for a high shelf (both Butterworth, at the band's
        frequency). */
    inline BiquadCoefficients detectorFilter (FilterType type, double frequencyHz, double q, double sampleRate) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        const double f = std::clamp (frequencyHz, limits::minFrequency, limits::maxFrequencyFractionOfSampleRate * sampleRate);
        const double w0 = 2.0 * pi * f / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double qq = type == FilterType::Bell ? std::clamp (q, limits::minQ, limits::maxQ) : limits::defaultQ;
        const double alpha = sw / (2.0 * qq);
        const double a0 = 1.0 + alpha;

        BiquadCoefficients c;
        switch (type)
        {
            case FilterType::LowShelf:   // low-pass
                c.b0 = (1.0 - cw) * 0.5 / a0; c.b1 = (1.0 - cw) / a0; c.b2 = c.b0;
                break;
            case FilterType::HighShelf:  // high-pass
                c.b0 = (1.0 + cw) * 0.5 / a0; c.b1 = -(1.0 + cw) / a0; c.b2 = c.b0;
                break;
            case FilterType::Bell:
            case FilterType::LowPass:
            case FilterType::HighPass:
            case FilterType::Notch:
            case FilterType::BandPass:
            case FilterType::NumTypes:
            default:                     // band-pass, 0 dB peak
                c.b0 = alpha / a0; c.b1 = 0.0; c.b2 = -alpha / a0;
                break;
        }
        c.a1 = -2.0 * cw / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }
}

} // namespace peq
