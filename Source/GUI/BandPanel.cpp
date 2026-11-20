#include "BandPanel.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace peq::gui
{

BandPanel::BandPanel (ParaEQAudioProcessor& p, AllBands& b)
    : processor (p), bands (b)
{
    auto setupLabel = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (theme::font (11.0f));
        addAndMakeVisible (l);
    };

    typeBox.addItemList (typeNames(), 1);
    juce::StringArray slopes;
    for (int s : limits::slopesDbPerOct)
        slopes.add (juce::String (s) + " dB/oct");
    slopeBox.addItemList (slopes, 1);
    modeBox.addItemList (modeNames(), 1);

    for (auto* c : { &typeBox, &slopeBox, &modeBox })
    {
        c->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*c);
    }

    for (auto* k : { &freqKnob, &gainKnob, &qKnob })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        addAndMakeVisible (*k);
    }
    gainKnob.setDoubleClickReturnValue (true, 0.0);

    setupLabel (typeLabel, "TYPE");
    setupLabel (slopeLabel, "SLOPE");
    setupLabel (modeLabel, "CHANNELS");
    setupLabel (freqLabel, "FREQ");
    setupLabel (gainLabel, "GAIN");
    setupLabel (qLabel, "Q");

    enableButton.setClickingTogglesState (true);
    soloButton.setClickingTogglesState (false);
    soloButton.onClick = [this]
    {
        if (band >= 0 && onSoloToggled)
            onSoloToggled (band);
    };
    deleteButton.onClick = [this]
    {
        if (band >= 0 && onDeleteRequested)
            onDeleteRequested (band);
    };
    for (auto* bt : { &enableButton, &soloButton, &deleteButton })
        addAndMakeVisible (*bt);

    showBand (-1);
    startTimerHz (10);
}

BandPanel::~BandPanel() = default;

void BandPanel::showBand (int newBand)
{
    // Destroy old attachments before re-attaching.
    typeAtt.reset(); slopeAtt.reset(); modeAtt.reset();
    freqAtt.reset(); gainAtt.reset(); qAtt.reset(); enableAtt.reset();

    band = newBand;
    const bool has = band >= 0;
    for (auto* c : getChildren())
        c->setVisible (has);

    if (has)
    {
        using params::BandField;
        auto& state = processor.getState();
        auto id = [this] (BandField f) { return params::bandID (band, f); };
        typeAtt  = std::make_unique<ComboAttachment> (state, id (BandField::type), typeBox);
        slopeAtt = std::make_unique<ComboAttachment> (state, id (BandField::slope), slopeBox);
        modeAtt  = std::make_unique<ComboAttachment> (state, id (BandField::mode), modeBox);
        freqAtt  = std::make_unique<SliderAttachment> (state, id (BandField::freq), freqKnob);
        gainAtt  = std::make_unique<SliderAttachment> (state, id (BandField::gain), gainKnob);
        qAtt     = std::make_unique<SliderAttachment> (state, id (BandField::q), qKnob);
        enableAtt = std::make_unique<ButtonAttachment> (state, id (BandField::on), enableButton);

        const auto colour = theme::bandColour (band);
        for (auto* k : { &freqKnob, &gainKnob, &qKnob })
            k->setColour (juce::Slider::rotarySliderFillColourId, colour);
        enableButton.setColour (juce::TextButton::buttonOnColourId, colour);
        updateEnablement();
    }
    repaint();
}

void BandPanel::updateEnablement()
{
    if (band < 0)
        return;
    const auto type = bands[(size_t) band].filterType();
    gainKnob.setEnabled (typeHasGain (type));
    slopeBox.setEnabled (typeHasSlope (type));

    const bool soloed = processor.getSoloBand() == band;
    soloButton.setToggleState (soloed, juce::dontSendNotification);
    soloButton.setColour (juce::TextButton::buttonOnColourId, theme::warning);
}

void BandPanel::timerCallback()
{
    updateEnablement();
}

void BandPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::panelEdge);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    auto area = getLocalBounds().reduced (12, 8);
    if (band < 0)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.0f));
        g.drawFittedText ("Click the graph to add a band.  Drag a dot to move it (Shift = fine).  "
                          "Mouse wheel = Q.  Double-click = reset gain.  Right-click = more options.",
                          area, juce::Justification::centred, 2);
        return;
    }

    auto badge = area.removeFromLeft (64).toFloat().withSizeKeepingCentre (56.0f, 56.0f);
    g.setColour (theme::bandColour (band));
    g.fillEllipse (badge);
    g.setColour (theme::graphBg);
    g.setFont (theme::font (22.0f, true));
    g.drawText (juce::String (band + 1), badge, juce::Justification::centred);
}

void BandPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    area.removeFromLeft (72);  // badge

    auto buttons = area.removeFromRight (80);
    const int bh = juce::jmin (24, (buttons.getHeight() - 8) / 3);
    enableButton.setBounds (buttons.removeFromTop (bh)); buttons.removeFromTop (4);
    soloButton.setBounds (buttons.removeFromTop (bh));   buttons.removeFromTop (4);
    deleteButton.setBounds (buttons.removeFromTop (bh));
    area.removeFromRight (12);

    auto column = [&area] (int width) { auto c = area.removeFromLeft (width); area.removeFromLeft (10); return c; };

    auto placeCombo = [] (juce::Rectangle<int> c, juce::Label& l, juce::ComboBox& box)
    {
        l.setBounds (c.removeFromTop (16));
        box.setBounds (c.removeFromTop (26).reduced (0, 1));
    };
    auto combos = column (130);
    auto top = combos.removeFromTop (combos.getHeight() / 2);
    placeCombo (top, typeLabel, typeBox);
    placeCombo (combos, slopeLabel, slopeBox);
    auto combos2 = column (110);
    placeCombo (combos2.removeFromTop (combos2.getHeight() / 2), modeLabel, modeBox);

    auto placeKnob = [] (juce::Rectangle<int> c, juce::Label& l, juce::Slider& k)
    {
        l.setBounds (c.removeFromTop (16));
        k.setBounds (c);
    };
    const int knobW = juce::jlimit (70, 110, area.getWidth() / 3 - 10);
    placeKnob (column (knobW), freqLabel, freqKnob);
    placeKnob (column (knobW), gainLabel, gainKnob);
    placeKnob (column (knobW), qLabel, qKnob);
}

} // namespace peq::gui
