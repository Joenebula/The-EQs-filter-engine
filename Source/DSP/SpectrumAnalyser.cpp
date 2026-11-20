#include "SpectrumAnalyser.h"

#include <algorithm>
#include <cmath>

namespace peq
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
}

FFT::FFT (int order)
    : n (1 << std::clamp (order, 1, 16))
{
    twiddles.resize ((size_t) n / 2);
    for (int k = 0; k < n / 2; ++k)
        twiddles[(size_t) k] = std::polar (1.0, -2.0 * pi * k / n);

    bitReverse.resize ((size_t) n);
    int bits = 0;
    while ((1 << bits) < n)
        ++bits;
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        bitReverse[(size_t) i] = r;
    }
}

void FFT::perform (std::vector<std::complex<double>>& data) const noexcept
{
    if ((int) data.size() != n)
        return;

    for (int i = 0; i < n; ++i)
        if (i < bitReverse[(size_t) i])
            std::swap (data[(size_t) i], data[(size_t) bitReverse[(size_t) i]]);

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2;
        const int step = n / len;
        for (int start = 0; start < n; start += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const auto t = twiddles[(size_t) (k * step)] * data[(size_t) (start + k + half)];
                const auto u = data[(size_t) (start + k)];
                data[(size_t) (start + k)] = u + t;
                data[(size_t) (start + k + half)] = u - t;
            }
        }
    }
}

//==============================================================================
void SpectrumAnalyser::configure (int fftOrder, double sampleRate)
{
    fft = FFT (fftOrder);
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int n = fft.size();

    window.resize ((size_t) n);
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / n);
        sum += window[(size_t) i];
    }
    // A sine of amplitude A gives |X| = A * sum(w) / 2 at its bin.
    windowGain = 2.0 / sum;

    history.assign ((size_t) n, 0.0f);
    work.resize ((size_t) n);
    reset();
}

void SpectrumAnalyser::reset()
{
    std::fill (history.begin(), history.end(), 0.0f);
    historyPos = 0;
    newSamples = 0;
    smoothedDb.assign ((size_t) fft.size() / 2 + 1, floorDb);
    peakDb.assign ((size_t) fft.size() / 2 + 1, floorDb);
}

void SpectrumAnalyser::setPeakHold (bool enabled) noexcept
{
    peakHold = enabled;
    if (! enabled)
        std::fill (peakDb.begin(), peakDb.end(), floorDb);
}

void SpectrumAnalyser::addSamples (const float* data, int count)
{
    const int n = fft.size();
    for (int i = 0; i < count; ++i)
    {
        history[(size_t) historyPos] = data[i];
        historyPos = (historyPos + 1) & (n - 1);
    }
    newSamples = std::min (newSamples + count, n);
}

bool SpectrumAnalyser::computeIfReady (double elapsedSeconds)
{
    const int n = fft.size();
    const int bins = n / 2 + 1;

    // Release and peak decay happen with time even when no audio arrives.
    const double fall = releaseTime > 0.0 ? 20.0 * elapsedSeconds / releaseTime : 1000.0;
    const double peakFall = 6.0 * elapsedSeconds;  // 6 dB per second

    const bool ready = newSamples >= n / 4;
    if (ready)
    {
        newSamples = 0;
        for (int i = 0; i < n; ++i)
        {
            const float s = history[(size_t) ((historyPos + i) & (n - 1))];
            work[(size_t) i] = { (double) s * window[(size_t) i], 0.0 };
        }
        fft.perform (work);
    }

    for (int b = 0; b < bins; ++b)
    {
        double& level = smoothedDb[(size_t) b];
        if (ready)
        {
            const double mag = std::abs (work[(size_t) b]) * windowGain * (b == 0 || b == n / 2 ? 0.5 : 1.0);
            const double db = mag > 1.0e-15 ? std::max (floorDb, 20.0 * std::log10 (mag)) : floorDb;
            level = db > level ? db : std::max (db, level - fall);  // instant attack
        }
        else
        {
            level = std::max (floorDb, level - fall);
        }

        if (peakHold)
        {
            double& p = peakDb[(size_t) b];
            p = level > p ? level : std::max (floorDb, p - peakFall);
        }
    }
    return ready;
}

double SpectrumAnalyser::levelAt (const std::vector<double>& binsDb, double frequencyHz,
                                  double binWidthHz, double octaves) noexcept
{
    const int bins = (int) binsDb.size();
    if (bins < 2 || binWidthHz <= 0.0)
        return floorDb;

    const double centre = frequencyHz / binWidthHz;
    if (octaves <= 0.0)
    {
        const int b0 = std::clamp ((int) std::floor (centre), 0, bins - 1);
        const int b1 = std::min (b0 + 1, bins - 1);
        const double t = std::clamp (centre - b0, 0.0, 1.0);
        return binsDb[(size_t) b0] + t * (binsDb[(size_t) b1] - binsDb[(size_t) b0]);
    }

    // Power average over [f / 2^(oct/2), f * 2^(oct/2)], at least one bin wide.
    const double ratio = std::pow (2.0, octaves * 0.5);
    int lo = (int) std::floor (centre / ratio);
    int hi = (int) std::ceil (centre * ratio);
    lo = std::clamp (lo, 0, bins - 1);
    hi = std::clamp (hi, lo, bins - 1);

    double power = 0.0;
    for (int b = lo; b <= hi; ++b)
        power += std::pow (10.0, binsDb[(size_t) b] / 10.0);
    power /= (double) (hi - lo + 1);
    return power > 0.0 ? std::max (floorDb, 10.0 * std::log10 (power)) : floorDb;
}

void SpectrumAnalyser::buildPowerPrefix (const std::vector<double>& binsDb, std::vector<double>& prefix)
{
    prefix.resize (binsDb.size() + 1);
    prefix[0] = 0.0;
    for (size_t i = 0; i < binsDb.size(); ++i)
        prefix[i + 1] = prefix[i] + std::pow (10.0, binsDb[i] / 10.0);
}

double SpectrumAnalyser::levelAtPrefix (const std::vector<double>& prefix, double frequencyHz,
                                        double binWidthHz, double octaves) noexcept
{
    const int bins = (int) prefix.size() - 1;
    if (bins < 2 || binWidthHz <= 0.0)
        return floorDb;

    const double centre = frequencyHz / binWidthHz;
    int lo, hi;
    if (octaves <= 0.0)
    {
        lo = hi = std::clamp ((int) std::lround (centre), 0, bins - 1);
    }
    else
    {
        const double ratio = std::pow (2.0, octaves * 0.5);
        lo = std::clamp ((int) std::floor (centre / ratio), 0, bins - 1);
        hi = std::clamp ((int) std::ceil (centre * ratio), lo, bins - 1);
    }
    const double power = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1);
    return power > 0.0 ? std::max (floorDb, 10.0 * std::log10 (power)) : floorDb;
}

} // namespace peq
