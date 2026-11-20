#include "Parameters.h"

#include "DSP/Dynamics.h"

#include <cmath>

namespace peq::params
{

using namespace juce;

String bandID (int bandIndex, BandField field)
{
    static const char* const suffix[] = { "on", "type", "freq", "gain", "q", "slope", "mode", "used", "dyn", "thr", "range", "att", "rel" };
    return "b" + String (bandIndex + 1).paddedLeft ('0', 2) + "_" + suffix[(int) field];
}

float defaultFrequency (int bandIndex)
{
    // 30 Hz .. 16 kHz, evenly spaced on a log scale.
    const double t = numBands > 1 ? (double) bandIndex / (double) (numBands - 1) : 0.0;
    return (float) (30.0 * std::pow (16000.0 / 30.0, t));
}

namespace
{
    /** Range that moves evenly on a log scale (frequency, Q). */
    NormalisableRange<float> logRange (float lo, float hi)
    {
        return NormalisableRange<float> (
            lo, hi,
            // Double precision inside, so saving and reloading a project
            // does not nudge values (float pow/log drift by ~1 ppm per trip).
            [] (float start, float end, float v)
            { return (float) ((double) start * std::pow ((double) end / (double) start, (double) v)); },
            [] (float start, float end, float x)
            { return (float) (std::log ((double) x / (double) start) / std::log ((double) end / (double) start)); },
            [] (float start, float end, float x) { return jlimit (start, end, x); });
    }

    String frequencyText (float hz, int)
    {
        return hz >= 1000.0f ? String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz"
                             : String (hz, hz >= 100.0f ? 0 : 1) + " Hz";
    }

    float frequencyValue (const String& text)
    {
        const auto t = text.trim().toLowerCase();
        const float v = t.getFloatValue();
        return t.contains ("k") ? v * 1000.0f : v;
    }

    StringArray slopeNames()
    {
        StringArray s;
        for (int v : limits::slopesDbPerOct)
            s.add (String (v) + " dB/oct");
        return s;
    }
}

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dbAttributes = AudioParameterFloatAttributes()
                            .withLabel ("dB")
                            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { inputGain, version }, "Input Gain",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                                       dbAttributes));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { outputGain, version }, "Output Gain",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                                       dbAttributes));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, version }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { analogMatch, version }, "Analog Match", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { gainScale, version }, "Gain Scale",
                                                       NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
                                                       AudioParameterFloatAttributes()
                                                           .withLabel ("%")
                                                           .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " %"; })
                                                           .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); })));

    // Order must match peq::FilterType and peq::ChannelMode.
    const StringArray typeNames { "Bell", "Low Shelf", "High Shelf", "Low-pass", "High-pass", "Notch", "Band-pass" };
    const StringArray modeNames { "Stereo", "Left", "Right", "Mid", "Side" };
    const StringArray slopes = slopeNames();
    const int defaultSlopeIndex = 1;  // 12 dB/oct
    jassert (typeNames.size() == (int) FilterType::NumTypes);
    jassert (modeNames.size() == (int) ChannelMode::NumModes);
    jassert (slopes[defaultSlopeIndex] == "12 dB/oct");

    const auto freqAttributes = AudioParameterFloatAttributes()
                                    .withLabel ("Hz")
                                    .withStringFromValueFunction (frequencyText)
                                    .withValueFromStringFunction (frequencyValue);
    const auto qAttributes = AudioParameterFloatAttributes()
                                 .withStringFromValueFunction ([] (float v, int) { return String (v, v < 1.0f ? 3 : 2); });

    const auto msAttributes = AudioParameterFloatAttributes()
                                  .withLabel ("ms")
                                  .withStringFromValueFunction ([] (float v, int)
                                  {
                                      return v >= 1000.0f ? String (v / 1000.0f, 2) + " s"
                                                          : String (v, v < 10.0f ? 1 : 0) + " ms";
                                  })
                                  .withValueFromStringFunction ([] (const String& t)
                                  {
                                      const auto s = t.trim().toLowerCase();
                                      const float v = s.getFloatValue();
                                      return (s.endsWith ("s") && ! s.endsWith ("ms")) ? v * 1000.0f : v;
                                  });

    for (int b = 0; b < numBands; ++b)
    {
        const String n = "Band " + String (b + 1) + " ";
        auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), "Band " + String (b + 1), " | ");

        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { bandID (b, BandField::used), version },
                                                               n + "Used", false));
        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { bandID (b, BandField::on), version },
                                                               n + "Enabled", true));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandID (b, BandField::type), version },
                                                                 n + "Type", typeNames, 0));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandID (b, BandField::freq), version },
                                                                n + "Freq",
                                                                logRange ((float) limits::minFrequency, (float) limits::maxFrequency),
                                                                defaultFrequency (b), freqAttributes));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandID (b, BandField::gain), version },
                                                                n + "Gain",
                                                                NormalisableRange<float> ((float) limits::minGainDb, (float) limits::maxGainDb, 0.01f),
                                                                0.0f, dbAttributes));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandID (b, BandField::q), version },
                                                                n + "Q",
                                                                logRange ((float) limits::minQ, (float) limits::maxQ),
                                                                (float) limits::defaultQ, qAttributes));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandID (b, BandField::slope), version },
                                                                 n + "Slope", slopes, defaultSlopeIndex));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandID (b, BandField::mode), version },
                                                                 n + "Channels", modeNames, 0));

        // Dynamic EQ (Phase 6).
        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { bandID (b, BandField::dyn), version },
                                                               n + "Dynamic", false));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandID (b, BandField::thr), version },
                                                                n + "Threshold",
                                                                NormalisableRange<float> ((float) dynamics::minThresholdDb, (float) dynamics::maxThresholdDb, 0.1f),
                                                                -24.0f, dbAttributes));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandID (b, BandField::range), version },
                                                                n + "Range",
                                                                NormalisableRange<float> ((float) dynamics::minRangeDb, (float) dynamics::maxRangeDb, 0.1f),
                                                                -6.0f, dbAttributes));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandID (b, BandField::att), version },
                                                                n + "Attack",
                                                                logRange ((float) dynamics::minAttackMs, (float) dynamics::maxAttackMs),
                                                                10.0f, msAttributes));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandID (b, BandField::rel), version },
                                                                n + "Release",
                                                                logRange ((float) dynamics::minReleaseMs, (float) dynamics::maxReleaseMs),
                                                                150.0f, msAttributes));
        layout.add (std::move (group));
    }

    return layout;
}

} // namespace peq::params
