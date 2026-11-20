#include "PluginEditor.h"

ParaEQAudioProcessorEditor::ParaEQAudioProcessorEditor (ParaEQAudioProcessor& p)
    : AudioProcessorEditor (p), generic (p)
{
    addAndMakeVisible (generic);
    setResizable (true, true);
    setResizeLimits (420, 220, 2400, 1600);
    setSize (520, 260);
}

void ParaEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));
    g.setColour (juce::Colour (0xffd6dbe4));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("ParaEQ  -  development build (EQ bands arrive in Phase 3)",
                getLocalBounds().removeFromTop (36).reduced (12, 0),
                juce::Justification::centredLeft);
}

void ParaEQAudioProcessorEditor::resized()
{
    generic.setBounds (getLocalBounds().withTrimmedTop (36).reduced (8));
}
