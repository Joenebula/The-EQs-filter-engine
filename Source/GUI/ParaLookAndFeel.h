#pragma once

#include "Theme.h"

namespace peq::gui
{

/** ParaEQ's own dark look for the standard JUCE widgets. */
class ParaLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ParaLookAndFeel()
    {
        using namespace juce;
        setColourScheme ({ theme::panel, theme::background, theme::panel, theme::panelEdge,
                           theme::text, theme::panel, theme::text, theme::accent, theme::text });

        setColour (ComboBox::backgroundColourId, theme::graphBg);
        setColour (ComboBox::outlineColourId, theme::panelEdge);
        setColour (ComboBox::textColourId, theme::text);
        setColour (ComboBox::arrowColourId, theme::textDim);
        setColour (PopupMenu::backgroundColourId, theme::panel);
        setColour (PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.35f));
        setColour (TextButton::buttonColourId, theme::graphBg);
        setColour (TextButton::buttonOnColourId, theme::accent.withAlpha (0.8f));
        setColour (TextButton::textColourOffId, theme::text);
        setColour (TextButton::textColourOnId, theme::graphBg);
        setColour (Slider::textBoxTextColourId, theme::text);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour (Slider::rotarySliderFillColourId, theme::accent);
        setColour (Slider::rotarySliderOutlineColourId, theme::panelEdge);
        setColour (Label::textColourId, theme::textDim);
        setColour (ToggleButton::textColourId, theme::text);
        setColour (ToggleButton::tickColourId, theme::accent);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace juce;
        const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float lineW = jmax (2.0f, radius * 0.14f);
        const float arcR = radius - lineW * 0.5f;
        const auto fill = slider.findColour (Slider::rotarySliderFillColourId);
        const bool enabled = slider.isEnabled();

        Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::panelEdge);
        g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

        // Bipolar parameters (gain) fill from the centre.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float from = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
        Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, jmin (from, angle), jmax (from, angle), true);
        g.setColour (enabled ? fill : theme::textDim.withAlpha (0.4f));
        g.strokePath (value, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

        const float knobR = radius - lineW * 1.8f;
        g.setColour (theme::graphBg);
        g.fillEllipse (Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre));
        Path pointer;
        pointer.addRoundedRectangle (-1.25f, -knobR, 2.5f, knobR * 0.55f, 1.0f);
        g.setColour (enabled ? theme::text : theme::textDim);
        g.fillPath (pointer, AffineTransform::rotation (angle).translated (centre));
    }
};

} // namespace peq::gui
