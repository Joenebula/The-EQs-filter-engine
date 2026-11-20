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

            setParam (BandField::dyn, 0.0f);
            proc->processBlock (buffer, midi);
            check (near (proc->getDynamicGainDb (dynBand), 0.0, 1e-6), "switching dynamics off clears the gain change");
        }

        settings.show = peq::gui::AnalyserSettings::Show::off;
        graph.setAnalyserSettings (settings);
    }

    // Small window still lays out.
    editor->setSize (760, 460);
    save (*editor, outDir.getChildFile ("04-small.png"));

    base.reset();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL GUI CHECKS PASSED" : "GUI CHECKS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
