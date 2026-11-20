// Tests for the spectrum analyser (Phase 5): FFT correctness, level
// calibration, smoothing, peak hold, fractional-octave averaging and the
// lock-free audio -> GUI sample FIFO.

#include "TestFramework.h"
#include "../Source/DSP/SampleFifo.h"
#include "../Source/DSP/SpectrumAnalyser.h"

#include <atomic>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

using namespace peq;

namespace
{
constexpr double pi = 3.14159265358979323846;

std::vector<float> sineF (double f, double fs, int n, double amp)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = (float) (amp * std::sin (2.0 * pi * f * i / fs));
    return x;
}

int peakBin (const std::vector<double>& db)
{
    int best = 0;
    for (int i = 1; i < (int) db.size(); ++i)
        if (db[(size_t) i] > db[(size_t) best])
            best = i;
    return best;
}
}

TEST_CASE ("Analyser: FFT matches a direct DFT")
{
    for (int order : { 3, 6, 10 })
    {
        FFT fft (order);
        const int n = fft.size();
        std::mt19937 rng ((unsigned) order);
        std::uniform_real_distribution<double> d (-1.0, 1.0);
        std::vector<std::complex<double>> x ((size_t) n);
        for (auto& v : x) v = { d (rng), d (rng) };
        auto y = x;
        fft.perform (y);

        double maxErr = 0.0;
        for (int k = 0; k < n; ++k)
        {
            std::complex<double> s = 0.0;
            for (int t = 0; t < n; ++t)
                s += x[(size_t) t] * std::polar (1.0, -2.0 * pi * k * t / n);
            maxErr = std::max (maxErr, std::abs (s - y[(size_t) k]));
        }
        CHECK (maxErr < 1.0e-9 * n);
    }
}

TEST_CASE ("Analyser: a sine reads its level at its frequency")
{
    const double fs = 48000.0;
    for (int order : { 11, 13 })
    {
        SpectrumAnalyser a;
        a.configure (order, fs);
        const int n = a.fftSize();
        for (double amp : { 1.0, 0.1 })
        {
            // Centre the sine on a bin so there is no scalloping loss.
            const int bin = n / 16;
            const double f = bin * fs / n;
            a.reset();
            auto x = sineF (f, fs, n, amp);
            a.addSamples (x.data(), n);
            CHECK (a.computeIfReady (0.0));
            CHECK (peakBin (a.levelsDb()) == bin);
            CHECK_NEAR (a.levelsDb()[(size_t) bin], 20.0 * std::log10 (amp), 0.01);
            // Far from the sine the Hann window keeps leakage very low.
            CHECK (a.levelsDb()[(size_t) (bin * 4)] < 20.0 * std::log10 (amp) - 90.0);
        }
    }
}

TEST_CASE ("Analyser: silence reads the floor; release falls at the set speed")
{
    SpectrumAnalyser a;
    a.configure (11, 48000.0);
    a.setReleaseSeconds (1.0);  // 20 dB per second
    const int n = a.fftSize();
    const int bin = 64;
    auto x = sineF (bin * 48000.0 / n, 48000.0, n, 1.0);
    a.addSamples (x.data(), n);
    a.computeIfReady (0.0);
    CHECK_NEAR (a.levelsDb()[(size_t) bin], 0.0, 0.01);

    // Feed silence: the display falls by 20 dB per second of elapsed time,
    // not instantly.
    std::vector<float> zeros ((size_t) n, 0.0f);
    a.addSamples (zeros.data(), n);
    a.computeIfReady (0.25);
    CHECK_NEAR (a.levelsDb()[(size_t) bin], -5.0, 0.01);
    a.computeIfReady (0.25);
    CHECK_NEAR (a.levelsDb()[(size_t) bin], -10.0, 0.01);

    for (int i = 0; i < 20; ++i)
        a.computeIfReady (1.0);
    CHECK_NEAR (a.levelsDb()[(size_t) bin], SpectrumAnalyser::floorDb, 1e-9);
}

TEST_CASE ("Analyser: peak hold keeps the maximum and decays slowly")
{
    SpectrumAnalyser a;
    a.configure (11, 48000.0);
    a.setReleaseSeconds (0.1);
    a.setPeakHold (true);
    const int n = a.fftSize();
    const int bin = 100;
    auto x = sineF (bin * 48000.0 / n, 48000.0, n, 0.5);
    a.addSamples (x.data(), n);
    a.computeIfReady (0.0);
    const double level = a.levelsDb()[(size_t) bin];

    std::vector<float> zeros ((size_t) n, 0.0f);
    a.addSamples (zeros.data(), n);
    a.computeIfReady (0.5);
    CHECK (a.levelsDb()[(size_t) bin] < level - 50.0);        // live level dropped fast
    CHECK_NEAR (a.peaksDb()[(size_t) bin], level - 3.0, 0.01); // peak fell 6 dB/s * 0.5 s

    a.setPeakHold (false);
    CHECK_NEAR (a.peaksDb()[(size_t) bin], SpectrumAnalyser::floorDb, 1e-9);
}

TEST_CASE ("Analyser: fractional-octave averaging")
{
    // Flat spectrum stays flat at any smoothing; a single loud bin is spread
    // out (lower peak) the wider the smoothing.
    std::vector<double> flat (1025, -40.0);
    for (double oct : { 0.0, 1.0 / 12.0, 1.0 / 3.0, 1.0 })
        CHECK_NEAR (SpectrumAnalyser::levelAt (flat, 1000.0, 23.4375, oct), -40.0, 1e-9);

    std::vector<double> spike (1025, SpectrumAnalyser::floorDb);
    spike[400] = 0.0;
    const double bw = 10.0, f = 4000.0;
    const double none = SpectrumAnalyser::levelAt (spike, f, bw, 0.0);
    const double narrow = SpectrumAnalyser::levelAt (spike, f, bw, 1.0 / 12.0);
    const double wide = SpectrumAnalyser::levelAt (spike, f, bw, 1.0);
    CHECK_NEAR (none, 0.0, 1e-9);
    CHECK (narrow < none && wide < narrow);
    CHECK (wide > -40.0);  // still visible

    // The fast prefix-sum version agrees with the direct one.
    std::mt19937 rng (5);
    std::uniform_real_distribution<double> d (-120.0, 0.0);
    std::vector<double> noisy (2049);
    for (auto& v : noisy) v = d (rng);
    std::vector<double> prefix;
    SpectrumAnalyser::buildPowerPrefix (noisy, prefix);
    double maxDiff = 0.0;
    for (double oct : { 1.0 / 12.0, 1.0 / 6.0, 1.0 / 3.0, 1.0 })
        for (double hz = 20.0; hz < 24000.0; hz *= 1.1)
            maxDiff = std::max (maxDiff, std::abs (SpectrumAnalyser::levelAt (noisy, hz, 11.71875, oct)
                                                   - SpectrumAnalyser::levelAtPrefix (prefix, hz, 11.71875, oct)));
    CHECK (maxDiff < 1.0e-6);
}

TEST_CASE ("Analyser FIFO: order preserved, overflow drops, works across threads")
{
    SampleFifo fifo;
    fifo.setCapacity (100);
    CHECK (fifo.capacity() >= 100);

    std::vector<float> in (300);
    for (size_t i = 0; i < in.size(); ++i) in[i] = (float) i;
    const size_t written = fifo.push (in.data(), in.size());
    CHECK (written == fifo.capacity());   // the rest is dropped, no blocking
    std::vector<float> out (300, -1.0f);
    const size_t read = fifo.pop (out.data(), out.size());
    CHECK (read == written);
    bool inOrder = true;
    for (size_t i = 0; i < read; ++i)
        inOrder = inOrder && out[i] == (float) i;
    CHECK (inOrder);
    CHECK (fifo.available() == 0);

    // Concurrent producer/consumer: every sample that was accepted arrives
    // exactly once and in order.
    fifo.setCapacity (1024);
    std::atomic<bool> done { false };
    std::vector<float> received;
    received.reserve (200000);
    std::thread consumer ([&]
    {
        std::vector<float> buf (300);
        while (! done.load() || fifo.available() > 0)
        {
            const size_t n = fifo.pop (buf.data(), buf.size());
            received.insert (received.end(), buf.begin(), buf.begin() + (long) n);
        }
    });
    std::vector<float> sent;
    float next = 0.0f;
    for (int block = 0; block < 2000; ++block)
    {
        float chunk[64];
        for (float& c : chunk) c = next++;
        const size_t n = fifo.push (chunk, 64);
        sent.insert (sent.end(), chunk, chunk + n);
        if (n < 64)
            next = chunk[n];  // dropped samples are skipped, keep values unique
    }
    done.store (true);
    consumer.join();
    CHECK (received == sent);
    CHECK (! received.empty());
}
