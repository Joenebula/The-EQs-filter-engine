#pragma once

#include "FilterDesign.h"
#include "SpectrumAnalyser.h"   // FFT

#include <array>
#include <atomic>
#include <complex>
#include <vector>

namespace peq
{

/** Linear-phase EQ (1.5): the combined magnitude of a set of bands as one
    symmetric FIR filter, so every frequency is delayed by the same amount
    (no phase shift between frequencies), applied by fast convolution.

    Kernel: the bands' magnitude is sampled on an N-point grid (zero phase),
    turned into an impulse by an inverse FFT, centred and Blackman-windowed.
    N grows with the sample rate (8192 taps at 44.1/48 kHz, about 170 ms),
    so the frequency detail is about the same at every rate. The window
    smooths very narrow features below roughly 50 Hz.

    Convolution: uniformly partitioned (partitionSize samples), so the cost
    is low and constant. Latency = N/2 (the kernel's centre) + partitionSize.

    Threads: prepare() and setKernel() on the message thread, process() on
    the audio thread. A new kernel is written into a spare slot and picked
    up by the audio thread at the next partition, crossfading from the old
    one over one partition (no clicks). Real-time safe after prepare().
*/
class LinearPhaseFIR
{
public:
    static constexpr int partitionSize = 256;
    static constexpr int maxChannels = 2;

    /** Kernel length for a sample rate (a power of two). */
    static int kernelLengthFor (double sampleRate) noexcept;

    /** Designs the zero-phase-centred, windowed kernel (n taps) for the
        enabled bands. Message thread (allocates nothing if `out` already
        has n elements). */
    static void designKernel (const BandParameters* bands, int numBands, double sampleRate,
                              int n, std::vector<double>& out);

    void prepare (double sampleRate);
    void reset() noexcept;

    int kernelLength() const noexcept { return taps; }
    int latencySamples() const noexcept { return taps / 2 + partitionSize; }

    /** Installs a new kernel (taps values, see designKernel). Returns false
        if the previous one has not been picked up yet (try again later). */
    bool setKernel (const std::vector<double>& kernel);

    /** True once a kernel has been installed and picked up. */
    bool hasKernel() const noexcept { return active.load() >= 0; }

    void process (double* const* channels, int numChannels, int numSamples) noexcept;

private:
    using Complex = std::complex<double>;

    void processPartition (int channel) noexcept;
    void convolve (int channel, int slot, std::vector<Complex>& out) noexcept;

    int taps = 0, numPartitions = 0, fftSize = 2 * partitionSize;
    FFT fft { 9 };

    // Kernel spectra: [slot][partition * fftSize + bin].
    std::array<std::vector<Complex>, 2> kernels;
    std::atomic<int> active { -1 };    // slot in use (-1 none yet)
    std::atomic<int> pending { -1 };   // slot waiting to be picked up

    struct Channel
    {
        std::vector<Complex> history;   // input spectra, numPartitions x fftSize (circular)
        std::vector<double> inBlock;    // last 2 * partitionSize input samples
        std::vector<double> outBlock;   // partitionSize output samples ready to play
        int fill = 0;                   // samples collected in the current partition
        int head = 0;                   // newest history slot
    };
    std::array<Channel, maxChannels> chans;
    std::vector<Complex> scratch, accum, accumOld;
    int fadeSlot = -1;                  // old slot while crossfading
};

} // namespace peq
