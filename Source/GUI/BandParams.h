#pragma once

#include "../Parameters.h"
#include "../DSP/FilterDesign.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace peq::gui
{

/** Typed access to one band's host parameters from the GUI. All writes go
    through setValueNotifyingHost (so Cubase records automation and the undo
    manager sees them); callers wrap drags in begin/endGesture. */
struct BandParamRefs
{
    juce::RangedAudioParameter* used = nullptr;
    juce::RangedAudioParameter* on = nullptr;
    juce::RangedAudioParameter* type = nullptr;
    juce::RangedAudioParameter* freq = nullptr;
    juce::RangedAudioParameter* gain = nullptr;
    juce::RangedAudioParameter* q = nullptr;
    juce::RangedAudioParameter* slope = nullptr;
    juce::RangedAudioParameter* mode = nullptr;

    static float get (const juce::RangedAudioParameter* p) noexcept
    {
        return p->convertFrom0to1 (p->getValue());
    }

    static void set (juce::RangedAudioParameter* p, float value)
    {
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    /** One-shot change wrapped in its own gesture (for clicks and menu items). */
    static void setWithGesture (juce::RangedAudioParameter* p, float value)
    {
        p->beginChangeGesture();
        set (p, value);
        p->endChangeGesture();
    }

    bool isUsed() const noexcept      { return get (used) >= 0.5f; }
    bool isEnabled() const noexcept   { return get (on) >= 0.5f; }
    FilterType filterType() const noexcept { return (FilterType) juce::roundToInt (get (type)); }
    ChannelMode channelMode() const noexcept { return (ChannelMode) juce::roundToInt (get (mode)); }

    int slopeDbPerOct() const noexcept
    {
        const auto& s = limits::slopesDbPerOct;
        return s[(size_t) juce::jlimit (0, (int) s.size() - 1, juce::roundToInt (get (slope)))];
    }

    BandParameters toBandParameters() const noexcept
    {
        BandParameters p;
        p.type = filterType();
        p.frequencyHz = (double) get (freq);
        p.gainDb = (double) get (gain);
        p.q = (double) get (q);
        p.slopeDbPerOct = slopeDbPerOct();
        p.enabled = isUsed() && isEnabled();
        return p;
    }
};

using AllBands = std::array<BandParamRefs, FilterEngine::numBands>;

inline AllBands makeBandRefs (juce::AudioProcessorValueTreeState& state)
{
    using params::BandField;
    AllBands refs;
    for (int b = 0; b < FilterEngine::numBands; ++b)
    {
        auto get = [&] (BandField f)
        {
            auto* p = state.getParameter (params::bandID (b, f));
            jassert (p != nullptr);
            return p;
        };
        auto& r = refs[(size_t) b];
        r.used = get (BandField::used);
        r.on = get (BandField::on);
        r.type = get (BandField::type);
        r.freq = get (BandField::freq);
        r.gain = get (BandField::gain);
        r.q = get (BandField::q);
        r.slope = get (BandField::slope);
        r.mode = get (BandField::mode);
    }
    return refs;
}

/** True for filter types whose gain control does something. */
inline bool typeHasGain (FilterType t) noexcept
{
    return t == FilterType::Bell || t == FilterType::LowShelf || t == FilterType::HighShelf;
}

/** True for filter types whose slope control does something. */
inline bool typeHasSlope (FilterType t) noexcept
{
    return t == FilterType::LowPass || t == FilterType::HighPass || t == FilterType::BandPass
        || t == FilterType::LowShelf || t == FilterType::HighShelf;
}

inline juce::String formatFrequency (double hz)
{
    return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz"
                        : juce::String (hz, hz >= 100.0 ? 0 : 1) + " Hz";
}

inline juce::String formatGain (double db)
{
    return (db > 0.0 ? "+" : "") + juce::String (db, 1) + " dB";
}

inline const juce::StringArray& typeNames()
{
    static const juce::StringArray names { "Bell", "Low Shelf", "High Shelf", "Low-pass", "High-pass", "Notch", "Band-pass" };
    return names;
}

inline const juce::StringArray& modeNames()
{
    static const juce::StringArray names { "Stereo", "Left", "Right", "Mid", "Side" };
    return names;
}

inline const char* modeTag (ChannelMode m) noexcept
{
    switch (m)
    {
        case ChannelMode::Left:  return "L";
        case ChannelMode::Right: return "R";
        case ChannelMode::Mid:   return "M";
        case ChannelMode::Side:  return "S";
        case ChannelMode::Stereo:
        case ChannelMode::NumModes: return "";
    }
    return "";
}

} // namespace peq::gui
