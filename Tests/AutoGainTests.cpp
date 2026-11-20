// Tests for Auto Gain (1.4): the correction worked out from the EQ curve.

#include "TestFramework.h"
#include "../Source/DSP/AutoGain.h"

#include <array>
#include <cmath>

using namespace peq;

namespace
{
constexpr double fs = 48000.0;

BandParameters bell (double f, double g, double q)
{
    BandParameters p;
    p.type = FilterType::Bell; p.frequencyHz = f; p.gainDb = g; p.q = q;
    return p;
}

double correction (std::initializer_list<BandParameters> list, ChannelMode mode = ChannelMode::Stereo)
{
    std::array<BandParameters, FilterEngine::numBands> bands {};
    std::array<ChannelMode, FilterEngine::numBands> modes {};
    for (auto& b : bands) b.enabled = false;
    int i = 0;
    for (const auto& p : list)
    {
        bands[(size_t) i] = p;
        modes[(size_t) i] = mode;
        ++i;
    }
    return autogain::correctionDb (bands.data(), modes.data(), FilterEngine::numBands, fs);
}
} // namespace

TEST_CASE ("Auto Gain: flat EQ needs no correction")
{
    CHECK_NEAR (correction ({}), 0.0, 1e-12);
    CHECK_NEAR (correction ({ bell (1000.0, 0.0, 1.0) }), 0.0, 1e-9);
    auto off = bell (1000.0, 12.0, 1.0);
    off.enabled = false;
    CHECK_NEAR (correction ({ off }), 0.0, 1e-12);
}

TEST_CASE ("Auto Gain: boosts are turned down, cuts turned up, about symmetrically")
{
    const double boost = correction ({ bell (1000.0, 6.0, 0.7) });
    const double cut   = correction ({ bell (1000.0, -6.0, 0.7) });
    CHECK (boost < -0.5);
    CHECK (cut > 0.5);
    CHECK (std::abs (boost + cut) < 1.0);   // power average: a cut corrects a little less than a boost
}

TEST_CASE ("Auto Gain: broad changes count more than narrow ones")
{
    const double wide   = correction ({ bell (1000.0, 6.0, 0.3) });
    const double narrow = correction ({ bell (1000.0, 6.0, 8.0) });
    CHECK (wide < narrow);
    CHECK (narrow < 0.0 && narrow > -0.5);
}

TEST_CASE ("Auto Gain: a shelf lifting the whole range is undone (-6 dB)")
{
    // A +6 dB high shelf at 20 Hz lifts (nearly) everything by 6 dB.
    BandParameters hs;
    hs.type = FilterType::HighShelf; hs.frequencyHz = 10.0; hs.gainDb = 6.0; hs.q = 0.707;
    CHECK_NEAR (correction ({ hs }), -6.0, 0.3);
}

TEST_CASE ("Auto Gain: Left/Right and Mid/Side bands count at about half")
{
    const double both = correction ({ bell (1000.0, 6.0, 0.5) });
    const double left = correction ({ bell (1000.0, 6.0, 0.5) }, ChannelMode::Left);
    const double mid  = correction ({ bell (1000.0, 6.0, 0.5) }, ChannelMode::Mid);
    CHECK (left > both && left < 0.0);
    CHECK_NEAR (left, mid, 1e-9);
}

TEST_CASE ("Auto Gain: clamped and finite for extreme settings")
{
    std::array<BandParameters, FilterEngine::numBands> bands {};
    std::array<ChannelMode, FilterEngine::numBands> modes {};
    for (int i = 0; i < FilterEngine::numBands; ++i)
    {
        BandParameters p;
        p.type = FilterType::HighShelf; p.frequencyHz = 10.0; p.gainDb = 30.0;
        bands[(size_t) i] = p;
    }
    const double c = autogain::correctionDb (bands.data(), modes.data(), FilterEngine::numBands, fs);
    CHECK (std::isfinite (c));
    CHECK_NEAR (c, -autogain::maxCorrectionDb, 1e-9);

    for (auto& b : bands)
    {
        b.type = FilterType::LowPass; b.frequencyHz = 20.0; b.slopeDbPerOct = 96;
    }
    const double d = autogain::correctionDb (bands.data(), modes.data(), FilterEngine::numBands, fs);
    CHECK (std::isfinite (d));
    CHECK_NEAR (d, autogain::maxCorrectionDb, 1e-9);
    CHECK_NEAR (autogain::correctionDb (nullptr, nullptr, 0, fs), 0.0, 1e-12);
}
