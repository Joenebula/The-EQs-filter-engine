#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Colours and fonts for ParaEQ's own (original) dark look. */
namespace peq::theme
{
    inline const juce::Colour background   { 0xff111317 };
    inline const juce::Colour panel        { 0xff1a1d23 };
    inline const juce::Colour panelEdge    { 0xff2a2e37 };
    inline const juce::Colour graphBg      { 0xff0c0e12 };
    inline const juce::Colour gridMajor    { 0xff262a33 };
    inline const juce::Colour gridMinor    { 0xff181b21 };
    inline const juce::Colour gridText     { 0xff6c7482 };
    inline const juce::Colour text         { 0xffd9dee7 };
    inline const juce::Colour textDim      { 0xff8b93a1 };
    inline const juce::Colour curve        { 0xfff2f4f8 };
    inline const juce::Colour accent       { 0xff4fc3a1 };
    inline const juce::Colour warning      { 0xffe8b04a };

    /** One colour per band, spread around the hue circle and repeating every
        8 bands with slightly different brightness, so neighbours differ. */
    inline juce::Colour bandColour (int bandIndex)
    {
        static const float hues[] = { 0.55f, 0.08f, 0.33f, 0.80f, 0.15f, 0.62f, 0.95f, 0.45f };
        const float hue = hues[bandIndex % 8];
        const float bright = 0.95f - 0.12f * (float) ((bandIndex / 8) % 3);
        return juce::Colour::fromHSV (hue, 0.62f, bright, 1.0f);
    }

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }
}
