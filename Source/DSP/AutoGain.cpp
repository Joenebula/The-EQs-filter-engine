#include "AutoGain.h"

#include <algorithm>
#include <cmath>

namespace peq::autogain
{

double correctionDb (const BandParameters* bands, const ChannelMode* modes, int numBands, double sampleRate) noexcept
{
    if (bands == nullptr || numBands <= 0 || ! (sampleRate > 0.0))
        return 0.0;

    // Per-point power gain on the left and right channels.
    double left[numPoints], right[numPoints], freq[numPoints];
    const double lo = 20.0, hi = std::min (20000.0, 0.499 * sampleRate);
    for (int i = 0; i < numPoints; ++i)
    {
        freq[i] = lo * std::pow (hi / lo, ((double) i + 0.5) / (double) numPoints);
        left[i] = right[i] = 1.0;
    }

    bool any = false;
    CascadeCoefficients c;
    for (int b = 0; b < numBands; ++b)
    {
        const auto& p = bands[b];
        if (! p.enabled)
            continue;
        any = true;
        FilterDesign::design (p, sampleRate, c);
        const ChannelMode mode = modes != nullptr ? modes[b] : ChannelMode::Stereo;
        for (int i = 0; i < numPoints; ++i)
        {
            const double m = c.magnitude (freq[i], sampleRate);
            const double power = m * m;
            switch (mode)
            {
                case ChannelMode::Left:  left[i] *= power; break;
                case ChannelMode::Right: right[i] *= power; break;
                case ChannelMode::Mid:
                case ChannelMode::Side:
                {
                    const double half = 0.5 * (power + 1.0);
                    left[i] *= half; right[i] *= half;
                    break;
                }
                case ChannelMode::Stereo:
                case ChannelMode::NumModes:
                default:
                    left[i] *= power; right[i] *= power;
                    break;
            }
        }
    }
    if (! any)
        return 0.0;

    double sum = 0.0;
    for (int i = 0; i < numPoints; ++i)
        sum += 0.5 * (left[i] + right[i]);
    const double meanPower = sum / (double) numPoints;
    if (! (meanPower > 0.0) || ! std::isfinite (meanPower))
        return 0.0;
    return std::clamp (-10.0 * std::log10 (meanPower), -maxCorrectionDb, maxCorrectionDb);
}

} // namespace peq::autogain
