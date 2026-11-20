// Automated tests for the core EQ DSP (Phase 2).
//
// Most response tests compare against closed-form analogue prototypes mapped
// through the bilinear transform, so the expected values are exact rather than
// "roughly right": Omega = tan(pi f / fs) / tan(pi fc / fs).

#include "TestFramework.h"
#include "../Source/DSP/EQBand.h"
#include "../Source/DSP/FilterDesign.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace peq;

namespace
{
constexpr double pi = 3.14159265358979323846;
const double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

double toDb (double lin) { return 20.0 * std::log10 (lin); }

double warped (double f, double fc, double fs)
{
    return std::tan (pi * f / fs) / std::tan (pi * fc / fs);
}

CascadeCoefficients designBand (FilterType type, double fc, double gainDb, double q, int slope, double fs)
{
    BandParameters p;
    p.type = type;
    p.frequencyHz = fc;
    p.gainDb = gainDb;
    p.q = q;
    p.slopeDbPerOct = slope;
    CascadeCoefficients c;
    FilterDesign::design (p, fs, c);
    return c;
}

/** Amplitude of the component at frequency f in x (lock-in / single-bin DFT
    over a whole number of cycles). */
double amplitudeAt (const std::vector<double>& x, size_t start, size_t length, double f, double fs)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < length; ++i)
    {
        const double ph = 2.0 * pi * f * (double) (start + i) / fs;
        re += x[start + i] * std::cos (ph);
        im += x[start + i] * std::sin (ph);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (double) length;
}

std::vector<double> sine (double f, double fs, size_t n, double amp = 1.0)
{
    std::vector<double> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = amp * std::sin (2.0 * pi * f * (double) i / fs);
    return x;
}

/** Runs a mono buffer through a band in irregular block sizes. */
void runBand (EQBand& band, std::vector<double>& x, size_t from, size_t to, int maxBlock = 256)
{
    size_t pos = from;
    int k = 0;
    while (pos < to)
    {
        const int block = std::min<int> ((int) (to - pos), 1 + (k++ * 97) % maxBlock);
        double* ch[1] = { x.data() + pos };
        band.process (ch, 1, block);
        pos += (size_t) block;
    }
}

/** Peak of the high-frequency residue (content above ~4 kHz) in a signal
    whose wanted content is a low sine. Used as a click detector. */
double clickLevelDb (const std::vector<double>& y, double fs)
{
    EQBand hp;
    BandParameters p;
    p.type = FilterType::HighPass;
    p.frequencyHz = 4000.0;
    p.slopeDbPerOct = 96;
    hp.setTarget (p);
    hp.prepare (fs);
    std::vector<double> r = y;
    double* ch[1] = { r.data() };
    hp.process (ch, 1, (int) r.size());
    double peak = 0.0;
    // skip the filter's own start-up transient
    for (size_t i = (size_t) (0.05 * fs); i < r.size(); ++i)
        peak = std::max (peak, std::abs (r[i]));
    return toDb (std::max (peak, 1.0e-12));
}
} // namespace

//==============================================================================
TEST_CASE ("Bell: exact gain at centre frequency, all sample rates")
{
    for (double fs : sampleRates)
        for (double fc : { 20.0, 100.0, 1000.0, 5000.0, 15000.0 })
            for (double g : { -30.0, -12.0, -3.0, 0.0, 0.5, 6.0, 18.0, 30.0 })
                for (double q : { 0.1, 0.707, 2.0, 10.0 })
                {
                    const auto c = designBand (FilterType::Bell, fc, g, q, 12, fs);
                    CHECK_NEAR (c.magnitudeDb (fc, fs), g, 1.0e-9);
                }
}

TEST_CASE ("Bell: Q sets the bandwidth between half-gain points")
{
    // For the bell prototype the half-gain (dB/2) edges are at
    // w = sqrt(1 + 1/(4Q^2)) +/- 1/(2Q)  (normalised analogue frequency).
    for (double fs : sampleRates)
        for (double q : { 0.3, 0.707, 1.0, 4.0, 12.0 })
            for (double g : { -15.0, 9.0, 24.0 })
            {
                const double fc = 1000.0;
                const auto c = designBand (FilterType::Bell, fc, g, q, 12, fs);
                const double root = std::sqrt (1.0 + 1.0 / (4.0 * q * q));
                for (double w : { root - 1.0 / (2.0 * q), root + 1.0 / (2.0 * q) })
                {
                    // map analogue edge back to a digital frequency
                    const double f = fs / pi * std::atan (w * std::tan (pi * fc / fs));
                    CHECK_NEAR (c.magnitudeDb (f, fs), g / 2.0, 1.0e-8);
                }
            }
}

TEST_CASE ("Bell: no effect far from the centre and at 0 dB")
{
    const double fs = 48000.0;
    const auto c = designBand (FilterType::Bell, 1000.0, 12.0, 2.0, 12, fs);
    CHECK_NEAR (c.magnitudeDb (10.0, fs), 0.0, 0.01);
    CHECK_NEAR (c.magnitudeDb (23999.0, fs), 0.0, 0.01);

    const auto flat = designBand (FilterType::Bell, 1000.0, 0.0, 2.0, 12, fs);
    for (double f = 10.0; f < 24000.0; f *= 1.1)
        CHECK_NEAR (flat.magnitudeDb (f, fs), 0.0, 1.0e-9);
}

TEST_CASE ("Shelves (12 dB/oct): DC, Nyquist and midpoint gain")
{
    for (double fs : sampleRates)
        for (double g : { -30.0, -6.0, 3.0, 15.0, 30.0 })
            for (double q : { 0.5, 0.707, 1.5 })
            {
                const double fc = 800.0;
                const auto lo = designBand (FilterType::LowShelf, fc, g, q, 12, fs);
                CHECK_NEAR (lo.magnitudeDb (0.0, fs), g, 1.0e-9);
                CHECK_NEAR (lo.magnitudeDb (fc, fs), g / 2.0, 1.0e-9);
                CHECK_NEAR (lo.magnitudeDb (fs / 2.0, fs), 0.0, 1.0e-9);

                const auto hi = designBand (FilterType::HighShelf, fc, g, q, 12, fs);
                CHECK_NEAR (hi.magnitudeDb (0.0, fs), 0.0, 1.0e-9);
                CHECK_NEAR (hi.magnitudeDb (fc, fs), g / 2.0, 1.0e-9);
                CHECK_NEAR (hi.magnitudeDb (fs / 2.0, fs), g, 1.0e-9);
            }
}

TEST_CASE ("Shelves (6 dB/oct): DC, Nyquist and midpoint gain")
{
    for (double fs : sampleRates)
        for (double g : { -24.0, -6.0, 3.0, 12.0, 30.0 })
        {
            const double fc = 2000.0;
            const auto lo = designBand (FilterType::LowShelf, fc, g, 1.0, 6, fs);
            CHECK (lo.numSections == 1 && lo.sections[0].b2 == 0.0 && lo.sections[0].a2 == 0.0);
            CHECK_NEAR (lo.magnitudeDb (0.0, fs), g, 1.0e-9);
            CHECK_NEAR (lo.magnitudeDb (fc, fs), g / 2.0, 1.0e-9);
            CHECK_NEAR (lo.magnitudeDb (fs / 2.0, fs), 0.0, 1.0e-9);

            const auto hi = designBand (FilterType::HighShelf, fc, g, 1.0, 6, fs);
            CHECK_NEAR (hi.magnitudeDb (0.0, fs), 0.0, 1.0e-9);
            CHECK_NEAR (hi.magnitudeDb (fc, fs), g / 2.0, 1.0e-9);
            CHECK_NEAR (hi.magnitudeDb (fs / 2.0, fs), g, 1.0e-9);
        }
}

TEST_CASE ("Low/high-pass: exact Butterworth response for every slope")
{
    for (double fs : sampleRates)
        for (int slope : limits::slopesDbPerOct)
            for (double fc : { 30.0, 500.0, 8000.0 })
            {
                const int order = slope / 6;
                const auto lp = designBand (FilterType::LowPass, fc, 0.0, limits::defaultQ, slope, fs);
                const auto hp = designBand (FilterType::HighPass, fc, 0.0, limits::defaultQ, slope, fs);
                CHECK (lp.numSections == FilterDesign::numSectionsFor (FilterType::LowPass, slope));

                CHECK_NEAR (lp.magnitudeDb (fc, fs), -3.0103, 1.0e-3);
                CHECK_NEAR (hp.magnitudeDb (fc, fs), -3.0103, 1.0e-3);

                for (double f = 10.0; f < 0.49 * fs; f *= 1.37)
                {
                    const double W = warped (f, fc, fs);
                    const double expLp = -10.0 * std::log10 (1.0 + std::pow (W, 2.0 * order));
                    const double expHp = -10.0 * std::log10 (1.0 + std::pow (1.0 / W, 2.0 * order));
                    if (expLp > -250.0) CHECK_NEAR (lp.magnitudeDb (f, fs), expLp, 1.0e-6);
                    if (expHp > -250.0) CHECK_NEAR (hp.magnitudeDb (f, fs), expHp, 1.0e-6);
                }
            }
}

TEST_CASE ("Low/high-pass: slope per octave matches the setting")
{
    // Measured one octave apart, far enough past the cutoff for the response to
    // be on its asymptote but above the -300 dB measurement floor.
    const double fs = 192000.0;
    const double fc = 100.0;
    for (int slope : limits::slopesDbPerOct)
    {
        const int order = slope / 6;
        const double W1 = order < 4 ? 16.0 : 4.0, W2 = 2.0 * W1;
        auto digitalFor = [&] (double W) { return fs / pi * std::atan (W * std::tan (pi * fc / fs)); };

        for (auto type : { FilterType::LowPass, FilterType::HighPass })
        {
            const bool lp = type == FilterType::LowPass;
            const auto c = designBand (type, fc, 0.0, limits::defaultQ, slope, fs);
            const double fa = digitalFor (lp ? W1 : 1.0 / W1), fb = digitalFor (lp ? W2 : 1.0 / W2);
            const double perOct = c.magnitudeDb (fb, fs) - c.magnitudeDb (fa, fs);
            const double exact = -10.0 * std::log10 ((1.0 + std::pow (W2, 2.0 * order)) / (1.0 + std::pow (W1, 2.0 * order)));
            CHECK_NEAR (perOct, exact, 1.0e-6);
            // Nominal: 6.02 dB per order. Within 0.25 dB of the label.
            CHECK_NEAR (perOct, -(double) order * 20.0 * std::log10 (2.0), 0.25);
        }
    }
}

TEST_CASE ("Low/high-pass: Q adds resonance at the cutoff (12 dB/oct)")
{
    for (double fs : sampleRates)
        for (double q : { 0.5, 1.0, 4.0, 10.0 })
        {
            const auto lp = designBand (FilterType::LowPass, 1000.0, 0.0, q, 12, fs);
            const auto hp = designBand (FilterType::HighPass, 1000.0, 0.0, q, 12, fs);
            CHECK_NEAR (lp.magnitudeDb (1000.0, fs), toDb (q), 1.0e-9);
            CHECK_NEAR (hp.magnitudeDb (1000.0, fs), toDb (q), 1.0e-9);
        }
}

TEST_CASE ("Notch: deep null at centre, Q sets -3 dB bandwidth")
{
    for (double fs : sampleRates)
        for (double q : { 0.5, 2.0, 8.0 })
        {
            const double fc = 3000.0;
            const auto c = designBand (FilterType::Notch, fc, 0.0, q, 12, fs);
            CHECK (c.magnitudeDb (fc, fs) < -200.0);
            CHECK_NEAR (c.magnitudeDb (0.0, fs), 0.0, 1.0e-9);
            CHECK_NEAR (c.magnitudeDb (fs / 2.0, fs), 0.0, 1.0e-9);
            const double root = std::sqrt (1.0 + 1.0 / (4.0 * q * q));
            for (double w : { root - 1.0 / (2.0 * q), root + 1.0 / (2.0 * q) })
            {
                const double f = fs / pi * std::atan (w * std::tan (pi * fc / fs));
                CHECK_NEAR (c.magnitudeDb (f, fs), -3.0103, 1.0e-3);
            }
        }
}

TEST_CASE ("Band-pass: unity at centre and -3 dB bandwidth = fc/Q for every slope")
{
    for (double fs : sampleRates)
        for (int slope : { 6, 12, 18, 24, 36, 48 })
            for (double q : { 0.5, 1.0, 4.0 })
            {
                const double fc = 1500.0;
                const auto c = designBand (FilterType::BandPass, fc, 0.0, q, slope, fs);
                CHECK (c.numSections == slope / 6);
                CHECK_NEAR (c.magnitudeDb (fc, fs), 0.0, 1.0e-9);
                const double root = std::sqrt (1.0 + 1.0 / (4.0 * q * q));
                for (double w : { root - 1.0 / (2.0 * q), root + 1.0 / (2.0 * q) })
                {
                    const double f = fs / pi * std::atan (w * std::tan (pi * fc / fs));
                    CHECK_NEAR (c.magnitudeDb (f, fs), -3.0103, 1.0e-3);
                }
            }
}

TEST_CASE ("Slope sanitising")
{
    CHECK (FilterDesign::sanitiseSlope (FilterType::LowShelf, 24) == 12);
    CHECK (FilterDesign::sanitiseSlope (FilterType::HighShelf, 6) == 6);
    CHECK (FilterDesign::sanitiseSlope (FilterType::LowPass, 30) == 24 || FilterDesign::sanitiseSlope (FilterType::LowPass, 30) == 36);
    CHECK (FilterDesign::sanitiseSlope (FilterType::LowPass, 1000) == 96);
    CHECK (FilterDesign::sanitiseSlope (FilterType::BandPass, 96) == 48);
    CHECK (FilterDesign::sanitiseSlope (FilterType::Bell, 48) == 12);
    CHECK (FilterDesign::numSectionsFor (FilterType::HighPass, 96) == 8);
    CHECK (FilterDesign::numSectionsFor (FilterType::HighPass, 18) == 2);
    CHECK (FilterDesign::numSectionsFor (FilterType::HighPass, 6) == 1);
}

TEST_CASE ("Every design across the full parameter range is stable")
{
    int designs = 0;
    for (double fs : sampleRates)
        for (int t = 0; t < (int) FilterType::NumTypes; ++t)
            for (int slope : limits::slopesDbPerOct)
                for (double fc = limits::minFrequency; fc <= limits::maxFrequency * 1.001; fc *= 1.25)
                    for (double q : { limits::minQ, 0.1, limits::defaultQ, 3.0, limits::maxQ })
                        for (double g : { limits::minGainDb, 0.0, limits::maxGainDb })
                        {
                            const auto c = designBand ((FilterType) t, fc, g, q, slope, fs);
                            bool ok = c.numSections >= 1;
                            for (int s = 0; s < c.numSections; ++s)
                            {
                                const auto& k = c.sections[(size_t) s];
                                ok = ok && k.isStable() && std::isfinite (k.b0) && std::isfinite (k.b1)
                                        && std::isfinite (k.b2) && std::isfinite (k.a1) && std::isfinite (k.a2);
                            }
                            CHECK (ok);
                            ++designs;
                        }
    std::printf ("    (%d designs checked)\n", designs);
}

TEST_CASE ("Frequency above Nyquist is clamped, not unstable (30 kHz at 44.1 kHz)")
{
    const double fs = 44100.0;
    const auto c = designBand (FilterType::Bell, 30000.0, 12.0, 1.0, 12, fs);
    CHECK (c.sections[0].isStable());
    CHECK_NEAR (c.magnitudeDb (0.49 * fs, fs), 12.0, 1.0e-9);
}

//==============================================================================
TEST_CASE ("Processing matches the designed response (sine measurements)")
{
    struct Case { FilterType type; double fc, gain, q; int slope; };
    const Case cases[] = {
        { FilterType::Bell,      1000.0,  9.0, 1.4, 12 },
        { FilterType::Bell,        60.0, -18.0, 4.0, 12 },
        { FilterType::LowShelf,   200.0,  6.0, 0.7, 12 },
        { FilterType::HighShelf, 6000.0, -8.0, 0.7, 6 },
        { FilterType::LowPass,   2000.0,  0.0, 0.707, 48 },
        { FilterType::HighPass,   120.0,  0.0, 2.0, 24 },
        { FilterType::Notch,      500.0,  0.0, 3.0, 12 },
        { FilterType::BandPass,  3000.0,  0.0, 1.0, 24 },
    };

    for (double fs : { 44100.0, 96000.0, 192000.0 })
        for (const auto& cs : cases)
            for (double f : { 40.0, 250.0, 1000.0, 2500.0, 9000.0 })
            {
                BandParameters p;
                p.type = cs.type; p.frequencyHz = cs.fc; p.gainDb = cs.gain; p.q = cs.q; p.slopeDbPerOct = cs.slope;
                EQBand band;
                band.setTarget (p);
                band.prepare (fs);

                const size_t n = (size_t) (1.0 * fs);
                auto x = sine (f, fs, n, 0.5);
                runBand (band, x, 0, n);

                // measure the last 0.25 s over a whole number of cycles
                const size_t len = (size_t) (std::floor (0.25 * f) * fs / f);
                const double measured = toDb (amplitudeAt (x, n - len, len, f, fs) / 0.5);
                const double expected = band.currentCoefficients().magnitudeDb (f, fs);
                if (expected > -100.0)
                    CHECK_NEAR (measured, expected, 0.01);
                else
                    CHECK (measured < -90.0);
            }
}

TEST_CASE ("Smoothing settles exactly on the target")
{
    const double fs = 48000.0;
    EQBand band;
    BandParameters p; p.type = FilterType::Bell; p.frequencyHz = 100.0; p.gainDb = 0.0; p.q = 1.0;
    band.setTarget (p);
    band.prepare (fs);

    p.frequencyHz = 8000.0; p.gainDb = -20.0; p.q = 6.0;
    band.setTarget (p);
    CHECK (band.isSmoothing());
    std::vector<double> x ((size_t) fs, 0.0);
    runBand (band, x, 0, x.size());
    CHECK (! band.isSmoothing());

    const auto cur = band.currentParameters();
    CHECK_NEAR (cur.frequencyHz, 8000.0, 1.0e-6);
    CHECK_NEAR (cur.gainDb, -20.0, 1.0e-9);
    CHECK_NEAR (cur.q, 6.0, 1.0e-9);
    const auto direct = designBand (FilterType::Bell, 8000.0, -20.0, 6.0, 12, fs);
    CHECK_NEAR (band.currentCoefficients().magnitudeDb (8000.0, fs), direct.magnitudeDb (8000.0, fs), 1.0e-6);
}

namespace
{
/** Plays a 100 Hz sine, applies `change` half-way through, returns the click level. */
template <typename Change>
double clickTest (BandParameters start, Change change, bool smoothing, double fs = 48000.0)
{
    EQBand band;
    if (! smoothing)
        band.setSmoothingTimes (0.0, 0.0);
    band.setTarget (start);
    band.prepare (fs);
    const size_t n = (size_t) (0.6 * fs);
    auto x = sine (100.0, fs, n, 0.5);
    runBand (band, x, 0, n / 2, 64);
    change (band);
    runBand (band, x, n / 2, n, 64);
    return clickLevelDb (x, fs);
}
}

TEST_CASE ("Smoothing: parameter jumps, bypass and type changes are click-free")
{
    BandParameters bell; bell.type = FilterType::Bell; bell.frequencyHz = 100.0; bell.gainDb = 0.0; bell.q = 1.0;
    const double reference = clickTest (bell, [] (EQBand&) {}, true);
    std::printf ("    reference (no change)          %.1f dB\n", reference);

    auto gainJump = [bell] (EQBand& b) { auto p = bell; p.gainDb = 24.0; b.setTarget (p); };
    auto freqJump = [bell] (EQBand& b) { auto p = bell; p.gainDb = 12.0; p.frequencyHz = 3000.0; b.setTarget (p); };
    auto bypass   = [bell] (EQBand& b) { auto p = bell; p.gainDb = 12.0; p.enabled = false; b.setTarget (p); };
    auto typeSwap = [bell] (EQBand& b) { auto p = bell; p.type = FilterType::HighPass; p.frequencyHz = 400.0; p.slopeDbPerOct = 48; b.setTarget (p); };

    BandParameters loud = bell; loud.gainDb = 12.0;
    auto bypassFromLoud = [loud] (EQBand& b) { auto p = loud; p.enabled = false; b.setTarget (p); };

    struct Named { const char* name; double smooth, hard; };
    const Named results[] = {
        { "gain 0 -> +24 dB",        clickTest (bell, gainJump, true), clickTest (bell, gainJump, false) },
        { "freq 100 Hz -> 3 kHz",    clickTest (bell, freqJump, true), clickTest (bell, freqJump, false) },
        { "bypass +12 dB band",      clickTest (loud, bypassFromLoud, true), clickTest (loud, bypassFromLoud, false) },
        { "bell -> 48 dB high-pass", clickTest (bell, typeSwap, true), clickTest (bell, typeSwap, false) },
    };
    (void) bypass;

    for (const auto& r : results)
    {
        std::printf ("    %-26s smoothed %7.1f dB   unsmoothed %7.1f dB\n", r.name, r.smooth, r.hard);
        CHECK (r.smooth < -80.0);           // inaudible
        CHECK (r.smooth < r.hard - 20.0);   // and the detector really does see clicks
    }
}

TEST_CASE ("Rapid random automation stays stable at every sample rate")
{
    std::mt19937 rng (1234);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    std::normal_distribution<double> noise (0.0, 0.25);

    for (double fs : sampleRates)
    {
        EQBand band;
        band.prepare (fs);
        std::vector<double> l (512), r (512);
        double peak = 0.0;
        bool finite = true;
        const int blocks = (int) (5.0 * fs / 256.0);
        for (int b = 0; b < blocks; ++b)
        {
            BandParameters p;
            p.type = (FilterType) (int) (uni (rng) * (int) FilterType::NumTypes);
            p.frequencyHz = limits::minFrequency * std::pow (limits::maxFrequency / limits::minFrequency, uni (rng));
            p.gainDb = limits::minGainDb + uni (rng) * (limits::maxGainDb - limits::minGainDb);
            p.q = limits::minQ * std::pow (limits::maxQ / limits::minQ, uni (rng));
            p.slopeDbPerOct = limits::slopesDbPerOct[(size_t) (uni (rng) * 8.0) % 8];
            p.enabled = uni (rng) > 0.1;
            // change parameters on most blocks; structure changes less often
            if (uni (rng) < 0.7)
                band.setTarget (p);

            const int n = 1 + (int) (uni (rng) * 511.0);
            for (int i = 0; i < n; ++i) { l[(size_t) i] = noise (rng); r[(size_t) i] = noise (rng); }
            double* ch[2] = { l.data(), r.data() };
            band.process (ch, 2, n);
            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                peak = std::max ({ peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
            }
        }
        CHECK (finite);
        CHECK (peak < 1000.0);
        std::printf ("    %.1f kHz: peak %.1f (input sigma 0.25)\n", fs / 1000.0, peak);
    }
}

TEST_CASE ("Stereo: channels are independent and match mono processing")
{
    const double fs = 48000.0;
    BandParameters p; p.type = FilterType::HighShelf; p.frequencyHz = 3000.0; p.gainDb = 6.0;

    EQBand stereo, mono;
    stereo.setTarget (p); stereo.prepare (fs);
    mono.setTarget (p);   mono.prepare (fs);

    auto l = sine (5000.0, fs, 4800);
    std::vector<double> r (4800, 0.0);  // silent right channel
    auto m = l;

    double* ch[2] = { l.data(), r.data() };
    stereo.process (ch, 2, 4800);
    double* mch[1] = { m.data() };
    mono.process (mch, 1, 4800);

    double maxDiff = 0.0, maxRight = 0.0;
    for (size_t i = 0; i < l.size(); ++i)
    {
        maxDiff = std::max (maxDiff, std::abs (l[i] - m[i]));
        maxRight = std::max (maxRight, std::abs (r[i]));
    }
    CHECK (maxDiff == 0.0);
    CHECK (maxRight == 0.0);
}

TEST_CASE ("Sample-rate change re-designs coefficients correctly")
{
    BandParameters p; p.type = FilterType::Bell; p.frequencyHz = 15000.0; p.gainDb = 10.0; p.q = 2.0;
    EQBand band;
    band.setTarget (p);
    band.prepare (44100.0);
    CHECK_NEAR (band.currentCoefficients().magnitudeDb (15000.0, 44100.0), 10.0, 1.0e-9);
    band.prepare (192000.0);
    CHECK_NEAR (band.currentCoefficients().magnitudeDb (15000.0, 192000.0), 10.0, 1.0e-9);

    // 30 kHz request: clamped at 44.1k, honoured at 96k and above
    p.frequencyHz = 30000.0;
    band.setTarget (p); band.prepare (44100.0);
    CHECK_NEAR (band.currentParameters().frequencyHz, 0.49 * 44100.0, 1.0e-6);
    band.setTarget (p); band.prepare (96000.0);
    CHECK_NEAR (band.currentParameters().frequencyHz, 30000.0, 1.0e-6);
}

TEST_CASE ("A disabled band is bit-transparent")
{
    BandParameters p; p.type = FilterType::LowPass; p.frequencyHz = 200.0; p.slopeDbPerOct = 96; p.enabled = false;
    EQBand band;
    band.setTarget (p);
    band.prepare (48000.0);
    auto x = sine (1000.0, 48000.0, 2048);
    const auto orig = x;
    runBand (band, x, 0, x.size());
    CHECK (x == orig);
}
