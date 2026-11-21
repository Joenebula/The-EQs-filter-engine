// Headless GUI test (development only): builds the real processor and editor,
// drives the graph through the same actions the mouse handlers use, checks
// the resulting parameter values and saves screenshots.
//
// Usage: peq_gui_tests <output folder>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/GUI/GraphMath.h"

#include <cstdio>

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (! ok)
        ++failures;
}

bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }

float value (ParaEQAudioProcessor& p, int band, peq::params::BandField f)
{
    auto* param = p.getState().getParameter (peq::params::bandID (band, f));
    return param->convertFrom0to1 (param->getValue());
}

juce::Point<float> pointFor (peq::gui::EQGraph& g, double hz, double db)
{
    const auto plot = g.plotArea();
    return { plot.getX() + (float) peq::graph::freqToX (hz, plot.getWidth()),
             plot.getY() + (float) peq::graph::dbToY (db, plot.getHeight(), g.getDisplayRange()) };
}

void save (juce::Component& c, const juce::File& file)
{
    auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
    std::printf ("       saved %s\n", file.getFullPathName().toRawUTF8());
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using peq::params::BandField;

    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    std::unique_ptr<ParaEQAudioProcessor> proc (dynamic_cast<ParaEQAudioProcessor*> (createPluginFilter()));
    proc->setRateAndBufferSizeDetails (48000.0, 512);
    proc->prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> base (proc->createEditor());
    auto* editor = dynamic_cast<ParaEQAudioProcessorEditor*> (base.get());
    check (editor != nullptr, "editor is created");
    editor->setSize (1040, 640);
    auto& graph = editor->getGraph();

    save (*editor, outDir.getChildFile ("01-empty.png"));

    // 1.4: the Auto (Auto Gain) button sits in the top bar and fits beside
    // the preset name at the default size.
    {
        juce::TextButton* autoButton = nullptr;
        for (auto* c : editor->getChildren())
            if (auto* tb = dynamic_cast<juce::TextButton*> (c); tb != nullptr && tb->getButtonText() == "Auto")
                autoButton = tb;
        check (autoButton != nullptr && autoButton->isVisible() && autoButton->getWidth() >= 40
                   && autoButton->getY() < 46,
               "top bar shows the Auto (Auto Gain) button");
        if (autoButton != nullptr)
        {
            autoButton->setToggleState (true, juce::sendNotificationSync);
            check (proc->getState().getRawParameterValue (peq::params::autoGain)->load() >= 0.5f, "Auto button switches Auto Gain on");
            check (autoButton->getTooltip().contains ("Correction for the current curve"), "Auto button tooltip shows the correction");
            autoButton->setToggleState (false, juce::sendNotificationSync);
        }

        // 1.5: the curve button (Analog / Classic / Linear).
        auto findCurve = [&]() -> juce::TextButton*
        {
            for (auto* c : editor->getChildren())
                if (auto* tb = dynamic_cast<juce::TextButton*> (c))
                    if (tb->getButtonText() == "Analog" || tb->getButtonText() == "Classic" || tb->getButtonText() == "Linear")
                        return tb;
            return nullptr;
        };
        auto* curve = findCurve();
        check (curve != nullptr && curve->getButtonText() == "Analog", "curve button shows Analog for a new instance");
        auto paramOn = [&] (const char* id) { return proc->getState().getRawParameterValue (id)->load() >= 0.5f; };
        editor->setCurveMode (2);
        check (curve != nullptr && curve->getButtonText() == "Linear" && paramOn (peq::params::linearPhase)
                   && paramOn (peq::params::analogMatch),
               "curve menu: Linear switches linear phase on (analog shape)");
        editor->setCurveMode (0);
        check (curve != nullptr && curve->getButtonText() == "Classic" && ! paramOn (peq::params::linearPhase)
                   && ! paramOn (peq::params::analogMatch),
               "curve menu: Classic switches both off");
        editor->setCurveMode (1);
        check (curve != nullptr && curve->getButtonText() == "Analog" && ! paramOn (peq::params::linearPhase)
                   && paramOn (peq::params::analogMatch),
               "curve menu: Analog is analog-matched, minimum phase");
    }

    // Click at 1 kHz / +6 dB -> a bell band there.
    const int b0 = graph.addBandAt (pointFor (graph, 1000.0, 6.0));
    check (b0 == 0, "clicking the graph adds band 1");
    check (value (*proc, 0, BandField::used) > 0.5f, "band 1 is in use");
    check ((int) value (*proc, 0, BandField::type) == (int) peq::FilterType::Bell, "band 1 is a bell");
    check (near (value (*proc, 0, BandField::freq), 1000.0, 15.0), "band 1 frequency ~1 kHz");
    check (near (value (*proc, 0, BandField::gain), 6.0, 0.2), "band 1 gain ~+6 dB");
    check (graph.getSelectedBand() == 0, "new band is selected");

    // Click near the bottom of the range -> low cut; near the top -> high cut.
    const int b1 = graph.addBandAt (pointFor (graph, 20.0, 0.0));
    check ((int) value (*proc, b1, BandField::type) == (int) peq::FilterType::HighPass, "click at 20 Hz adds a low cut (high-pass)");
    const int b2 = graph.addBandAt (pointFor (graph, 19000.0, 0.0));
    check ((int) value (*proc, b2, BandField::type) == (int) peq::FilterType::LowPass, "click at 19 kHz adds a high cut (low-pass)");

    // More bands for the picture.
    const int b3 = graph.addBandAt (pointFor (graph, 120.0, 4.0));
    const int b4 = graph.addBandAt (pointFor (graph, 3500.0, -7.0));
    graph.adjustQ (b4, 6.0);   // narrower
    check (value (*proc, b4, BandField::q) > 3.0f, "mouse wheel narrows Q");
    const int b5 = graph.addBandAt (pointFor (graph, 9000.0, 3.0));
    peq::gui::BandParamRefs::setWithGesture (proc->getState().getParameter (peq::params::bandID (b5, BandField::type)),
                                             (float) peq::FilterType::HighShelf);
    peq::gui::BandParamRefs::setWithGesture (proc->getState().getParameter (peq::params::bandID (b5, BandField::mode)),
                                             (float) peq::ChannelMode::Side);
    const int b6 = graph.addBandAt (pointFor (graph, 400.0, -3.0));
    peq::gui::BandParamRefs::setWithGesture (proc->getState().getParameter (peq::params::bandID (b6, BandField::on)), 0.0f);

    graph.setSelectedBand (b4);
    save (*editor, outDir.getChildFile ("02-bands.png"));

    // Double-click resets gain.
    graph.resetGain (b3);
    check (near (value (*proc, b3, BandField::gain), 0.0, 1e-4), "double-click resets gain to 0 dB");

    // Undo / redo.
    auto& um = proc->getUndoManager();
    proc->flushUndo();
    um.undo();
    check (near (value (*proc, b3, BandField::gain), 4.0, 0.2), "undo restores the gain");
    um.redo();
    check (near (value (*proc, b3, BandField::gain), 0.0, 1e-4), "redo re-applies the reset");

    // Delete, then undo the delete.
    graph.deleteBand (b6);
    check (value (*proc, b6, BandField::used) < 0.5f, "delete removes the band");
    um.undo();
    check (value (*proc, b6, BandField::used) > 0.5f, "undo brings the deleted band back");

    // Solo.
    graph.toggleSolo (b4);
    check (proc->getSoloBand() == b4, "solo selects the band");
    save (*editor, outDir.getChildFile ("03-solo.png"));
    graph.toggleSolo (b4);
    check (proc->getSoloBand() == -1, "solo toggles off");

    // Audio still runs and stays finite with all of this.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::Random rng (1);
    bool finite = true;
    juce::MidiBuffer midi;
    for (int block = 0; block < 200; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, rng.nextFloat() - 0.5f);
        proc->processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                finite = finite && std::isfinite (buffer.getSample (ch, i));
    }
    check (finite, "processing with the GUI-made bands stays finite");

    // State round-trip keeps the bands.
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    std::unique_ptr<ParaEQAudioProcessor> other (dynamic_cast<ParaEQAudioProcessor*> (createPluginFilter()));
    other->setStateInformation (state.getData(), (int) state.getSize());
    check (near (value (*other, b4, BandField::freq), value (*proc, b4, BandField::freq), 0.01), "saved state restores band frequency");
    check (near (value (*other, b4, BandField::q), value (*proc, b4, BandField::q), 1e-4), "saved state restores band Q");
    check (value (*other, b6, BandField::on) < 0.5f && value (*other, b6, BandField::used) > 0.5f, "saved state restores a bypassed band");

    // Spectrum analyser: run audio (pink-ish noise plus a 1 kHz tone) through
    // the processor and let the graph analyse it.
    {
        auto settings = graph.getAnalyserSettings();
        settings.show = peq::gui::AnalyserSettings::Show::preAndPost;
        settings.peakHold = true;
        graph.setAnalyserSettings (settings);
        juce::Random r2 (7);
        double phase = 0.0, lp = 0.0;
        bool gotSpectrum = false;
        for (int block = 0; block < 120; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                lp = 0.97 * lp + 0.03 * (r2.nextFloat() - 0.5);
                phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / 48000.0;
                const float s = (float) (3.0 * lp + 0.25 * std::sin (phase));
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            proc->processBlock (buffer, midi);
            gotSpectrum |= graph.updateAnalyser (512.0 / 48000.0);
        }
        check (gotSpectrum, "analyser produces spectra from the processor's audio");
        save (*editor, outDir.getChildFile ("05-analyser.png"));

        // Output meter shows the level.
        editor->getOutputMeter().update (0.03f);
        check (editor->getOutputMeter().levelDb (0) > -40.0f && editor->getOutputMeter().levelDb (1) > -40.0f,
               "output meter shows the output level");

        // Dynamic EQ on the 1 kHz band: the tone is loud, so the band must
        // pull down by most of its range, and the graph and panel show it.
        int dynBand = -1;
        for (int b = 0; b < peq::FilterEngine::numBands; ++b)
            if (value (*proc, b, BandField::used) >= 0.5f && near (value (*proc, b, BandField::freq), 1000.0, 50.0)
                && juce::roundToInt (value (*proc, b, BandField::type)) == 0)
                dynBand = b;
        check (dynBand >= 0, "found the 1 kHz bell for the dynamic test");
        if (dynBand >= 0)
        {
            auto setParam = [&] (BandField f, float v)
            {
                auto* prm = proc->getState().getParameter (peq::params::bandID (dynBand, f));
                prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            };
            setParam (BandField::dyn, 1.0f);
            setParam (BandField::thr, -30.0f);
            setParam (BandField::range, -10.0f);
            setParam (BandField::att, 5.0f);
            setParam (BandField::rel, 120.0f);

            for (int block = 0; block < 60; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    lp = 0.97 * lp + 0.03 * (r2.nextFloat() - 0.5);
                    phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / 48000.0;
                    const float s = (float) (3.0 * lp + 0.25 * std::sin (phase));
                    buffer.setSample (0, i, s);
                    buffer.setSample (1, i, s);
                }
                proc->processBlock (buffer, midi);
                graph.updateAnalyser (512.0 / 48000.0);
            }
            const float dynGain = proc->getDynamicGainDb (dynBand);
            std::printf ("       dynamic gain on band %d: %.2f dB\n", dynBand + 1, (double) dynGain);
            check (dynGain < -6.0f && dynGain >= -10.01f, "dynamic band pulls down within its range on a loud tone");
            graph.setSelectedBand (dynBand);
            graph.pollParameters();
            editor->getBandPanel().refreshActivity();
            save (*editor, outDir.getChildFile ("06-dynamic.png"));

            // 1.3: the SC button and Ratio knob sit in the dynamic section,
            // are live while Dynamic is on, and drive their parameters.
            juce::TextButton* scButton = nullptr;
            juce::Label* ratioLabel = nullptr;
            for (auto* c : editor->getBandPanel().getChildren())
            {
                if (auto* tb = dynamic_cast<juce::TextButton*> (c); tb != nullptr && tb->getButtonText() == "SC")
                    scButton = tb;
                if (auto* lb = dynamic_cast<juce::Label*> (c); lb != nullptr && lb->getText() == "RATIO")
                    ratioLabel = lb;
            }
            check (scButton != nullptr && scButton->isVisible() && scButton->isEnabled() && ! scButton->getBounds().isEmpty()
                       && ratioLabel != nullptr && ratioLabel->isVisible(),
                   "band panel shows the SC button and Ratio knob for a dynamic band");
            if (scButton != nullptr)
            {
                scButton->setToggleState (true, juce::sendNotificationSync);
                check (value (*proc, dynBand, BandField::sc) >= 0.5f, "SC button switches the band to the sidechain");
                scButton->setToggleState (false, juce::sendNotificationSync);
            }
            // 1.4: LA (look-ahead, global) sits next to the activity bar.
            juce::TextButton* laButton = nullptr;
            for (auto* c : editor->getBandPanel().getChildren())
                if (auto* tb = dynamic_cast<juce::TextButton*> (c); tb != nullptr && tb->getButtonText() == "LA")
                    laButton = tb;
            check (laButton != nullptr && laButton->isVisible() && laButton->isEnabled() && laButton->getWidth() >= 24,
                   "band panel shows the LA (look-ahead) button for a dynamic band");
            if (laButton != nullptr)
            {
                laButton->setToggleState (true, juce::sendNotificationSync);
                check (proc->getState().getRawParameterValue (peq::params::lookahead)->load() >= 0.5f,
                       "LA button switches look-ahead on");
                laButton->setToggleState (false, juce::sendNotificationSync);
            }

            setParam (BandField::ratio, 6.0f);
            editor->getBandPanel().refreshActivity();
            bool ratioShown = false;
            for (auto* c : editor->getBandPanel().getChildren())
                if (auto* sl = dynamic_cast<juce::Slider*> (c); sl != nullptr && near (sl->getValue(), 6.0, 1e-3))
                    ratioShown = true;
            check (ratioShown, "Ratio knob follows the parameter (6:1)");
            setParam (BandField::ratio, 2.0f);

            setParam (BandField::dyn, 0.0f);
            editor->getBandPanel().refreshActivity();
            check (scButton == nullptr || ! scButton->isEnabled(), "SC button greys out when Dynamic is off");
            proc->processBlock (buffer, midi);
            check (near (proc->getDynamicGainDb (dynBand), 0.0, 1e-6), "switching dynamics off clears the gain change");
        }

        settings.show = peq::gui::AnalyserSettings::Show::off;
        graph.setAnalyserSettings (settings);
    }

    // Small window still lays out.
    editor->setSize (760, 460);
    save (*editor, outDir.getChildFile ("04-small.png"));

    // Presets from the window: step to the next preset, label follows.
    {
        editor->setSize (1040, 640);
        auto& pm = proc->getPresets();
        pm.setUserFolder (outDir.getChildFile ("presets"));
        pm.loadFactory (pm.getFactoryNames().indexOf ("Vocal - clarity"));
        editor->stepPreset (+1);
        check (pm.getCurrentName() == "Vocal - de-ess (dynamic)", "next-preset button steps to the next factory preset");
        check (editor->getPresetLabel() == "Vocal - de-ess (dynamic)", "preset bar shows the preset name");
        auto* gain = proc->getState().getParameter (peq::params::bandID (0, BandField::freq));
        gain->setValueNotifyingHost (gain->convertTo0to1 (100.0f));
        check (editor->getPresetLabel().endsWith (" *"), "preset bar marks changes with *");
        editor->stepPreset (-1);
        check (pm.getCurrentName() == "Vocal - clarity", "previous-preset button steps back");
        graph.setSelectedBand (2);
        graph.pollParameters();
        save (*editor, outDir.getChildFile ("07-preset.png"));
        editor->setSize (760, 460);
        save (*editor, outDir.getChildFile ("08-preset-small.png"));
        outDir.getChildFile ("presets").deleteRecursively();
    }

    // Workflow (1.2): arrow-key nudging, Gain Scale, Alt-drag for Q, cursor readout.
    {
        editor->setSize (1040, 640);
        auto& pm = proc->getPresets();
        pm.loadFactory (0); // flat
        const int b = graph.addBandAt (pointFor (graph, 1000.0, 4.0));
        graph.setSelectedBand (b);
        const float f0 = value (*proc, b, BandField::freq);
        const float g0 = value (*proc, b, BandField::gain);

        editor->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        editor->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        check (near (value (*proc, b, BandField::freq), f0 * std::pow (2.0, 1.0 / 12.0), f0 * 0.002),
               "right arrow moves the band up a semitone");
        check (near (value (*proc, b, BandField::gain), std::round ((g0 + 0.5) * 10.0) / 10.0, 0.01), "up arrow adds 0.5 dB");
        editor->keyPressed (juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0));
        check (near (value (*proc, b, BandField::freq), f0 * std::pow (2.0, 1.0 / 12.0 - 1.0 / 48.0), f0 * 0.002),
               "shift+left is a finer step");
        auto& um2 = proc->getUndoManager();
        proc->flushUndo();
        um2.undo();
        check (near (value (*proc, b, BandField::freq), f0, f0 * 0.002) && near (value (*proc, b, BandField::gain), g0, 0.01),
               "one undo takes back a burst of arrow-key nudges");

        // Gain Scale: the node sits at gain x scale.
        auto* scale = proc->getState().getParameter (peq::params::gainScale);
        scale->setValueNotifyingHost (scale->convertTo0to1 (50.0f));
        graph.pollParameters();
        const float yHalf = graph.nodePosition (b).y;
        check (near (yHalf, pointFor (graph, 1000.0, value (*proc, b, BandField::gain) * 0.5).y, 1.0),
               "Gain Scale 50 %: node drawn at half its gain");
        graph.pollParameters();
        save (*editor, outDir.getChildFile ("09-gain-scale.png"));
        scale->setValueNotifyingHost (scale->convertTo0to1 (100.0f));
        graph.pollParameters();

        // Alt-drag up changes Q, not gain.
        const float q0 = value (*proc, b, BandField::q);
        const float gBefore = value (*proc, b, BandField::gain);
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto start = graph.nodePosition (b);
        auto makeEvent = [&] (juce::Point<float> pos, juce::ModifierKeys mods)
        {
            return juce::MouseEvent (src, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &graph, &graph,
                                     juce::Time::getCurrentTime(), start, juce::Time::getCurrentTime(), 1, true);
        };
        const auto alt = juce::ModifierKeys (juce::ModifierKeys::altModifier | juce::ModifierKeys::leftButtonModifier);
        graph.mouseDown (makeEvent (start, alt));
        graph.mouseDrag (makeEvent (start.translated (0.0f, -100.0f), alt));
        graph.mouseUp (makeEvent (start.translated (0.0f, -100.0f), alt));
        check (near (value (*proc, b, BandField::q), q0 * 2.0, q0 * 0.02), "Alt-drag up 100 px doubles Q");
        check (near (value (*proc, b, BandField::gain), gBefore, 1e-4), "Alt-drag leaves gain alone");

        // Cursor readout.
        const auto text = graph.cursorReadout (pointFor (graph, 440.0, 3.0));
        check (text.contains ("440 Hz") && text.contains ("A4") && text.contains ("+3.0 dB"),
               "cursor readout shows frequency, note and level");
        std::printf ("       cursor readout at 440 Hz / +3 dB: \"%s\"\n", text.toRawUTF8());
    }

    // 1.6: EQ Match panel (opened from the Analyser menu).
    {
        editor->setSize (1040, 640);
        auto& panel = editor->getMatchPanel();
        check (! panel.isVisible(), "EQ Match panel is hidden at first");
        panel.open();
        check (panel.isVisible() && panel.getStatusText().contains ("side-chain") && ! panel.getApplyButton().isEnabled(),
               "EQ Match panel opens and explains the side-chain when it is off");
        check (panel.getBounds().getY() > 46 && panel.getRight() < editor->getWidth(), "EQ Match panel sits over the graph");

        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        proc->setBusesLayout (l);
        proc->prepareToPlay (48000.0, 512);

        int usedBefore = 0;
        for (int b = 0; b < peq::FilterEngine::numBands; ++b)
            usedBefore += value (*proc, b, BandField::used) > 0.5f ? 1 : 0;

        panel.getLearnButton().onClick();
        check (proc->isMatchLearning() && panel.getLearnButton().getButtonText() == "Stop", "Learn starts listening");
        peq::BandParameters tilt;
        tilt.type = peq::FilterType::HighShelf; tilt.frequencyHz = 3000.0; tilt.gainDb = 6.0; tilt.analogMatched = true;
        peq::EQBand shelf;
        shelf.prepare (48000.0);
        shelf.setTarget (tilt);
        shelf.snapToTarget();
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer matchMidi;
        juce::Random matchRng (3);
        std::vector<double> ref (512);
        for (int block = 0; block < 48000 * 5 / 512; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float x = (matchRng.nextFloat() - 0.5f) * 0.4f;
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
                ref[(size_t) i] = (matchRng.nextDouble() - 0.5) * 0.4;
            }
            double* ch[1] { ref.data() };
            shelf.process (ch, 1, 512);
            for (int i = 0; i < 512; ++i)
            {
                buf.setSample (2, i, (float) ref[(size_t) i]);
                buf.setSample (3, i, (float) ref[(size_t) i]);
            }
            proc->processBlock (buf, matchMidi);
            if (block % 40 == 0)
                proc->pumpMatch();
        }
        proc->pumpMatch();
        panel.update();
        check (panel.getStatusText().contains ("Listening") && panel.getApplyButton().isEnabled() && graph.isShowingMatchCurve(),
               "while learning: progress shown, Apply enabled, correction drawn on the graph");
        std::printf ("       EQ Match status: \"%s\"\n", panel.getStatusText().toRawUTF8());
        save (*editor, outDir.getChildFile ("10-match.png"));

        panel.getApplyButton().onClick();
        int usedAfter = 0;
        for (int b = 0; b < peq::FilterEngine::numBands; ++b)
            usedAfter += value (*proc, b, BandField::used) > 0.5f ? 1 : 0;
        check (usedAfter > usedBefore && usedAfter - usedBefore <= panel.getBandsBox().getSelectedId()
                   && ! proc->isMatchLearning() && panel.getStatusText().contains ("Added") && ! graph.isShowingMatchCurve(),
               "Apply adds bands (no more than chosen), stops listening, hides the dashed line");
        std::printf ("       EQ Match status: \"%s\"\n", panel.getStatusText().toRawUTF8());
        graph.pollParameters();
        save (*editor, outDir.getChildFile ("11-match-applied.png"));

        panel.getLearnButton().onClick();
        panel.close();
        check (! panel.isVisible() && ! proc->isMatchLearning(), "closing the panel stops listening");
    }

    // 1.7: Delta listen button.
    {
        editor->setSize (1040, 640);
        auto& delta = editor->getDeltaButton();
        check (delta.isVisible() && delta.getWidth() >= 28 && delta.getY() < 46
                   && delta.getRight() <= editor->getWidth() - 70,
               "top bar shows the Delta button beside Bypass");
        delta.setToggleState (true, juce::sendNotificationSync);
        delta.onClick();
        check (proc->getDeltaListen(), "Delta button switches delta listen on");
        graph.repaint();
        save (*editor, outDir.getChildFile ("12-delta.png"));
        delta.setToggleState (false, juce::sendNotificationSync);
        delta.onClick();
        check (! proc->getDeltaListen(), "Delta button switches it off again");
        proc->setDeltaListen (true);
        editor->timerCallbackForTests();
        check (delta.getToggleState(), "Delta button follows the processor");
        proc->setDeltaListen (false);
        editor->setSize (900, 600);
        check (delta.isVisible() && delta.getWidth() >= 28, "Delta button still there in the smallest window");
        editor->setSize (1040, 640);
    }

    // 1.8: the side-chain spectrum in the analyser (the side-chain is on
    // from the EQ Match checks above).
    {
        auto a = graph.getAnalyserSettings();
        a.show = peq::gui::AnalyserSettings::Show::post;
        a.showSidechain = true;
        graph.setAnalyserSettings (a);
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer scMidi;
        juce::Random scRng (11);
        double lowState = 0.0;
        for (int block = 0; block < 60; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float x = (scRng.nextFloat() - 0.5f) * 0.3f;
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
                lowState += 0.02 * ((scRng.nextDouble() - 0.5) - lowState);   // a dark, kick-like rumble
                buf.setSample (2, i, (float) (lowState * 8.0));
                buf.setSample (3, i, (float) (lowState * 8.0));
            }
            proc->processBlock (buf, scMidi);
            graph.updateAnalyser (512.0 / 48000.0);
        }
        check (graph.hasSidechainSpectrum(), "analyser draws the side-chain when asked");
        editor->timerCallbackForTests();
        graph.repaint();
        save (*editor, outDir.getChildFile ("13-sidechain.png"));
        a.showSidechain = false;
        graph.setAnalyserSettings (a);
    }

    // 1.9: resonance finder.
    {
        auto a = graph.getAnalyserSettings();
        a.show = peq::gui::AnalyserSettings::Show::post;
        a.findResonances = true;
        graph.setAnalyserSettings (a);
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer rMidi;
        juce::Random rRng (17);
        long t = 0;
        for (int block = 0; block < 300; ++block)
        {
            for (int i = 0; i < 512; ++i, ++t)
            {
                const float x = (rRng.nextFloat() - 0.5f) * 0.3f
                                + 0.05f * (float) std::sin (2.0 * 3.14159265358979 * 3150.0 * (double) t / 48000.0);
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
                buf.setSample (2, i, 0.0f);
                buf.setSample (3, i, 0.0f);
            }
            proc->processBlock (buf, rMidi);
            graph.updateAnalyser (512.0 / 48000.0);
        }
        const auto& rings = graph.getResonances();
        int ring = -1;
        for (int i = 0; i < (int) rings.size(); ++i)
            if (std::abs (std::log2 (rings[(size_t) i].frequencyHz / 3150.0)) < 1.0 / 24.0)
                ring = i;
        std::printf ("       resonance finder: %d marker(s); 3.15 kHz ring %s\n", (int) rings.size(), ring >= 0 ? "found" : "missing");
        check (ring >= 0, "resonance finder marks a ring in the spectrum (3.15 kHz tone in noise)");
        if (ring >= 0)
        {
            check (graph.resonanceAt (graph.resonanceMarkerPosition (ring)) == ring, "the marker can be clicked");
            graph.repaint();
            save (*editor, outDir.getChildFile ("14-resonances.png"));
            const double ringHz = rings[(size_t) ring].frequencyHz;
            const int b = graph.addBandAtResonance (ring);
            check (b >= 0 && near (value (*proc, b, BandField::freq), ringHz, 1.0) && value (*proc, b, BandField::gain) <= -3.0f
                       && value (*proc, b, BandField::q) >= 2.0f && graph.getSelectedBand() == b,
                   "clicking a marker adds a narrow cut there and selects it");
            proc->getUndoManager().undo();
            check (b >= 0 && value (*proc, b, BandField::used) < 0.5f, "one undo removes the cut");
        }
        a.findResonances = false;
        graph.setAnalyserSettings (a);
        graph.updateAnalyser (0.01);
        check (graph.getResonances().empty(), "markers go when the finder is switched off");
    }

    base.reset();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL GUI CHECKS PASSED" : "GUI CHECKS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
