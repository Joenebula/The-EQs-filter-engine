#pragma once

#include "../Parameters.h"
#include "../DSP/FilterDesign.h"
#include "../DSP/Dynamics.h"

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
    juce::RangedAudioParameter* dyn = nullptr;
    juce::RangedAudioParameter* thr = nullptr;
    juce::RangedAudioParameter* range = nullptr;
    juce::RangedAudioParameter* att = nullptr;
    juce::RangedAudioParameter* rel = nullptr;

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
    bool isDynamic() const noexcept   { return get (dyn) >= 0.5f; }
    /** Dynamics switched on and the type supports them. */
    bool dynamicsActive() const noexcept { return isDynamic() && dynamics::typeSupportsDynamics (filterType()); }

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
        r.dyn = get (BandField::dyn);
        r.thr = get (BandField::thr);
        r.range = get (BandField::range);
        r.att = get (BandField::att);
        r.rel = get (BandField::rel);
    }
    return refs;
}

//==============================================================================
// Copy and paste of a band's settings. The text goes on the system clipboard,
// so a band can be pasted into another ParaEQ instance too.

inline const char* bandClipboardTag() noexcept { return "ParaEQBand"; }

/** The band's settings (all fields except "used") as clipboard text. */
inline juce::String bandToClipboardText (const BandParamRefs& r)
{
    juce::XmlElement xml (bandClipboardTag());
    auto add = [&xml] (const char* name, const juce::RangedAudioParameter* p)
    {
        xml.setAttribute (name, (double) BandParamRefs::get (p));
    };
    add ("on", r.on); add ("type", r.type); add ("freq", r.freq); add ("gain", r.gain);
    add ("q", r.q); add ("slope", r.slope); add ("mode", r.mode);
    add ("dyn", r.dyn); add ("thr", r.thr); add ("range", r.range); add ("att", r.att); add ("rel", r.rel);
    return xml.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

/** True if the text is a copied ParaEQ band. */
inline bool isBandClipboardText (const juce::String& text)
{
    auto xml = juce::parseXML (text);
    return xml != nullptr && xml->hasTagName (bandClipboardTag());
}

/** Applies copied settings to a band (and marks it used). Each change is its
    own host gesture. Returns false if the text is not a copied band. */
inline bool pasteBandFromClipboardText (BandParamRefs& r, const juce::String& text)
{
    auto xml = juce::parseXML (text);
    if (xml == nullptr || ! xml->hasTagName (bandClipboardTag()))
        return false;
    auto put = [&xml] (const char* name, juce::RangedAudioParameter* p)
    {
        if (xml->hasAttribute (name))
            BandParamRefs::setWithGesture (p, (float) xml->getDoubleAttribute (name));
    };
    put ("type", r.type); put ("slope", r.slope); put ("mode", r.mode);
    put ("freq", r.freq); put ("gain", r.gain); put ("q", r.q);
    put ("dyn", r.dyn); put ("thr", r.thr); put ("range", r.range); put ("att", r.att); put ("rel", r.rel);
    put ("on", r.on);
    BandParamRefs::setWithGesture (r.used, 1.0f);
    return true;
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
