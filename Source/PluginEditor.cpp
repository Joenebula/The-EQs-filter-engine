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
                           anTiltId { "analyserTilt" },
                           anSidechainId { "analyserSidechain" },
                           anResonanceId { "analyserResonances" },
                           anClashId { "analyserClashes" },
                           anChannelId { "analyserChannel" },
                           listenDragId { "listenWhileDragging" };
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
    addChildComponent (matchPanel);

    graph.onSelectionChanged = [this] (int band) { bandPanel.showBand (band); };
    bandPanel.onDeleteRequested = [this] (int band) { graph.deleteBand (band); };
    bandPanel.onSoloToggled = [this] (int band) { graph.toggleSolo (band); };

    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Y)");
    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, peq::theme::warning);
    analogButton.setClickingTogglesState (false);
    analogButton.setColour (juce::TextButton::buttonOnColourId, peq::theme::accent);
    analogButton.setTooltip ("Curve type. Analog: bands keep their analogue shape right up to 20 kHz, zero latency. "
                             "Classic: plain digital curves as in ParaEQ 1.0, zero latency. "
                             "Linear: linear phase (no phase shift between frequencies), analog-accurate shape, "
                             "adds about 90 ms latency.");
    analogButton.onClick = [this] { showCurveMenu(); };
    deltaButton.setClickingTogglesState (true);
    deltaButton.setColour (juce::TextButton::buttonOnColourId, peq::theme::warning);
    deltaButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    deltaButton.setTooltip ("Delta listen: hear only what the EQ changes (what a cut removes, what a boost adds). "
                            "A listening aid: not saved, and switched off when the window closes.");
    deltaButton.onClick = [this] { processor.setDeltaListen (deltaButton.getToggleState()); graph.repaint(); };
    addAndMakeVisible (deltaButton);
    autoGainButton.setClickingTogglesState (true);
    autoGainButton.setColour (juce::TextButton::buttonOnColourId, peq::theme::accent);
    analyserButton.onClick = [this] { showAnalyserMenu(); };
    analyserButton.setTooltip ("Spectrum analyser settings");
    presetButton.onClick = [this] { showPresetMenu(); };
    presetButton.setTooltip ("Presets: load, save, rename, delete, import, export");
    prevPresetButton.onClick = [this] { stepPreset (-1); };
    nextPresetButton.onClick = [this] { stepPreset (+1); };
    prevPresetButton.setTooltip ("Previous preset");
    nextPresetButton.setTooltip ("Next preset");
    abButton.onClick = [this] { processor.switchAB(); updatePresetBar(); };
    abButton.setTooltip ("A/B compare: switch between two settings (the preset menu can copy one to the other)");
    for (auto* b : { &undoButton, &redoButton, &bypassButton, &analogButton, &autoGainButton, &analyserButton,
                     &presetButton, &prevPresetButton, &nextPresetButton, &abButton })
        addAndMakeVisible (*b);
    updatePresetBar();
    loadAnalyserSettings();
    graph.setListenWhileDragging ((bool) processor.getState().state.getProperty (listenDragId, false));
    graph.onListenWhileDraggingChanged = [this] (bool on) { processor.getState().state.setProperty (listenDragId, on, nullptr); };

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
    scaleKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    scaleKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    scaleKnob.setDoubleClickReturnValue (true, 100.0);
    scaleKnob.setPopupDisplayEnabled (true, true, this);
    scaleKnob.setTooltip ("Gain Scale: makes every band's boost or cut bigger or smaller together "
                          "(100 % = as set, 0 % = flat, 200 % = double). Double-click for 100 %.");
    addAndMakeVisible (scaleKnob);
    scaleAtt   = std::make_unique<SliderAttachment> (state, peq::params::gainScale, scaleKnob);
    inGainAtt  = std::make_unique<SliderAttachment> (state, peq::params::inputGain, inGainKnob);
    outGainAtt = std::make_unique<SliderAttachment> (state, peq::params::outputGain, outGainKnob);
    bypassAtt  = std::make_unique<ButtonAttachment> (state, peq::params::bypass, bypassButton);
    autoGainAtt = std::make_unique<ButtonAttachment> (state, peq::params::autoGain, autoGainButton);
    autoGainButton.onStateChange = [this] { updateAutoGainTooltip(); };
    updateAutoGainTooltip();
    updateCurveButton();

    for (auto [label, text] : { std::pair { &inGainLabel, "IN" }, std::pair { &outGainLabel, "OUT" }, std::pair { &rangeLabel, "RANGE" }, std::pair { &scaleLabel, "SCALE" } })
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
    processor.stopMatchLearning();
    processor.setDeltaListen (false);
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
    a.showSidechain = (bool) st.getProperty (anSidechainId, a.showSidechain);
    a.findResonances = (bool) st.getProperty (anResonanceId, a.findResonances);
    a.showClashes = (bool) st.getProperty (anClashId, a.showClashes);
    a.channel = juce::jlimit (0, 3, (int) st.getProperty (anChannelId, a.channel));
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
    st.setProperty (anSidechainId, a.showSidechain, nullptr);
    st.setProperty (anResonanceId, a.findResonances, nullptr);
    st.setProperty (anClashId, a.showClashes, nullptr);
    st.setProperty (anChannelId, a.channel, nullptr);
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
    static const Choice channels[]  = { { "Stereo (L+R)", 0 }, { "Left", 1 }, { "Right", 2 }, { "Side (L-R)", 3 } };

    enum { showBase = 100, orderBase = 200, speedBase = 300, smoothBase = 400, opacityBase = 500, tiltBase = 600, channelBase = 700, peakId = 1, matchId = 2, sidechainId = 3, resonanceId = 4, clashId = 5, snapshotId = 6, clearSnapshotId = 7 };

    auto addGroup = [] (juce::PopupMenu& m, int base, const Choice* choices, int count, double current)
    {
        for (int i = 0; i < count; ++i)
            m.addItem (base + i, choices[i].name, true, std::abs (choices[i].value - current) < 1.0e-6);
    };

    juce::PopupMenu menu, orderM, speedM, smoothM, opacityM, tiltM, channelM;
    menu.addSectionHeader ("Show");
    addGroup (menu, showBase, shows, 4, (double) (int) a.show);
    menu.addSeparator();
    addGroup (orderM, orderBase, orders, 4, (double) a.fftOrder);
    addGroup (speedM, speedBase, speeds, 4, a.releaseSeconds);
    addGroup (smoothM, smoothBase, smooths, 4, a.smoothingOctaves);
    addGroup (opacityM, opacityBase, opacities, 4, (double) a.opacity);
    addGroup (tiltM, tiltBase, tilts, 3, a.tiltDbPerOctave);
    addGroup (channelM, channelBase, channels, 4, (double) a.channel);
    menu.addSubMenu ("Channel", channelM);
    menu.addSubMenu ("Resolution", orderM);
    menu.addSubMenu ("Speed", speedM);
    menu.addSubMenu ("Smoothing", smoothM);
    menu.addSubMenu ("Tilt", tiltM);
    menu.addSubMenu ("Opacity", opacityM);
    menu.addItem (peakId, "Peak hold", true, a.peakHold);
    menu.addItem (sidechainId, "Show side-chain (another track)", true, a.showSidechain);
    menu.addItem (resonanceId, "Find resonances (click a marker to cut)", true, a.findResonances);
    menu.addItem (clashId, "Show clashes with side-chain (click a bar to cut)", true, a.showClashes);
    menu.addSeparator();
    menu.addItem (snapshotId, "Take snapshot (compare later)", a.enabled());
    menu.addItem (clearSnapshotId, "Clear snapshot", graph.hasSnapshot());
    menu.addSeparator();
    menu.addItem (matchId, "EQ Match (follow a reference track)...", true, matchPanel.isVisible());

    juce::Component::SafePointer<ParaEQAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&analyserButton),
                        [safe, a] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        if (r == snapshotId)      { safe->graph.takeSnapshot(); return; }
        if (r == clearSnapshotId) { safe->graph.clearSnapshot(); return; }
        if (r == matchId)
        {
            if (safe->matchPanel.isVisible()) safe->matchPanel.close();
            else                              safe->matchPanel.open();
            return;
        }
        auto n = a;
        if      (r >= showBase && r < showBase + 4)       n.show = (AnalyserSettings::Show) (r - showBase);
        else if (r >= orderBase && r < orderBase + 4)     n.fftOrder = (int) orders[r - orderBase].value;
        else if (r >= speedBase && r < speedBase + 4)     n.releaseSeconds = speeds[r - speedBase].value;
        else if (r >= smoothBase && r < smoothBase + 4)   n.smoothingOctaves = smooths[r - smoothBase].value;
        else if (r >= opacityBase && r < opacityBase + 4) n.opacity = (float) opacities[r - opacityBase].value;
        else if (r >= tiltBase && r < tiltBase + 3)       n.tiltDbPerOctave = tilts[r - tiltBase].value;
        else if (r >= channelBase && r < channelBase + 4) n.channel = r - channelBase;
        else if (r == peakId)                             n.peakHold = ! n.peakHold;
        else if (r == sidechainId)                        n.showSidechain = ! n.showSidechain;
        else if (r == resonanceId)                        n.findResonances = ! n.findResonances;
        else if (r == clashId)                            n.showClashes = ! n.showClashes;
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
    updatePresetBar();
    updateAutoGainTooltip();
    updateCurveButton();
    matchPanel.update();
    if (deltaButton.getToggleState() != processor.getDeltaListen())
        deltaButton.setToggleState (processor.getDeltaListen(), juce::dontSendNotification);
}

void ParaEQAudioProcessorEditor::updateCurveButton()
{
    auto& st = processor.getState();
    const bool linear = st.getRawParameterValue (peq::params::linearPhase)->load() >= 0.5f;
    const bool analog = st.getRawParameterValue (peq::params::analogMatch)->load() >= 0.5f;
    const juce::String text = linear ? "Linear" : (analog ? "Analog" : "Classic");
    if (analogButton.getButtonText() != text)
        analogButton.setButtonText (text);
    analogButton.setToggleState (linear || analog, juce::dontSendNotification);
}

void ParaEQAudioProcessorEditor::setCurveMode (int mode)
{
    // 0 = Classic, 1 = Analog, 2 = Linear (linear phase uses the analog shape).
    auto set = [this] (const char* id, bool on)
    {
        auto* p = processor.getState().getParameter (id);
        const float v = on ? 1.0f : 0.0f;
        if (! juce::exactlyEqual (p->getValue(), v))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (v);
            p->endChangeGesture();
        }
    };
    processor.beginUndoStep ("Curve type");
    set (peq::params::analogMatch, mode != 0);
    set (peq::params::linearPhase, mode == 2);
    processor.flushUndo();
    updateCurveButton();
}

void ParaEQAudioProcessorEditor::showCurveMenu()
{
    auto& st = processor.getState();
    const bool linear = st.getRawParameterValue (peq::params::linearPhase)->load() >= 0.5f;
    const bool analog = st.getRawParameterValue (peq::params::analogMatch)->load() >= 0.5f;
    const int current = linear ? 2 : (analog ? 1 : 0);
    juce::PopupMenu m;
    m.addSectionHeader ("Curve type");
    m.addItem (2, "Analog  (zero latency)", true, current == 1);
    m.addItem (1, "Classic  (zero latency, as in 1.0)", true, current == 0);
    m.addItem (3, "Linear phase  (adds about 90 ms latency)", true, current == 2);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&analogButton),
                     [safe = juce::Component::SafePointer<ParaEQAudioProcessorEditor> (this)] (int r)
                     {
                         if (safe != nullptr && r > 0)
                             safe->setCurveMode (r - 1);
                     });
}

void ParaEQAudioProcessorEditor::updateAutoGainTooltip()
{
    const float db = processor.getAutoGainCorrectionDb();
    const auto value = (db >= 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    const auto tip = "Auto Gain: corrects the output so the EQ doesn't make the sound louder or quieter overall, "
                     "for fair before/after listening. It follows the EQ curve, not the audio, so it never pumps. "
                     "Correction for the current curve: " + value
                     + (autoGainButton.getToggleState() ? " (applied)." : " (not applied while off).");
    if (autoGainButton.getTooltip() != tip)
        autoGainButton.setTooltip (tip);
}

//==============================================================================
juce::String ParaEQAudioProcessorEditor::getPresetLabel() const
{
    auto& pm = processor.getPresets();
    return pm.getCurrentName() + (pm.isModified() ? " *" : "");
}

void ParaEQAudioProcessorEditor::updatePresetBar()
{
    const auto label = getPresetLabel();
    if (presetButton.getButtonText() != label)
        presetButton.setButtonText (label);
    const juce::String ab = processor.getActiveSlot() == 0 ? "A" : "B";
    if (abButton.getButtonText() != ab)
        abButton.setButtonText (ab);
}

void ParaEQAudioProcessorEditor::stepPreset (int direction)
{
    auto& pm = processor.getPresets();
    const auto factory = pm.getFactoryNames();
    const auto user = pm.getUserNames();
    const int total = factory.size() + user.size();
    if (total == 0)
        return;

    const auto current = pm.getCurrentName();
    int index = user.indexOf (current);
    index = index >= 0 ? factory.size() + index : factory.indexOf (current);

    const int next = index < 0 ? (direction > 0 ? 0 : total - 1)
                               : (index + direction + total) % total;
    if (next < factory.size())
        pm.loadFactory (next);
    else
        pm.loadUser (user[next - factory.size()]);
    updatePresetBar();
}

void ParaEQAudioProcessorEditor::askForName (const juce::String& title, const juce::String& initial,
                                             std::function<void (juce::String)> onOk)
{
    auto* w = new juce::AlertWindow (title, "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ParaEQAudioProcessorEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, onOk] (int result)
    {
        const auto name = PresetManager::sanitiseName (w->getTextEditorContents ("name"));
        if (safe != nullptr && result == 1 && name.isNotEmpty())
            onOk (name);
    }), true);
}

void ParaEQAudioProcessorEditor::saveAsWithName (const juce::String& name)
{
    auto& pm = processor.getPresets();
    auto doSave = [this, name]
    {
        if (! processor.getPresets().saveUser (name))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "ParaEQ",
                                                    "Could not save the preset to\n"
                                                    + processor.getPresets().getUserFolder().getFullPathName());
        updatePresetBar();
    };

    if (! pm.userPresetExists (name))
    {
        doSave();
        return;
    }

    juce::Component::SafePointer<ParaEQAudioProcessorEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace preset?",
                                        "A preset called \"" + name + "\" already exists. Replace it?",
                                        "Replace", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([safe, doSave] (int ok)
                                        {
                                            if (safe != nullptr && ok != 0)
                                                doSave();
                                        }));
}

void ParaEQAudioProcessorEditor::importPreset()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Import a ParaEQ preset", juce::File(),
                                                       juce::String ("*") + PresetManager::fileExtension);
    juce::Component::SafePointer<ParaEQAudioProcessorEditor> safe (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        if (safe->processor.getPresets().importFrom (fc.getResult()).isEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "ParaEQ",
                                                    "That file is not a ParaEQ preset.");
        safe->updatePresetBar();
    });
}

void ParaEQAudioProcessorEditor::exportPreset()
{
    auto& pm = processor.getPresets();
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (PresetManager::sanitiseName (pm.getCurrentName()) + PresetManager::fileExtension);
    fileChooser = std::make_unique<juce::FileChooser> ("Export preset", start,
                                                       juce::String ("*") + PresetManager::fileExtension);
    juce::Component::SafePointer<ParaEQAudioProcessorEditor> safe (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        if (! safe->processor.getPresets().exportTo (fc.getResult()))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "ParaEQ",
                                                    "Could not write the preset file.");
    });
}

void ParaEQAudioProcessorEditor::showPresetMenu()
{
    auto& pm = processor.getPresets();
    const auto factory = pm.getFactoryNames();
    const auto user = pm.getUserNames();
    const auto current = pm.getCurrentName();
    const bool currentIsUser = pm.userPresetExists (current);

    enum { factoryBase = 1000, userBase = 2000, saveId = 1, saveAsId, renameId, deleteId,
           importId, exportId, folderId, copyAbId };

    juce::PopupMenu factoryMenu;
    for (int i = 0; i < factory.size(); ++i)
        factoryMenu.addItem (factoryBase + i, factory[i], true, factory[i] == current && ! currentIsUser);

    juce::PopupMenu menu;
    menu.addSubMenu ("Factory presets", factoryMenu);
    menu.addSectionHeader ("My presets");
    if (user.isEmpty())
        menu.addItem (-1, "(none yet: use Save as...)", false);
    for (int i = 0; i < user.size(); ++i)
        menu.addItem (userBase + i, user[i], true, user[i] == current);
    menu.addSeparator();
    menu.addItem (saveId, "Save \"" + current + "\"", currentIsUser);
    menu.addItem (saveAsId, "Save as...");
    menu.addItem (renameId, "Rename...", currentIsUser);
    menu.addItem (deleteId, "Delete", currentIsUser);
    menu.addSeparator();
    menu.addItem (importId, "Import...");
    menu.addItem (exportId, "Export...");
    menu.addItem (folderId, "Show presets folder");
    menu.addSeparator();
    menu.addItem (copyAbId, processor.getActiveSlot() == 0 ? "Copy A to B" : "Copy B to A");

    juce::Component::SafePointer<ParaEQAudioProcessorEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safe, factory, user, current] (int r)
    {
        if (safe == nullptr || r <= 0)
            return;
        auto& self = *safe;
        auto& p = self.processor.getPresets();

        if (r >= factoryBase && r < factoryBase + factory.size())  p.loadFactory (r - factoryBase);
        else if (r >= userBase && r < userBase + user.size())      p.loadUser (user[r - userBase]);
        else if (r == saveId)    self.saveAsWithName (current);
        else if (r == saveAsId)
            self.askForName ("Save preset", current, [safe] (juce::String name)
            {
                if (safe != nullptr) safe->saveAsWithName (name);
            });
        else if (r == renameId)
            self.askForName ("Rename preset", current, [safe, current] (juce::String name)
            {
                if (safe == nullptr)
                    return;
                if (! safe->processor.getPresets().renameUser (current, name))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "ParaEQ",
                                                            "Could not rename: a preset called \"" + name + "\" may already exist.");
                safe->updatePresetBar();
            });
        else if (r == deleteId)
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset?",
                                                "Delete \"" + current + "\"? It goes to the Recycle Bin.",
                                                "Delete", "Cancel", safe.getComponent(),
                                                juce::ModalCallbackFunction::create ([safe, current] (int ok)
                                                {
                                                    if (safe != nullptr && ok != 0)
                                                        safe->processor.getPresets().deleteUser (current);
                                                }));
        else if (r == importId)  self.importPreset();
        else if (r == exportId)  self.exportPreset();
        else if (r == folderId)
        {
            auto folder = p.getUserFolder();
            folder.createDirectory();
            folder.startAsProcess();
        }
        else if (r == copyAbId)  self.processor.copyActiveToOther();

        self.updatePresetBar();
    });
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
    if (mods.isCommandDown() && (code == 'C' || code == 'c'))
    {
        graph.copyBand (graph.getSelectedBand());
        return true;
    }
    if (mods.isCommandDown() && (code == 'V' || code == 'v'))
    {
        graph.pasteBand (-1);
        return true;
    }
    // Arrow keys nudge the selected band: left/right 1/12 octave (a
    // semitone), up/down 0.5 dB; with Shift 1/48 octave and 0.1 dB.
    {
        const bool fine = mods.isShiftDown();
        double oct = 0.0, db = 0.0;
        if (code == juce::KeyPress::leftKey)  oct = fine ? -1.0 / 48.0 : -1.0 / 12.0;
        if (code == juce::KeyPress::rightKey) oct = fine ?  1.0 / 48.0 :  1.0 / 12.0;
        if (code == juce::KeyPress::downKey)  db  = fine ? -0.1 : -0.5;
        if (code == juce::KeyPress::upKey)    db  = fine ?  0.1 :  0.5;
        if ((! juce::exactlyEqual (oct, 0.0) || ! juce::exactlyEqual (db, 0.0)) && graph.getSelectedBand() >= 0)
        {
            graph.nudgeBand (graph.getSelectedBand(), oct, db);
            bandPanel.showBand (graph.getSelectedBand());
            return true;
        }
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
}

void ParaEQAudioProcessorEditor::resized()
{
    auto& state = processor.getState().state;
    state.setProperty (editorWidthId, getWidth(), nullptr);
    state.setProperty (editorHeightId, getHeight(), nullptr);

    auto area = getLocalBounds();
    auto bar = area.removeFromTop (topBarHeight).reduced (10, 9);
    const bool compact = getWidth() < 960;
    const bool mid = getWidth() < 1180;      // default size: tighter spacing
    bar.removeFromLeft (compact ? 72 : (mid ? 84 : 92));  // title

    // Right side: IN, OUT, meter, Bypass.
    bypassButton.setBounds (bar.removeFromRight (compact ? 62 : (mid ? 66 : 70)));
    bar.removeFromRight (4);
    deltaButton.setBounds (bar.removeFromRight (compact ? 28 : 32));
    bar.removeFromRight (compact ? 6 : (mid ? 6 : 10));
    outputMeter.setBounds (bar.removeFromRight (14).withSizeKeepingCentre (14, juce::jmin (bar.getHeight() + 6, 34)));
    bar.removeFromRight (compact ? 6 : 10);
    autoGainButton.setBounds (bar.removeFromRight (compact ? 40 : (mid ? 44 : 48)));
    bar.removeFromRight (compact ? 2 : 6);
    const int knobW = compact ? 30 : (mid ? 76 : 100);   // compact: knob only, value on hover/drag
    for (auto* k : { &inGainKnob, &outGainKnob })
        k->setTextBoxStyle (compact ? juce::Slider::NoTextBox : juce::Slider::TextBoxRight, false, mid ? 52 : 58, 18);
    outGainKnob.setBounds (bar.removeFromRight (knobW));
    outGainLabel.setBounds (bar.removeFromRight (compact ? 30 : 34));
    bar.removeFromRight (compact ? 2 : (mid ? 6 : 10));
    inGainKnob.setBounds (bar.removeFromRight (knobW));
    inGainLabel.setBounds (bar.removeFromRight (compact ? 20 : 26));
    bar.removeFromRight (compact ? 2 : 8);
    // Gain Scale: the knob always; its label only when there is room (the
    // tooltip and the value pop-up explain it otherwise).
    const bool scaleLabelFits = getWidth() >= 1180;
    scaleKnob.setBounds (bar.removeFromRight (28));
    scaleLabel.setVisible (scaleLabelFits);
    if (scaleLabelFits)
        scaleLabel.setBounds (bar.removeFromRight (44));
    bar.removeFromRight (compact ? 4 : (mid ? 6 : 10));

    // Left side: Undo, Redo, presets, A/B, range, analyser.
    undoButton.setBounds (bar.removeFromLeft (compact ? 42 : (mid ? 48 : 56)));
    bar.removeFromLeft (4);
    redoButton.setBounds (bar.removeFromLeft (compact ? 42 : (mid ? 48 : 56)));
    bar.removeFromLeft (compact ? 6 : (mid ? 10 : 14));

    rangeLabel.setVisible (false);
    analogButton.setBounds (bar.removeFromRight (compact ? 52 : 62));
    bar.removeFromRight (4);
    analyserButton.setBounds (bar.removeFromRight (compact ? 64 : (mid ? 72 : 80)));
    bar.removeFromRight (compact ? 4 : 6);
    rangeBox.setBounds (bar.removeFromRight (compact ? 88 : (mid ? 82 : 98)));
    bar.removeFromRight (compact ? 4 : (mid ? 6 : 10));

    abButton.setBounds (bar.removeFromRight (34));
    bar.removeFromRight (4);
    const bool arrows = bar.getWidth() >= 150;
    prevPresetButton.setVisible (arrows);
    nextPresetButton.setVisible (arrows);
    if (arrows)
    {
        prevPresetButton.setBounds (bar.removeFromLeft (24));
        nextPresetButton.setBounds (bar.removeFromRight (24));
    }
    presetButton.setBounds (bar);

    bandPanel.setBounds (area.removeFromBottom (panelHeight));
    graph.setBounds (area);
    matchPanel.setBounds (area.getX() + 48, area.getY() + 12,
                          juce::jmin (peq::gui::MatchPanel::preferredWidth, area.getWidth() - 96),
                          peq::gui::MatchPanel::preferredHeight);
}
