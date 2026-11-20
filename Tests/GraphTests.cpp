// Tests for the EQ graph's coordinate maths (Phase 4). Pure C++, no JUCE.

#include "TestFramework.h"
#include "../Source/GUI/GraphMath.h"

using namespace peq::graph;

TEST_CASE ("Graph: frequency <-> x round-trips and is logarithmic")
{
    const double w = 937.0;
    CHECK_NEAR (freqToX (minDisplayHz, w), 0.0, 1e-9);
    CHECK_NEAR (freqToX (maxDisplayHz, w), w, 1e-9);
    for (double hz : { 10.0, 31.6, 100.0, 1234.5, 9999.0, 30000.0 })
        CHECK_NEAR (xToFreq (freqToX (hz, w), w), hz, hz * 1e-12);
    // Every octave is the same width.
    const double o1 = freqToX (200.0, w) - freqToX (100.0, w);
    const double o2 = freqToX (8000.0, w) - freqToX (4000.0, w);
    CHECK_NEAR (o1, o2, 1e-9);
    // Out of range is clamped, not extrapolated.
    CHECK_NEAR (freqToX (1.0, w), 0.0, 1e-9);
    CHECK_NEAR (xToFreq (-50.0, w), minDisplayHz, 1e-9);
    CHECK_NEAR (xToFreq (w + 50.0, w), maxDisplayHz, 1e-6);
}

TEST_CASE ("Graph: dB <-> y round-trips with 0 dB in the middle")
{
    const double h = 400.0;
    for (double range : { 6.0, 12.0, 18.0, 30.0 })
    {
        CHECK_NEAR (dbToY (0.0, h, range), h / 2.0, 1e-12);
        CHECK_NEAR (dbToY (range, h, range), 0.0, 1e-12);
        CHECK_NEAR (dbToY (-range, h, range), h, 1e-12);
        for (double db : { -range, -3.3, 0.0, 1.5, range })
            CHECK_NEAR (yToDb (dbToY (db, h, range), h, range), db, 1e-12);
    }
}

TEST_CASE ("Graph: mouse wheel changes Q by a constant ratio, within limits")
{
    CHECK_NEAR (qAfterWheel (1.0, 1.0, 0.025, 40.0), 1.25, 1e-12);
    CHECK_NEAR (qAfterWheel (2.0, -1.0, 0.025, 40.0), 1.6, 1e-12);
    CHECK_NEAR (qAfterWheel (qAfterWheel (0.7, 3.0, 0.025, 40.0), -3.0, 0.025, 40.0), 0.7, 1e-12);
    CHECK_NEAR (qAfterWheel (39.0, 10.0, 0.025, 40.0), 40.0, 1e-12);
    CHECK_NEAR (qAfterWheel (0.03, -10.0, 0.025, 40.0), 0.025, 1e-12);
}
