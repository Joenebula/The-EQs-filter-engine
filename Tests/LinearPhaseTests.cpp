// Tests for the linear-phase mode (1.5): kernel design and convolution.

#include "TestFramework.h"
#include "../Source/DSP/LinearPhase.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace peq;

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double fs = 48000.0;

BandParameters bell (double f, double g, double q)
{
    BandParameters p;
    p.type = FilterType::Bell; p.frequencyHz = f; p.gainDb = g; p.q = q; p.analogMatched = true;
    return p;
}

/** Magnitude (dB) of a real kernel at frequency f. */
double kernelDb (const std::vector<double>& h, double f)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < h.size(); ++i)
    {
        const double ph = 2.0 * pi * f * (double) i / fs;
        re += h[i] * std::cos (ph);
        im -= h[i] * std::sin (ph);
    }
    return 20.0 * std::log10 (std::sqrt (re * re + im * im));
}

/** Runs a signal through a prepared FIR in uneven blocks. */
std::vector<double> run (LinearPhaseFIR& fir, std::vector<double> x)
{
    size_t pos = 0;
    int block = 37;
    while (pos < x.size())
    {
        const int n = (int) std::min<size_t> ((size_t) block, x.size() - pos);
        double* ch[1] = { x.data() + pos };
        fir.process (ch, 1, n);
        pos += (size_t) n;
        block = block == 37 ? 512 : (block == 512 ? 5 : 37);
    }
    return x;
}
} // namespace

TEST_CASE ("Linear phase: kernel length follows the sample rate")
{
    CHECK (LinearPhaseFIR::kernelLengthFor (44100.0) == 8192);
    CHECK (LinearPhaseFIR::kernelLengthFor (48000.0) == 8192);
    CHECK (LinearPhaseFIR::kernelLengthFor (96000.0) == 16384);
    CHECK (LinearPhaseFIR::kernelLengthFor (192000.0) == 32768);
}

TEST_CASE ("Linear phase: kernel is symmetric and matches the bands' curve")
{
    std::vector<BandParameters> bands { bell (1000.0, 6.0, 1.0), bell (5000.0, -4.0, 2.0) };
    BandParameters hp;
    hp.type = FilterType::HighPass; hp.frequencyHz = 80.0; hp.slopeDbPerOct = 24; hp.analogMatched = true;
    bands.push_back (hp);

    std::vector<double> h;
    LinearPhaseFIR::designKernel (bands.data(), (int) bands.size(), fs, 8192, h);
    CHECK ((int) h.size() == 8192);

    // Symmetric about n/2 (exact linear phase): h[n/2 + k] == h[n/2 - k].
    double asym = 0.0, peak = 0.0;
    for (int k = 1; k < 4096; ++k)
    {
        asym = std::max (asym, std::abs (h[(size_t) (4096 + k)] - h[(size_t) (4096 - k)]));
        peak = std::max (peak, std::abs (h[(size_t) (4096 + k)]));
    }
    CHECK (asym < 1e-12 * std::max (1.0, peak) + 1e-15);

    // Magnitude matches the IIR design within 0.3 dB above 150 Hz.
    double worst = 0.0;
    for (double f : { 150.0, 300.0, 700.0, 1000.0, 2000.0, 3000.0, 5000.0, 8000.0, 12000.0, 18000.0 })
    {
        double ref = 0.0;
        CascadeCoefficients c;
        for (const auto& b : bands)
        {
            FilterDesign::design (b, fs, c);
            ref += c.magnitudeDb (f, fs);
        }
        worst = std::max (worst, std::abs (kernelDb (h, f) - ref));
    }
    std::printf ("    linear-phase kernel vs IIR curve, 150 Hz - 18 kHz: worst %.3f dB\n", worst);
    CHECK (worst < 0.3);
}

TEST_CASE ("Linear phase: flat kernel is a pure delay of n/2 + partition")
{
    LinearPhaseFIR fir;
    fir.prepare (fs);
    CHECK (fir.latencySamples() == 4096 + LinearPhaseFIR::partitionSize);
    std::vector<double> h;
    LinearPhaseFIR::designKernel (nullptr, 0, fs, fir.kernelLength(), h);
    CHECK (fir.setKernel (h));

    std::vector<double> x (20000, 0.0);
    x[1000] = 1.0;
    const auto y = run (fir, x);
    CHECK (fir.hasKernel());
    const int expected = 1000 + fir.latencySamples();
    CHECK_NEAR (y[(size_t) expected], 1.0, 1e-9);
    double rest = 0.0;
    for (size_t i = 0; i < y.size(); ++i)
        if ((int) i != expected)
            rest = std::max (rest, std::abs (y[i]));
    CHECK (rest < 1e-9);
}

TEST_CASE ("Linear phase: convolution gives the designed gain on a sine, same delay at every frequency")
{
    std::vector<BandParameters> bands { bell (1000.0, 6.0, 0.7) };
    LinearPhaseFIR fir;
    fir.prepare (fs);
    std::vector<double> h;
    LinearPhaseFIR::designKernel (bands.data(), 1, fs, fir.kernelLength(), h);
    CHECK (fir.setKernel (h));

    for (double f : { 200.0, 1000.0, 6000.0 })
    {
        fir.reset();
        const size_t n = 48000;
        std::vector<double> x (n);
        for (size_t i = 0; i < n; ++i)
            x[i] = 0.25 * std::sin (2.0 * pi * f * (double) i / fs);
        const auto y = run (fir, x);
        // Compare against the input delayed by the latency: same phase, gain as designed.
        const size_t d = (size_t) fir.latencySamples();
        double re = 0.0, im = 0.0, rx = 0.0, ix = 0.0;
        for (size_t i = n / 2; i < n; ++i)
        {
            const double ph = 2.0 * pi * f * (double) (i - d) / fs;
            re += y[i] * std::cos (ph); im += y[i] * std::sin (ph);
            rx += x[i - d] * std::cos (ph); ix += x[i - d] * std::sin (ph);
        }
        const double gainDb = 20.0 * std::log10 (std::sqrt (re * re + im * im) / std::sqrt (rx * rx + ix * ix));
        const double phaseDiff = std::atan2 (im, re) - std::atan2 (ix, rx);
        CascadeCoefficients c;
        FilterDesign::design (bands[0], fs, c);
        CHECK_NEAR (gainDb, c.magnitudeDb (f, fs), 0.1);
        CHECK (std::abs (std::remainder (phaseDiff, 2.0 * pi)) < 0.01);   // no phase shift
    }
}

TEST_CASE ("Linear phase: switching kernels while playing is smooth")
{
    LinearPhaseFIR fir;
    fir.prepare (fs);
    std::vector<BandParameters> a { bell (1000.0, 0.0, 1.0) }, b { bell (1000.0, 12.0, 1.0) };
    std::vector<double> ha, hb;
    LinearPhaseFIR::designKernel (a.data(), 1, fs, fir.kernelLength(), ha);
    LinearPhaseFIR::designKernel (b.data(), 1, fs, fir.kernelLength(), hb);
    CHECK (fir.setKernel (ha));
    CHECK (! fir.setKernel (hb));   // first one not picked up yet

    const size_t n = 48000;
    std::vector<double> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = 0.2 * std::sin (2.0 * pi * 1000.0 * (double) i / fs);
    double maxStep = 0.0, prev = 0.0;
    size_t pos = 0;
    bool switched = false;
    while (pos < n)
    {
        const int len = (int) std::min<size_t> (300, n - pos);
        double* ch[1] = { x.data() + pos };
        fir.process (ch, 1, len);
        for (int i = 0; i < len; ++i)
        {
            CHECK (std::isfinite (x[pos + (size_t) i]));
            if (pos + (size_t) i > 10000)
                maxStep = std::max (maxStep, std::abs (x[pos + (size_t) i] - prev));
            prev = x[pos + (size_t) i];
        }
        pos += (size_t) len;
        if (! switched && pos > 20000)
            switched = fir.setKernel (hb);
    }
    CHECK (switched);
    // A 1 kHz sine at up to 0.8 (+12 dB of 0.2) moves at most 0.8 * 2 pi * 1000 / 48000 = 0.105 per sample.
    CHECK (maxStep < 0.11);
}
