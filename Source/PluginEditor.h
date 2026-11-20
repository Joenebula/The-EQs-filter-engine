#pragma once

#include "PluginProcessor.h"

/** Phase 1 placeholder editor: JUCE's generic parameter editor in a resizable
    dark window, so the plugin can be loaded and tested in a host. Replaced by
    the custom EQ graph interface in Phase 4. */
class ParaEQAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ParaEQAudioProcessorEditor (ParaEQAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::GenericAudioProcessorEditor generic;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaEQAudioProcessorEditor)
};
