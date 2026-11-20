// Tests for the analog-matched filter designs (no high-frequency cramping).
//
// The reference is the analogue prototype itself, evaluated at the true
// frequency (no frequency warping). A bilinear-transform design is exact at
// low frequencies but squeezes everything towards Nyquist; the matched design
// should follow the analogue curve right up to 20 kHz.

#include "TestFramework.h"
#include "../Source/DSP/EQBand.h"
#include "../Source/DSP/FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace peq;

namespace
{
constexpr double kPi = 3.14159265358979323846;

/** Analogue magnitude (dB) of a whole band, built from the same prototypes
    and section structure as FilterDesign::design(). */
double analogDb (const BandParameters& p, double f)
{
    using namespace FilterDesign;
    const int slope = sanitiseSlope (p.type, p.slopeDbPerOct);
    const double x = f / p.frequencyHz;
    double m2 = 1.0;
    switch (p.type)
    {
        case FilterType::Bell:
        case FilterType::Notch:
            m2 = analogPrototype (p.type, 2, p.gainDb, p.q).magnitudeSquared (x);
            break;
        case FilterType::LowShelf:
        case FilterType::HighShelf:
            m2 = analogPrototype (p.type, slope / 6, p.gainDb, p.q).magnitudeSquared (x);
            break;
        case FilterType::LowPass:
        case FilterType::HighPass:
        {
            std::array<double, CascadeCoefficients::maxSections> qs {};
            bool firstOrder = false;
            const int n2 = butterworthQs (slope / 6, qs, firstOrder);
            if (n2 > 0)
                qs[(size_t) (n2 - 1)] *= p.q / limits::defaultQ;
            if (firstOrder)
                m2 *= analogPrototype (p.type, 1, 0.0, 1.0).magnitudeSquared (x);
            for (int i = 0; i < n2; ++i)
                m2 *= analogPrototype (p.type, 2, 0.0, qs[(size_t) i]).magnitudeSquared (x);
            break;
        }
        case FilterType::BandPass:
        {
            const int n = slope / 6;
            const double sq = p.q * std::sqrt (std::pow (2.0, 1.0 / n) - 1.0);
            for (int i = 0; i < n; ++i)
                m2 *= analogPrototype (p.type, 2, 0.0, sq).magnitudeSquared (x);
            break;
        }
        case FilterType::NumTypes:
        default:
            break;
    }
    return 10.0 * std::log10 (std::max (m2, 1.0e-30));
}

CascadeCoefficients designWith (BandParameters p, bool matched, double fs)
{
    p.analogMatched = matched;
    CascadeCoefficients c;
    FilterDesign::design (p, fs, c);
    return c;
}

/** Largest |digital - analogue| in dB from 20 Hz to min(20 kHz, 0.45 fs),
    ignoring points where the analogue response is below -40 dB (deep
    stop-bands, where a dB error means little). */
double worstErrorDb (const BandParameters& p, const CascadeCoefficients& c, double fs)
{
    double worst = 0.0;
    const double top = std::min (20000.0, 0.45 * fs);
    for (double f = 20.0; f <= top; f *= 1.02)
    {
        const double a = analogDb (p, f);
        if (a < -40.0)
            continue;
        const double d = c.magnitudeDb (f, fs);
        if (! std::isfinite (d))
            return 1.0e9;
        worst = std::max (worst, std::abs (d - a));
    }
    return worst;
}

bool sectionStable (const BiquadCoefficients& s)
{
    return std::isfinite (s.b0) && std::isfinite (s.b1) && std::isfinite (s.b2)
        && std::isfinite (s.a1) && std::isfinite (s.a2)
        && std::abs (s.a2) < 1.0 && std::abs (s.a1) < 1.0 + s.a2;
}

const char* typeName (FilterType t)
{
    static const char* names[] = { "bell", "low shelf", "high shelf", "low-pass", "high-pass", "notch", "band-pass" };
    return names[(int) t];
}
} // namespace

TEST_CASE ("Matched: off by default, and the classic design is unchanged")
{
    BandParameters p;
    CHECK (! p.analogMatched);

    for (int t = 0; t < (int) FilterType::NumTypes; ++t)
    {
        p.type = (FilterType) t;
        p.frequencyHz = 3000.0;
        p.gainDb = 6.0;
        p.q = 1.3;
        p.slopeDbPerOct = 24;
        CascadeCoefficients a, b;
        FilterDesign::design (p, 48000.0, a);
        p.analogMatched = false;
        FilterDesign::design (p, 48000.0, b);
        bool same = a.numSections == b.numSections;
        for (int s = 0; s < a.numSections; ++s)
            same = same && a.sections[(size_t) s].b0 == b.sections[(size_t) s].b0
                        && a.sections[(size_t) s].a1 == b.sections[(size_t) s].a1;
        CHECK (same);
    }
}

TEST_CASE ("Matched: same number of sections as the classic design")
{
    for (int t = 0; t < (int) FilterType::NumTypes; ++t)
        for (int slope : limits::slopesDbPerOct)
        {
            BandParameters p;
            p.type = (FilterType) t;
            p.slopeDbPerOct = slope;
            CHECK (designWith (p, true, 48000.0).numSections == designWith (p, false, 48000.0).numSections);
        }
}

TEST_CASE ("Matched: a 10 kHz bell at 44.1 kHz keeps its analogue shape")
{
    BandParameters p;
    p.type = FilterType::Bell;
    p.frequencyHz = 10000.0;
    p.gainDb = 12.0;
    p.q = 1.0;
    const double fs = 44100.0;
    const double em = worstErrorDb (p, designWith (p, true, fs), fs);
    const double eb = worstErrorDb (p, designWith (p, false, fs), fs);
    std::printf ("       10 kHz bell, +12 dB, Q 1 @ 44.1 kHz: worst error matched %.2f dB, classic %.2f dB\n", em, eb);
    CHECK (em < 1.0);
    CHECK (eb > 3.0); // the cramping this feature exists to fix
}

TEST_CASE ("Matched: exact at DC, and a bell is exact at its centre")
{
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    for (double fs : rates)
        for (double fc : { 30.0, 300.0, 3000.0, 12000.0, 19000.0 })
            for (double g : { -18.0, -3.0, 4.0, 18.0 })
                for (double q : { 0.4, 1.0, 5.0 })
                {
                    BandParameters p;
                    p.frequencyHz = fc;
                    p.gainDb = g;
                    p.q = q;

                    p.type = FilterType::Bell;
                    auto c = designWith (p, true, fs);
                    if (fc <= 0.375 * fs) // exact centre up to 16.5 kHz at 44.1 kHz
                        CHECK_NEAR (c.magnitudeDb (fc, fs), g, 1.0e-6);
                    CHECK_NEAR (c.magnitudeDb (0.0, fs), 0.0, 0.05);

                    for (int slope : { 6, 12 })
                    {
                        p.slopeDbPerOct = slope;
                        p.type = FilterType::LowShelf;
                        CHECK_NEAR (designWith (p, true, fs).magnitudeDb (0.0, fs), g, 1.0e-6);
                        p.type = FilterType::HighShelf;
                        CHECK_NEAR (designWith (p, true, fs).magnitudeDb (0.0, fs), 0.0, 1.0e-6);
                    }

                    p.type = FilterType::LowPass;
                    p.slopeDbPerOct = 24;
                    CHECK_NEAR (designWith (p, true, fs).magnitudeDb (0.0, fs), 0.0, 1.0e-6);
                    p.type = FilterType::Notch;
                    CHECK_NEAR (designWith (p, true, fs).magnitudeDb (0.0, fs), 0.0, 1.0e-6);
                    CHECK (designWith (p, true, fs).magnitudeDb (fc, fs) < -100.0);
                }
}

TEST_CASE ("Matched: follows the analogue curve to 20 kHz, never worse than classic")
{
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    // Worst error allowed per type over the whole grid (fc up to 0.49 fs,
    // Q 0.3 to 8, gain +/-24 dB, all slopes up to 24 dB/oct).
    // (Shelves reach their worst only at Q 8, a deliberately resonant shelf;
    // the notch at a very wide Q centred right at Nyquist.)
    const double cap[] = { 3.0, 7.0, 7.0, 3.0, 7.0, 13.0, 7.0 };

    for (int t = 0; t < (int) FilterType::NumTypes; ++t)
    {
        double worstM = 0.0, worstB = 0.0;
        bool neverWorse = true;
        for (double fs : rates)
            for (double fc : { 10.0, 20.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 19000.0,
                               0.45 * fs, 0.47 * fs, 0.49 * fs })
                for (double g : { -24.0, -6.0, 6.0, 24.0 })
                    for (double q : { 0.3, 0.707, 2.0, 8.0 })
                        for (int slope : { 6, 12, 24 })
                        {
                            if (fc > 0.49 * fs || fc > limits::maxFrequency)
                                continue;
                            BandParameters p;
                            p.type = (FilterType) t;
                            p.frequencyHz = fc;
                            p.gainDb = g;
                            p.q = q;
                            p.slopeDbPerOct = slope;
                            const double em = worstErrorDb (p, designWith (p, true, fs), fs);
                            const double eb = worstErrorDb (p, designWith (p, false, fs), fs);
                            worstM = std::max (worstM, em);
                            worstB = std::max (worstB, eb);
                            if (em > eb + 0.25)
                                neverWorse = false;
                        }
        std::printf ("       %-10s worst error vs analogue: matched %.2f dB, classic %.2f dB\n",
                     typeName ((FilterType) t), worstM, worstB);
        CHECK (worstM <= cap[t]);
        CHECK (neverWorse);
    }
}

TEST_CASE ("Matched: everyday settings stay within 1.5 dB of analogue up to 20 kHz")
{
    // Bells and shelves as they are normally used: Q 0.5 to 2, +/-12 dB,
    // centre up to 16 kHz, at the common 44.1 and 48 kHz rates.
    double worst = 0.0;
    char where[160] = "";
    for (double fs : { 44100.0, 48000.0 })
        for (int t : { (int) FilterType::Bell, (int) FilterType::LowShelf, (int) FilterType::HighShelf })
            for (double fc : { 50.0, 200.0, 1000.0, 4000.0, 8000.0, 12000.0, 16000.0 })
                for (double g : { -12.0, -6.0, -2.0, 2.0, 6.0, 12.0 })
                    for (double q : { 0.5, 0.707, 1.0, 2.0 })
                        for (int slope : { 6, 12 })
                        {
                            if (t != (int) FilterType::Bell && q > 1.0)
                                continue; // shelf Q above 1 adds a deliberate bump
                            BandParameters p;
                            p.type = (FilterType) t;
                            p.frequencyHz = fc;
                            p.gainDb = g;
                            p.q = q;
                            p.slopeDbPerOct = slope;
                            const double e = worstErrorDb (p, designWith (p, true, fs), fs);
                            if (e > worst)
                                std::snprintf (where, sizeof (where), "%s fc %g gain %g Q %g slope %d at %g Hz",
                                               typeName (p.type), fc, g, q, slope, fs);
                            worst = std::max (worst, e);
                        }
    std::printf ("       everyday bells/shelves: worst error vs analogue %.2f dB (%s)\n", worst, where);
    CHECK (worst < 1.5);
}

TEST_CASE ("Matched: always stable and finite (random settings)")
{
    std::mt19937 rng (1234);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    const double rates[] = { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
    bool ok = true;
    for (int i = 0; i < 20000 && ok; ++i)
    {
        BandParameters p;
        p.type = (FilterType) (i % (int) FilterType::NumTypes);
        p.frequencyHz = limits::minFrequency * std::pow (limits::maxFrequency / limits::minFrequency, u (rng));
        p.gainDb = limits::minGainDb + (limits::maxGainDb - limits::minGainDb) * u (rng);
        p.q = limits::minQ * std::pow (limits::maxQ / limits::minQ, u (rng));
        p.slopeDbPerOct = limits::slopesDbPerOct[(size_t) (i / 7) % limits::slopesDbPerOct.size()];
        p.analogMatched = true;
        const double fs = rates[(size_t) i % 7];
        CascadeCoefficients c;
        FilterDesign::design (p, fs, c);
        for (int s = 0; s < c.numSections; ++s)
            ok = ok && sectionStable (c.sections[(size_t) s]);
        for (double f : { 0.0, 1000.0, 0.25 * fs, 0.49 * fs })
            ok = ok && std::isfinite (c.magnitudeDb (f, fs));
        if (! ok)
            std::printf ("       unstable: type %d fc %g gain %g q %g slope %d fs %g\n",
                         (int) p.type, p.frequencyHz, p.gainDb, p.q, p.slopeDbPerOct, fs);
    }
    CHECK (ok);
}

TEST_CASE ("Matched: switching on and off while playing is smooth")
{
    EQBand band;
    band.prepare (48000.0);
    BandParameters p;
    p.type = FilterType::HighShelf;
    p.frequencyHz = 12000.0;
    p.gainDb = 9.0;
    band.setTarget (p);
    band.snapToTarget();

    std::vector<double> buf (48000);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = 0.5 * std::sin (2.0 * kPi * 1000.0 * (double) i / 48000.0);

    double* chans[1];
    double maxJump = 0.0, prev = 0.0;
    bool finite = true;
    for (int block = 0; block < 100; ++block)
    {
        if (block % 10 == 5)
        {
            p.analogMatched = ! p.analogMatched;
            band.setTarget (p);
        }
        chans[0] = buf.data() + block * 480;
        band.process (chans, 1, 480);
        for (int i = 0; i < 480; ++i)
        {
            const double v = chans[0][i];
            finite = finite && std::isfinite (v);
            if (block > 0 || i > 0)
                maxJump = std::max (maxJump, std::abs (v - prev));
            prev = v;
        }
    }
    CHECK (finite);
    // A 1 kHz sine at this level moves at most ~0.1 per sample; a click would
    // show as a much bigger step.
    CHECK (maxJump < 0.15);
}
