#include "FilterEngine.h"

#include <algorithm>

namespace peq
{

namespace
{
    ChannelMode sanitiseMode (ChannelMode m) noexcept
    {
        const int v = (int) m;
        return (v >= 0 && v < (int) ChannelMode::NumModes) ? m : ChannelMode::Stereo;
    }

    void toMidSide (double* l, double* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double a = l[i], b = r[i];
            l[i] = 0.5 * (a + b);
            r[i] = 0.5 * (a - b);
        }
    }

    void toLeftRight (double* m, double* s, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const double a = m[i], b = s[i];
            m[i] = a + b;
            s[i] = a - b;
        }
    }
}

void FilterEngine::prepare (double sampleRate) noexcept
{
    for (int i = 0; i < numBands; ++i)
    {
        auto& s = slots[(size_t) i];
        s.active = s.requested;
        bands[(size_t) i].setTarget (s.params);
        bands[(size_t) i].prepare (sampleRate);
    }
}

void FilterEngine::reset() noexcept
{
    for (auto& b : bands)
        b.reset();
}

void FilterEngine::setSmoothingTimes (double parameterSeconds, double fadeSeconds) noexcept
{
    for (auto& b : bands)
        b.setSmoothingTimes (parameterSeconds, fadeSeconds);
}

void FilterEngine::setBand (int index, const BandParameters& params, ChannelMode mode) noexcept
{
    if (index < 0 || index >= numBands)
        return;

    auto& s = slots[(size_t) index];
    s.params = params;
    s.requested = sanitiseMode (mode);

    auto& b = bands[(size_t) index];

    if (s.requested == s.active)
    {
        b.setTarget (params);
        return;
    }

    if (! b.isActive())
    {
        applyPendingMode (index);
        return;
    }

    // Fade out under the old routing first; the swap happens in process().
    BandParameters off = params;
    off.enabled = false;
    b.setTarget (off);
}

void FilterEngine::applyPendingMode (int index) noexcept
{
    auto& s = slots[(size_t) index];
    auto& b = bands[(size_t) index];
    s.active = s.requested;
    b.reset();
    b.setTarget (s.params);
}

void FilterEngine::snapToTargets() noexcept
{
    for (int i = 0; i < numBands; ++i)
    {
        auto& s = slots[(size_t) i];
        if (s.active != s.requested)
            applyPendingMode (i);
        bands[(size_t) i].setTarget (s.params);
        bands[(size_t) i].snapToTarget();
    }
}

void FilterEngine::process (double* const* channels, int numChannels, int numSamples,
                            const double* const* sidechain, int numSidechainChannels) noexcept
{
    numChannels = std::clamp (numChannels, 0, maxChannels);
    if (numChannels == 0 || numSamples <= 0)
        return;

    // The sidechain is only valid during this call: hand it to the bands now
    // and take it back at the end.
    for (auto& b : bands)
        b.setSidechain (sidechain, numSidechainChannels);
    struct ClearSidechain
    {
        std::array<EQBand, numBands>& bands;
        ~ClearSidechain() { for (auto& b : bands) b.setSidechain (nullptr, 0); }
    } clearSidechain { bands };

    double* l = channels[0];
    double* r = numChannels > 1 ? channels[1] : nullptr;
    bool inMidSide = false;

    for (int i = 0; i < numBands; ++i)
    {
        auto& s = slots[(size_t) i];
        auto& b = bands[(size_t) i];

        if (s.active != s.requested && ! b.isActive())
            applyPendingMode (i);

        if (! b.isActive())
        {
            // Cheap: keeps the band's internal settings current, touches no audio.
            b.process (channels, numChannels, numSamples);
            continue;
        }

        if (r == nullptr)
        {
            b.process (channels, 1, numSamples);
            continue;
        }

        const bool wantMidSide = usesMidSide (s.active);
        if (wantMidSide != inMidSide)
        {
            if (wantMidSide) toMidSide (l, r, numSamples);
            else             toLeftRight (l, r, numSamples);
            inMidSide = wantMidSide;
        }

        switch (s.active)
        {
            case ChannelMode::Stereo:   b.process (channels, 2, numSamples); break;
            case ChannelMode::Left:
            case ChannelMode::Mid:      b.processChannel (l, 0, numSamples); break;
            case ChannelMode::Right:
            case ChannelMode::Side:     b.processChannel (r, 1, numSamples); break;
            case ChannelMode::NumModes: break;
        }
    }

    if (inMidSide)
        toLeftRight (l, r, numSamples);
}

} // namespace peq
