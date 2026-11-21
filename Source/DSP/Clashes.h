#pragma once

#include <vector>

namespace peq
{

/** A frequency range where another track (on the side-chain) is about as
    loud as this one, so the two fight for the same space: a candidate for a
    gentle, wide cut on one of them. */
struct Clash
{
    double lowHz = 0.0, highHz = 0.0;  // the core of the clash, where both are strongest
    double centreHz = 0.0;        // weighted towards where both are strongest
    double q = 1.0;               // from the width of the range
    double suggestedCutDb = -3.0; // negative; deeper when the other track is louder here
    double score = 0.0;           // width (octaves) x how strong both are; for ordering
};

/** Finds ranges where `otherDb` (the side-chain) masks `trackDb` (this
    track). Both are magnitude spectra in dB per FFT bin, as the analyser
    produces. A point clashes when both tracks are within 24 dB of their own
    loudest point there (so it matters to both) and the other track is no
    more than 6 dB quieter. Each range is trimmed to its core, where both
    are at least half as strong (in that 24 dB window) as at its peak. Levels are smoothed over 1/3 octave; ranges
    narrower than 1/3 octave are ignored. The strongest maxCount ranges are
    returned, strongest first. Framework-free; runs on the message thread. */
std::vector<Clash> findClashes (const std::vector<double>& trackDb, const std::vector<double>& otherDb,
                                double binWidthHz, int maxCount = 4,
                                double loHz = 30.0, double hiHz = 16000.0);

} // namespace peq
