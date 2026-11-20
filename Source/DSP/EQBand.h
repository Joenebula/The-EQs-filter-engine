#pragma once

#include "Dynamics.h"

#include <array>

namespace peq
{

/** One EQ band: a smoothed, click-free cascade of biquads for up to
    maxChannels channels.

    Real-time safe: no allocation, locking or I/O after prepare(). Call
    setTarget() from the audio thread (e.g. at the start of each block, after
    reading the host parameters); the band glides to the new settings.

    Smoothing strategy
    - Frequency and Q glide in the log domain, gain in dB, with a one-pole
      (exponential) smoother. While gliding, the filter is redesigned every
      coefficientUpdateInterval samples and coefficients are ramped linearly
      per sample in between (no zipper noise).
    - Turning a band on or off crossfades between dry and filtered signal.
    - Changing filter type or slope changes the number of sections, which
      cannot be glided, so the band fades to dry, swaps structure, clears its
      state and fades back in.
*/
class EQBand
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int coefficientUpdateInterval = 16;  // samples

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Sets the parameters to glide towards. */
    void setTarget (const BandParameters& target) noexcept;

    /** Jumps straight to the target with no smoothing (use after loading state
        while audio is stopped). */
    void snapToTarget() noexcept;

    /** Processes in place. numChannels is clamped to maxChannels. */
    void process (double* const* channels, int numChannels, int numSamples) noexcept;

    /** Processes a single channel slot in place (used for mid/side and
        left/right-only modes). channelIndex selects the filter state. */
    void processChannel (double* samples, int channelIndex, int numSamples) noexcept;

    /** True while frequency/gain/Q/mix are still moving. */
    bool isSmoothing() const noexcept;

    /** True if the band currently affects the signal at all. */
    bool isActive() const noexcept { return mix > 0.0 || mixTarget > 0.0; }

    /** Coefficients currently in use (for tests and GUI curves). */
    const CascadeCoefficients& currentCoefficients() const noexcept { return coeffs; }

    /** The settings currently being applied (smoothed values). */
    BandParameters currentParameters() const noexcept;

    /** Dynamic EQ settings (see DynamicSettings). Call from the audio thread,
        like setTarget(). Ignored for types without gain. */
    void setDynamics (const DynamicSettings& settings) noexcept;
    const DynamicSettings& getDynamics() const noexcept { return dyn; }

    /** The gain change the dynamics are applying right now, in dB. */
    double currentDynamicOffsetDb() const noexcept { return dynOffsetDb; }

    /** The detector's current level in dBFS (for the activity display). */
    double currentDetectorLevelDb() const noexcept;

    /** Smoothing times, adjustable for tests. */
    void setSmoothingTimes (double parameterSeconds, double fadeSeconds) noexcept;

private:
    struct Smoothed
    {
        double current = 0.0, target = 0.0;
        bool settled() const noexcept;
    };

    /** Advances smoothers and redesigns; returns true if coefficients changed. */
    bool advanceParameters (int numSamples) noexcept;
    void applyStructure() noexcept;
    void updateCoefficients() noexcept;
    void beginFade (double newTarget) noexcept;
    double desiredMix() const noexcept;
    bool dynamicsActive() const noexcept;
    /** Runs the detector over a segment (read only); returns true if the
        gain offset moved enough to need new coefficients. */
    bool updateDynamics (double* const* channels, int firstChannel, int numChannels, int pos, int len) noexcept;
    void clearDynamics() noexcept;
    void processBlockSegment (double* const* channels, int firstChannel, int numChannels, int start, int length) noexcept;

    double sampleRate = 48000.0;
    double parameterTime = 0.020, fadeTime = 0.010;
    double mixStep = 0.0;

    BandParameters requested {};  // as set by the user (unclamped)
    BandParameters target {};     // clamped for the current sample rate
    FilterType activeType = FilterType::Bell;
    int activeSlope = 12;

    Smoothed logFreq, gainDb, logQ;
    double mix = 0.0, mixTarget = 0.0;
    bool structurePending = false;
    bool prepared = false;

    // Dynamic EQ.
    DynamicSettings dyn {};
    double attackCoeff = 1.0, releaseCoeff = 1.0, rmsCoeff = 1.0;
    double meanSquare = 0.0;      // short RMS average of the detector output
    double envelope = 0.0;        // meanSquare after attack/release
    double dynOffsetDb = 0.0;
    BiquadCoefficients detectorCoeffs {};
    std::array<BiquadState, maxChannels> detectorState {};

    CascadeCoefficients coeffs {};
    std::array<std::array<BiquadState, CascadeCoefficients::maxSections>, maxChannels> state {};
};

} // namespace peq
