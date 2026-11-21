// Tests for the resonance finder (1.9).

#include "TestFramework.h"
#include "../Source/DSP/Resonances.h"
#include "../Source/DSP/SpectrumAnalyser.h"

#include <cmath>
#include <cstdio>
#include <array>
#include <vector>

using namespace peq;

namespace
{
constexpr double fs = 48000.0;
constexpr int fftSize = 8192;
constexpr double binWidth = fs / fftSize;

// A pink-ish spectrum (-3 dB/octave) with optional bumps (Gaussian in log frequency).
std::vector<double> spectrum (std::initializer_list<std::array<double, 3>> bumps)   // {freq, dB, octaves wide}
{
    std::vector<double> db (fftSize / 2 + 1);
    for (size_t b = 0; b < db.size(); ++b)
    {
        const double f = std::max (1.0, b * binWidth);
        double v = -40.0 - 3.0 * std::log2 (f / 1000.0);
        for (const auto& k : bumps)
        {
            const double d = std::log2 (f / k[0]) / k[2];
            v += k[1] * std::exp (-0.5 * d * d);
        }
        db[b] = v;
    }
    return db;
}
} // namespace

TEST_CASE ("Resonances: a smooth spectrum has none")
{
    CHECK (findResonances (spectrum ({}), binWidth).empty());
}

TEST_CASE ("Resonances: finds a narrow ring, ignores a broad hill")
{
    const auto db = spectrum ({ { 2500.0, 12.0, 0.03 }, { 300.0, 8.0, 1.2 } });
    const auto r = findResonances (db, binWidth);
    std::printf ("    found %d: first at %.0f Hz, +%.1f dB, Q %.1f\n", (int) r.size(),
                 r.empty() ? 0.0 : r[0].frequencyHz, r.empty() ? 0.0 : r[0].prominenceDb, r.empty() ? 0.0 : r[0].q);
    CHECK (r.size() == 1);
    CHECK (! r.empty() && std::abs (std::log2 (r[0].frequencyHz / 2500.0)) < 1.0 / 24.0);
    CHECK (! r.empty() && r[0].prominenceDb > 8.0 && r[0].q > 4.0);
}

TEST_CASE ("Resonances: strongest first, close peaks merged, count limited")
{
    const auto db = spectrum ({ { 800.0, 9.0, 0.03 }, { 840.0, 7.0, 0.03 }, { 5000.0, 14.0, 0.03 }, { 150.0, 10.0, 0.08 } });
    const auto r = findResonances (db, binWidth);
    for (const auto& x : r)
        std::printf ("    %.0f Hz +%.1f dB\n", x.frequencyHz, x.prominenceDb);
    CHECK (r.size() == 3);
    CHECK (r.size() == 3 && r[0].frequencyHz > 4500.0 && r[0].prominenceDb >= r[1].prominenceDb && r[1].prominenceDb >= r[2].prominenceDb);
    CHECK (findResonances (db, binWidth, 6.0, 1).size() == 1);
    CHECK (findResonances (db, binWidth, 30.0).empty());
}

TEST_CASE ("Resonances: silence finds nothing")
{
    std::vector<double> db (fftSize / 2 + 1, SpectrumAnalyser::floorDb);
    CHECK (findResonances (db, binWidth).empty());
}
