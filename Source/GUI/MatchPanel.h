#pragma once

#include "../PluginProcessor.h"
#include "EQGraph.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace peq::gui
{

/** EQ Match (1.6): a small panel over the graph. Learn this track against
    the reference on the sidechain, then add bands that make this track's
    tone follow the reference's. The learned correction is drawn on the
    graph as a dashed line. */
class MatchPanel final : public juce::Component
{
public:
    MatchPanel (ParaEQAudioProcessor& p, EQGraph& g) : processor (p), graph (g)
    {
        title.setText ("EQ MATCH", juce::dontSendNotification);
        title.setFont (theme::font (13.0f, true));
        title.setColour (juce::Label::textColourId, theme::warning);
        status.setFont (theme::font (12.5f));
        status.setColour (juce::Label::textColourId, theme::text);
        status.setJustificationType (juce::Justification::topLeft);
        status.setMinimumHorizontalScale (1.0f);

        learnButton.setClickingTogglesState (false);
        learnButton.setColour (juce::TextButton::buttonOnColourId, theme::warning.darker (0.3f));
        learnButton.setTooltip ("Start or stop listening. Play this track and the reference (on the side-chain); "
                                "they can play together or one after the other.");
        learnButton.onClick = [this]
        {
            if (processor.isMatchLearning())
                processor.stopMatchLearning();
            else
            {
                lastAdded = -1;
                processor.startMatchLearning();
            }
            update();
        };

        for (int n : { 4, 6, 8, 12 })
            bandsBox.addItem (juce::String (n) + " bands", n);
        bandsBox.setSelectedId (6, juce::dontSendNotification);
        bandsBox.setTooltip ("Most bands EQ Match may add (it uses fewer when fewer are needed, and only free bands).");
        for (int a : { 100, 75, 50, 25 })
            amountBox.addItem ("Amount " + juce::String (a) + " %", a);
        amountBox.setSelectedId (75, juce::dontSendNotification);
        amountBox.setTooltip ("How much of the difference to correct. 50 to 75 % usually sounds most natural.");
        amountBox.onChange = [this] { update(); };

        applyButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
        applyButton.setTooltip ("Add the bands. One Undo takes them all away again.");
        applyButton.onClick = [this]
        {
            processor.stopMatchLearning();
            lastAdded = processor.applyMatch (bandsBox.getSelectedId(), amountBox.getSelectedId() / 100.0);
            update();
        };
        closeButton.setTooltip ("Close EQ Match (stops listening).");
        closeButton.onClick = [this] { close(); };

        for (juce::Component* c : { (juce::Component*) &title, (juce::Component*) &status, (juce::Component*) &learnButton,
                                    (juce::Component*) &bandsBox, (juce::Component*) &amountBox,
                                    (juce::Component*) &applyButton, (juce::Component*) &closeButton })
            addAndMakeVisible (c);
        setVisible (false);
    }

    void open()
    {
        lastAdded = -1;
        setVisible (true);
        toFront (false);
        update();
    }

    void close()
    {
        processor.stopMatchLearning();
        setVisible (false);
        graph.setMatchCurve ({}, {});
    }

    /** Refreshes the text, buttons and the dashed curve (editor timer). */
    void update()
    {
        if (! isVisible())
            return;
        const auto& m = processor.getMatch();
        const bool learning = processor.isMatchLearning();
        const bool sidechain = processor.isSidechainActive();

        learnButton.setButtonText (learning ? "Stop" : (m.sourceSeconds() > 0.0 || m.referenceSeconds() > 0.0 ? "Learn again" : "Learn"));
        learnButton.setToggleState (learning, juce::dontSendNotification);
        applyButton.setEnabled (m.hasEnough());

        juce::String text;
        const auto secs = [] (double s) { return juce::String (s, 1) + " s"; };
        if (lastAdded > 0)
            text = "Added " + juce::String (lastAdded) + " band" + (lastAdded == 1 ? "" : "s") + ". Undo takes them away.";
        else if (lastAdded == 0)
            text = "No bands added: no free band, or the tones already match.";
        else if (! sidechain && ! learning)
            text = "Switch on ParaEQ's side-chain in Cubase and send the reference track to it, then click Learn.";
        else if (learning)
            text = "Listening... this track " + secs (m.sourceSeconds()) + ", reference " + secs (m.referenceSeconds())
                   + (m.hasEnough() ? ". Apply when ready." : ". Play both.");
        else if (m.hasEnough())
            text = "Learned: this track " + secs (m.sourceSeconds()) + ", reference " + secs (m.referenceSeconds()) + ".";
        else
            text = "Click Learn, then play this track and the reference for at least a few seconds.";
        if (status.getText() != text)
            status.setText (text, juce::dontSendNotification);

        if (m.hasEnough() && lastAdded < 0)
        {
            auto f = peq::EQMatch::fitFrequencies (processor.getCurrentSampleRateOrDefault());
            std::vector<double> db (f.size());
            m.differenceDb (f.data(), (int) f.size(), db.data());
            const double amount = amountBox.getSelectedId() / 100.0;
            for (auto& d : db)
                d *= amount;
            graph.setMatchCurve (std::move (f), std::move (db));
        }
        else
            graph.setMatchCurve ({}, {});
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::panel.withAlpha (0.96f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::warning.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto top = r.removeFromTop (20);
        closeButton.setBounds (top.removeFromRight (22));
        title.setBounds (top);
        r.removeFromTop (2);
        status.setBounds (r.removeFromTop (34));
        r.removeFromTop (4);
        auto row = r.removeFromTop (26);
        learnButton.setBounds (row.removeFromLeft (88));
        row.removeFromLeft (6);
        bandsBox.setBounds (row.removeFromLeft (92));
        row.removeFromLeft (6);
        amountBox.setBounds (row.removeFromLeft (118));
        row.removeFromLeft (6);
        applyButton.setBounds (row);
    }

    static constexpr int preferredWidth = 400, preferredHeight = 102;

    juce::TextButton& getLearnButton() noexcept { return learnButton; }
    juce::TextButton& getApplyButton() noexcept { return applyButton; }
    juce::ComboBox& getBandsBox() noexcept { return bandsBox; }
    juce::ComboBox& getAmountBox() noexcept { return amountBox; }
    juce::String getStatusText() const { return status.getText(); }

private:
    ParaEQAudioProcessor& processor;
    EQGraph& graph;
    juce::Label title, status;
    juce::TextButton learnButton { "Learn" }, applyButton { "Apply" }, closeButton { "X" };
    juce::ComboBox bandsBox, amountBox;
    int lastAdded = -1;
};

} // namespace peq::gui
