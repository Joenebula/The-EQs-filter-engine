// Plugin-level tests (Phase 7): the real ParaEQ processor as a host such as
// Cubase sees it. No editor, no display, so these run in CI on Windows.
//
// Covers: parameter registration (count, IDs, automatable, ranges), state
// save/restore (all parameters, old-version states, corrupt data), host
// automation while playing, sample-rate changes, bypass, mono/stereo
// layouts, float vs double processing, and several instances at once.

#include "../Source/PluginProcessor.h"
#include "../Source/Parameters.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const char* what)
{
    ++checks;
    std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (! ok)
        ++failures;
}

bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }

using peq::params::BandField;
constexpr double pi = 3.14159265358979323846;

std::unique_ptr<ParaEQAudioProcessor> makeProcessor (double sr = 48000.0, int block = 512)
{
    std::unique_ptr<ParaEQAudioProcessor> p (dynamic_cast<ParaEQAudioProcessor*> (createPluginFilter()));
    p->setRateAndBufferSizeDetails (sr, block);
    p->prepareToPlay (sr, block);
    return p;
}

juce::RangedAudioParameter* param (ParaEQAudioProcessor& p, const juce::String& id)
{
    return p.getState().getParameter (id);
}

juce::RangedAudioParameter* bandParam (ParaEQAudioProcessor& p, int band, BandField f)
{
    return param (p, peq::params::bandID (band, f));
}

void setValue (juce::RangedAudioParameter* prm, float v)
{
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

float getValue (juce::RangedAudioParameter* prm)
{
    return prm->convertFrom0to1 (prm->getValue());
}

/** A bell at 1 kHz, +6 dB, on band 1. */
void setupBell (ParaEQAudioProcessor& p, float gainDb = 6.0f)
{
    setValue (bandParam (p, 0, BandField::used), 1.0f);
    setValue (bandParam (p, 0, BandField::type), 0.0f);
    setValue (bandParam (p, 0, BandField::freq), 1000.0f);
    setValue (bandParam (p, 0, BandField::gain), gainDb);
    setValue (bandParam (p, 0, BandField::q), 1.0f);
}

/** Plays a sine through the processor and returns the output amplitude at
    that frequency over the last part of the run. */
double sineGainDb (ParaEQAudioProcessor& p, double sr, double freq, int block, double seconds = 0.5)
{
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double re = 0.0, im = 0.0;
    int counted = 0;
    const int measureFrom = total / 2;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const float s = (float) (0.25 * std::sin (2.0 * pi * freq * (double) (pos + i) / sr));
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < n; ++i)
        {
            if (pos + i < measureFrom)
                continue;
            const double ph = 2.0 * pi * freq * (double) (pos + i) / sr;
            re += buf.getSample (0, i) * std::cos (ph);
            im += buf.getSample (0, i) * std::sin (ph);
            ++counted;
        }
    }
    const double amp = 2.0 * std::sqrt (re * re + im * im) / (double) counted;
    return 20.0 * std::log10 (amp / 0.25);
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)) || std::abs (b.getSample (ch, i)) > 100.0f)
                return false;
    return true;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---------------------------------------------------------------- identity
    {
        auto p = makeProcessor();
        check (p->getName() == "ParaEQ", "plugin name is ParaEQ");
        check (p->getLatencySamples() == 0, "zero latency reported");
        check (juce::exactlyEqual (p->getTailLengthSeconds(), 0.0), "zero tail reported");
        check (! p->acceptsMidi() && ! p->producesMidi(), "no MIDI in or out");
        check (p->supportsDoublePrecisionProcessing(), "supports 64-bit processing");
        check (p->getBypassParameter() != nullptr, "host bypass parameter is provided");
    }

    // ---------------------------------------------------------------- buses
    {
        auto p = makeProcessor();
        using CS = juce::AudioChannelSet;
        auto layout = [] (CS in, CS out)
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (in);
            l.outputBuses.add (out);
            return l;
        };
        check (p->checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo())), "stereo in/out supported");
        check (p->checkBusesLayoutSupported (layout (CS::mono(), CS::mono())), "mono in/out supported");
        check (! p->checkBusesLayoutSupported (layout (CS::mono(), CS::stereo())), "mono-to-stereo refused (in = out only)");
        check (! p->checkBusesLayoutSupported (layout (CS::create5point1(), CS::create5point1())), "5.1 refused");
    }

    // ---------------------------------------------------------------- parameters
    {
        auto p = makeProcessor();
        const auto& params = p->getParameters();
        const int expected = 3 + peq::params::numBands * 13;
        check (params.size() == expected, "parameter count = 3 global + 24 bands x 13");

        bool allAutomatable = true, idsOk = true, uniqueIds = true;
        juce::StringArray ids;
        for (auto* prm : params)
        {
            allAutomatable = allAutomatable && prm->isAutomatable();
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
            {
                if (ids.contains (withId->paramID))
                    uniqueIds = false;
                ids.add (withId->paramID);
            }
            else
            {
                idsOk = false;
            }
        }
        check (allAutomatable, "every parameter is automatable");
        check (idsOk && uniqueIds, "every parameter has a unique ID");

        // IDs saved in projects must never change.
        const char* const mustExist[] = { "input_gain", "output_gain", "bypass",
                                          "b01_on", "b01_type", "b01_freq", "b01_gain", "b01_q", "b01_slope", "b01_mode", "b01_used",
                                          "b01_dyn", "b01_thr", "b01_range", "b01_att", "b01_rel",
                                          "b24_on", "b24_freq", "b24_rel" };
        bool present = true;
        for (auto* id : mustExist)
            present = present && ids.contains (id);
        check (present, "stable parameter IDs present (b01_..b24_, globals)");

        // Text <-> value round trips the way Cubase shows and types them.
        auto* f = bandParam (*p, 0, BandField::freq);
        check (f->getText (f->convertTo0to1 (2500.0f), 32) == "2.50 kHz", "frequency displays as 2.50 kHz");
        check (near (f->convertFrom0to1 (f->getValueForText ("3.2k")), 3200.0, 1.0), "typing 3.2k sets 3200 Hz");
        auto* a = bandParam (*p, 0, BandField::att);
        check (near (a->convertFrom0to1 (a->getValueForText ("25 ms")), 25.0, 0.1), "typing 25 ms sets attack");
        auto* r = bandParam (*p, 0, BandField::rel);
        check (near (r->convertFrom0to1 (r->getValueForText ("1.5 s")), 1500.0, 1.0), "typing 1.5 s sets release");
    }

    // ---------------------------------------------------------------- state
    {
        auto a = makeProcessor();
        juce::Random rng (1234);
        for (auto* prm : a->getParameters())
            if (prm != a->getBypassParameter())
                prm->setValueNotifyingHost (rng.nextFloat());
        a->getState().state.setProperty ("displayRange", 12.0, nullptr);

        juce::MemoryBlock saved;
        a->getStateInformation (saved);

        auto b = makeProcessor();
        b->setStateInformation (saved.getData(), (int) saved.getSize());

        int mismatches = 0;
        for (int i = 0; i < a->getParameters().size(); ++i)
        {
            auto* pa = dynamic_cast<juce::RangedAudioParameter*> (a->getParameters()[i]);
            auto* pb = dynamic_cast<juce::RangedAudioParameter*> (b->getParameters()[i]);
            // Compare in real units: choices/bools snap, so compare snapped values.
            if (! near (getValue (pa), getValue (pb), 1e-3 * std::max (1.0, std::abs ((double) getValue (pa)))))
                ++mismatches;
        }
        check (mismatches == 0, "save then load restores all 315 parameters");
        check (juce::exactlyEqual ((double) b->getState().state.getProperty ("displayRange"), 12.0), "editor settings saved with the state");

        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        check (xml != nullptr && xml->getIntAttribute ("stateVersion") == ParaEQAudioProcessor::currentStateVersion,
               "saved state carries the state version");

        // Loading again is stable (no drift).
        juce::MemoryBlock saved2;
        b->getStateInformation (saved2);
        auto c = makeProcessor();
        c->setStateInformation (saved2.getData(), (int) saved2.getSize());
        juce::MemoryBlock saved3;
        c->getStateInformation (saved3);
        const auto x2 = juce::AudioProcessor::getXmlFromBinary (saved2.getData(), (int) saved2.getSize())->toString();
        const auto x3 = juce::AudioProcessor::getXmlFromBinary (saved3.getData(), (int) saved3.getSize())->toString();
        check (x2 == x3, "save/load/save gives identical data");
    }

    // An old (Phase 1-5) project: no version tag and no dynamic parameters.
    {
        auto a = makeProcessor();
        setupBell (*a, 4.5f);
        juce::MemoryBlock saved;
        a->getStateInformation (saved);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        xml->removeAttribute ("stateVersion");
        for (auto* child = xml->getFirstChildElement(); child != nullptr;)
        {
            auto* next = child->getNextElement();
            const auto id = child->getStringAttribute ("id");
            if (id.endsWith ("_dyn") || id.endsWith ("_thr") || id.endsWith ("_range") || id.endsWith ("_att") || id.endsWith ("_rel"))
                xml->removeChildElement (child, true);
            child = next;
        }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);

        auto b = makeProcessor();
        setValue (bandParam (*b, 0, BandField::dyn), 1.0f);      // must be reset by the load
        setValue (bandParam (*b, 0, BandField::range), 12.0f);
        b->setStateInformation (old.getData(), (int) old.getSize());
        check (near (getValue (bandParam (*b, 0, BandField::gain)), 4.5, 0.01), "old project: band settings restored");
        check (getValue (bandParam (*b, 0, BandField::dyn)) < 0.5f, "old project: dynamics off (default)");
        check (near (getValue (bandParam (*b, 0, BandField::range)), -6.0, 0.01), "old project: new parameters at defaults");
        check (near (sineGainDb (*b, 48000.0, 1000.0, 512), 4.5, 0.15), "old project sounds as saved (+4.5 dB at 1 kHz)");
    }

    // Corrupt or foreign data is ignored.
    {
        auto p = makeProcessor();
        setupBell (*p, 3.0f);
        juce::Random rng (99);
        std::vector<char> junk (4096);
        for (auto& c : junk) c = (char) rng.nextInt (256);
        p->setStateInformation (junk.data(), (int) junk.size());
        p->setStateInformation (junk.data(), 0);
        juce::XmlElement other ("SomeOtherPlugin");
        other.setAttribute ("x", 1);
        juce::MemoryBlock otherData;
        juce::AudioProcessor::copyXmlToBinary (other, otherData);
        p->setStateInformation (otherData.getData(), (int) otherData.getSize());
        check (near (getValue (bandParam (*p, 0, BandField::gain)), 3.0, 0.01), "corrupt / foreign state ignored, settings kept");
    }

    // ---------------------------------------------------------------- automation
    {
        auto p = makeProcessor (48000.0, 256);
        setupBell (*p, 0.0f);
        auto* gain = bandParam (*p, 0, BandField::gain);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        double phase = 0.0, maxStep = 0.0, maxInStep = 0.0;
        float prev = 0.0f, prevIn = 0.0f;
        bool finite = true;
        // Automation ramp: gain sweeps -12 .. +12 dB, a new value every block
        // (as Cubase sends), then jumps.
        for (int block = 0; block < 400; ++block)
        {
            const float target = block < 200 ? -12.0f + 24.0f * (float) block / 200.0f
                                             : ((block / 40) % 2 == 0 ? 12.0f : -12.0f);
            gain->setValueNotifyingHost (gain->convertTo0to1 (target));
            for (int i = 0; i < 256; ++i)
            {
                phase += 2.0 * pi * 1000.0 / 48000.0;
                const float s = (float) (0.2 * std::sin (phase));
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
                maxInStep = std::max (maxInStep, (double) std::abs (s - prevIn));
                prevIn = s;
            }
            p->processBlock (buf, midi);
            for (int i = 0; i < 256; ++i)
            {
                const float y = buf.getSample (0, i);
                finite = finite && std::isfinite (y);
                maxStep = std::max (maxStep, (double) std::abs (y - prev));
                prev = y;
            }
        }
        check (finite, "automation: output stays finite");
        // Largest sample step is bounded by the input's step times the largest gain (+12 dB = 4x).
        check (maxStep <= maxInStep * 4.0 * 1.05, "automation: no jumps or clicks while gain is automated");

        gain->setValueNotifyingHost (gain->convertTo0to1 (-9.0f));
        check (near (sineGainDb (*p, 48000.0, 1000.0, 256, 0.3), -9.0, 0.15), "automation: lands on the automated value");
    }

    // ---------------------------------------------------------------- sample rates
    {
        auto p = makeProcessor (44100.0, 512);
        setupBell (*p, 6.0f);
        // A band near the top of the range, which must be clamped at low rates.
        setValue (bandParam (*p, 1, BandField::used), 1.0f);
        setValue (bandParam (*p, 1, BandField::type), 2.0f);          // high shelf
        setValue (bandParam (*p, 1, BandField::freq), 28000.0f);
        setValue (bandParam (*p, 1, BandField::gain), 6.0f);

        bool allOk = true;
        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, 22050.0, 48000.0 })
        {
            for (int block : { 32, 512, 2048 })
            {
                p->releaseResources();
                p->setRateAndBufferSizeDetails (sr, block);
                p->prepareToPlay (sr, block);
                const double g = sineGainDb (*p, sr, 1000.0, block, 0.3);
                if (! near (g, 6.0, 0.25))
                {
                    std::printf ("       %.0f Hz / %d: %.2f dB\n", sr, block, g);
                    allOk = false;
                }
            }
        }
        check (allOk, "sample-rate and block-size changes (22.05-192 kHz, 32-2048): bell stays +6 dB at 1 kHz");

        // Noise at the lowest rate with the 28 kHz shelf: finite and bounded.
        p->setRateAndBufferSizeDetails (22050.0, 333);
        p->prepareToPlay (22050.0, 333);
        juce::AudioBuffer<float> buf (2, 333);
        juce::MidiBuffer midi;
        juce::Random rng (5);
        bool finite = true;
        for (int block = 0; block < 100; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 333; ++i)
                    buf.setSample (ch, i, rng.nextFloat() - 0.5f);
            p->processBlock (buf, midi);
            finite = finite && allFinite (buf);
        }
        check (finite, "band above Nyquist at 22.05 kHz: stable output");

        // A host block larger than announced must not crash or allocate wrongly.
        juce::AudioBuffer<float> big (2, 5000);
        big.clear();
        p->processBlock (big, midi);
        check (allFinite (big), "host block larger than announced is handled");
    }

    // ---------------------------------------------------------------- bypass
    {
        auto p = makeProcessor (48000.0, 512);
        setupBell (*p, 12.0f);
        setValue (param (*p, peq::params::bypass), 1.0f);
        juce::AudioBuffer<float> buf (2, 512), ref (2, 512);
        juce::MidiBuffer midi;
        juce::Random rng (3);
        bool identical = true;
        for (int block = 0; block < 20; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, rng.nextFloat() - 0.5f);
            ref.makeCopyOf (buf);
            p->processBlock (buf, midi);
            if (block >= 2)   // after the 10 ms crossfade
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        identical = identical && juce::exactlyEqual (buf.getSample (ch, i), ref.getSample (ch, i));
        }
        check (identical, "bypass passes audio through bit-exactly");
    }

    // ---------------------------------------------------------------- mono
    {
        auto p = makeProcessor (48000.0, 512);
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (p->setBusesLayout (mono), "switches to mono");
        p->prepareToPlay (48000.0, 512);
        setupBell (*p, -6.0f);
        juce::AudioBuffer<float> buf (1, 512);
        juce::MidiBuffer midi;
        double re = 0.0, im = 0.0;
        int n = 0;
        for (int block = 0; block < 60; ++block)
        {
            for (int i = 0; i < 512; ++i)
                buf.setSample (0, i, (float) (0.25 * std::sin (2.0 * pi * 1000.0 * (block * 512 + i) / 48000.0)));
            p->processBlock (buf, midi);
            if (block >= 30)
                for (int i = 0; i < 512; ++i)
                {
                    const double ph = 2.0 * pi * 1000.0 * (block * 512 + i) / 48000.0;
                    re += buf.getSample (0, i) * std::cos (ph);
                    im += buf.getSample (0, i) * std::sin (ph);
                    ++n;
                }
        }
        const double g = 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / n / 0.25);
        check (near (g, -6.0, 0.15), "mono: bell works (-6 dB at 1 kHz)");
    }

    // ---------------------------------------------------------------- float vs double
    {
        auto pf = makeProcessor (48000.0, 512);
        auto pd = makeProcessor (48000.0, 512);
        pd->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
        pd->prepareToPlay (48000.0, 512);
        for (auto* p : { pf.get(), pd.get() })
        {
            setupBell (*p, 9.0f);
            setValue (bandParam (*p, 0, BandField::dyn), 1.0f);
            setValue (bandParam (*p, 0, BandField::thr), -30.0f);
        }
        juce::AudioBuffer<float> bf (2, 512);
        juce::AudioBuffer<double> bd (2, 512);
        juce::MidiBuffer midi;
        juce::Random rng (11);
        double maxDiff = 0.0;
        for (int block = 0; block < 50; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const float s = rng.nextFloat() - 0.5f;
                    bf.setSample (ch, i, s);
                    bd.setSample (ch, i, (double) s);
                }
            pf->processBlock (bf, midi);
            pd->processBlock (bd, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    maxDiff = std::max (maxDiff, std::abs ((double) bf.getSample (ch, i) - bd.getSample (ch, i)));
        }
        check (maxDiff < 1e-5, "32-bit and 64-bit processing give the same result");
    }

    // ---------------------------------------------------------------- instances
    {
        // Four instances on four threads with different settings must not
        // affect each other (no shared state between instances).
        constexpr int numInstances = 4;
        std::vector<std::unique_ptr<ParaEQAudioProcessor>> procs;
        for (int k = 0; k < numInstances; ++k)
        {
            procs.push_back (makeProcessor (48000.0, 256));
            setupBell (*procs.back(), -9.0f + 6.0f * (float) k);
        }
        std::vector<double> results (numInstances, 0.0);
        std::vector<std::thread> threads;
        for (int k = 0; k < numInstances; ++k)
            threads.emplace_back ([&, k] { results[(size_t) k] = sineGainDb (*procs[(size_t) k], 48000.0, 1000.0, 256, 1.0); });
        for (auto& t : threads)
            t.join();
        bool ok = true;
        for (int k = 0; k < numInstances; ++k)
            ok = ok && near (results[(size_t) k], -9.0 + 6.0 * k, 0.15);
        check (ok, "4 instances processing at once stay independent");
    }

    std::printf ("\n%d checks, %d failures: %s\n", checks, failures, failures == 0 ? "ALL PLUGIN CHECKS PASSED" : "PLUGIN CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
