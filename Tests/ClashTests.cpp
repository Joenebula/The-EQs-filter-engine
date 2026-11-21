// Tests for the clash (masking) finder (2.0).

#include "TestFramework.h"
#include "../Source/DSP/Clashes.h"
#include "../Source/DSP/SpectrumAnalyser.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace peq;

namespace
{
constexpr double fs = 48000.0;
constexpr int fftSize = 8192;
constexpr double binWidth = fs / fftSize;

// A spectrum that is `peakDb` across lo..hi Hz and falls away 24 dB/octave outside it.
std::vector<double> band (double lo, double hi, double peakDb)
{
    std::vector<double> db (fftSize / 2 + 1);
    for (size_t b = 0; b < db.size(); ++b)
    {
        const double f = std::max (1.0, b * binWidth);
        double v = peakDb;
        if (f < lo) v -= 24.0 * std::log2 (lo / f);
        if (f > hi) v -= 24.0 * std::log2 (f / hi);
        db[b] = std::max (v, SpectrumAnalyser::floorDb);
    }
    return db;
}
} // namespace

TEST_CASE ("Clashes: two tracks loud in the same range clash there")
{
    const auto track = band (60.0, 4000.0, -30.0);   // a full track
    const auto other = band (80.0, 250.0, -28.0);    // a bass in its low end
    const auto c = findClashes (track, other, binWidth);
    for (const auto& x : c)
        std::printf ("    %.0f-%.0f Hz, centre %.0f, Q %.2f, cut %.1f dB\n", x.lowHz, x.highHz, x.centreHz, x.q, x.suggestedCutDb);
    CHECK (c.size() == 1);
    CHECK (! c.empty() && c[0].centreHz > 80.0 && c[0].centreHz < 250.0);
    CHECK (! c.empty() && c[0].lowHz < 100.0 && c[0].highHz > 200.0 && c[0].highHz < 500.0);
    CHECK (! c.empty() && c[0].suggestedCutDb <= -1.5 && c[0].suggestedCutDb >= -4.5);
    CHECK (! c.empty() && c[0].q >= 0.5 && c[0].q <= 4.0);
}

TEST_CASE ("Clashes: a much quieter other track does not clash")
{
    const auto track = band (60.0, 4000.0, -30.0);
    CHECK (findClashes (track, band (80.0, 250.0, -45.0), binWidth).empty());
}

TEST_CASE ("Clashes: tracks in different ranges do not clash")
{
    const auto lows = band (40.0, 200.0, -30.0);
    const auto highs = band (3000.0, 10000.0, -30.0);
    CHECK (findClashes (lows, highs, binWidth).empty());
}

TEST_CASE ("Clashes: silence or mismatched input finds nothing")
{
    std::vector<double> quiet (fftSize / 2 + 1, SpectrumAnalyser::floorDb);
    const auto track = band (60.0, 4000.0, -30.0);
    CHECK (findClashes (track, quiet, binWidth).empty());
    CHECK (findClashes (quiet, track, binWidth).empty());
    CHECK (findClashes (track, std::vector<double> (100, -30.0), binWidth).empty());
}

TEST_CASE ("Clashes: strongest first, count limited")
{
    const auto track = band (40.0, 12000.0, -30.0);
    auto other = band (80.0, 200.0, -30.0);
    const auto other2 = band (2000.0, 6000.0, -28.0);
    for (size_t b = 0; b < other.size(); ++b)   // add the two in power
        other[b] = 10.0 * std::log10 (std::pow (10.0, other[b] / 10.0) + std::pow (10.0, other2[b] / 10.0));
    const auto c = findClashes (track, other, binWidth);
    for (const auto& x : c)
        std::printf ("    %.0f-%.0f Hz score %.2f\n", x.lowHz, x.highHz, x.score);
    CHECK (c.size() == 2);
    CHECK (c.size() == 2 && c[0].score >= c[1].score);
    CHECK (findClashes (track, other, binWidth, 1).size() == 1);
}
