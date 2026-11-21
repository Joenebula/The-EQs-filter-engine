#pragma once

#include "PluginProcessor.h"
#include "GUI/BandPanel.h"
#include "GUI/BandParams.h"
#include "GUI/EQGraph.h"
#include "GUI/MatchPanel.h"
#include "GUI/OutputMeter.h"
#include "GUI/ParaLookAndFeel.h"

/** ParaEQ's window: a top bar (undo/redo, presets, A/B, display range,
    analyser, input/output gain, meter, bypass), the interactive EQ graph and
    the selected band's controls.
    Resizable; the size is remembered with the project. */
class ParaEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit ParaEQAudioProcessorEditor (ParaEQAudioProcessor&);
    ~ParaEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    peq::gui::EQGraph& getGraph() noexcept { return graph; }
    peq::gui::BandPanel& getBandPanel() noexcept { return bandPanel; }
    peq::gui::MatchPanel& getMatchPanel() noexcept { return matchPanel; }
    juce::TextButton& getDeltaButton() noexcept { return deltaButton; }
    /** Undo history (2.5): step names, most recent first (at most 25).
        Right-click Undo or Redo to pick one; undoSteps (n) undoes n steps. */
    juce::StringArray getUndoHistory();
    juce::StringArray getRedoHistory();
    void undoSteps (int count);
    void redoSteps (int count);

    /** Runs one editor timer tick now (tests). */
    void timerCallbackForTests() { timerCallback(); }
    peq::gui::OutputMeter& getOutputMeter() noexcept { return outputMeter; }

    /** Steps through all presets (factory, then user); +1 next, -1 previous. */
    void stepPreset (int direction);
    /** Text shown on the preset button (name, plus " *" when changed). */
    juce::String getPresetLabel() const;

private:
    void timerCallback() override;
    void undo();
    void redo();
    void showHistoryMenu (bool undoSide);
    struct HistoryClick final : juce::MouseListener
    {
        HistoryClick (ParaEQAudioProcessorEditor& e, bool u) : editor (e), undoSide (u) {}
        void mouseDown (const juce::MouseEvent& ev) override
        {
            if (ev.mods.isPopupMenu())
                editor.showHistoryMenu (undoSide);
        }
        ParaEQAudioProcessorEditor& editor;
        bool undoSide;
    };
    HistoryClick undoHistoryClick { *this, true }, redoHistoryClick { *this, false };
    void showAnalyserMenu();
    void loadAnalyserSettings();
    void saveAnalyserSettings (const peq::gui::AnalyserSettings&);
    void showPresetMenu();
    void askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk);
    void saveAsWithName (const juce::String& name);
    void importPreset();
    void exportPreset();
    void updatePresetBar();
    void updateAutoGainTooltip();
    void updateCurveButton();
    void showCurveMenu();
public:
    /** 0 = Classic, 1 = Analog, 2 = Linear phase (also used by tests). */
    void setCurveMode (int mode);
private:

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ParaEQAudioProcessor& processor;
    peq::gui::ParaLookAndFeel lookAndFeel;
    peq::gui::AllBands bands;

    peq::gui::EQGraph graph;
    peq::gui::BandPanel bandPanel;
    peq::gui::MatchPanel matchPanel { processor, graph };

    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, bypassButton { "Bypass" };
    juce::TextButton analogButton { "Analog" }, autoGainButton { "Auto" };
    juce::TextButton analyserButton { "Analyser" };
    juce::TextButton deltaButton { juce::String::fromUTF8 ("\xce\x94") };   // Greek capital delta
    juce::TextButton presetButton, prevPresetButton { "<" }, nextPresetButton { ">" }, abButton { "A" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ComboBox rangeBox;
    juce::Slider inGainKnob, outGainKnob, scaleKnob;
    juce::Label inGainLabel, outGainLabel, rangeLabel, scaleLabel;
    peq::gui::OutputMeter outputMeter;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<SliderAttachment> inGainAtt, outGainAtt, scaleAtt;
    std::unique_ptr<ButtonAttachment> bypassAtt, autoGainAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQAudioProcessorEditor)
};
