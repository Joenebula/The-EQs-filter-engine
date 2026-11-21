// Tests for EQ Match (1.6): learning two spectra and fitting bands to the difference.

#include "TestFramework.h"
#include "../Source/DSP/EQMatch.h"
#include "../Source/DSP/EQBand.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace peq;

namespace
{
constexpr double fs = 48000.0;

BandParameters band (FilterType t, double f, double g, double q)
{
    BandParameters p;
    p.type = t; p.frequencyHz = f; p.gainDb = g; p.q = q; p.analogMatched = true;
    return p;
}

double curveDb (const std::vector<BandParameters>& bands, double f)
{
    double db = 0.0;
    for (const auto& b : bands)
    {
        CascadeCoefficients c;
        FilterDesign::design (b, fs, c);
        db += c.magnitudeDb (f, fs);
    }
    return db;
}

std::vector<float> noise (int count, uint32_t seed, double level)
{
    std::vector<float> v ((size_t) count);
    uint32_t s = seed;
    for (auto& x : v)
    {
        s = s * 1664525u + 1013904223u;
        x = (float) (level * ((double) (s >> 8) / 8388608.0 - 1.0));
    }
    return v;
}

std::vector<float> filtered (const std::vector<float>& in, const std::vector<BandParameters>& bands, double gain)
{
    std::vector<double> d (in.begin(), in.end());
    for (const auto& b : bands)
    {
        EQBand eq;
        eq.prepare (fs);
        eq.setTarget (b);
        eq.snapToTarget();
        double* ch[1] { d.data() };
        eq.process (ch, 1, (int) d.size());
    }
    std::vector<float> out (d.size());
    for (size_t i = 0; i < d.size(); ++i)
        out[i] = (float) (d[i] * gain);
    return out;
}

// Largest gap between two curves over [lo, hi], after removing their mean
// difference over 100 Hz to 10 kHz (EQ Match ignores overall level).
double worstError (const std::vector<BandParameters>& fitted, const std::vector<BandParameters>& truth, double lo, double hi)
{
    double sum = 0.0; int count = 0;
    for (double f = 100.0; f <= 10000.0; f *= 1.05)
    {
        sum += curveDb (truth, f) - curveDb (fitted, f);
        ++count;
    }
    const double offset = sum / count;
    double worst = 0.0;
    for (double f = lo; f <= hi; f *= 1.03)
        worst = std::max (worst, std::abs (curveDb (truth, f) - curveDb (fitted, f) - offset));
    return worst;
}
} // namespace

TEST_CASE ("EQ Match: fit follows a known curve within 1 dB")
{
    const std::vector<BandParameters> truth { band (FilterType::Bell, 200.0, 4.0, 1.0),
                                              band (FilterType::Bell, 3000.0, -3.0, 2.0),
                                              band (FilterType::HighShelf, 8000.0, 3.0, 0.707) };
    const auto f = EQMatch::fitFrequencies (fs);
    std::vector<double> target (f.size());
    for (size_t i = 0; i < f.size(); ++i)
        target[i] = curveDb (truth, f[i]);
    const auto fitted = EQMatch::fit (f, target, 8, fs, true);
    double worst = 0.0;
    for (size_t i = 0; i < f.size(); ++i)
        if (f[i] >= 30.0 && f[i] <= 18000.0)
            worst = std::max (worst, std::abs (curveDb (fitted, f[i]) - target[i]));
    std::printf ("    fit: %d bands, worst error %.2f dB (30 Hz-18 kHz)\n", (int) fitted.size(), worst);
    CHECK (! fitted.empty() && (int) fitted.size() <= 8);
    CHECK (worst < 1.0);
}

TEST_CASE ("EQ Match: a flat target gives no bands")
{
    const auto f = EQMatch::fitFrequencies (fs);
    std::vector<double> target (f.size(), 0.0);
    CHECK (EQMatch::fit (f, target, 8, fs, true).empty());
}

TEST_CASE ("EQ Match: learns noise against an EQ'd copy and finds that EQ")
{
    const std::vector<BandParameters> truth { band (FilterType::LowShelf, 120.0, 5.0, 0.707),
                                              band (FilterType::Bell, 1500.0, -4.0, 1.4),
                                              band (FilterType::Bell, 6000.0, 3.0, 1.0) };
    const auto src = noise ((int) (fs * 12), 12345u, 0.3);
    const auto ref = filtered (noise ((int) (fs * 12), 98765u, 0.3), truth, 2.0);   // +6 dB louder too

    EQMatch m;
    m.prepare (fs);
    for (size_t i = 0; i < src.size(); i += 512)
    {
        const int n = (int) std::min<size_t> (512, src.size() - i);
        m.addSource (src.data() + i, n);
        m.addReference (ref.data() + i, n);
    }
    CHECK (m.hasEnough());
    CHECK_NEAR (m.sourceSeconds(), 12.0, 0.2);

    const auto fitted = m.makeBands (8, 1.0, true);
    const double worst = worstError (fitted, truth, 40.0, 15000.0);
    std::printf ("    learn: %d bands, worst error %.2f dB (40 Hz-15 kHz)\n", (int) fitted.size(), worst);
    CHECK (worst < 1.5);

    // Half the amount: about half the correction.
    const auto half = m.makeBands (8, 0.5, true);
    const double atLow = curveDb (half, 50.0) - curveDb (half, 1000.0);
    const double fullLow = curveDb (fitted, 50.0) - curveDb (fitted, 1000.0);
    std::printf ("    amount 50 %%: %.2f dB vs %.2f dB at 100 %%\n", atLow, fullLow);
    CHECK_NEAR (atLow, fullLow * 0.5, 1.0);
}

TEST_CASE ("EQ Match: silence is ignored and the same sound needs no EQ")
{
    EQMatch m;
    m.prepare (fs);
    std::vector<float> silence (48000, 0.0f);
    m.addSource (silence.data(), (int) silence.size());
    m.addReference (silence.data(), (int) silence.size());
    CHECK (m.sourceSeconds() < 0.01 && ! m.hasEnough());

    const auto a = noise ((int) (fs * 10), 1u, 0.3);
    const auto b = noise ((int) (fs * 10), 2u, 0.05);   // same colour, quieter
    m.addSource (a.data(), (int) a.size());
    m.addReference (b.data(), (int) b.size());
    const auto bands = m.makeBands (8, 1.0, true);
    double biggest = 0.0;
    for (auto& x : bands) biggest = std::max (biggest, std::abs (x.gainDb));
    std::printf ("    same sound: %d bands, largest %.2f dB\n", (int) bands.size(), biggest);
    CHECK (biggest < 1.0);

    m.reset();
    CHECK (m.sourceSeconds() < 0.01);
}
