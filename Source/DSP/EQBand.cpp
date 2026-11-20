#include "EQBand.h"

#include <algorithm>
#include <cmath>

namespace peq
{

namespace
{
    constexpr double logFreqEpsilon = 1.0e-6;  // ~0.0001 % frequency
    constexpr double gainEpsilon    = 1.0e-5;  // dB
    constexpr double logQEpsilon    = 1.0e-6;
}

bool EQBand::Smoothed::settled() const noexcept
{
    return current == target;
}

//==============================================================================
void EQBand::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    mixStep = 1.0 / std::max (1.0, fadeTime * sampleRate);
    prepared = true;
    setTarget (requested);   // re-clamp the user's settings for the new rate
    snapToTarget();
}

void EQBand::setSmoothingTimes (double parameterSeconds, double fadeSeconds) noexcept
{
    parameterTime = std::max (0.0, parameterSeconds);
    fadeTime = std::max (0.0, fadeSeconds);
    mixStep = 1.0 / std::max (1.0, fadeTime * sampleRate);
}

void EQBand::reset() noexcept
{
    for (auto& ch : state)
        for (auto& s : ch)
            s.reset();
}

double EQBand::desiredMix() const noexcept
{
    return target.enabled ? 1.0 : 0.0;
}

void EQBand::setTarget (const BandParameters& newTarget) noexcept
{
    requested = newTarget;
    target = newTarget;
    target.slopeDbPerOct = FilterDesign::sanitiseSlope (target.type, target.slopeDbPerOct);

    const double upper = std::min (limits::maxFrequency, limits::maxFrequencyFractionOfSampleRate * sampleRate);
    target.frequencyHz = std::clamp (target.frequencyHz, limits::minFrequency, upper);
    target.gainDb      = std::clamp (target.gainDb, limits::minGainDb, limits::maxGainDb);
    target.q           = std::clamp (target.q, limits::minQ, limits::maxQ);

    logFreq.target = std::log (target.frequencyHz);
    gainDb.target  = target.gainDb;
    logQ.target    = std::log (target.q);

    const bool structural = target.type != activeType || target.slopeDbPerOct != activeSlope;

    if (structural)
    {
        if (mix <= 0.0)
        {
            applyStructure();
            mixTarget = desiredMix();
        }
        else
        {
            structurePending = true;
            mixTarget = 0.0;
        }
    }
    else if (! structurePending)
    {
        mixTarget = desiredMix();
    }
}

void EQBand::snapToTarget() noexcept
{
    activeType  = target.type;
    activeSlope = target.slopeDbPerOct;
    structurePending = false;

    logFreq.current = logFreq.target;
    gainDb.current  = gainDb.target;
    logQ.current    = logQ.target;
    mix = mixTarget = desiredMix();

    reset();
    updateCoefficients();
}

void EQBand::applyStructure() noexcept
{
    activeType  = target.type;
    activeSlope = target.slopeDbPerOct;
    structurePending = false;
    reset();
    updateCoefficients();
}

void EQBand::updateCoefficients() noexcept
{
    FilterDesign::design (currentParameters(), sampleRate, coeffs);
}

BandParameters EQBand::currentParameters() const noexcept
{
    BandParameters p;
    p.type          = activeType;
    p.slopeDbPerOct = activeSlope;
    p.frequencyHz   = std::exp (logFreq.current);
    p.gainDb        = gainDb.current;
    p.q             = std::exp (logQ.current);
    p.enabled       = target.enabled;
    return p;
}

bool EQBand::isSmoothing() const noexcept
{
    return ! logFreq.settled() || ! gainDb.settled() || ! logQ.settled()
        || mix != mixTarget || structurePending;
}

bool EQBand::advanceParameters (int numSamples) noexcept
{
    if (parameterTime <= 0.0)
    {
        // Smoothing disabled (used by tests): jump without ramping.
        const bool changed = ! logFreq.settled() || ! gainDb.settled() || ! logQ.settled();
        logFreq.current = logFreq.target;
        gainDb.current  = gainDb.target;
        logQ.current    = logQ.target;
        if (changed)
            updateCoefficients();
        return false;
    }

    const double a = 1.0 - std::exp (-(double) numSamples / (parameterTime * sampleRate));

    bool changed = false;

    auto step = [a, &changed] (Smoothed& s, double eps) noexcept
    {
        if (s.settled())
            return;
        s.current += (s.target - s.current) * a;
        if (std::abs (s.target - s.current) < eps)
            s.current = s.target;
        changed = true;
    };

    step (logFreq, logFreqEpsilon);
    step (gainDb, gainEpsilon);
    step (logQ, logQEpsilon);

    if (changed)
        updateCoefficients();
    return changed;
}

//==============================================================================
void EQBand::process (double* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::clamp (numChannels, 0, maxChannels);
    processBlockSegment (channels, 0, numChannels, 0, numSamples);
}

void EQBand::processChannel (double* samples, int channelIndex, int numSamples) noexcept
{
    if (channelIndex < 0 || channelIndex >= maxChannels)
        return;
    double* chans[1] = { samples };
    processBlockSegment (chans, channelIndex, 1, 0, numSamples);
}

void EQBand::processBlockSegment (double* const* channels, int firstChannel, int numChannels,
                                  int start, int length) noexcept
{
    int pos = start;
    const int end = start + length;

    while (pos < end)
    {
        // Fully off: nothing to do. Keep parameters current so turning the band
        // back on starts from the right settings rather than gliding from stale ones.
        if (mix <= 0.0 && mixTarget <= 0.0 && ! structurePending)
        {
            if (! logFreq.settled() || ! gainDb.settled() || ! logQ.settled())
            {
                logFreq.current = logFreq.target;
                gainDb.current  = gainDb.target;
                logQ.current    = logQ.target;
                updateCoefficients();
            }
            reset();
            return;
        }

        // A pending type/slope change is applied once the fade-out has finished.
        if (structurePending && mix <= 0.0)
        {
            applyStructure();
            mixTarget = desiredMix();
            continue;
        }

        const int len = std::min (end - pos, coefficientUpdateInterval);

        // While parameters glide, design the filter for where the parameters
        // will be at the end of this segment and ramp every coefficient
        // linearly towards it, sample by sample. This avoids the zipper noise
        // a stepped coefficient update would cause, at the cost of only one
        // filter design per segment.
        const CascadeCoefficients startCoeffs = coeffs;
        const bool ramping = advanceParameters (len);
        const int numSections = coeffs.numSections;

        std::array<BiquadCoefficients, CascadeCoefficients::maxSections> delta {};
        if (ramping)
        {
            const double inv = 1.0 / (double) len;
            for (int s = 0; s < numSections; ++s)
            {
                const auto& a = startCoeffs.sections[(size_t) s];
                const auto& b = coeffs.sections[(size_t) s];
                auto& d = delta[(size_t) s];
                d.b0 = (b.b0 - a.b0) * inv;
                d.b1 = (b.b1 - a.b1) * inv;
                d.b2 = (b.b2 - a.b2) * inv;
                d.a1 = (b.a1 - a.a1) * inv;
                d.a2 = (b.a2 - a.a2) * inv;
            }
        }

        const bool mixSettled = mix == mixTarget;
        double finalMix = mix;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            double* data = channels[ch] + pos;
            auto& st = state[(size_t) (firstChannel + ch)];
            double m = mix;
            auto c = ramping ? startCoeffs.sections : coeffs.sections;

            for (int i = 0; i < len; ++i)
            {
                if (ramping)
                {
                    for (int s = 0; s < numSections; ++s)
                    {
                        auto& k = c[(size_t) s];
                        const auto& d = delta[(size_t) s];
                        k.b0 += d.b0; k.b1 += d.b1; k.b2 += d.b2; k.a1 += d.a1; k.a2 += d.a2;
                    }
                }

                const double dry = data[i];
                double y = dry;
                for (int s = 0; s < numSections; ++s)
                    y = st[(size_t) s].process (c[(size_t) s], y);

                if (! mixSettled)
                {
                    m = m < mixTarget ? std::min (mixTarget, m + mixStep)
                                      : std::max (mixTarget, m - mixStep);
                    data[i] = dry + m * (y - dry);
                }
                else
                {
                    data[i] = m >= 1.0 ? y : dry + m * (y - dry);
                }
            }

            finalMix = m;
        }

        if (numChannels == 0 && ! mixSettled)
            finalMix = mixTarget;

        mix = finalMix;
        pos += len;
    }
}

} // namespace peq
