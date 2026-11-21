#pragma once

#include "BandParams.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

class ParaEQAudioProcessor;

namespace peq::gui
{

/** The strip under the graph showing every setting of the selected band:
    type, slope, channels, frequency, gain, Q, dynamics (on, sidechain,
    threshold, range, ratio, attack, release, live gain change), enable, solo and delete. Its
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

    /** Updates enablement and the live dynamic readout (timer; public for tests). */
    void refreshActivity();

    /** Learn threshold (2.4): listens to the selected dynamic band's region
        for a few seconds, then sets Threshold so roughly the loudest quarter
        goes over it (one undo step). */
    void startLearning();
    bool isLearning() const noexcept { return learnSecondsLeft > 0.0; }
    /** Advances learning by `seconds` (the timer calls it; public for tests). */
    void learnTick (double seconds);
    juce::TextButton& getLearnButton() noexcept { return learnButton; }
    static constexpr double learnSeconds = 4.0;

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

    // Dynamic EQ.
    juce::TextButton dynButton { "Dynamic" }, scButton { "SC" }, lookaheadButton { "LA" }, learnButton { "Learn" };
    std::vector<double> learnLevels;
    double learnSecondsLeft = 0.0, learnMessageSecondsLeft = 0.0;
    void stopLearning (const juce::String& message);
    juce::Slider thrKnob, rangeKnob, ratioKnob, attKnob, relKnob;
    juce::Label thrLabel, rangeLabel, ratioLabel, attLabel, relLabel;
    juce::Rectangle<int> dynActivityArea, dynSectionArea;
    float shownDynGain = 0.0f;

    std::unique_ptr<ComboAttachment> typeAtt, slopeAtt, modeAtt;
    std::unique_ptr<SliderAttachment> freqAtt, gainAtt, qAtt;
    std::unique_ptr<ButtonAttachment> enableAtt;
    std::unique_ptr<SliderAttachment> thrAtt, rangeAtt, ratioAtt, attAtt, relAtt;
    std::unique_ptr<ButtonAttachment> dynAtt, scAtt, lookaheadAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
};

} // namespace peq::gui
