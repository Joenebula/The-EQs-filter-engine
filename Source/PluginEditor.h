#pragma once

#include "PluginProcessor.h"
#include "GUI/BandPanel.h"
#include "GUI/BandParams.h"
#include "GUI/EQGraph.h"
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
    peq::gui::OutputMeter& getOutputMeter() noexcept { return outputMeter; }

    /** Steps through all presets (factory, then user); +1 next, -1 previous. */
    void stepPreset (int direction);
    /** Text shown on the preset button (name, plus " *" when changed). */
    juce::String getPresetLabel() const;

private:
    void timerCallback() override;
    void undo();
    void redo();
    void showAnalyserMenu();
    void loadAnalyserSettings();
    void saveAnalyserSettings (const peq::gui::AnalyserSettings&);
    void showPresetMenu();
    void askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onOk);
    void saveAsWithName (const juce::String& name);
    void importPreset();
    void exportPreset();
    void updatePresetBar();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ParaEQAudioProcessor& processor;
    peq::gui::ParaLookAndFeel lookAndFeel;
    peq::gui::AllBands bands;

    peq::gui::EQGraph graph;
    peq::gui::BandPanel bandPanel;

    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, bypassButton { "Bypass" };
    juce::TextButton analyserButton { "Analyser" };
    juce::TextButton presetButton, prevPresetButton { "<" }, nextPresetButton { ">" }, abButton { "A" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::ComboBox rangeBox;
    juce::Slider inGainKnob, outGainKnob;
    juce::Label inGainLabel, outGainLabel, rangeLabel;
    peq::gui::OutputMeter outputMeter;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<SliderAttachment> inGainAtt, outGainAtt;
    std::unique_ptr<ButtonAttachment> bypassAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQAudioProcessorEditor)
};
