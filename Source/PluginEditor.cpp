#include "PluginEditor.h"
#include "Parameters.h"
#include "GUI/Theme.h"

namespace
{
    constexpr int topBarHeight = 46;
    constexpr int panelHeight = 118;
    const juce::Identifier editorWidthId  { "editorWidth" };
    const juce::Identifier editorHeightId { "editorHeight" };
    const juce::Identifier displayRangeId { "displayRange" };
    const double displayRanges[] = { 6.0, 12.0, 18.0, 30.0 };
}

ParaEQAudioProcessorEditor::ParaEQAudioProcessorEditor (ParaEQAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      bands (peq::gui::makeBandRefs (p.getState())),
      graph (p, bands),
      bandPanel (p, bands)
{
    setLookAndFeel (&lookAndFeel);
    auto& state = processor.getState();

    addAndMakeVisible (graph);
    addAndMakeVisible (bandPanel);

    graph.onSelectionChanged = [this] (int band) { bandPanel.showBand (band); };
    bandPanel.onDeleteRequested = [this] (int band) { graph.deleteBand (band); };
    bandPanel.onSoloToggled = [this] (int band) { graph.toggleSolo (band); };

    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Y)");
    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, peq::theme::warning);
    for (auto* b : { &undoButton, &redoButton, &bypassButton })
        addAndMakeVisible (*b);

    rangeBox.addItemList ({ "+/- 6 dB", "+/- 12 dB", "+/- 18 dB", "+/- 30 dB" }, 1);
    const double savedRange = (double) state.state.getProperty (displayRangeId, 18.0);
    int rangeIndex = 2;
    for (int i = 0; i < 4; ++i)
        if (std::abs (displayRanges[i] - savedRange) < 0.01)
            rangeIndex = i;
    rangeBox.setSelectedItemIndex (rangeIndex, juce::dontSendNotification);
    graph.setDisplayRange (displayRanges[rangeIndex]);
    rangeBox.onChange = [this]
    {
        const double r = displayRanges[juce::jlimit (0, 3, rangeBox.getSelectedItemIndex())];
        graph.setDisplayRange (r);
        processor.getState().state.setProperty (displayRangeId, r, nullptr);  // not an undo step
    };
    addAndMakeVisible (rangeBox);

    for (auto* k : { &inGainKnob, &outGainKnob })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 18);
        k->setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (*k);
    }
    inGainAtt  = std::make_unique<SliderAttachment> (state, peq::params::inputGain, inGainKnob);
    outGainAtt = std::make_unique<SliderAttachment> (state, peq::params::outputGain, outGainKnob);
    bypassAtt  = std::make_unique<ButtonAttachment> (state, peq::params::bypass, bypassButton);

    for (auto [label, text] : { std::pair { &inGainLabel, "IN" }, std::pair { &outGainLabel, "OUT" }, std::pair { &rangeLabel, "RANGE" } })
    {
        label->setText (text, juce::dontSendNotification);
        label->setFont (peq::theme::font (11.0f));
        label->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*label);
    }

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (760, 460, 3000, 2000);
    setSize ((int) state.state.getProperty (editorWidthId, 1040),
             (int) state.state.getProperty (editorHeightId, 640));

    startTimerHz (8);
}

ParaEQAudioProcessorEditor::~ParaEQAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ParaEQAudioProcessorEditor::undo()
{
    processor.flushUndo();
    processor.getUndoManager().undo();
}

void ParaEQAudioProcessorEditor::redo()
{
    processor.flushUndo();
    processor.getUndoManager().redo();
}

void ParaEQAudioProcessorEditor::timerCallback()
{
    // Record knob/menu changes promptly so they become their own undo steps.
    processor.flushUndo();
    auto& um = processor.getUndoManager();
    undoButton.setEnabled (um.canUndo());
    redoButton.setEnabled (um.canRedo());
}

bool ParaEQAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();

    if (mods.isCommandDown() && (code == 'Z' || code == 'z'))
    {
        if (mods.isShiftDown()) redo(); else undo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'Y' || code == 'y'))
    {
        redo();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        graph.deleteBand (graph.getSelectedBand());
        return true;
    }
    return false;
}

void ParaEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (peq::theme::background);

    auto bar = getLocalBounds().removeFromTop (topBarHeight);
    g.setColour (peq::theme::panel);
    g.fillRect (bar);
    g.setColour (peq::theme::panelEdge);
    g.drawHorizontalLine (topBarHeight - 1, 0.0f, (float) getWidth());

    g.setColour (peq::theme::text);
    g.setFont (peq::theme::font (19.0f, true));
    g.drawText ("ParaEQ", bar.reduced (14, 0), juce::Justification::centredLeft);
    if (getWidth() >= 960)
    {
        g.setColour (peq::theme::textDim);
        g.setFont (peq::theme::font (11.0f));
        g.drawText ("24-band parametric EQ", bar.reduced (14, 0).withTrimmedLeft (84), juce::Justification::centredLeft);
    }
}

void ParaEQAudioProcessorEditor::resized()
{
    auto& state = processor.getState().state;
    state.setProperty (editorWidthId, getWidth(), nullptr);
    state.setProperty (editorHeightId, getHeight(), nullptr);

    auto area = getLocalBounds();
    auto bar = area.removeFromTop (topBarHeight).reduced (10, 9);
    const bool compact = getWidth() < 960;
    bar.removeFromLeft (compact ? 100 : 230);  // title

    bypassButton.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (compact ? 8 : 14);
    outGainKnob.setBounds (bar.removeFromRight (compact ? 88 : 100));
    outGainLabel.setBounds (bar.removeFromRight (34));
    bar.removeFromRight (compact ? 4 : 10);
    inGainKnob.setBounds (bar.removeFromRight (compact ? 88 : 100));
    inGainLabel.setBounds (bar.removeFromRight (26));

    undoButton.setBounds (bar.removeFromLeft (56));
    bar.removeFromLeft (4);
    redoButton.setBounds (bar.removeFromLeft (56));
    bar.removeFromLeft (compact ? 6 : 16);
    rangeLabel.setBounds (bar.removeFromLeft (compact ? 0 : 48));
    rangeLabel.setVisible (! compact);
    bar.removeFromLeft (compact ? 0 : 6);
    rangeBox.setBounds (bar.removeFromLeft (juce::jmin (110, bar.getWidth())));

    bandPanel.setBounds (area.removeFromBottom (panelHeight));
    graph.setBounds (area);
}
