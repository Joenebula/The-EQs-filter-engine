// Automated tests for the dynamic EQ (Phase 6): gain curve, detector
// selectivity, cut and boost, attack and release timing, smoothness, and
// switching dynamics off.

#include "TestFramework.h"
#include "../Source/DSP/FilterEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace peq;

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double fs = 48000.0;

double amplitudeAt (const std::vector<double>& x, size_t start, size_t length, double f)
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

std::vector<double> tone (double f, size_t n, double amp)
{
    std::vector<double> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = amp * std::sin (2.0 * pi * f * (double) i / fs);
    return x;
}

double toDb (double a) { return 20.0 * std::log10 (a); }

EQBand makeBand (FilterType type, double freq, double gain, double q, DynamicSettings d)
{
    EQBand b;
    BandParameters p;
    p.type = type; p.frequencyHz = freq; p.gainDb = gain; p.q = q;
    b.setTarget (p);
    b.setDynamics (d);
    b.prepare (fs);
    return b;
}

/** Processes in 256-sample blocks; optionally records the offset per block. */
void runMono (EQBand& b, std::vector<double>& x, size_t from, size_t to, std::vector<double>* offsets = nullptr)
{
    for (size_t pos = from; pos < to; pos += 256)
    {
        const int n = (int) std::min<size_t> (256, to - pos);
        double* ch[1] = { x.data() + pos };
        b.process (ch, 1, n);
        if (offsets != nullptr)
            offsets->push_back (b.currentDynamicOffsetDb());
    }
}
} // namespace

TEST_CASE ("Dynamics: gain curve (threshold, soft knee, slope, range)")
{
    DynamicSettings d;
    d.thresholdDb = -30.0;
    d.rangeDb = -12.0;

    CHECK_NEAR (dynamics::gainOffset (-60.0, d), 0.0, 1e-12);
    CHECK_NEAR (dynamics::gainOffset (-33.0, d), 0.0, 1e-12);            // knee starts 3 dB below
    CHECK (dynamics::gainOffset (-30.0, d) < 0.0);                        // inside the knee
    CHECK_NEAR (dynamics::gainOffset (-20.0, d), -5.0, 1e-9);             // 10 dB over -> 5 dB
    CHECK_NEAR (dynamics::gainOffset (0.0, d), -12.0, 1e-9);              // capped at range

    d.rangeDb = 8.0;
    CHECK_NEAR (dynamics::gainOffset (-20.0, d), 5.0, 1e-9);
    CHECK_NEAR (dynamics::gainOffset (0.0, d), 8.0, 1e-9);

    // Continuous through the knee.
    double prev = dynamics::gainOffset (-40.0, d), maxStep = 0.0;
    for (double l = -40.0; l <= 0.0; l += 0.01)
    {
        const double g = dynamics::gainOffset (l, d);
        maxStep = std::max (maxStep, std::abs (g - prev));
        prev = g;
    }
    CHECK (maxStep < 0.01);
}

TEST_CASE ("Dynamics: loud tone in the band is cut by the range, quiet tone untouched")
{
    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -30.0; d.rangeDb = -12.0; d.attackMs = 5.0; d.releaseMs = 100.0;

    const size_t n = (size_t) fs;  // 1 s

    // Loud: -6 dBFS, 24 dB over -> 12 dB cut (capped by range).
    {
        auto b = makeBand (FilterType::Bell, 2000.0, 0.0, 1.0, d);
        auto x = tone (2000.0, n, 0.5);
        runMono (b, x, 0, n);
        const double outDb = toDb (amplitudeAt (x, n - 9600, 9600, 2000.0));
        CHECK_NEAR (outDb, toDb (0.5) - 12.0, 0.3);
        CHECK_NEAR (b.currentDynamicOffsetDb(), -12.0, 0.05);
        CHECK_NEAR (b.currentDetectorLevelDb(), toDb (0.5), 0.5);   // a sine reads its peak level
    }

    // Quiet: -50 dBFS, well below threshold -> unchanged.
    {
        auto b = makeBand (FilterType::Bell, 2000.0, 0.0, 1.0, d);
        auto x = tone (2000.0, n, 0.00316);
        runMono (b, x, 0, n);
        CHECK_NEAR (toDb (amplitudeAt (x, n - 9600, 9600, 2000.0)), toDb (0.00316), 0.05);
        CHECK_NEAR (b.currentDynamicOffsetDb(), 0.0, 1e-9);
    }

    // Static gain plus dynamics: +6 dB bell, range -12 -> net -6 dB when loud.
    {
        auto b = makeBand (FilterType::Bell, 2000.0, 6.0, 1.0, d);
        auto x = tone (2000.0, n, 0.5);
        runMono (b, x, 0, n);
        CHECK_NEAR (toDb (amplitudeAt (x, n - 9600, 9600, 2000.0)), toDb (0.5) - 6.0, 0.3);
    }
}

TEST_CASE ("Dynamics: positive range boosts when loud (upward)")
{
    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -20.0; d.rangeDb = 6.0; d.attackMs = 5.0; d.releaseMs = 100.0;

    const size_t n = (size_t) fs;
    auto b = makeBand (FilterType::Bell, 500.0, 0.0, 1.0, d);
    auto x = tone (500.0, n, 0.25);   // -12 dBFS, 8 dB over -> +4 dB
    runMono (b, x, 0, n);
    CHECK_NEAR (toDb (amplitudeAt (x, n - 9600, 9600, 500.0)), toDb (0.25) + 4.0, 0.3);
}

TEST_CASE ("Dynamics: detector only hears the band's own region")
{
    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -30.0; d.rangeDb = -12.0;

    const size_t n = (size_t) fs / 2;

    // Bell at 5 kHz, Q 4: a loud 100 Hz tone must not trigger it.
    auto bell = makeBand (FilterType::Bell, 5000.0, 0.0, 4.0, d);
    auto x = tone (100.0, n, 0.5);
    runMono (bell, x, 0, n);
    CHECK (bell.currentDynamicOffsetDb() > -0.1);

    // High shelf at 8 kHz hears highs, not lows; low shelf at 150 Hz the reverse.
    auto hs = makeBand (FilterType::HighShelf, 8000.0, 0.0, 0.7, d);
    auto lows = tone (60.0, n, 0.5);
    runMono (hs, lows, 0, n);
    CHECK (hs.currentDynamicOffsetDb() > -0.1);

    auto hs2 = makeBand (FilterType::HighShelf, 8000.0, 0.0, 0.7, d);
    auto highs = tone (14000.0, n, 0.5);
    runMono (hs2, highs, 0, n);
    CHECK (hs2.currentDynamicOffsetDb() < -11.0);

    auto ls = makeBand (FilterType::LowShelf, 150.0, 0.0, 0.7, d);
    auto lows2 = tone (60.0, n, 0.5);
    runMono (ls, lows2, 0, n);
    CHECK (ls.currentDynamicOffsetDb() < -11.0);
}

TEST_CASE ("Dynamics: attack and release times")
{
    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -30.0; d.rangeDb = -12.0; d.attackMs = 5.0; d.releaseMs = 100.0;

    auto b = makeBand (FilterType::Bell, 1000.0, 0.0, 1.0, d);
    const size_t onLen = (size_t) (0.5 * fs), offLen = (size_t) (1.5 * fs);
    auto x = tone (1000.0, onLen + offLen, 0.5);
    std::fill (x.begin() + (long) onLen, x.end(), 0.0);

    std::vector<double> offsets;   // one per 256 samples (5.33 ms)
    runMono (b, x, 0, x.size(), &offsets);
    const auto at = [&] (double seconds) { return offsets[(size_t) (seconds * fs / 256.0)]; };

    CHECK (at (0.050) < -11.5);                // reaches the range within 50 ms (10 attack times)
    CHECK (at (0.5 + 0.030) < -8.0);           // 30 ms after the tone stops, still holding
    CHECK (at (0.5 + 0.300) > at (0.5 + 0.030)); // ... and recovering
    CHECK (at (0.5 + 1.200) > -0.05);          // back to normal after ~1 s

    // Slower release keeps the cut longer.
    d.releaseMs = 1000.0;
    auto slow = makeBand (FilterType::Bell, 1000.0, 0.0, 1.0, d);
    auto y = tone (1000.0, onLen + offLen, 0.5);
    std::fill (y.begin() + (long) onLen, y.end(), 0.0);
    std::vector<double> slowOffsets;
    runMono (slow, y, 0, y.size(), &slowOffsets);
    CHECK (slowOffsets[(size_t) ((0.5 + 0.3) * fs / 256.0)] < at (0.5 + 0.3) - 3.0);
}

TEST_CASE ("Dynamics: gain moves smoothly (no clicks) with a fast attack")
{
    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -40.0; d.rangeDb = -20.0; d.attackMs = 0.1; d.releaseMs = 5.0;

    // A tone that jumps in level every 50 ms: the dynamics slam up and down.
    const double f = 300.0;
    const size_t n = (size_t) fs;
    std::vector<double> x (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double amp = ((i / 2400) % 2 == 0) ? 0.5 : 0.01;
        x[i] = amp * std::sin (2.0 * pi * f * (double) i / fs);
    }
    std::vector<double> in = x;

    auto b = makeBand (FilterType::Bell, f, 0.0, 1.0, d);
    runMono (b, x, 0, n);

    // The largest sample step must stay near that of the input itself.
    double maxIn = 0.0, maxOut = 0.0;
    bool finite = true;
    for (size_t i = 1; i < n; ++i)
    {
        maxIn  = std::max (maxIn, std::abs (in[i] - in[i - 1]));
        maxOut = std::max (maxOut, std::abs (x[i] - x[i - 1]));
        finite = finite && std::isfinite (x[i]);
    }
    CHECK (finite);
    CHECK (maxOut <= maxIn * 1.05);
}

TEST_CASE ("Dynamics: switching off returns to static gain; non-gain types ignore it")
{
    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -30.0; d.rangeDb = -12.0; d.attackMs = 5.0; d.releaseMs = 50.0;

    const size_t n = (size_t) fs;
    auto b = makeBand (FilterType::Bell, 2000.0, 3.0, 1.0, d);
    auto x = tone (2000.0, 2 * n, 0.5);
    runMono (b, x, 0, n);
    CHECK (b.currentDynamicOffsetDb() < -11.0);

    d.enabled = false;
    b.setDynamics (d);
    runMono (b, x, n, 2 * n);
    CHECK_NEAR (b.currentDynamicOffsetDb(), 0.0, 1e-12);
    CHECK_NEAR (toDb (amplitudeAt (x, 2 * n - 9600, 9600, 2000.0)), toDb (0.5) + 3.0, 0.05);

    // A low-pass has no gain: dynamics have no effect.
    d.enabled = true;
    auto lp = makeBand (FilterType::LowPass, 20000.0, 0.0, 0.707, d);
    auto y = tone (2000.0, n, 0.5);
    runMono (lp, y, 0, n);
    CHECK_NEAR (lp.currentDynamicOffsetDb(), 0.0, 1e-12);
}

TEST_CASE ("Dynamics: engine routes detection per channel mode (Side band ignores mono)")
{
    FilterEngine e;
    BandParameters p;
    p.type = FilterType::Bell; p.frequencyHz = 1000.0; p.q = 1.0;
    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -30.0; d.rangeDb = -10.0;

    e.setBand (0, p, ChannelMode::Side);
    e.setBandDynamics (0, d);
    e.setBand (1, p, ChannelMode::Mid);
    e.setBandDynamics (1, d);
    e.prepare (fs);

    // Identical left and right: all mid, no side.
    const size_t n = (size_t) fs / 2;
    auto l = tone (1000.0, n, 0.5), r = l;
    for (size_t pos = 0; pos < n; pos += 512)
    {
        double* ch[2] = { l.data() + pos, r.data() + pos };
        e.process (ch, 2, (int) std::min<size_t> (512, n - pos));
    }
    CHECK (e.dynamicOffsetDb (0) > -0.1);    // side band: nothing to hear
    CHECK (e.dynamicOffsetDb (1) < -9.5);    // mid band: loud
}

TEST_CASE ("Dynamics: ratio sets the slope (1:1 none, 2:1 default, 4:1, 20:1)")
{
    DynamicSettings d;
    d.thresholdDb = -30.0;
    d.rangeDb = -30.0;
    CHECK_NEAR (d.ratio, 2.0, 1e-12);                                      // default = old fixed 2:1
    CHECK_NEAR (dynamics::gainOffset (-10.0, d), -10.0, 1e-9);             // 20 dB over at 2:1 -> 10 dB
    d.ratio = 4.0;
    CHECK_NEAR (dynamics::gainOffset (-10.0, d), -15.0, 1e-9);             // 20 dB over at 4:1 -> 15 dB
    d.ratio = 20.0;
    CHECK_NEAR (dynamics::gainOffset (-10.0, d), -19.0, 1e-9);
    d.ratio = 1.0;
    CHECK_NEAR (dynamics::gainOffset (0.0, d), 0.0, 1e-12);                // 1:1 does nothing
    d.ratio = 4.0; d.rangeDb = 30.0;
    CHECK_NEAR (dynamics::gainOffset (-10.0, d), 15.0, 1e-9);              // upward uses it too

    d.ratio = 0.2;
    CHECK_NEAR (dynamics::sanitise (d).ratio, dynamics::minRatio, 1e-12);
    d.ratio = 500.0;
    CHECK_NEAR (dynamics::sanitise (d).ratio, dynamics::maxRatio, 1e-12);
    d.ratio = std::nan ("");
    CHECK_NEAR (dynamics::sanitise (d).ratio, dynamics::defaultRatio, 1e-12);

    // Through a real band: a -6 dBFS tone, threshold -30, 4:1 -> 18 dB cut.
    DynamicSettings live;
    live.enabled = true; live.thresholdDb = -30.0; live.rangeDb = -30.0; live.ratio = 4.0;
    live.attackMs = 5.0; live.releaseMs = 100.0;
    const size_t n = (size_t) fs;
    auto b = makeBand (FilterType::Bell, 2000.0, 0.0, 1.0, live);
    auto x = tone (2000.0, n, 0.5);
    runMono (b, x, 0, n);
    CHECK_NEAR (b.currentDynamicOffsetDb(), -18.0, 0.3);
    CHECK_NEAR (toDb (amplitudeAt (x, n - 9600, 9600, 2000.0)), toDb (0.5) - 18.0, 0.5);
}

TEST_CASE ("Dynamics: external sidechain drives the band (ducking), own signal ignored")
{
    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -30.0; d.rangeDb = -10.0; d.attackMs = 5.0; d.releaseMs = 50.0;
    d.externalSidechain = true;

    const size_t n = (size_t) fs;
    const size_t half = n / 2;

    // Main: a steady loud 100 Hz tone (would trigger the band on its own).
    // Sidechain: a loud 100 Hz "kick" for the first half second, then silence.
    auto b = makeBand (FilterType::Bell, 100.0, 0.0, 1.0, d);
    auto x = tone (100.0, n, 0.5);
    auto sc = tone (100.0, n, 0.5);
    std::fill (sc.begin() + (long) half, sc.end(), 0.0);

    std::vector<double> offsets;
    for (size_t pos = 0; pos < n; pos += 256)
    {
        const int len = (int) std::min<size_t> (256, n - pos);
        double* ch[1] = { x.data() + pos };
        const double* s[1] = { sc.data() + pos };
        b.setSidechain (s, 1);
        b.process (ch, 1, len);
        b.setSidechain (nullptr, 0);
        offsets.push_back (b.currentDynamicOffsetDb());
    }
    const size_t blocksPerHalf = half / 256;
    CHECK (offsets[blocksPerHalf - 1] < -9.5);            // sidechain loud: ducked by the range
    CHECK (offsets.back() > -0.1);                        // sidechain silent: back to set gain,
                                                          // although the main signal stays loud
    CHECK_NEAR (toDb (amplitudeAt (x, half - 9600, 9600, 100.0)), toDb (0.5) - 10.0, 0.5);
    CHECK_NEAR (toDb (amplitudeAt (x, n - 9600, 9600, 100.0)), toDb (0.5), 0.1);
}

TEST_CASE ("Dynamics: external sidechain with nothing connected leaves the band static")
{
    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -30.0; d.rangeDb = -10.0; d.externalSidechain = true;

    FilterEngine e;
    BandParameters p;
    p.type = FilterType::Bell; p.frequencyHz = 1000.0; p.q = 1.0;
    e.setBand (0, p, ChannelMode::Stereo);
    e.setBandDynamics (0, d);
    e.prepare (fs);

    const size_t n = (size_t) fs / 2;
    auto l = tone (1000.0, n, 0.5), r = l;
    for (size_t pos = 0; pos < n; pos += 512)
    {
        double* ch[2] = { l.data() + pos, r.data() + pos };
        e.process (ch, 2, (int) std::min<size_t> (512, n - pos));   // no sidechain passed
    }
    CHECK_NEAR (e.dynamicOffsetDb (0), 0.0, 1e-9);

    // Same band through the engine with a stereo sidechain that is loud only
    // on the right: linked detection still ducks.
    auto l2 = tone (1000.0, n, 0.5), r2 = l2;
    std::vector<double> scL (n, 0.0);
    auto scR = tone (1000.0, n, 0.5);
    for (size_t pos = 0; pos < n; pos += 512)
    {
        const int len = (int) std::min<size_t> (512, n - pos);
        double* ch[2] = { l2.data() + pos, r2.data() + pos };
        const double* s[2] = { scL.data() + pos, scR.data() + pos };
        e.process (ch, 2, len, s, 2);
    }
    CHECK (e.dynamicOffsetDb (0) < -9.5);
}

TEST_CASE ("Dynamics: switching the detector source mid-stream stays smooth and finite")
{
    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -40.0; d.rangeDb = -12.0; d.attackMs = 1.0; d.releaseMs = 20.0;

    auto b = makeBand (FilterType::Bell, 500.0, 0.0, 1.0, d);
    const size_t n = (size_t) fs;
    auto x = tone (500.0, n, 0.3);
    auto sc = tone (500.0, n, 0.01);   // quiet sidechain
    double maxStep = 0.0, prev = 0.0;
    for (size_t pos = 0; pos < n; pos += 128)
    {
        if (pos == n / 2)
        {
            d.externalSidechain = true;
            b.setDynamics (d);
        }
        const int len = (int) std::min<size_t> (128, n - pos);
        double* ch[1] = { x.data() + pos };
        const double* s[1] = { sc.data() + pos };
        b.setSidechain (s, 1);
        b.process (ch, 1, len);
        for (int i = 0; i < len; ++i)
        {
            CHECK (std::isfinite (x[pos + (size_t) i]));
            if (pos + (size_t) i > 0)
                maxStep = std::max (maxStep, std::abs (x[pos + (size_t) i] - prev));
            prev = x[pos + (size_t) i];
        }
    }
    // A 500 Hz sine at 0.3 moves at most 0.3 * 2 pi * 500 / 48000 = 0.0196 per sample.
    CHECK (maxStep < 0.025);
}

TEST_CASE ("Dynamics: look-ahead input lets the band act before a burst arrives")
{
    // Silence, then a loud 2 kHz burst. The processed audio is the burst
    // delayed by 5 ms; the detector hears the undelayed burst.
    const int delay = 240;   // 5 ms at 48 kHz
    const size_t n = 9600;
    const size_t onset = 4800;
    std::vector<double> raw (n, 0.0);
    for (size_t i = onset; i < n; ++i)
        raw[i] = 0.5 * std::sin (2.0 * pi * 2000.0 * (double) (i - onset) / fs);
    std::vector<double> delayed (n, 0.0);
    for (size_t i = (size_t) delay; i < n; ++i)
        delayed[i] = raw[i - (size_t) delay];

    DynamicSettings d;
    d.enabled = true; d.thresholdDb = -30.0; d.rangeDb = -12.0; d.attackMs = 1.0; d.releaseMs = 100.0;
    BandParameters p;
    p.type = FilterType::Bell; p.frequencyHz = 2000.0; p.q = 1.0;

    auto firstMsDb = [&] (bool useLookahead)
    {
        FilterEngine e;
        e.setBand (0, p, ChannelMode::Stereo);
        e.setBandDynamics (0, d);
        e.prepare (fs);
        auto l = delayed, r = delayed;
        for (size_t pos = 0; pos < n; pos += 64)
        {
            const int len = (int) std::min<size_t> (64, n - pos);
            double* ch[2] = { l.data() + pos, r.data() + pos };
            FilterEngine::LookaheadInput la;
            la.lr[0] = la.lr[1] = raw.data() + pos;
            la.ms[0] = raw.data() + pos;
            la.ms[1] = raw.data() + pos;   // unused for a stereo band
            e.process (ch, 2, len, nullptr, 0, useLookahead ? &la : nullptr);
        }
        // Level of the first 2 ms of the burst as it leaves the band.
        double peak = 0.0;
        for (size_t i = onset + (size_t) delay; i < onset + (size_t) delay + 96; ++i)
            peak = std::max (peak, std::abs (l[i]));
        return toDb (peak);
    };
    const double without = firstMsDb (false);
    const double with = firstMsDb (true);
    std::printf ("    look-ahead burst onset: %.2f dB without, %.2f dB with\n", without, with);
    // Without look-ahead the burst's start gets through almost untouched;
    // with it the band is already turned down when the burst arrives.
    CHECK (without > toDb (0.5) - 3.0);
    CHECK (with < without - 4.0);   // measured: about 5.5 dB lower (-6.3 vs -11.8 dB)
}
