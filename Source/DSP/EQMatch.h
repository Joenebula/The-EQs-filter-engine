#pragma once

#include "FilterDesign.h"
#include "SpectrumAnalyser.h"

#include <complex>
#include <vector>

namespace peq
{

/** EQ Match: learns the long-term average spectrum of two signals (this
    track, the "source", and a reference) and works out a set of EQ bands
    that make the source's tonal balance follow the reference's.

    Runs on the message thread (no real-time constraints). Frames quieter
    than -60 dBFS are ignored, so the two signals may be learned at
    different times (for example, solo the reference, then play the track).
    Overall level differences are removed: only the tonal balance is matched.
*/
class EQMatch
{
public:
    void prepare (double sampleRate);
    void reset();

    void addSource (const float* data, int count)    { source.add (data, count, *this); }
    void addReference (const float* data, int count) { reference.add (data, count, *this); }

    /** Seconds of non-silent audio learned so far. */
    double sourceSeconds() const noexcept    { return source.frames * hop / rate; }
    double referenceSeconds() const noexcept { return reference.frames * hop / rate; }
    /** At least a second of each. */
    bool hasEnough() const noexcept { return sourceSeconds() >= 1.0 && referenceSeconds() >= 1.0; }

    /** The correction the source needs (reference minus source, level-
        normalised, 1/3-octave smoothed, clamped to +/-maxCorrectionDb), at
        each frequency. 0 where either signal has no content. */
    void differenceDb (const double* frequencies, int count, double* out) const;

    /** Up to maxBands bands (bells, plus shelves at the ends) whose combined
        curve follows `amount` (0..1) of the difference. */
    std::vector<BandParameters> makeBands (int maxBands, double amount, bool analogMatched) const;

    /** Fits bands to a target curve (dB at log-spaced frequencies). Exposed for tests. */
    static std::vector<BandParameters> fit (const std::vector<double>& frequencies, const std::vector<double>& targetDb,
                                            int maxBands, double sampleRate, bool analogMatched);

    /** Log-spaced frequencies (1/24 octave) the fit works on, up to 20 kHz or 0.45 fs. */
    static std::vector<double> fitFrequencies (double sampleRate);

    static constexpr double maxCorrectionDb = 12.0;
    static constexpr double silenceDb = -60.0;

private:
    struct Stream
    {
        std::vector<float> history;
        int pos = 0;
        int fresh = 0;
        std::vector<double> powerSum;
        double frames = 0.0;
        void add (const float* data, int count, EQMatch& owner);
        void clear();
    };

    void analyse (Stream& s);
    std::vector<double> averageDb (const Stream& s) const;

    double rate = 48000.0;
    int fftSize = 8192;
    double hop = 4096.0;
    FFT fft { 13 };
    std::vector<double> window;
    double windowPower = 1.0;
    std::vector<std::complex<double>> work;
    Stream source, reference;
};

} // namespace peq
