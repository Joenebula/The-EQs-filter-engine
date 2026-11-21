#include "Resonances.h"
#include "SpectrumAnalyser.h"

#include <algorithm>
#include <cmath>

namespace peq
{

std::vector<Resonance> findResonances (const std::vector<double>& binsDb, double binWidthHz,
                                       double minProminenceDb, int maxCount, double loHz, double hiHz)
{
    std::vector<Resonance> found;
    if (binsDb.size() < 16 || binWidthHz <= 0.0 || maxCount <= 0)
        return found;
    hiHz = std::min (hiHz, binWidthHz * (double) (binsDb.size() - 2));
    if (hiHz <= loHz)
        return found;

    // Log grid, 1/48 octave.
    std::vector<double> f;
    for (double x = loHz; x <= hiHz; x *= std::pow (2.0, 1.0 / 48.0))
        f.push_back (x);
    const size_t n = f.size();
    if (n < 8)
        return found;

    std::vector<double> prefix;
    SpectrumAnalyser::buildPowerPrefix (binsDb, prefix);
    std::vector<double> level (n), prom (n);
    for (size_t i = 0; i < n; ++i)
    {
        level[i] = SpectrumAnalyser::levelAtPrefix (prefix, f[i], binWidthHz, 1.0 / 24.0);
        const double around = SpectrumAnalyser::levelAtPrefix (prefix, f[i], binWidthHz, 1.0);
        prom[i] = level[i] - around;
    }

    for (size_t i = 1; i + 1 < n; ++i)
    {
        if (prom[i] < minProminenceDb || prom[i] < prom[i - 1] || prom[i] < prom[i + 1])
            continue;
        if (level[i] <= SpectrumAnalyser::floorDb + 10.0)
            continue;   // silence

        // Width where the peak falls to half its prominence -> Q.
        size_t lo = i, hi = i;
        while (lo > 0 && prom[lo - 1] > prom[i] * 0.5) --lo;
        while (hi + 1 < n && prom[hi + 1] > prom[i] * 0.5) ++hi;
        const double width = std::max (f[hi] - f[lo], f[i] * (std::pow (2.0, 1.0 / 48.0) - 1.0));

        Resonance r;
        r.frequencyHz = f[i];
        r.prominenceDb = prom[i];
        r.q = std::clamp (f[i] / width, 2.0, 16.0);
        found.push_back (r);
    }

    // Strongest first; drop weaker peaks within 1/6 octave of a stronger one.
    std::sort (found.begin(), found.end(), [] (const Resonance& a, const Resonance& b) { return a.prominenceDb > b.prominenceDb; });
    std::vector<Resonance> kept;
    for (const auto& r : found)
    {
        bool near = false;
        for (const auto& k : kept)
            near = near || std::abs (std::log2 (r.frequencyHz / k.frequencyHz)) < 1.0 / 6.0;
        if (! near)
            kept.push_back (r);
        if ((int) kept.size() >= maxCount)
            break;
    }
    return kept;
}

} // namespace peq
