#pragma once

#include <algorithm>
#include <cmath>

/** Pure maths for the EQ graph: mapping between screen positions and
    frequency / dB. No JUCE, so it is unit-tested with the DSP tests. */
namespace peq::graph
{
    constexpr double minDisplayHz = 10.0;
    constexpr double maxDisplayHz = 30000.0;

    /** x in [0, width] <-> frequency, logarithmic. */
    inline double freqToX (double hz, double width) noexcept
    {
        hz = std::clamp (hz, minDisplayHz, maxDisplayHz);
        return width * std::log (hz / minDisplayHz) / std::log (maxDisplayHz / minDisplayHz);
    }

    inline double xToFreq (double x, double width) noexcept
    {
        const double t = width > 0.0 ? std::clamp (x / width, 0.0, 1.0) : 0.0;
        return minDisplayHz * std::pow (maxDisplayHz / minDisplayHz, t);
    }

    /** y in [0, height] <-> dB in [-rangeDb, +rangeDb], 0 dB in the middle. */
    inline double dbToY (double db, double height, double rangeDb) noexcept
    {
        return height * 0.5 * (1.0 - db / rangeDb);
    }

    inline double yToDb (double y, double height, double rangeDb) noexcept
    {
        return height > 0.0 ? rangeDb * (1.0 - 2.0 * y / height) : 0.0;
    }

    /** New Q after `wheelSteps` mouse-wheel notches (positive = narrower). Each
        notch multiplies Q by a fixed ratio, so it feels the same at any Q. */
    inline double qAfterWheel (double q, double wheelSteps, double minQ, double maxQ) noexcept
    {
        return std::clamp (q * std::pow (1.25, wheelSteps), minQ, maxQ);
    }
}
