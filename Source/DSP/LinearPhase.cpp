#include "LinearPhase.h"

#include <algorithm>
#include <cmath>

namespace peq
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    int orderOf (int n) noexcept
    {
        int o = 0;
        while ((1 << o) < n)
            ++o;
        return o;
    }

    /** Inverse FFT through the forward one: conj (fft (conj (x))) / n. */
    void inverse (const FFT& fft, std::vector<std::complex<double>>& x) noexcept
    {
        for (auto& v : x) v = std::conj (v);
        fft.perform (x);
        const double scale = 1.0 / (double) x.size();
        for (auto& v : x) v = std::conj (v) * scale;
    }
}

int LinearPhaseFIR::kernelLengthFor (double sampleRate) noexcept
{
    if (sampleRate <= 50000.0)  return 8192;
    if (sampleRate <= 100000.0) return 16384;
    return 32768;
}

void LinearPhaseFIR::designKernel (const BandParameters* bands, int numBands, double sampleRate,
                                   int n, std::vector<double>& out)
{
    out.assign ((size_t) n, 0.0);
    const FFT fft (orderOf (n));
    std::vector<std::complex<double>> spectrum ((size_t) n, 1.0);

    // Zero-phase magnitude on the FFT grid (bins 0..n/2, mirrored).
    CascadeCoefficients c;
    for (int b = 0; b < numBands; ++b)
    {
        if (! bands[b].enabled)
            continue;
        FilterDesign::design (bands[b], sampleRate, c);
        for (int k = 0; k <= n / 2; ++k)
        {
            const double f = (double) k * sampleRate / (double) n;
            const double m = c.magnitude (std::max (f, 0.001), sampleRate);
            spectrum[(size_t) k] *= m;
            if (k > 0 && k < n / 2)
                spectrum[(size_t) (n - k)] *= m;
        }
    }

    inverse (fft, spectrum);

    // Centre it (the zero-phase impulse wraps around index 0) and window.
    for (int i = 0; i < n; ++i)
    {
        const int src = (i + n / 2) % n;
        const double x = (double) i / (double) n;
        const double w = 0.42 - 0.5 * std::cos (2.0 * pi * x) + 0.08 * std::cos (4.0 * pi * x);   // Blackman
        out[(size_t) i] = spectrum[(size_t) src].real() * w;
    }
}

void LinearPhaseFIR::prepare (double sampleRate)
{
    taps = kernelLengthFor (sampleRate);
    numPartitions = taps / partitionSize;
    for (auto& k : kernels)
        k.assign ((size_t) (numPartitions * fftSize), Complex {});
    for (auto& ch : chans)
    {
        ch.history.assign ((size_t) (numPartitions * fftSize), Complex {});
        ch.inBlock.assign ((size_t) fftSize, 0.0);
        ch.outBlock.assign ((size_t) partitionSize, 0.0);
    }
    scratch.assign ((size_t) fftSize, Complex {});
    accum.assign ((size_t) fftSize, Complex {});
    accumOld.assign ((size_t) fftSize, Complex {});
    active = -1;
    pending = -1;
    fadeSlot = -1;
    reset();
}

void LinearPhaseFIR::reset() noexcept
{
    for (auto& ch : chans)
    {
        std::fill (ch.history.begin(), ch.history.end(), Complex {});
        std::fill (ch.inBlock.begin(), ch.inBlock.end(), 0.0);
        std::fill (ch.outBlock.begin(), ch.outBlock.end(), 0.0);
        ch.fill = 0;
        ch.head = 0;
    }
    fadeSlot = -1;
}

bool LinearPhaseFIR::setKernel (const std::vector<double>& kernel)
{
    if (numPartitions == 0 || (int) kernel.size() != taps || pending.load() >= 0)
        return false;

    const int slot = active.load() == 0 ? 1 : 0;
    auto& dest = kernels[(size_t) slot];
    std::vector<Complex> buf ((size_t) fftSize);
    for (int p = 0; p < numPartitions; ++p)
    {
        std::fill (buf.begin(), buf.end(), Complex {});
        for (int i = 0; i < partitionSize; ++i)
            buf[(size_t) i] = kernel[(size_t) (p * partitionSize + i)];
        fft.perform (buf);
        std::copy (buf.begin(), buf.end(), dest.begin() + (long) p * fftSize);
    }
    pending = slot;
    return true;
}

void LinearPhaseFIR::convolve (int channel, int slot, std::vector<Complex>& out) noexcept
{
    auto& ch = chans[(size_t) channel];
    const auto& k = kernels[(size_t) slot];
    std::fill (out.begin(), out.end(), Complex {});
    for (int p = 0; p < numPartitions; ++p)
    {
        const int h = (ch.head - p + numPartitions) % numPartitions;
        const Complex* x = ch.history.data() + (size_t) h * (size_t) fftSize;
        const Complex* kk = k.data() + (size_t) p * (size_t) fftSize;
        for (int i = 0; i < fftSize; ++i)
            out[(size_t) i] += x[i] * kk[i];
    }
    inverse (fft, out);
}

void LinearPhaseFIR::processPartition (int channel) noexcept
{
    auto& ch = chans[(size_t) channel];

    // Spectrum of the last two partitions of input (overlap-save).
    for (int i = 0; i < fftSize; ++i)
        scratch[(size_t) i] = ch.inBlock[(size_t) i];
    fft.perform (scratch);
    ch.head = (ch.head + 1) % numPartitions;
    std::copy (scratch.begin(), scratch.end(), ch.history.begin() + (long) ch.head * fftSize);

    const int slot = active.load();
    if (slot < 0)
    {
        std::fill (ch.outBlock.begin(), ch.outBlock.end(), 0.0);
        return;
    }

    convolve (channel, slot, accum);
    if (fadeSlot >= 0)
    {
        convolve (channel, fadeSlot, accumOld);
        for (int i = 0; i < partitionSize; ++i)
        {
            const double t = ((double) i + 0.5) / (double) partitionSize;
            ch.outBlock[(size_t) i] = t * accum[(size_t) (partitionSize + i)].real()
                                    + (1.0 - t) * accumOld[(size_t) (partitionSize + i)].real();
        }
    }
    else
    {
        for (int i = 0; i < partitionSize; ++i)
            ch.outBlock[(size_t) i] = accum[(size_t) (partitionSize + i)].real();
    }
}

void LinearPhaseFIR::process (double* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::clamp (numChannels, 0, maxChannels);
    if (numPartitions == 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = chans[(size_t) c];
            const double x = channels[c][i];
            channels[c][i] = ch.outBlock[(size_t) ch.fill];
            ch.inBlock[(size_t) (partitionSize + ch.fill)] = x;
        }

        auto& first = chans[0];
        if (++first.fill < partitionSize)
        {
            for (int c = 1; c < numChannels; ++c)
                chans[(size_t) c].fill = first.fill;
            continue;
        }

        // A partition is complete: pick up a new kernel (crossfading from the
        // old one for this partition), then run each channel.
        fadeSlot = -1;
        const int next = pending.load();
        if (next >= 0)
        {
            fadeSlot = active.load();
            active = next;
        }
        for (int c = 0; c < numChannels; ++c)
        {
            processPartition (c);
            auto& ch = chans[(size_t) c];
            std::copy (ch.inBlock.begin() + partitionSize, ch.inBlock.end(), ch.inBlock.begin());
            ch.fill = 0;
        }
        fadeSlot = -1;
        // Only now may the message thread reuse the old slot.
        if (next >= 0)
            pending = -1;
    }
}

} // namespace peq
