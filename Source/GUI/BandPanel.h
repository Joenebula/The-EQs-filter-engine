#pragma once

#include "BandParams.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

class ParaEQAudioProcessor;

namespace peq::gui
{

/** The strip under the graph showing every setting of the selected band:
    type, slope, channels, frequency, gain, Q, enable, solo and delete. Its
    controls are attached to the host parameters, so they stay in sync with
    the graph, automation and undo. */
class BandPanel final : public juce::Component,
                        private juce::Timer
{
public:
    BandPanel (ParaEQAudioProcessor& processor, AllBands& bands);
    ~BandPanel() override;

    void showBand (int band);   // -1 = nothing selected
    std::function<void (int)> onDeleteRequested;
    std::function<void (int)> onSoloToggled;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateEnablement();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ParaEQAudioProcessor& processor;
    AllBands& bands;
    int band = -1;

    juce::ComboBox typeBox, slopeBox, modeBox;
    juce::Slider freqKnob, gainKnob, qKnob;
    juce::Label typeLabel, slopeLabel, modeLabel, freqLabel, gainLabel, qLabel;
    juce::TextButton enableButton { "On" }, soloButton { "Solo" }, deleteButton { "Delete" };

    std::unique_ptr<ComboAttachment> typeAtt, slopeAtt, modeAtt;
    std::unique_ptr<SliderAttachment> freqAtt, gainAtt, qAtt;
    std::unique_ptr<ButtonAttachment> enableAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
};

} // namespace peq::gui
