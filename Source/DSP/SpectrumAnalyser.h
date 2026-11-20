#pragma once

#include <complex>
#include <vector>

namespace peq
{

/** In-place radix-2 complex FFT (forward). Size must be a power of two. */
class FFT
{
public:
    explicit FFT (int order = 12);
    int size() const noexcept { return n; }
    void perform (std::vector<std::complex<double>>& data) const noexcept;

private:
    int n;
    std::vector<std::complex<double>> twiddles;
    std::vector<int> bitReverse;
};

/** Turns a stream of samples into a smoothed magnitude spectrum in dBFS.

    Runs on the GUI thread (no real-time constraints). Each call to
    addSamples() appends audio; computeIfReady() analyses the most recent
    fftSize samples with a Hann window whenever at least hopSize new samples
    arrived (75 % overlap), then applies:
    - level calibration: a full-scale sine reads 0 dBFS at its frequency;
    - time smoothing: fast attack, release set by the "speed" setting;
    - peak hold: the highest level per bin, falling slowly.
*/
class SpectrumAnalyser
{
public:
    void configure (int fftOrder, double sampleRate);
    int fftSize() const noexcept { return fft.size(); }
    double sampleRate() const noexcept { return rate; }

    /** Release time of the display, in seconds per 20 dB fall. */
    void setReleaseSeconds (double secondsPer20dB) noexcept { releaseTime = secondsPer20dB; }
    void setPeakHold (bool enabled) noexcept;
    void reset();

    void addSamples (const float* data, int count);

    /** Returns true if a new spectrum was computed. elapsedSeconds is the time
        since the previous call (used for release and peak decay). */
    bool computeIfReady (double elapsedSeconds);

    /** Smoothed level per bin (fftSize/2 + 1 values), dBFS. */
    const std::vector<double>& levelsDb() const noexcept { return smoothedDb; }
    const std::vector<double>& peaksDb() const noexcept { return peakDb; }

    /** Frequency of a bin. */
    double binFrequency (int bin) const noexcept { return bin * rate / (double) fft.size(); }

    /** Level at an arbitrary frequency, averaged over a band `octaves` wide
        (fractional-octave smoothing; 0 = nearest bin, interpolated). */
    static double levelAt (const std::vector<double>& binsDb, double frequencyHz, double binWidthHz, double octaves) noexcept;

    /** Fast version of levelAt for drawing many points: build a running sum
        of bin powers once per frame, then each lookup is O(1). */
    static void buildPowerPrefix (const std::vector<double>& binsDb, std::vector<double>& prefix);
    static double levelAtPrefix (const std::vector<double>& prefix, double frequencyHz, double binWidthHz, double octaves) noexcept;

    static constexpr double floorDb = -150.0;

private:
    FFT fft { 12 };
    double rate = 48000.0;
    std::vector<float> history;   // circular, last fftSize samples
    int historyPos = 0;
    int newSamples = 0;
    std::vector<double> window;
    double windowGain = 1.0;
    std::vector<std::complex<double>> work;
    std::vector<double> smoothedDb, peakDb;
    double releaseTime = 0.5;
    bool peakHold = false;
};

} // namespace peq
