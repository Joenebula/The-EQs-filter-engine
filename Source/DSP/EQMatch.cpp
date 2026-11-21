#include "EQMatch.h"

#include <algorithm>
#include <cmath>

namespace peq
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double fitLowHz = 20.0;
    constexpr double minBandGainDb = 0.5;
    constexpr double maxBandGainDb = 15.0;
}

void EQMatch::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    // About 0.17 s per frame: 5.9 Hz bins at 48 kHz, fine enough for the lows.
    int order = 13;
    if (rate > 60000.0)  ++order;
    if (rate > 120000.0) ++order;
    fft = FFT (order);
    fftSize = fft.size();
    hop = fftSize / 2.0;

    window.resize ((size_t) fftSize);
    double power = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / fftSize);
        power += window[(size_t) i] * window[(size_t) i];
    }
    windowPower = power;
    work.assign ((size_t) fftSize, {});
    for (auto* s : { &source, &reference })
    {
        s->history.assign ((size_t) fftSize, 0.0f);
        s->powerSum.assign ((size_t) fftSize / 2 + 1, 0.0);
    }
    reset();
}

void EQMatch::reset()
{
    source.clear();
    reference.clear();
}

void EQMatch::Stream::clear()
{
    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (powerSum.begin(), powerSum.end(), 0.0);
    pos = 0;
    fresh = 0;
    frames = 0.0;
}

void EQMatch::Stream::add (const float* data, int count, EQMatch& owner)
{
    if (history.empty())
        return;
    const int n = (int) history.size();
    for (int i = 0; i < count; ++i)
    {
        history[(size_t) pos] = data[i];
        pos = (pos + 1) % n;
        if (++fresh >= (int) owner.hop)
        {
            fresh = 0;
            owner.analyse (*this);
        }
    }
}

void EQMatch::analyse (Stream& s)
{
    double energy = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const double x = s.history[(size_t) ((s.pos + i) % fftSize)];
        energy += x * x;
        work[(size_t) i] = { x * window[(size_t) i], 0.0 };
    }
    const double rmsDb = 10.0 * std::log10 (energy / fftSize + 1e-30);
    if (rmsDb < silenceDb)
        return;   // silence or near silence: don't let it colour the average

    fft.perform (work);
    const double scale = 1.0 / windowPower;
    for (size_t b = 0; b < s.powerSum.size(); ++b)
        s.powerSum[b] += std::norm (work[b]) * scale;
    s.frames += 1.0;
}

std::vector<double> EQMatch::averageDb (const Stream& s) const
{
    std::vector<double> db (s.powerSum.size(), SpectrumAnalyser::floorDb);
    if (s.frames <= 0.0)
        return db;
    for (size_t b = 0; b < db.size(); ++b)
        db[b] = 10.0 * std::log10 (s.powerSum[b] / s.frames + 1e-30);
    return db;
}

std::vector<double> EQMatch::fitFrequencies (double sampleRate)
{
    std::vector<double> f;
    const double top = std::min (20000.0, 0.45 * sampleRate);
    for (double x = fitLowHz; x <= top * 1.0001; x *= std::pow (2.0, 1.0 / 24.0))
        f.push_back (x);
    return f;
}

void EQMatch::differenceDb (const double* frequencies, int count, double* out) const
{
    std::fill (out, out + count, 0.0);
    if (source.frames <= 0.0 || reference.frames <= 0.0 || count <= 0)
        return;

    const auto src = averageDb (source);
    const auto ref = averageDb (reference);
    const double binWidth = rate / fftSize;

    std::vector<double> s ((size_t) count), r ((size_t) count);
    double sMax = -1e9, rMax = -1e9;
    for (int i = 0; i < count; ++i)
    {
        s[(size_t) i] = SpectrumAnalyser::levelAt (src, frequencies[i], binWidth, 1.0 / 3.0);
        r[(size_t) i] = SpectrumAnalyser::levelAt (ref, frequencies[i], binWidth, 1.0 / 3.0);
        sMax = std::max (sMax, s[(size_t) i]);
        rMax = std::max (rMax, r[(size_t) i]);
    }

    // Only where both have real content (within 60 dB of their loudest region).
    std::vector<bool> valid ((size_t) count);
    double sum = 0.0, weight = 0.0;
    for (int i = 0; i < count; ++i)
    {
        valid[(size_t) i] = s[(size_t) i] > sMax - 60.0 && r[(size_t) i] > rMax - 60.0;
        if (valid[(size_t) i] && frequencies[i] >= 100.0 && frequencies[i] <= 10000.0)
        {
            sum += r[(size_t) i] - s[(size_t) i];
            weight += 1.0;
        }
    }
    if (weight <= 0.0)
        return;
    const double offset = sum / weight;   // the overall level difference

    for (int i = 0; i < count; ++i)
        if (valid[(size_t) i])
            out[i] = std::clamp (r[(size_t) i] - s[(size_t) i] - offset, -maxCorrectionDb, maxCorrectionDb);
}

std::vector<BandParameters> EQMatch::makeBands (int maxBands, double amount, bool analogMatched) const
{
    const auto f = fitFrequencies (rate);
    std::vector<double> target (f.size());
    differenceDb (f.data(), (int) f.size(), target.data());
    for (auto& t : target)
        t *= std::clamp (amount, 0.0, 1.0);
    return fit (f, target, maxBands, rate, analogMatched);
}

std::vector<BandParameters> EQMatch::fit (const std::vector<double>& f, const std::vector<double>& target,
                                          int maxBands, double sampleRate, bool analogMatched)
{
    std::vector<BandParameters> bands;
    const size_t n = std::min (f.size(), target.size());
    if (n < 3 || maxBands <= 0)
        return bands;

    // dB response of one band at every fit frequency.
    auto responseOf = [&] (const BandParameters& p, std::vector<double>& out)
    {
        CascadeCoefficients c;
        FilterDesign::design (p, sampleRate, c);
        out.resize (n);
        for (size_t i = 0; i < n; ++i)
            out[i] = c.magnitudeDb (f[i], sampleRate);
    };

    std::vector<std::vector<double>> responses;
    std::vector<double> residual (target.begin(), target.begin() + (long) n);

    auto recomputeResidual = [&]
    {
        for (size_t i = 0; i < n; ++i)
        {
            double total = 0.0;
            for (auto& r : responses)
                total += r[i];
            residual[i] = target[i] - total;
        }
    };

    // Least-squares touch-up of every gain (coordinate descent; a band's dB
    // curve is close to proportional to its gain).
    auto refineGains = [&]
    {
        for (int pass = 0; pass < 12; ++pass)
        {
            for (size_t k = 0; k < bands.size(); ++k)
            {
                recomputeResidual();
                const double g = bands[k].gainDb;
                if (std::abs (g) < 1e-6)
                    continue;
                double num = 0.0, den = 0.0;
                for (size_t i = 0; i < n; ++i)
                {
                    const double shape = responses[k][i] / g;
                    num += residual[i] * shape;
                    den += shape * shape;
                }
                if (den <= 1e-12)
                    continue;
                double newGain = std::clamp (g + num / den, -maxBandGainDb, maxBandGainDb);
                if (std::abs (newGain) < 0.05)
                    newGain = newGain < 0.0 ? -0.05 : 0.05;   // keep the sign so the shape stays defined
                bands[k].gainDb = newGain;
                responseOf (bands[k], responses[k]);
            }
        }
        recomputeResidual();
    };

    for (int k = 0; k < maxBands; ++k)
    {
        // The largest remaining error (a peak in the residual).
        size_t peak = 0;
        for (size_t i = 1; i < n; ++i)
            if (std::abs (residual[i]) > std::abs (residual[peak]))
                peak = i;
        const double height = residual[peak];
        if (std::abs (height) < minBandGainDb)
            break;

        // Its width: where the error falls to half (same sign).
        size_t lo = peak, hi = peak;
        while (lo > 0 && residual[lo - 1] * height > 0.0 && std::abs (residual[lo - 1]) > std::abs (height) * 0.5)
            --lo;
        while (hi + 1 < n && residual[hi + 1] * height > 0.0 && std::abs (residual[hi + 1]) > std::abs (height) * 0.5)
            ++hi;

        BandParameters p;
        p.analogMatched = analogMatched;
        p.gainDb = std::clamp (height, -maxBandGainDb, maxBandGainDb);
        if (lo == 0 && f[hi] < 400.0)
        {
            p.type = FilterType::LowShelf;   // keeps going to the bottom: a shelf
            p.frequencyHz = f[hi];
            p.q = limits::defaultQ;
        }
        else if (hi == n - 1 && f[lo] > 4000.0)
        {
            p.type = FilterType::HighShelf;
            p.frequencyHz = f[lo];
            p.q = limits::defaultQ;
        }
        else
        {
            p.type = FilterType::Bell;
            p.frequencyHz = f[peak];
            // Bandwidth between the half-gain points -> Q.
            const double octaves = std::max (std::log2 (f[hi] / f[lo]) + 1.0 / 24.0, 1.0 / 12.0);
            p.q = std::clamp (1.0 / (2.0 * std::sinh (std::log (2.0) * octaves * 0.5)), 0.3, 8.0);
        }
        p.frequencyHz = std::clamp (p.frequencyHz, limits::minFrequency, limits::maxFrequency);

        bands.push_back (p);
        responses.emplace_back();
        responseOf (bands.back(), responses.back());
        refineGains();
    }

    // Drop bands the touch-up made negligible.
    std::vector<BandParameters> kept;
    for (auto& b : bands)
        if (std::abs (b.gainDb) >= minBandGainDb)
            kept.push_back (b);
    return kept;
}

} // namespace peq
