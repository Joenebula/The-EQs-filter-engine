#include "Clashes.h"
#include "SpectrumAnalyser.h"

#include <algorithm>
#include <cmath>

namespace peq
{

std::vector<Clash> findClashes (const std::vector<double>& trackDb, const std::vector<double>& otherDb,
                                double binWidthHz, int maxCount, double loHz, double hiHz)
{
    std::vector<Clash> found;
    if (trackDb.size() < 16 || trackDb.size() != otherDb.size() || binWidthHz <= 0.0 || maxCount <= 0)
        return found;
    hiHz = std::min (hiHz, binWidthHz * (double) (trackDb.size() - 2));
    if (hiHz <= loHz)
        return found;

    // Log grid, 1/24 octave.
    const double step = std::pow (2.0, 1.0 / 24.0);
    std::vector<double> f;
    for (double x = loHz; x <= hiHz; x *= step)
        f.push_back (x);
    const size_t n = f.size();
    if (n < 8)
        return found;

    std::vector<double> tp, op;
    SpectrumAnalyser::buildPowerPrefix (trackDb, tp);
    SpectrumAnalyser::buildPowerPrefix (otherDb, op);
    std::vector<double> t (n), o (n);
    double tMax = SpectrumAnalyser::floorDb, oMax = SpectrumAnalyser::floorDb;
    for (size_t i = 0; i < n; ++i)
    {
        t[i] = SpectrumAnalyser::levelAtPrefix (tp, f[i], binWidthHz, 1.0 / 3.0);
        o[i] = SpectrumAnalyser::levelAtPrefix (op, f[i], binWidthHz, 1.0 / 3.0);
        tMax = std::max (tMax, t[i]);
        oMax = std::max (oMax, o[i]);
    }

    constexpr double window = 24.0;     // "matters" = within this of the track's own peak
    constexpr double margin = 6.0;      // the other track may be this much quieter and still mask
    const double silence = SpectrumAnalyser::floorDb + 10.0;
    std::vector<double> overlap (n, -1.0);
    for (size_t i = 0; i < n; ++i)
    {
        const double ts = t[i] - (tMax - window), os = o[i] - (oMax - window);
        if (ts > 0.0 && os > 0.0 && t[i] > silence && o[i] > silence && o[i] >= t[i] - margin)
            overlap[i] = std::min (ts, os);
    }

    const size_t minRun = 8;            // 1/3 octave
    for (size_t i = 0; i < n;)
    {
        if (overlap[i] < 0.0) { ++i; continue; }
        size_t j = i;
        while (j + 1 < n && overlap[j + 1] >= 0.0)
            ++j;
        if (j - i + 1 >= minRun)
        {
            // The core: where both are at least half as strong as at the
            // run's strongest point. That's what the cut should cover.
            double peak = 0.0;
            for (size_t k = i; k <= j; ++k)
                peak = std::max (peak, overlap[k]);
            size_t lo = i, hi = j;
            while (lo < j && overlap[lo] < 0.5 * peak) ++lo;
            while (hi > lo && overlap[hi] < 0.5 * peak) --hi;
            if (hi - lo + 1 < minRun / 2)   // keep the core at least 1/6 octave wide
            {
                const size_t grow = (minRun / 2 - (hi - lo + 1) + 1) / 2;
                lo = lo >= i + grow ? lo - grow : i;
                hi = std::min (j, hi + grow);
            }

            double wSum = 0.0, logSum = 0.0, depth = 0.0;
            for (size_t k = lo; k <= hi; ++k)
            {
                const double w = overlap[k] + 1.0e-3;
                wSum += w;
                logSum += w * std::log2 (f[k]);
                depth += std::clamp (o[k] - t[k] + margin, 0.0, 2.0 * margin);
            }
            const double count = (double) (hi - lo + 1);
            Clash c;
            c.lowHz = f[lo];
            c.highHz = f[hi];
            c.centreHz = std::pow (2.0, logSum / wSum);
            c.q = std::clamp (c.centreHz / (c.highHz - c.lowHz), 0.5, 4.0);
            c.suggestedCutDb = -std::clamp (0.35 * depth / count, 1.5, 4.5);
            c.score = std::log2 (c.highHz / c.lowHz) * (wSum / count);
            found.push_back (c);
        }
        i = j + 1;
    }

    std::sort (found.begin(), found.end(), [] (const Clash& a, const Clash& b) { return a.score > b.score; });
    if ((int) found.size() > maxCount)
        found.resize ((size_t) maxCount);
    return found;
}

} // namespace peq
