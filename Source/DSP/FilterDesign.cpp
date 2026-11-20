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

BiquadCoefficients invert (const BiquadCoefficients& c) noexcept;

namespace
{
    // Bells and shelves: a cut is the exact inverse of the matching boost in
    // the analogue domain, and the boost (narrow poles) matches much better,
    // so cuts are designed as inverted boosts.
    BiquadCoefficients scaled (BiquadCoefficients c, double k) noexcept
    {
        c.b0 *= k; c.b1 *= k; c.b2 *= k;
        return c;
    }

    // Bells and shelves. In the analogue domain a cut is the exact inverse of
    // the matching boost, and a high shelf is G / (low shelf boost). The low
    // shelf and bell boosts have their poles below the band frequency, where
    // impulse invariance is accurate, so everything is derived from those.
    BiquadCoefficients matchedGain (FilterType type, int order, double fc, double fs, double gainDb, double q) noexcept
    {
        const double g = std::abs (gainDb);
        if (type == FilterType::Bell)
        {
            const auto boost = matched (analogPrototype (FilterType::Bell, 2, g, q), fc, fs, true);
            return gainDb >= 0.0 ? boost : invert (boost);
        }

        const auto lowBoost = matched (analogPrototype (FilterType::LowShelf, order, g, q), fc, fs);
        const double G = std::pow (10.0, g / 20.0);
        if (type == FilterType::LowShelf)
            return gainDb >= 0.0 ? lowBoost : invert (lowBoost);

        return gainDb >= 0.0 ? scaled (invert (lowBoost), G) : scaled (lowBoost, 1.0 / G);
    }
}

void design (const BandParameters& p, double fs, CascadeCoefficients& out) noexcept
{
    const double fc   = clampFrequency (p.frequencyHz, fs);
    const double gain = std::clamp (p.gainDb, limits::minGainDb, limits::maxGainDb);
    const double q    = std::clamp (p.q, limits::minQ, limits::maxQ);
    const int slope   = sanitiseSlope (p.type, p.slopeDbPerOct);
    const bool m      = p.analogMatched;

    out.numSections = 0;

    auto push = [&out] (const BiquadCoefficients& c) noexcept
    {
        if (out.numSections < CascadeCoefficients::maxSections)
            out.sections[(size_t) out.numSections++] = c;
    };

    switch (p.type)
    {
        case FilterType::Bell:
            push (m ? matchedGain (p.type, 2, fc, fs, gain, q) : bell (fc, fs, gain, q));
            break;

        case FilterType::LowShelf:
            if (m)
                push (matchedGain (p.type, slope / 6, fc, fs, gain, q));
            else
                push (slope == 6 ? lowShelf1 (fc, fs, gain) : lowShelf2 (fc, fs, gain, q));
            break;

        case FilterType::HighShelf:
            if (m)
                push (matchedGain (p.type, slope / 6, fc, fs, gain, q));
            else
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
                push (m ? matched (analogPrototype (p.type, 1, 0.0, 1.0), fc, fs)
                        : (lp ? lowPass1 (fc, fs) : highPass1 (fc, fs)));

            for (int i = 0; i < n2; ++i)
            {
                const double sq = qs[(size_t) i];
                push (m ? matched (analogPrototype (p.type, 2, 0.0, sq), fc, fs)
                        : (lp ? lowPass2 (fc, fs, sq) : highPass2 (fc, fs, sq)));
            }
            break;
        }

        case FilterType::Notch:
            push (m ? matched (analogPrototype (p.type, 2, 0.0, q), fc, fs)
                    : notch (fc, fs, q));
            break;

        case FilterType::BandPass:
        {
            // n identical sections; per-section Q chosen so the cascade's -3 dB
            // bandwidth stays fc / Q regardless of slope.
            const int n = slope / 6;
            const double sectionQ = q * std::sqrt (std::pow (2.0, 1.0 / n) - 1.0);
            const auto c = m ? matched (analogPrototype (p.type, 2, 0.0, sectionQ), fc, fs)
                             : bandPass (fc, fs, sectionQ);
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

//==============================================================================
// Analog-matched designs.

double AnalogSection::magnitudeSquared (double x) const noexcept
{
    // s = j x:  s^2 = -x^2
    const std::complex<double> num (n0 - n2 * x * x, n1 * x);
    const std::complex<double> den (d0 - d2 * x * x, d1 * x);
    return std::norm (num) / std::max (std::norm (den), 1.0e-300);
}

AnalogSection analogPrototype (FilterType type, int order, double gainDb, double q) noexcept
{
    AnalogSection a;
    const double A  = std::pow (10.0, gainDb / 40.0); // sqrt of the linear gain
    const double sA = std::sqrt (A);
    q = std::max (q, 1.0e-6);

    switch (type)
    {
        case FilterType::Bell:        // (s^2 + s A/Q + 1) / (s^2 + s/(A Q) + 1)
            a = { 1.0, A / q, 1.0,   1.0, 1.0 / (A * q), 1.0 };
            break;

        case FilterType::LowShelf:
            if (order == 1)           // (s + sqrt G) / (s + 1/sqrt G)
                a = { A, 1.0, 0.0,   1.0 / A, 1.0, 0.0 };
            else                      // A (s^2 + s sqrtA/Q + A) / (A s^2 + s sqrtA/Q + 1)
                a = { A * A, A * sA / q, A,   1.0, sA / q, A };
            break;

        case FilterType::HighShelf:
            if (order == 1)           // (sqrt G s + 1) / (s / sqrt G + 1)
                a = { 1.0, A, 0.0,   1.0, 1.0 / A, 0.0 };
            else                      // A (A s^2 + s sqrtA/Q + 1) / (s^2 + s sqrtA/Q + A)
                a = { A, A * sA / q, A * A,   A, sA / q, 1.0 };
            break;

        case FilterType::LowPass:
            a = order == 1 ? AnalogSection { 1.0, 0.0, 0.0,   1.0, 1.0, 0.0 }
                           : AnalogSection { 1.0, 0.0, 0.0,   1.0, 1.0 / q, 1.0 };
            break;

        case FilterType::HighPass:
            a = order == 1 ? AnalogSection { 0.0, 1.0, 0.0,   1.0, 1.0, 0.0 }
                           : AnalogSection { 0.0, 0.0, 1.0,   1.0, 1.0 / q, 1.0 };
            break;

        case FilterType::Notch:       // (s^2 + 1) / (s^2 + s/Q + 1)
            a = { 1.0, 0.0, 1.0,   1.0, 1.0 / q, 1.0 };
            break;

        case FilterType::BandPass:    // (s/Q) / (s^2 + s/Q + 1), 0 dB at fc
            a = { 0.0, 1.0 / q, 0.0,   1.0, 1.0 / q, 1.0 };
            break;

        case FilterType::NumTypes:
        default:
            break;
    }
    return a;
}

BiquadCoefficients matched (const AnalogSection& a, double fc, double fs, bool exactAtFc) noexcept
{
    const double wc = 2.0 * pi * fc / fs;
    auto target = [&a, wc] (double w) noexcept { return a.magnitudeSquared (w / wc); };
    const double t0 = target (0.0);
    const double tN = target (pi);

    BiquadCoefficients c;

    if (a.d2 == 0.0)
    {
        // First order: pole by impulse invariance, magnitude matched at DC and
        // Nyquist. |N|^2 = B0 cos^2(w/2) + B1 sin^2(w/2).
        const double wp = wc * a.d0 / a.d1;
        const double pole = std::exp (-wp);
        const double oneMinusP = -std::expm1 (-wp);
        const double s0 = std::sqrt (t0) * oneMinusP;
        const double s1 = std::sqrt (tN) * (1.0 + pole);
        c.b0 = 0.5 * (s0 + s1);
        c.b1 = 0.5 * (s0 - s1);
        c.b2 = 0.0;
        c.a1 = -pole;
        c.a2 = 0.0;
        return c;
    }

    // Second order. Poles: impulse invariance of s^2 + 2 zeta wp s + wp^2.
    const double wp   = wc * std::sqrt (a.d0 / a.d2);
    const double zeta = a.d1 / (2.0 * std::sqrt (a.d0 * a.d2));

    double a1, a2, sumA, altA; // sumA = 1 + a1 + a2, altA = 1 - a1 + a2
    if (zeta < 1.0)
    {
        // A resonance above Nyquist would alias down into the audio band; keep
        // its damping but hold its angle just below Nyquist.
        const double theta = std::min (wp * std::sqrt (1.0 - zeta * zeta), 0.95 * pi);
        const double r = std::exp (-zeta * wp);
        const double oneMinusR = -std::expm1 (-zeta * wp);
        const double sh = std::sin (0.5 * theta);
        a1 = -2.0 * r * std::cos (theta);
        a2 = r * r;
        sumA = oneMinusR * oneMinusR + 4.0 * r * sh * sh;
        altA = (1.0 + r) * (1.0 + r) - 4.0 * r * sh * sh;
    }
    else
    {
        const double k = std::sqrt (zeta * zeta - 1.0);
        const double slow = wp / (zeta + k); // = (zeta - k) wp without cancellation
        const double fast = (zeta + k) * wp;
        const double r1 = std::exp (-slow), r2 = std::exp (-fast);
        a1 = -(r1 + r2);
        a2 = r1 * r2;
        sumA = std::expm1 (-slow) * std::expm1 (-fast);
        altA = (1.0 + r1) * (1.0 + r2);
    }

    // |A(e^jw)|^2 = A0 phi0 + A1 phi1 + A2 phi2, phi1 = sin^2(w/2),
    // phi0 = 1 - phi1, phi2 = 4 phi0 phi1. Same form for |N|^2 with B0..B2.
    const double A0 = sumA * sumA, A1 = altA * altA, A2 = -4.0 * a2;

    // Match point: fc, but no higher than 0.75 pi for bells (0.6 pi otherwise).
    // Near Nyquist the fit is already pinned by the Nyquist value, and a match
    // point too close to it leaves the middle of the band unconstrained.
    const double wm   = std::min (wc, (exactAtFc ? 0.75 : 0.6) * pi);
    const double sm   = std::sin (0.5 * wm);
    const double phi1 = sm * sm;
    const double phi0 = 1.0 - phi1;
    const double phi2 = 4.0 * phi0 * phi1;

    const double Am = A0 * phi0 + A1 * phi1 + A2 * phi2;
    c.a1 = a1;
    c.a2 = a2;

    if (a.n0 == 0.0 && a.n1 == 0.0)
    {
        // High-pass: keep the double zero at DC (12 dB/oct below fc) and match
        // the magnitude at fc.  |1 - z^-1|^4 = (4 phi1)^2.
        const double k = std::sqrt (std::max (target (wm) * Am, 0.0)) / (4.0 * phi1);
        c.b0 = k;
        c.b1 = -2.0 * k;
        c.b2 = k;
        return c;
    }

    if (a.n1 == 0.0 && a.n0 > 0.0 && a.n2 > 0.0 && t0 > 0.0 && std::abs (a.n0 - a.n2) <= 1.0e-12 * a.n0)
    {
        // Notch: zeros exactly on the unit circle at the analogue notch
        // frequency (no warping), gain matched at DC.
        const double wz = std::min (wc * std::sqrt (a.n0 / a.n2), 0.98 * pi);
        const double sz = std::sin (0.5 * wz);
        const double k = std::sqrt (t0 * A0) / (4.0 * sz * sz);
        c.b0 = k;
        c.b1 = -2.0 * std::cos (wz) * k;
        c.b2 = k;
        return c;
    }

    if (a.n2 > 0.0 && a.n0 > 0.0)
    {
        // Lightly damped zeros (a resonant dip) are placed like the poles,
        // which keeps the dip's depth and position; the gain is then matched
        // at DC. Well-damped zeros use the three-point fit below.
        const double wz    = wc * std::sqrt (a.n0 / a.n2);
        const double zetaZ = a.n1 / (2.0 * std::sqrt (a.n0 * a.n2));
        if (! exactAtFc && zetaZ < 0.35 && wz < 0.97 * pi)
        {
            const double thetaZ = wz * std::sqrt (1.0 - zetaZ * zetaZ);
            const double rz = std::exp (-zetaZ * wz);
            const double shz = std::sin (0.5 * thetaZ);
            const double sumB = (-std::expm1 (-zetaZ * wz)) * (-std::expm1 (-zetaZ * wz)) + 4.0 * rz * shz * shz;
            c.b0 = 1.0;
            c.b1 = -2.0 * rz * std::cos (thetaZ);
            c.b2 = rz * rz;
            const double k = std::sqrt (t0) * sumA / sumB;
            c.b0 = k;
            c.b1 *= k;
            c.b2 *= k;
            return c;
        }
    }

    const double B0 = t0 * A0;
    const double B1 = tN * A1;
    const double B2 = (target (wm) * Am - B0 * phi0 - B1 * phi1) / phi2;

    // Factor |N|^2 back into b0 + b1 z^-1 + b2 z^-2:
    //   b0 + b1 + b2 = sqrt(B0), b0 - b1 + b2 = sqrt(B1), b0 b2 = -B2 / 4.
    const double s0 = std::sqrt (std::max (B0, 0.0));
    const double s1 = std::sqrt (std::max (B1, 0.0));
    const double W  = 0.5 * (s0 + s1);
    const double disc = std::max (W * W + B2, 0.0);
    c.b0 = 0.5 * (W + std::sqrt (disc));
    c.b2 = W - c.b0;
    c.b1 = 0.5 * (s0 - s1);
    return c;
}

BiquadCoefficients invert (const BiquadCoefficients& c) noexcept
{
    // 1 / H(z). The zeros become poles, so first reflect any zero outside the
    // unit circle inwards (same magnitude response, minimum phase).
    double b0 = c.b0, b1 = c.b1, b2 = c.b2;
    const double disc = b1 * b1 - 4.0 * b0 * b2;
    if (disc >= 0.0)
    {
        // Real zeros z1, z2 of b0 + b1 z^-1 + b2 z^-2.
        const double sq = std::sqrt (disc);
        double z1 = (-b1 + sq) / (2.0 * b0), z2 = (-b1 - sq) / (2.0 * b0);
        double g = b0;
        if (std::abs (z1) > 1.0) { g *= std::abs (z1); z1 = 1.0 / z1; }
        if (std::abs (z2) > 1.0) { g *= std::abs (z2); z2 = 1.0 / z2; }
        const double lim = 0.999999;
        z1 = std::clamp (z1, -lim, lim);
        z2 = std::clamp (z2, -lim, lim);
        b0 = g;
        b1 = -g * (z1 + z2);
        b2 = g * z1 * z2;
    }
    else
    {
        // Complex pair with |z|^2 = b2 / b0. Reversing the polynomial reflects
        // the pair to 1/z* with the same magnitude response.
        if (b2 > b0)
            std::swap (b0, b2);
        b2 = std::min (b2, 0.999998 * b0);
    }

    const double inv = 1.0 / b0;
    BiquadCoefficients r;
    r.b0 = inv;
    r.b1 = c.a1 * inv;
    r.b2 = c.a2 * inv;
    r.a1 = b1 * inv;
    r.a2 = b2 * inv;
    return r;
}

} // namespace FilterDesign
} // namespace peq
