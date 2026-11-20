#include "FilterDesign.h"

#include <algorithm>
#include <cmath>

namespace peq
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    double clampFrequency (double fc, double fs) noexcept
    {
        const double upper = std::min (limits::maxFrequency, limits::maxFrequencyFractionOfSampleRate * fs);
        return std::clamp (fc, limits::minFrequency, upper);
    }

    BiquadCoefficients normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        const double inv = 1.0 / a0;
        BiquadCoefficients c;
        c.b0 = b0 * inv;
        c.b1 = b1 * inv;
        c.b2 = b2 * inv;
        c.a1 = a1 * inv;
        c.a2 = a2 * inv;
        return c;
    }

    struct Trig
    {
        double cosw, sinw, oneMinusCos, onePlusCos;
    };

    Trig trig (double fc, double fs) noexcept
    {
        const double w = 2.0 * pi * fc / fs;
        const double s = std::sin (0.5 * w);
        Trig t;
        t.cosw        = std::cos (w);
        t.sinw        = std::sin (w);
        t.oneMinusCos = 2.0 * s * s;     // accurate for very small w
        t.onePlusCos  = 2.0 - t.oneMinusCos;
        return t;
    }

    bool isShelf (FilterType t) noexcept { return t == FilterType::LowShelf || t == FilterType::HighShelf; }
    bool isPass  (FilterType t) noexcept { return t == FilterType::LowPass  || t == FilterType::HighPass;  }

    int nearestSlope (int slope, int maxAllowed) noexcept
    {
        int best = limits::slopesDbPerOct.front();
        for (int s : limits::slopesDbPerOct)
        {
            if (s > maxAllowed)
                break;
            if (std::abs (s - slope) < std::abs (best - slope))
                best = s;
        }
        return best;
    }
}

//==============================================================================
std::complex<double> CascadeCoefficients::response (double frequencyHz, double sampleRate) const noexcept
{
    const double w = 2.0 * pi * frequencyHz / sampleRate;
    std::complex<double> h { 1.0, 0.0 };
    for (int i = 0; i < numSections; ++i)
        h *= sections[(size_t) i].response (w);
    return h;
}

double CascadeCoefficients::magnitude (double frequencyHz, double sampleRate) const noexcept
{
    return std::abs (response (frequencyHz, sampleRate));
}

double CascadeCoefficients::magnitudeDb (double frequencyHz, double sampleRate) const noexcept
{
    return 20.0 * std::log10 (std::max (magnitude (frequencyHz, sampleRate), 1.0e-15));
}

//==============================================================================
namespace FilterDesign
{

int sanitiseSlope (FilterType type, int slopeDbPerOct) noexcept
{
    if (isShelf (type))
        return slopeDbPerOct <= 9 ? 6 : 12;

    if (isPass (type))
        return nearestSlope (slopeDbPerOct, 96);

    if (type == FilterType::BandPass)
        return nearestSlope (slopeDbPerOct, 48);

    return 12; // bell and notch have a fixed 2nd-order shape
}

int numSectionsFor (FilterType type, int slopeDbPerOct) noexcept
{
    const int slope = sanitiseSlope (type, slopeDbPerOct);

    if (isPass (type))
    {
        const int order = slope / 6;
        return (order + 1) / 2;
    }

    if (type == FilterType::BandPass)
        return slope / 6; // each 2nd-order band-pass has 6 dB/oct skirts

    return 1;
}

int butterworthQs (int order, std::array<double, CascadeCoefficients::maxSections>& qs, bool& hasFirstOrder) noexcept
{
    order = std::clamp (order, 1, 2 * CascadeCoefficients::maxSections);
    hasFirstOrder = (order % 2) != 0;
    const int numSecondOrder = order / 2;
    const int oddOffset = hasFirstOrder ? 1 : 0;

    // Pole angle measured from the negative real axis:
    //   even N: (2k - 1) * pi / (2N),   odd N: k * pi / N
    for (int k = 1; k <= numSecondOrder; ++k)
    {
        const double phi = (2.0 * k - 1.0 + oddOffset) * pi / (2.0 * order);
        qs[(size_t) (k - 1)] = 1.0 / (2.0 * std::cos (phi));
    }

    // Ascending order: the last section has the highest Q (it carries resonance).
    std::sort (qs.begin(), qs.begin() + numSecondOrder);
    return numSecondOrder;
}

void design (const BandParameters& p, double fs, CascadeCoefficients& out) noexcept
{
    const double fc   = clampFrequency (p.frequencyHz, fs);
    const double gain = std::clamp (p.gainDb, limits::minGainDb, limits::maxGainDb);
    const double q    = std::clamp (p.q, limits::minQ, limits::maxQ);
    const int slope   = sanitiseSlope (p.type, p.slopeDbPerOct);

    out.numSections = 0;

    auto push = [&out] (const BiquadCoefficients& c) noexcept
    {
        if (out.numSections < CascadeCoefficients::maxSections)
            out.sections[(size_t) out.numSections++] = c;
    };

    switch (p.type)
    {
        case FilterType::Bell:
            push (bell (fc, fs, gain, q));
            break;

        case FilterType::LowShelf:
            push (slope == 6 ? lowShelf1 (fc, fs, gain) : lowShelf2 (fc, fs, gain, q));
            break;

        case FilterType::HighShelf:
            push (slope == 6 ? highShelf1 (fc, fs, gain) : highShelf2 (fc, fs, gain, q));
            break;

        case FilterType::LowPass:
        case FilterType::HighPass:
        {
            const bool lp = p.type == FilterType::LowPass;
            const int order = slope / 6;
            std::array<double, CascadeCoefficients::maxSections> qs {};
            bool firstOrder = false;
            const int n2 = butterworthQs (order, qs, firstOrder);

            // The user's Q acts as resonance on the highest-Q section. At the
            // default Q (0.707) the cascade is an exact Butterworth response.
            if (n2 > 0)
                qs[(size_t) (n2 - 1)] *= q / limits::defaultQ;

            if (firstOrder)
                push (lp ? lowPass1 (fc, fs) : highPass1 (fc, fs));

            for (int i = 0; i < n2; ++i)
                push (lp ? lowPass2 (fc, fs, qs[(size_t) i]) : highPass2 (fc, fs, qs[(size_t) i]));
            break;
        }

        case FilterType::Notch:
            push (notch (fc, fs, q));
            break;

        case FilterType::BandPass:
        {
            // n identical sections; per-section Q chosen so the cascade's -3 dB
            // bandwidth stays fc / Q regardless of slope.
            const int n = slope / 6;
            const double sectionQ = q * std::sqrt (std::pow (2.0, 1.0 / n) - 1.0);
            const auto c = bandPass (fc, fs, sectionQ);
            for (int i = 0; i < n; ++i)
                push (c);
            break;
        }

        case FilterType::NumTypes:
        default:
            push (BiquadCoefficients::identity());
            break;
    }
}

//==============================================================================
// Section designs. Second-order forms follow the well-known bilinear-transform
// "audio EQ cookbook" equations (R. Bristow-Johnson), written out here from the
// published maths. First-order forms are bilinear transforms of the analogue
// prototypes given in the comments.

BiquadCoefficients bell (double fc, double fs, double gainDb, double q) noexcept
{
    const auto t = trig (fc, fs);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double alpha = t.sinw / (2.0 * q);
    return normalise (1.0 + alpha * A, -2.0 * t.cosw, 1.0 - alpha * A,
                      1.0 + alpha / A, -2.0 * t.cosw, 1.0 - alpha / A);
}

BiquadCoefficients lowShelf2 (double fc, double fs, double gainDb, double q) noexcept
{
    const auto t = trig (fc, fs);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double beta = 2.0 * std::sqrt (A) * t.sinw / (2.0 * q);
    const double c = t.cosw;
    return normalise (A * ((A + 1.0) - (A - 1.0) * c + beta),
                      2.0 * A * ((A - 1.0) - (A + 1.0) * c),
                      A * ((A + 1.0) - (A - 1.0) * c - beta),
                      (A + 1.0) + (A - 1.0) * c + beta,
                      -2.0 * ((A - 1.0) + (A + 1.0) * c),
                      (A + 1.0) + (A - 1.0) * c - beta);
}

BiquadCoefficients highShelf2 (double fc, double fs, double gainDb, double q) noexcept
{
    const auto t = trig (fc, fs);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double beta = 2.0 * std::sqrt (A) * t.sinw / (2.0 * q);
    const double c = t.cosw;
    return normalise (A * ((A + 1.0) + (A - 1.0) * c + beta),
                      -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
                      A * ((A + 1.0) + (A - 1.0) * c - beta),
                      (A + 1.0) - (A - 1.0) * c + beta,
                      2.0 * ((A - 1.0) - (A + 1.0) * c),
                      (A + 1.0) - (A - 1.0) * c - beta);
}

// Analogue: H(s) = (s/w + sqrt(G)) / (s/w + 1/sqrt(G))   -> G at DC, 1 at HF, sqrt(G) at w
BiquadCoefficients lowShelf1 (double fc, double fs, double gainDb) noexcept
{
    const double k = std::tan (pi * fc / fs);
    const double rg = std::pow (10.0, gainDb / 40.0); // sqrt of linear gain
    const double ik = 1.0 / k;
    return normalise (ik + rg, rg - ik, 0.0,
                      ik + 1.0 / rg, 1.0 / rg - ik, 0.0);
}

// Analogue: H(s) = (sqrt(G) s/w + 1) / (s/(sqrt(G) w) + 1)  -> 1 at DC, G at HF, sqrt(G) at w
BiquadCoefficients highShelf1 (double fc, double fs, double gainDb) noexcept
{
    const double k = std::tan (pi * fc / fs);
    const double rg = std::pow (10.0, gainDb / 40.0);
    return normalise (rg / k + 1.0, 1.0 - rg / k, 0.0,
                      1.0 / (rg * k) + 1.0, 1.0 - 1.0 / (rg * k), 0.0);
}

BiquadCoefficients lowPass2 (double fc, double fs, double q) noexcept
{
    const auto t = trig (fc, fs);
    const double alpha = t.sinw / (2.0 * q);
    return normalise (0.5 * t.oneMinusCos, t.oneMinusCos, 0.5 * t.oneMinusCos,
                      1.0 + alpha, -2.0 * t.cosw, 1.0 - alpha);
}

BiquadCoefficients highPass2 (double fc, double fs, double q) noexcept
{
    const auto t = trig (fc, fs);
    const double alpha = t.sinw / (2.0 * q);
    return normalise (0.5 * t.onePlusCos, -t.onePlusCos, 0.5 * t.onePlusCos,
                      1.0 + alpha, -2.0 * t.cosw, 1.0 - alpha);
}

// Analogue: H(s) = 1 / (s/w + 1)
BiquadCoefficients lowPass1 (double fc, double fs) noexcept
{
    const double k = std::tan (pi * fc / fs);
    return normalise (k, k, 0.0, k + 1.0, k - 1.0, 0.0);
}

// Analogue: H(s) = (s/w) / (s/w + 1)
BiquadCoefficients highPass1 (double fc, double fs) noexcept
{
    const double k = std::tan (pi * fc / fs);
    return normalise (1.0, -1.0, 0.0, k + 1.0, k - 1.0, 0.0);
}

BiquadCoefficients notch (double fc, double fs, double q) noexcept
{
    const auto t = trig (fc, fs);
    const double alpha = t.sinw / (2.0 * q);
    return normalise (1.0, -2.0 * t.cosw, 1.0,
                      1.0 + alpha, -2.0 * t.cosw, 1.0 - alpha);
}

BiquadCoefficients bandPass (double fc, double fs, double q) noexcept
{
    const auto t = trig (fc, fs);
    const double alpha = t.sinw / (2.0 * q);
    return normalise (alpha, 0.0, -alpha,
                      1.0 + alpha, -2.0 * t.cosw, 1.0 - alpha);
}

} // namespace FilterDesign
} // namespace peq
