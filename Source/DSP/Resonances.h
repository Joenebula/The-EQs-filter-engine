#pragma once

#include <vector>

namespace peq
{

/** A narrow peak that sticks out of the spectrum around it (a ring, a room
    mode, a harsh whistle): a candidate for a narrow cut. */
struct Resonance
{
    double frequencyHz = 0.0;
    double prominenceDb = 0.0;   // how far it rises above the 1-octave average around it
    double q = 4.0;              // from the width at half the prominence
};

/** Finds resonances in a magnitude spectrum (dB per FFT bin, as the
    analyser produces). Peaks must stand at least minProminenceDb above the
    spectrum's 1-octave average; peaks closer than 1/6 octave are merged; the
    most prominent maxCount are returned, strongest first. Searches
    loHz..hiHz. Framework-free; runs on the message thread. */
std::vector<Resonance> findResonances (const std::vector<double>& binsDb, double binWidthHz,
                                       double minProminenceDb = 6.0, int maxCount = 8,
                                       double loHz = 40.0, double hiHz = 16000.0);

} // namespace peq
