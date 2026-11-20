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

    // Analyser settings, saved with the project (not automatable, not undoable).
    const juce::Identifier anShowId { "analyserShow" }, anOrderId { "analyserOrder" },
                           anReleaseId { "analyserRelease" }, anPeakId { "analyserPeakHold" },
                           anSmoothId { "analyserSmoothing" }, anOpacityId { "analyserOpacity" },
                           anTiltId { "analyserTilt" };
}

ParaEQAudioProcessorEditor::ParaEQAudioProcessorEditor (ParaEQAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      bands (peq::gui::makeBandRefs (p.getState())),
      graph (p, bands),
      bandPanel (p, bands),
      outputMeter ([&p] (int ch) { return p.takeOutputPeak (ch); })
{
    setLookAndFeel (&lookAndFeel);
    auto& state = processor.getState();

    addAndMakeVisible (graph);
    addAndMakeVisible (outputMeter);
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
    analyserButton.onClick = [this] { showAnalyserMenu(); };
    analyserButton.setTooltip ("Spectrum analyser settings");
    for (auto* b : { &undoButton, &redoButton, &bypassButton, &analyserButton })
        addAndMakeVisible (*b);
    loadAnalyserSettings();

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
    processor.setAnalyserWanted (false);
    setLookAndFeel (nullptr);
}

void ParaEQAudioProcessorEditor::loadAnalyserSettings()
{
    using peq::gui::AnalyserSettings;
    const auto& st = processor.getState().state;
    AnalyserSettings a;
    a.show = (AnalyserSettings::Show) juce::jlimit (0, 3, (int) st.getProperty (anShowId, (int) a.show));
    a.fftOrder = juce::jlimit (11, 14, (int) st.getProperty (anOrderId, a.fftOrder));
    a.releaseSeconds = juce::jlimit (0.05, 5.0, (double) st.getProperty (anReleaseId, a.releaseSeconds));
    a.peakHold = (bool) st.getProperty (anPeakId, a.peakHold);
    a.smoothingOctaves = juce::jlimit (0.0, 1.0, (double) st.getProperty (anSmoothId, a.smoothingOctaves));
    a.opacity = (float) juce::jlimit (0.1, 1.0, (double) st.getProperty (anOpacityId, (double) a.opacity));
    a.tiltDbPerOctave = juce::jlimit (0.0, 6.0, (double) st.getProperty (anTiltId, a.tiltDbPerOctave));
    graph.setAnalyserSettings (a);
}

void ParaEQAudioProcessorEditor::saveAnalyserSettings (const peq::gui::AnalyserSettings& a)
{
    auto& st = processor.getState().state;
    st.setProperty (anShowId, (int) a.show, nullptr);
    st.setProperty (anOrderId, a.fftOrder, nullptr);
    st.setProperty (anReleaseId, a.releaseSeconds, nullptr);
    st.setProperty (anPeakId, a.peakHold, nullptr);
    st.setProperty (anSmoothId, a.smoothingOctaves, nullptr);
    st.setProperty (anOpacityId, (double) a.opacity, nullptr);
    st.setProperty (anTiltId, a.tiltDbPerOctave, nullptr);
    graph.setAnalyserSettings (a);
}

void ParaEQAudioProcessorEditor::showAnalyserMenu()
{
    using peq::gui::AnalyserSettings;
    const auto a = graph.getAnalyserSettings();

    struct Choice { const char* name; double value; };
    static const Choice shows[]     = { { "Off", 0 }, { "Post-EQ (output)", 1 }, { "Pre + Post", 2 }, { "Pre-EQ (input)", 3 } };
    static const Choice orders[]    = { { "Low (2048)", 11 }, { "Medium (4096)", 12 }, { "High (8192)", 13 }, { "Maximum (16384)", 14 } };
    static const Choice speeds[]    = { { "Fast", 0.2 }, { "Medium", 0.6 }, { "Slow", 1.5 }, { "Very slow", 3.0 } };
    static const Choice smooths[]   = { { "Off", 0.0 }, { "1/12 octave", 1.0 / 12.0 }, { "1/6 octave", 1.0 / 6.0 }, { "1/3 octave", 1.0 / 3.0 } };
    static const Choice opacities[] = { { "25 %", 0.25 }, { "50 %", 0.5 }, { "75 %", 0.75 }, { "100 %", 1.0 } };
    static const Choice tilts[]     = { { "Off (0 dB/oct)", 0.0 }, { "3 dB/oct", 3.0 }, { "4.5 dB/oct", 4.5 } };

    enum { showBase = 100, orderBase = 200, speedBase = 300, smoothBase = 400, opacityBase = 500, tiltBase = 600, peakId = 1 };

    auto addGroup = [] (juce::PopupMenu& m, int base, const Choice* choices, int count, double current)
    {
        for (int i = 0; i < count; ++i)
            m.addItem (base + i, choices[i].name, true, std::abs (choices[i].value - current) < 1.0e-6);
    };

    juce::PopupMenu menu, orderM, speedM, smoothM, opacityM, tiltM;
    menu.addSectionHeader ("Show");
    addGroup (menu, showBase, shows, 4, (double) (int) a.show);
    menu.addSeparator();
    addGroup (orderM, orderBase, orders, 4, (double) a.fftOrder);
    addGroup (speedM, speedBase, speeds, 4, a.releaseSeconds);
    addGroup (smoothM, smoothBase, smooths, 4, a.smoothingOctaves);
    addGroup (opacityM, opacityBase, opacities, 4, (double) a.opacity);
    addGroup (tiltM, tiltBase, tilts, 3, a.tiltDbPerOctave);
    menu.addSubMenu ("Resolution", orderM);
    menu.addSubMenu ("Speed", speedM);
    menu.addSubMenu ("Smoothing", smoothM);
    menu.addSubMenu ("Tilt", tiltM);
    menu.addSubMenu ("Opacity", opacityM);
    menu.addItem (peakId, "Peak hold", true, a.peakHold);

    juce::Component::SafePointer<ParaEQAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&analyserButton),
                        [safe, a] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto n = a;
        if      (r >= showBase && r < showBase + 4)       n.show = (AnalyserSettings::Show) (r - showBase);
        else if (r >= orderBase && r < orderBase + 4)     n.fftOrder = (int) orders[r - orderBase].value;
        else if (r >= speedBase && r < speedBase + 4)     n.releaseSeconds = speeds[r - speedBase].value;
        else if (r >= smoothBase && r < smoothBase + 4)   n.smoothingOctaves = smooths[r - smoothBase].value;
        else if (r >= opacityBase && r < opacityBase + 4) n.opacity = (float) opacities[r - opacityBase].value;
        else if (r >= tiltBase && r < tiltBase + 3)       n.tiltDbPerOctave = tilts[r - tiltBase].value;
        else if (r == peakId)                             n.peakHold = ! n.peakHold;
        safe->saveAnalyserSettings (n);
    });
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
    bar.removeFromRight (compact ? 6 : 10);
    outputMeter.setBounds (bar.removeFromRight (14).withSizeKeepingCentre (14, juce::jmin (bar.getHeight() + 6, 34)));
    bar.removeFromRight (compact ? 6 : 10);
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
    rangeBox.setBounds (bar.removeFromLeft (juce::jmin (compact ? 100 : 110, bar.getWidth())));
    bar.removeFromLeft (compact ? 6 : 10);
    analyserButton.setBounds (bar.removeFromLeft (juce::jmin (84, bar.getWidth())));

    bandPanel.setBounds (area.removeFromBottom (panelHeight));
    graph.setBounds (area);
}
