#pragma once

#include <array>
#include <cmath>
#include <complex>

namespace peq
{

/** Normalised biquad coefficients (a0 == 1).

    Transfer function:
        H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)

    A first-order section is expressed with b2 = a2 = 0.
*/
struct BiquadCoefficients
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;

    static BiquadCoefficients identity() noexcept { return {}; }

    /** Complex response at normalised angular frequency w (radians/sample). */
    std::complex<double> response (double w) const noexcept
    {
        const std::complex<double> z1 = std::polar (1.0, -w);  // z^-1
        const std::complex<double> z2 = z1 * z1;               // z^-2
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }

    /** True if both poles lie strictly inside the unit circle. */
    bool isStable() const noexcept
    {
        // Jury stability conditions for 1 + a1 z^-1 + a2 z^-2
        return std::abs (a2) < 1.0 && std::abs (a1) < 1.0 + a2;
    }
};

/** One biquad's filter state for one channel, transposed direct form II.

    TDF-II in double precision has low round-off noise and behaves well when
    coefficients change between blocks, which we rely on for smoothing.
*/
struct BiquadState
{
    double s1 = 0.0, s2 = 0.0;

    void reset() noexcept { s1 = s2 = 0.0; }

    inline double process (const BiquadCoefficients& c, double x) noexcept
    {
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

} // namespace peq
