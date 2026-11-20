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

    for (auto* k : { &freqKnob, &gainKnob, &qKnob, &thrKnob, &rangeKnob, &attKnob, &relKnob })
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
    setupLabel (thrLabel, "THRESHOLD");
    setupLabel (rangeLabel, "RANGE");
    setupLabel (attLabel, "ATTACK");
    setupLabel (relLabel, "RELEASE");
    rangeKnob.setDoubleClickReturnValue (true, 0.0);
    dynButton.setClickingTogglesState (true);
    dynButton.setTooltip ("Dynamic EQ: the band's gain moves by up to Range while its frequency region is above Threshold");
    addAndMakeVisible (dynButton);

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
    startTimerHz (20);
}

BandPanel::~BandPanel() = default;

void BandPanel::showBand (int newBand)
{
    // Destroy old attachments before re-attaching.
    typeAtt.reset(); slopeAtt.reset(); modeAtt.reset();
    freqAtt.reset(); gainAtt.reset(); qAtt.reset(); enableAtt.reset();
    thrAtt.reset(); rangeAtt.reset(); attAtt.reset(); relAtt.reset(); dynAtt.reset();
    shownDynGain = 0.0f;

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
        dynAtt   = std::make_unique<ButtonAttachment> (state, id (BandField::dyn), dynButton);
        thrAtt   = std::make_unique<SliderAttachment> (state, id (BandField::thr), thrKnob);
        rangeAtt = std::make_unique<SliderAttachment> (state, id (BandField::range), rangeKnob);
        attAtt   = std::make_unique<SliderAttachment> (state, id (BandField::att), attKnob);
        relAtt   = std::make_unique<SliderAttachment> (state, id (BandField::rel), relKnob);

        const auto colour = theme::bandColour (band);
        for (auto* k : { &freqKnob, &gainKnob, &qKnob, &thrKnob, &rangeKnob, &attKnob, &relKnob })
            k->setColour (juce::Slider::rotarySliderFillColourId, colour);
        enableButton.setColour (juce::TextButton::buttonOnColourId, colour);
        dynButton.setColour (juce::TextButton::buttonOnColourId, colour);
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

    const bool canDyn = dynamics::typeSupportsDynamics (type);
    const bool dynOn = canDyn && bands[(size_t) band].isDynamic();
    dynButton.setEnabled (canDyn);
    for (auto* k : { &thrKnob, &rangeKnob, &attKnob, &relKnob })
        k->setEnabled (dynOn);

    const bool soloed = processor.getSoloBand() == band;
    soloButton.setToggleState (soloed, juce::dontSendNotification);
    soloButton.setColour (juce::TextButton::buttonOnColourId, theme::warning);
}

void BandPanel::timerCallback()
{
    refreshActivity();
}

void BandPanel::refreshActivity()
{
    updateEnablement();

    if (band >= 0)
    {
        const float g = bands[(size_t) band].dynamicsActive() ? processor.getDynamicGainDb (band) : 0.0f;
        if (std::abs (g - shownDynGain) > 0.05f)
        {
            shownDynGain = g;
            repaint (dynActivityArea.expanded (2));
        }
    }
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

    const float badgeSize = getWidth() < 980 ? 42.0f : 56.0f;
    auto badge = area.removeFromLeft (getWidth() < 980 ? 46 : 64).toFloat().withSizeKeepingCentre (badgeSize, badgeSize);
    g.setColour (theme::bandColour (band));
    g.fillEllipse (badge);
    g.setColour (theme::graphBg);
    g.setFont (theme::font (22.0f, true));
    g.drawText (juce::String (band + 1), badge, juce::Justification::centred);

    // Dynamic section: a faint frame, and a live bar of the gain change.
    if (! dynSectionArea.isEmpty())
    {
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (dynSectionArea.toFloat(), 6.0f, 1.0f);
    }

    if (! dynActivityArea.isEmpty() && bands[(size_t) band].dynamicsActive())
    {
        auto r = dynActivityArea.toFloat();
        g.setColour (theme::graphBg);
        g.fillRoundedRectangle (r, 3.0f);

        // Bar grows from the centre: left = cut, right = boost (full = 30 dB).
        const float range = juce::jmax (1.0f, std::abs (BandParamRefs::get (bands[(size_t) band].range)));
        const float frac = juce::jlimit (-1.0f, 1.0f, shownDynGain / range);
        const float cx = r.getCentreX();
        auto bar = frac < 0.0f ? juce::Rectangle<float> (cx + frac * r.getWidth() * 0.5f, r.getY(), -frac * r.getWidth() * 0.5f, r.getHeight())
                               : juce::Rectangle<float> (cx, r.getY(), frac * r.getWidth() * 0.5f, r.getHeight());
        g.setColour (theme::bandColour (band).withAlpha (0.85f));
        g.fillRect (bar);
        g.setColour (theme::text);
        g.setFont (theme::font (11.0f));
        g.drawText (formatGain ((double) shownDynGain), r, juce::Justification::centred);
    }
}

void BandPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    const bool narrow = getWidth() < 980;
    area.removeFromLeft (narrow ? 50 : 72);  // badge
    const int gap = narrow ? 6 : 10;

    auto buttons = area.removeFromRight (narrow ? 62 : 80);
    const int bh = juce::jmin (24, (buttons.getHeight() - 8) / 3);
    enableButton.setBounds (buttons.removeFromTop (bh)); buttons.removeFromTop (4);
    soloButton.setBounds (buttons.removeFromTop (bh));   buttons.removeFromTop (4);
    deleteButton.setBounds (buttons.removeFromTop (bh));
    area.removeFromRight (12);

    auto column = [&area, gap] (int width) { auto c = area.removeFromLeft (width); area.removeFromLeft (gap); return c; };

    auto placeCombo = [] (juce::Rectangle<int> c, juce::Label& l, juce::ComboBox& box)
    {
        l.setBounds (c.removeFromTop (16));
        box.setBounds (c.removeFromTop (26).reduced (0, 1));
    };
    auto combos = column (narrow ? 104 : 124);
    auto top = combos.removeFromTop (combos.getHeight() / 2);
    placeCombo (top, typeLabel, typeBox);
    placeCombo (combos, slopeLabel, slopeBox);
    auto combos2 = column (narrow ? 92 : 108);
    placeCombo (combos2.removeFromTop (combos2.getHeight() / 2), modeLabel, modeBox);
    dynActivityArea = combos2.removeFromBottom (15);
    combos2.removeFromBottom (3);
    dynButton.setBounds (combos2.removeFromBottom (26).reduced (0, 1));

    auto placeKnob = [] (juce::Rectangle<int> c, juce::Label& l, juce::Slider& k)
    {
        l.setBounds (c.removeFromTop (16));
        k.setBounds (c);
    };
    // 7 knobs: freq, gain, Q, then the dynamic section.
    const int knobW = juce::jlimit (46, 100, (area.getWidth() - 7 * gap - 8) / 7);
    thrLabel.setText (knobW < 70 ? "THRESH" : "THRESHOLD", juce::dontSendNotification);
    for (auto* k : { &freqKnob, &gainKnob, &qKnob, &thrKnob, &rangeKnob, &attKnob, &relKnob })
        k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobW, 18);
    placeKnob (column (knobW), freqLabel, freqKnob);
    placeKnob (column (knobW), gainLabel, gainKnob);
    placeKnob (column (knobW), qLabel, qKnob);
    area.removeFromLeft (8);
    const int dynStart = area.getX();
    placeKnob (column (knobW), thrLabel, thrKnob);
    placeKnob (column (knobW), rangeLabel, rangeKnob);
    placeKnob (column (knobW), attLabel, attKnob);
    placeKnob (column (knobW), relLabel, relKnob);
    dynSectionArea = juce::Rectangle<int> (dynStart - 5, getLocalBounds().getY() + 4,
                                           area.getX() - dynStart, getHeight() - 8);
}

} // namespace peq::gui
