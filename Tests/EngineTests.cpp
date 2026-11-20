// Automated tests for the multi-band engine (Phase 3): routing, mid/side,
// band combination and click-free mode changes.

#include "TestFramework.h"
#include "../Source/DSP/FilterEngine.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace peq;

namespace
{
constexpr double pi = 3.14159265358979323846;

std::vector<double> tone (double f, double fs, size_t n, double amp = 0.5, double phase = 0.0)
{
    std::vector<double> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = amp * std::sin (2.0 * pi * f * (double) i / fs + phase);
    return x;
}

std::vector<double> noise (size_t n, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> d (-0.5, 0.5);
    std::vector<double> x (n);
    for (auto& v : x) v = d (rng);
    return x;
}

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

/** Runs a stereo (or mono when r is null) signal through the engine in
    irregular block sizes. */
void run (FilterEngine& e, std::vector<double>& l, std::vector<double>* r, size_t from, size_t to)
{
    size_t pos = from;
    int k = 0;
    while (pos < to)
    {
        const int block = std::min<int> ((int) (to - pos), 1 + (k++ * 89) % 300);
        double* ch[2] = { l.data() + pos, r != nullptr ? r->data() + pos : nullptr };
        e.process (ch, r != nullptr ? 2 : 1, block);
        pos += (size_t) block;
    }
}

/** Same as run() but through a single EQBand (reference). */
void runBand (EQBand& b, std::vector<double>& x)
{
    size_t pos = 0;
    int k = 0;
    while (pos < x.size())
    {
        const int block = std::min<int> ((int) (x.size() - pos), 1 + (k++ * 89) % 300);
        double* ch[1] = { x.data() + pos };
        b.process (ch, 1, block);
        pos += (size_t) block;
    }
}

double maxAbsDiff (const std::vector<double>& a, const std::vector<double>& b)
{
    double m = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        m = std::max (m, std::abs (a[i] - b[i]));
    return m;
}

BandParameters bell (double f, double g, double q = 1.0)
{
    BandParameters p; p.type = FilterType::Bell; p.frequencyHz = f; p.gainDb = g; p.q = q;
    return p;
}

BandParameters off()
{
    BandParameters p; p.enabled = false;
    return p;
}

void allOff (FilterEngine& e)
{
    for (int i = 0; i < FilterEngine::numBands; ++i)
        e.setBand (i, off(), ChannelMode::Stereo);
}

/** High-frequency residue above 4 kHz of a low-tone signal; a click detector. */
double clickLevelDb (const std::vector<double>& y, double fs)
{
    EQBand hp;
    BandParameters p; p.type = FilterType::HighPass; p.frequencyHz = 4000.0; p.slopeDbPerOct = 96;
    hp.setTarget (p);
    hp.prepare (fs);
    std::vector<double> r = y;
    double* ch[1] = { r.data() };
    hp.process (ch, 1, (int) r.size());
    double peak = 0.0;
    for (size_t i = (size_t) (0.05 * fs); i < r.size(); ++i)
        peak = std::max (peak, std::abs (r[i]));
    return 20.0 * std::log10 (std::max (peak, 1.0e-12));
}
} // namespace

TEST_CASE ("Engine: all bands off is bit-transparent (stereo and mono)")
{
    FilterEngine e;
    allOff (e);
    e.prepare (48000.0);
    auto l = noise (9000, 1), r = noise (9000, 2);
    const auto l0 = l, r0 = r;
    run (e, l, &r, 0, l.size());
    CHECK (l == l0);
    CHECK (r == r0);

    auto m = noise (9000, 3);
    const auto m0 = m;
    run (e, m, nullptr, 0, m.size());
    CHECK (m == m0);
}

TEST_CASE ("Engine: Left and Right bands touch only their channel")
{
    const double fs = 48000.0;
    const auto p = bell (2000.0, 9.0, 0.8);

    for (auto mode : { ChannelMode::Left, ChannelMode::Right })
    {
        FilterEngine e;
        allOff (e);
        e.setBand (5, p, mode);
        e.prepare (fs);

        auto l = noise (12000, 10), r = noise (12000, 11);
        const auto l0 = l, r0 = r;
        run (e, l, &r, 0, l.size());

        EQBand ref; ref.setTarget (p); ref.prepare (fs);
        auto expected = mode == ChannelMode::Left ? l0 : r0;
        runBand (ref, expected);

        if (mode == ChannelMode::Left)
        {
            CHECK (r == r0);
            CHECK (maxAbsDiff (l, expected) == 0.0);
        }
        else
        {
            CHECK (l == l0);
            CHECK (maxAbsDiff (r, expected) == 0.0);
        }
    }
}

TEST_CASE ("Engine: Mid and Side bands act on the mid and side signals")
{
    const double fs = 48000.0;
    const auto p = bell (800.0, -12.0, 0.5);
    const size_t n = 12000;

    // Mono-compatible input (L == R): a Side band does nothing, a Mid band acts
    // exactly like a Stereo band.
    {
        FilterEngine side; allOff (side); side.setBand (0, p, ChannelMode::Side); side.prepare (fs);
        auto l = noise (n, 20); auto r = l;
        const auto l0 = l;
        run (side, l, &r, 0, n);
        CHECK (maxAbsDiff (l, l0) < 1.0e-15);
        CHECK (maxAbsDiff (r, l0) < 1.0e-15);

        FilterEngine mid; allOff (mid); mid.setBand (0, p, ChannelMode::Mid); mid.prepare (fs);
        FilterEngine st;  allOff (st);  st.setBand (0, p, ChannelMode::Stereo); st.prepare (fs);
        auto ml = noise (n, 21); auto mr = ml;
        auto sl = ml; auto sr = ml;
        run (mid, ml, &mr, 0, n);
        run (st, sl, &sr, 0, n);
        CHECK (maxAbsDiff (ml, sl) < 1.0e-12);
        CHECK (maxAbsDiff (mr, sr) < 1.0e-12);
    }

    // Anti-phase input (R == -L): now it is the Mid band that does nothing.
    {
        FilterEngine mid; allOff (mid); mid.setBand (0, p, ChannelMode::Mid); mid.prepare (fs);
        auto l = noise (n, 22); std::vector<double> r (n);
        for (size_t i = 0; i < n; ++i) r[i] = -l[i];
        const auto l0 = l, r0 = r;
        run (mid, l, &r, 0, n);
        CHECK (maxAbsDiff (l, l0) < 1.0e-15);
        CHECK (maxAbsDiff (r, r0) < 1.0e-15);
    }

    // General signal: a Side band cut changes L and R in opposite directions
    // and leaves the mid (L + R) untouched.
    {
        FilterEngine side; allOff (side); side.setBand (3, p, ChannelMode::Side); side.prepare (fs);
        auto l = noise (n, 23), r = noise (n, 24);
        const auto l0 = l, r0 = r;
        run (side, l, &r, 0, n);
        double maxMidChange = 0.0, sideChange = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            maxMidChange = std::max (maxMidChange, std::abs ((l[i] + r[i]) - (l0[i] + r0[i])));
            sideChange += std::abs ((l[i] - r[i]) - (l0[i] - r0[i]));
        }
        CHECK (maxMidChange < 1.0e-14);
        CHECK (sideChange > 1.0);
    }
}

TEST_CASE ("Engine: 24 bands combine to the product of their responses")
{
    for (double fs : { 44100.0, 96000.0 })
    {
        FilterEngine e;
        std::mt19937 rng (7);
        std::uniform_real_distribution<double> logF (std::log (30.0), std::log (16000.0));
        std::uniform_real_distribution<double> gain (-8.0, 8.0);
        std::uniform_real_distribution<double> q (0.4, 4.0);

        std::vector<BandParameters> ps;
        for (int i = 0; i < FilterEngine::numBands; ++i)
        {
            ps.push_back (bell (std::exp (logF (rng)), gain (rng), q (rng)));
            e.setBand (i, ps.back(), ChannelMode::Stereo);
        }
        e.prepare (fs);

        for (double f : { 50.0, 400.0, 1500.0, 7000.0, 15000.0 })
        {
            double expectedDb = 0.0;
            for (const auto& p : ps)
            {
                CascadeCoefficients c;
                FilterDesign::design (p, fs, c);
                expectedDb += c.magnitudeDb (f, fs);
            }

            const size_t n = (size_t) fs;  // 1 s
            auto l = tone (f, fs, n), r = tone (f, fs, n);
            e.reset();
            run (e, l, &r, 0, n);
            const size_t settle = n / 2;
            const double cycles = std::floor ((double) (n - settle) * f / fs);
            const size_t len = (size_t) std::llround (cycles * fs / f);
            const double measuredDb = 20.0 * std::log10 (amplitudeAt (l, settle, len, f, fs) / 0.5);
            CHECK_NEAR (measuredDb, expectedDb, 0.02);
            CHECK (maxAbsDiff (l, r) == 0.0);
        }
    }
}

TEST_CASE ("Engine: mixed routing chain matches a manual L/R + M/S reference")
{
    const double fs = 48000.0;
    const size_t n = 20000;
    const auto pl = bell (300.0, 6.0), pm = bell (1200.0, -5.0), ps = bell (5000.0, 8.0), pst = bell (90.0, 4.0);

    FilterEngine e; allOff (e);
    e.setBand (0, pl, ChannelMode::Left);
    e.setBand (1, pm, ChannelMode::Mid);
    e.setBand (2, ps, ChannelMode::Side);
    e.setBand (3, pst, ChannelMode::Stereo);
    e.prepare (fs);

    auto l = noise (n, 30), r = noise (n, 31);
    auto rl = l, rr = r;
    run (e, l, &r, 0, n);

    EQBand b0, b1, b2, b3l, b3r;
    b0.setTarget (pl); b0.prepare (fs);
    b1.setTarget (pm); b1.prepare (fs);
    b2.setTarget (ps); b2.prepare (fs);
    b3l.setTarget (pst); b3l.prepare (fs);
    b3r.setTarget (pst); b3r.prepare (fs);

    runBand (b0, rl);
    std::vector<double> m (n), s (n);
    for (size_t i = 0; i < n; ++i) { m[i] = 0.5 * (rl[i] + rr[i]); s[i] = 0.5 * (rl[i] - rr[i]); }
    runBand (b1, m);
    runBand (b2, s);
    for (size_t i = 0; i < n; ++i) { rl[i] = m[i] + s[i]; rr[i] = m[i] - s[i]; }
    runBand (b3l, rl);
    runBand (b3r, rr);

    CHECK (maxAbsDiff (l, rl) < 1.0e-12);
    CHECK (maxAbsDiff (r, rr) < 1.0e-12);
}

TEST_CASE ("Engine: changing a band's channel mode while playing is click-free")
{
    const double fs = 48000.0;
    const size_t n = (size_t) (1.0 * fs);
    const auto p = bell (100.0, 12.0, 1.0);  // strong boost right on the tone

    const ChannelMode sequence[] = { ChannelMode::Stereo, ChannelMode::Left, ChannelMode::Mid,
                                     ChannelMode::Side, ChannelMode::Right, ChannelMode::Stereo };

    FilterEngine e; allOff (e);
    e.setBand (0, p, sequence[0]);
    e.prepare (fs);

    auto l = tone (100.0, fs, n, 0.5), r = tone (100.0, fs, n, 0.3, 1.0);
    const size_t step = n / 6;
    for (size_t k = 0; k < 6; ++k)
    {
        e.setBand (0, p, sequence[k]);
        run (e, l, &r, k * step, std::min (n, (k + 1) * step));
    }
    CHECK (e.activeMode (0) == ChannelMode::Stereo);

    // Reference: instant (unsmoothed) mode switching produces a clear click.
    FilterEngine hard; allOff (hard); hard.setSmoothingTimes (0.0, 0.0);
    hard.setBand (0, p, sequence[0]);
    hard.prepare (fs);
    auto hl = tone (100.0, fs, n, 0.5), hr = tone (100.0, fs, n, 0.3, 1.0);
    for (size_t k = 0; k < 6; ++k)
    {
        hard.setBand (0, p, sequence[k]);
        run (hard, hl, &hr, k * step, std::min (n, (k + 1) * step));
    }

    const double smoothL = clickLevelDb (l, fs), smoothR = clickLevelDb (r, fs);
    const double hardL = clickLevelDb (hl, fs);
    std::printf ("    mode-change click level: smoothed L %.1f dB, R %.1f dB; unsmoothed L %.1f dB\n",
                 smoothL, smoothR, hardL);
    CHECK (smoothL < -80.0);
    CHECK (smoothR < -80.0);
    CHECK (smoothL < hardL - 20.0);
}

TEST_CASE ("Engine: random automation of all 24 bands, types and modes stays stable")
{
    for (double fs : { 44100.0, 192000.0 })
    {
        FilterEngine e;
        std::mt19937 rng (99);
        std::uniform_int_distribution<int> typeD (0, (int) FilterType::NumTypes - 1);
        std::uniform_int_distribution<int> modeD (0, (int) ChannelMode::NumModes - 1);
        std::uniform_int_distribution<int> slopeD (0, (int) limits::slopesDbPerOct.size() - 1);
        std::uniform_real_distribution<double> u (0.0, 1.0);

        auto randomBand = [&]
        {
            BandParameters p;
            p.type = (FilterType) typeD (rng);
            p.frequencyHz = std::exp (std::log (10.0) + u (rng) * (std::log (30000.0) - std::log (10.0)));
            p.gainDb = -30.0 + 60.0 * u (rng);
            p.q = std::exp (std::log (0.025) + u (rng) * (std::log (40.0) - std::log (0.025)));
            p.slopeDbPerOct = limits::slopesDbPerOct[(size_t) slopeD (rng)];
            p.enabled = u (rng) < 0.8;
            return p;
        };

        for (int i = 0; i < FilterEngine::numBands; ++i)
            e.setBand (i, randomBand(), (ChannelMode) modeD (rng));
        e.prepare (fs);

        const size_t n = (size_t) (2.0 * fs);
        auto l = noise (n, 40), r = noise (n, 41);
        const int block = 128;
        bool finite = true;
        double peak = 0.0;
        for (size_t pos = 0; pos < n; pos += block)
        {
            for (int k = 0; k < 3; ++k)
                e.setBand ((int) (u (rng) * FilterEngine::numBands) % FilterEngine::numBands,
                           randomBand(), (ChannelMode) modeD (rng));
            double* ch[2] = { l.data() + pos, r.data() + pos };
            e.process (ch, 2, (int) std::min<size_t> (block, n - pos));
            for (size_t i = pos; i < std::min (n, pos + block); ++i)
            {
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
            }
        }
        CHECK (finite);
        CHECK (peak < 1.0e8);
    }
}
