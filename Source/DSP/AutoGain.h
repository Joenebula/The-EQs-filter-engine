#pragma once

#include "FilterEngine.h"

namespace peq
{

/** Auto Gain: an output correction that keeps the overall level roughly
    where it was before the EQ, so a before/after comparison is about tone,
    not loudness.

    It is worked out from the EQ curve itself, not by measuring the audio, so
    it never pumps or breathes: the static curve (dynamic bands at their set
    gain) is averaged as power over 20 Hz to 20 kHz with equal weight per
    octave, which is how pink noise, and roughly most music, spreads its
    energy. The result is the gain that undoes that average.

    Channel modes: Stereo bands count on both channels, Left/Right bands on
    their channel only, Mid/Side bands at half strength on both (each part
    carries part of a typical stereo signal). It is an approximation; a
    narrow boost changes the loudness much less than a broad one, and the
    correction follows that.

    Real-time safe (no allocation); cheap enough to run whenever a setting
    changes. */
namespace autogain
{
    constexpr int numPoints = 96;            // log-spaced, 20 Hz - 20 kHz
    constexpr double maxCorrectionDb = 24.0; // either way

    /** Correction in dB (negative when the EQ adds level). Bands that are
        off are ignored. */
    double correctionDb (const BandParameters* bands, const ChannelMode* modes, int numBands, double sampleRate) noexcept;
}

} // namespace peq
