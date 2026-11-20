#pragma once

#include "EQBand.h"

#include <array>

namespace peq
{

/** Which part of the signal a band works on. The numeric values are stored in
    plugin state, so only append new modes at the end. */
enum class ChannelMode : int
{
    Stereo = 0,  // both channels, linked
    Left,
    Right,
    Mid,         // (L + R) / 2
    Side,        // (L - R) / 2
    NumModes
};

/** The multi-band EQ: numBands EQBand instances applied in series, each with
    its own channel routing.

    Real-time safe: no allocation, locking or I/O after prepare().

    Routing
    - Stereo / Left / Right bands run on the left/right signal; Mid / Side
      bands run on the mid/side signal. The engine converts between the two
      only when consecutive active bands need a different domain, so a chain
      of mid/side bands costs one conversion each way.
    - Mono input: every band processes the single channel whatever its mode
      (in mono, mid is the signal itself and there is no side or right).
    - Changing a band's mode fades the band out, swaps the routing, clears
      its filter memory and fades it back in, so it never clicks.
*/
class FilterEngine
{
public:
    static constexpr int numBands = 24;
    static constexpr int maxChannels = EQBand::maxChannels;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Sets a band's target settings; out-of-range indices are ignored. */
    void setBand (int index, const BandParameters& params, ChannelMode mode) noexcept;

    /** Dynamic EQ settings for a band (audio thread, like setBand()). */
    void setBandDynamics (int index, const DynamicSettings& settings) noexcept
    {
        if (index >= 0 && index < numBands)
            bands[(size_t) index].setDynamics (settings);
    }

    /** Gain change the band's dynamics are applying now (dB), for display. */
    double dynamicOffsetDb (int index) const noexcept
    {
        return (index >= 0 && index < numBands) ? bands[(size_t) index].currentDynamicOffsetDb() : 0.0;
    }

    /** Jumps every band straight to its target (after loading state while
        audio is stopped). */
    void snapToTargets() noexcept;

    /** Processes in place. numChannels is clamped to maxChannels. */
    void process (double* const* channels, int numChannels, int numSamples) noexcept;

    const EQBand& band (int index) const noexcept { return bands[(size_t) index]; }

    /** The routing currently applied to a band (it can lag the requested one
        while the band fades out for a mode change). */
    ChannelMode activeMode (int index) const noexcept { return slots[(size_t) index].active; }

    /** Smoothing times for every band, adjustable for tests. */
    void setSmoothingTimes (double parameterSeconds, double fadeSeconds) noexcept;

private:
    struct Slot
    {
        BandParameters params {};
        ChannelMode requested = ChannelMode::Stereo;
        ChannelMode active = ChannelMode::Stereo;
    };

    void applyPendingMode (int index) noexcept;
    static bool usesMidSide (ChannelMode m) noexcept { return m == ChannelMode::Mid || m == ChannelMode::Side; }

    std::array<EQBand, numBands> bands {};
    std::array<Slot, numBands> slots {};
};

} // namespace peq
