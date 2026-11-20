#pragma once

#include "PluginProcessor.h"
#include "GUI/BandPanel.h"
#include "GUI/BandParams.h"
#include "GUI/EQGraph.h"
#include "GUI/ParaLookAndFeel.h"

/** ParaEQ's window: a top bar (undo/redo, display range, input/output gain,
    bypass), the interactive EQ graph and the selected band's controls.
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

private:
    void timerCallback() override;
    void undo();
    void redo();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ParaEQAudioProcessor& processor;
    peq::gui::ParaLookAndFeel lookAndFeel;
    peq::gui::AllBands bands;

    peq::gui::EQGraph graph;
    peq::gui::BandPanel bandPanel;

    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, bypassButton { "Bypass" };
    juce::ComboBox rangeBox;
    juce::Slider inGainKnob, outGainKnob;
    juce::Label inGainLabel, outGainLabel, rangeLabel;
    std::unique_ptr<SliderAttachment> inGainAtt, outGainAtt;
    std::unique_ptr<ButtonAttachment> bypassAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQAudioProcessorEditor)
};
