// Plugin-level tests (Phase 7): the real ParaEQ processor as a host such as
// Cubase sees it. No editor, no display, so these run in CI on Windows.
//
// Covers: parameter registration (count, IDs, automatable, ranges), state
// save/restore (all parameters, old-version states, corrupt data), host
// automation while playing, sample-rate changes, bypass, mono/stereo
// layouts, float vs double processing, and several instances at once.

#include "../Source/PluginProcessor.h"
#include "../Source/Parameters.h"
#include "../Source/GUI/BandParams.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

//==============================================================================
// Allocation counter: every heap allocation in the process goes through
// these. While countAllocations is set on a thread, allocations made on
// that thread are counted, so we can prove processBlock never allocates.
namespace
{
thread_local bool countAllocations = false;
std::atomic<long> allocationCount { 0 };
}

void* operator new (std::size_t size)
{
    if (countAllocations)
        allocationCount.fetch_add (1);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

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
        auto layout = [] (CS in, CS out, CS sidechain = CS::disabled())
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (in);
            l.inputBuses.add (sidechain);
            l.outputBuses.add (out);
            return l;
        };
        check (p->checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo())), "stereo in/out supported");
        check (p->checkBusesLayoutSupported (layout (CS::mono(), CS::mono())), "mono in/out supported");
        check (! p->checkBusesLayoutSupported (layout (CS::mono(), CS::stereo())), "mono-to-stereo refused (in = out only)");
        check (! p->checkBusesLayoutSupported (layout (CS::create5point1(), CS::create5point1())), "5.1 refused");
        check (p->getBusCount (true) == 2 && ! p->getBus (true, 1)->isEnabled(), "sidechain input exists and is off by default");
        check (p->getTotalNumInputChannels() == 2, "sidechain off: host sends 2 channels");
        check (p->checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo(), CS::stereo()))
                   && p->checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo(), CS::mono()))
                   && p->checkBusesLayoutSupported (layout (CS::mono(), CS::mono(), CS::stereo())),
               "sidechain mono or stereo supported, with mono or stereo main");
        check (! p->checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo(), CS::create5point1())), "5.1 sidechain refused");
    }

    // ---------------------------------------------------------------- parameters
    {
        auto p = makeProcessor();
        const auto& params = p->getParameters();
        const int expected = 8 + peq::params::numBands * 15;
        check (params.size() == expected, "parameter count = 8 global + 24 bands x 15");

        bool allAutomatable = true, idsOk = true, uniqueIds = true;
        juce::StringArray ids;
        for (auto* prm : params)
        {
            if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm);
                w == nullptr || (w->paramID != peq::params::lookahead && w->paramID != peq::params::linearPhase))
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
        check (allAutomatable, "every parameter except look-ahead and linear phase is automatable");
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
        check (mismatches == 0, "save then load restores all 368 parameters");
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

    // Analog-matched filters (1.1): on for new instances, off for projects
    // saved before it existed, and it changes what you hear near the top.
    {
        auto fresh = makeProcessor();
        check (getValue (param (*fresh, peq::params::analogMatch)) >= 0.5f, "analog match: on for a new instance");

        // A 1.0 project (state version 2, no analog_match parameter).
        auto a = makeProcessor();
        setupBell (*a, 4.5f);
        juce::MemoryBlock saved;
        a->getStateInformation (saved);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        xml->setAttribute ("stateVersion", 2);
        for (auto* child = xml->getFirstChildElement(); child != nullptr;)
        {
            auto* next = child->getNextElement();
            if (child->getStringAttribute ("id") == peq::params::analogMatch)
                xml->removeChildElement (child, true);
            child = next;
        }
        juce::MemoryBlock v2;
        juce::AudioProcessor::copyXmlToBinary (*xml, v2);
        auto b = makeProcessor();
        b->setStateInformation (v2.getData(), (int) v2.getSize());
        check (getValue (param (*b, peq::params::analogMatch)) < 0.5f, "analog match: off when loading a 1.0 project (same sound)");

        // A current project keeps whatever was saved.
        auto c = makeProcessor();
        c->setStateInformation (saved.getData(), (int) saved.getSize());
        check (getValue (param (*c, peq::params::analogMatch)) >= 0.5f, "analog match: a current project keeps it on");

        // What you hear: a 15 kHz bell (+6 dB, Q 1) at 44.1 kHz, measured at 10 kHz.
        auto p = makeProcessor (44100.0, 512);
        setupBell (*p, 6.0f);
        setValue (bandParam (*p, 0, BandField::freq), 15000.0f);
        const double analog = 10.0 * std::log10 (peq::FilterDesign::analogPrototype (peq::FilterType::Bell, 2, 6.0, 1.0)
                                                     .magnitudeSquared (10000.0 / 15000.0));
        const double on = sineGainDb (*p, 44100.0, 10000.0, 512);
        setValue (param (*p, peq::params::analogMatch), 0.0f);
        const double off = sineGainDb (*p, 44100.0, 10000.0, 512);
        std::printf ("       15 kHz bell at 10 kHz: analogue %.2f dB, matched %.2f dB, classic %.2f dB\n", analog, on, off);
        check (near (on, analog, 0.75) && std::abs (on - analog) < 0.5 * std::abs (off - analog),
               "analog match: a 15 kHz bell keeps its analogue shape (classic one is squeezed)");
    }

    // Gain Scale (1.2): scales every band's gain; old states load at 100 %.
    {
        auto p = makeProcessor();
        setupBell (*p, 6.0f);
        check (near (getValue (param (*p, peq::params::gainScale)), 100.0, 1e-3), "gain scale: 100 % for a new instance");
        setValue (param (*p, peq::params::gainScale), 50.0f);
        const double half = sineGainDb (*p, 48000.0, 1000.0, 512);
        setValue (param (*p, peq::params::gainScale), 0.0f);
        const double none = sineGainDb (*p, 48000.0, 1000.0, 512);
        setValue (param (*p, peq::params::gainScale), 200.0f);
        const double twice = sineGainDb (*p, 48000.0, 1000.0, 512);
        check (near (half, 3.0, 0.15) && near (none, 0.0, 0.1) && near (twice, 12.0, 0.2),
               "gain scale: +6 dB bell gives +3 / 0 / +12 dB at 50 / 0 / 200 %");

        // A state saved before Gain Scale existed.
        setValue (param (*p, peq::params::gainScale), 100.0f);
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        for (auto* child = xml->getFirstChildElement(); child != nullptr;)
        {
            auto* next = child->getNextElement();
            if (child->getStringAttribute ("id") == peq::params::gainScale)
                xml->removeChildElement (child, true);
            child = next;
        }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        auto q = makeProcessor();
        setValue (param (*q, peq::params::gainScale), 30.0f);
        q->setStateInformation (old.getData(), (int) old.getSize());
        check (near (getValue (param (*q, peq::params::gainScale)), 100.0, 1e-3), "gain scale: older projects load at 100 % (same sound)");
    }

    // Sidechain and ratio (1.3).
    {
        // Older states (no ratio / sc parameters) load at 2:1 and own signal,
        // exactly how dynamic bands worked before.
        auto p = makeProcessor();
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        for (auto* child = xml->getFirstChildElement(); child != nullptr;)
        {
            auto* next = child->getNextElement();
            const auto id = child->getStringAttribute ("id");
            if (id.endsWith ("_ratio") || id.endsWith ("_sc"))
                xml->removeChildElement (child, true);
            child = next;
        }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        auto q = makeProcessor();
        setValue (bandParam (*q, 3, BandField::ratio), 8.0f);
        setValue (bandParam (*q, 3, BandField::sc), 1.0f);
        q->setStateInformation (old.getData(), (int) old.getSize());
        check (near (getValue (bandParam (*q, 3, BandField::ratio)), 2.0, 1e-3)
                   && getValue (bandParam (*q, 3, BandField::sc)) < 0.5f,
               "sidechain/ratio: older projects load at 2:1, own signal (same sound)");
        check (bandParam (*q, 0, BandField::ratio)->getCurrentValueAsText() == "2.0:1", "ratio shows as 2.0:1");
    }
    {
        // Ducking through the real plugin: a 100 Hz bell listens to the
        // sidechain; the main input is a steady 100 Hz tone.
        auto p = makeProcessor (48000.0, 512);
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        check (p->setBusesLayout (l) && p->getTotalNumInputChannels() == 4, "sidechain can be switched on (4 input channels)");
        p->prepareToPlay (48000.0, 512);
        setValue (bandParam (*p, 0, BandField::used), 1.0f);
        setValue (bandParam (*p, 0, BandField::type), 0.0f);   // bell
        setValue (bandParam (*p, 0, BandField::freq), 100.0f);
        setValue (bandParam (*p, 0, BandField::gain), 0.0f);
        setValue (bandParam (*p, 0, BandField::dyn), 1.0f);
        setValue (bandParam (*p, 0, BandField::thr), -30.0f);
        setValue (bandParam (*p, 0, BandField::range), -12.0f);
        setValue (bandParam (*p, 0, BandField::ratio), 4.0f);
        setValue (bandParam (*p, 0, BandField::sc), 1.0f);
        setValue (bandParam (*p, 0, BandField::rel), 20.0f);

        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        auto run = [&] (float scLevel, int blocks)
        {
            double re = 0.0, im = 0.0, leak = 0.0;
            int n = 0;
            static int t = 0;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 512; ++i, ++t)
                {
                    const float x = (float) (0.25 * std::sin (2.0 * pi * 100.0 * t / 48000.0));
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                    buf.setSample (2, i, scLevel * x * 4.0f);
                    buf.setSample (3, i, scLevel * x * 4.0f);
                }
                p->processBlock (buf, midi);
                if (b >= blocks / 2)
                    for (int i = 0; i < 512; ++i)
                    {
                        const double ph = 2.0 * pi * 100.0 * (t - 512 + i) / 48000.0;
                        re += buf.getSample (0, i) * std::cos (ph);
                        im += buf.getSample (0, i) * std::sin (ph);
                        leak = std::max (leak, (double) std::abs (buf.getSample (2, i)) + std::abs (buf.getSample (3, i)));
                        ++n;
                    }
            }
            return std::pair<double, double> { 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / n / 0.25), leak };
        };
        const auto quiet = run (0.0f, 60);   // sidechain silent: band at its set 0 dB
        const auto loud  = run (1.0f, 60);   // sidechain at 0 dBFS: 30 dB over, 4:1 -> capped at -12
        const auto after = run (0.0f, 60);
        std::printf ("       sidechain ducking at 100 Hz: %.2f dB silent, %.2f dB loud, %.2f dB after\n",
                     quiet.first, loud.first, after.first);
        check (near (quiet.first, 0.0, 0.1) && near (loud.first, -12.0, 0.4) && near (after.first, 0.0, 0.1),
               "sidechain: band ducks only while the sidechain is loud (main signal alone does not)");
        check (getValue (bandParam (*p, 0, BandField::sc)) >= 0.5f && quiet.second <= 0.0 && loud.second <= 0.0,
               "sidechain channels are not passed on as output");

        // Same settings with SC off: the band reacts to its own loud input.
        setValue (bandParam (*p, 0, BandField::sc), 0.0f);
        const auto own = run (0.0f, 60);    // main at -12 dBFS: 18 dB over at 4:1 -> -12 (range cap)
        check (own.first < -10.0, "sidechain off: band follows its own signal again");

        // A dynamic band set to SC with the sidechain bus switched off stays static.
        auto r = makeProcessor (48000.0, 512);
        setupBell (*r, 0.0f);
        setValue (bandParam (*r, 0, BandField::dyn), 1.0f);
        setValue (bandParam (*r, 0, BandField::thr), -40.0f);
        setValue (bandParam (*r, 0, BandField::sc), 1.0f);
        check (near (sineGainDb (*r, 48000.0, 1000.0, 512), 0.0, 0.1), "SC band with no sidechain connected stays at its set gain");
    }

    // Auto Gain (1.4).
    {
        auto p = makeProcessor();
        check (getValue (param (*p, peq::params::autoGain)) < 0.5f, "auto gain: off for a new instance");
        setupBell (*p, 9.0f);
        setValue (bandParam (*p, 0, BandField::q), 0.4f);
        const double off = sineGainDb (*p, 48000.0, 1000.0, 512);
        const double corr = p->getAutoGainCorrectionDb();
        setValue (param (*p, peq::params::autoGain), 1.0f);
        const double on = sineGainDb (*p, 48000.0, 1000.0, 512);
        std::printf ("       auto gain: +9 dB wide bell, correction %.2f dB; 1 kHz %.2f -> %.2f dB\n", corr, off, on);
        check (corr < -2.0 && corr > -9.0 && near (on - off, corr, 0.1), "auto gain: wide +9 dB bell is turned down by its correction");

        setValue (bandParam (*p, 0, BandField::gain), -9.0f);
        sineGainDb (*p, 48000.0, 1000.0, 512);
        check (p->getAutoGainCorrectionDb() > 2.0f, "auto gain: a cut is turned up");

        // Not part of presets (a listening aid, like bypass).
        auto& pm = p->getPresets();
        const auto captured = pm.capture();
        check (! captured.getChildWithProperty ("id", peq::params::autoGain).isValid(), "auto gain is not saved in presets");

        // Saved with the project.
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        auto q = makeProcessor();
        q->setStateInformation (saved.getData(), (int) saved.getSize());
        check (getValue (param (*q, peq::params::autoGain)) >= 0.5f, "auto gain: saved with the project");
    }

    // Look-ahead (1.4).
    {
        auto p = makeProcessor (48000.0, 512);
        check (getValue (param (*p, peq::params::lookahead)) < 0.5f && p->getLatencySamples() == 0,
               "look-ahead: off for a new instance, zero latency");
        check (! param (*p, peq::params::lookahead)->isAutomatable(), "look-ahead is not automatable (it changes latency)");

        // Switched on: latency reported, audio delayed by exactly that much.
        setValue (param (*p, peq::params::lookahead), 1.0f);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        buf.clear();
        p->processBlock (buf, midi);                       // picks up the switch
        p->flushLatencyUpdate();                           // latency report is async
        const int latency = p->getLatencySamples();
        check (latency == 240 && p->getLookaheadLatency() == 240, "look-ahead: reports 5 ms latency (240 samples at 48 kHz)");

        auto impulseAt = [&] (ParaEQAudioProcessor& proc, int where)
        {
            int found = -1;
            for (int block = 0; block < 3 && found < 0; ++block)
            {
                buf.clear();
                if (block == 0)
                    for (int ch = 0; ch < 2; ++ch)
                        buf.setSample (ch, where, 1.0f);
                proc.processBlock (buf, midi);
                for (int i = 0; i < 512 && found < 0; ++i)
                    if (std::abs (buf.getSample (0, i)) > 0.5f)
                        found = block * 512 + i;
            }
            return found;
        };
        check (impulseAt (*p, 100) == 100 + 240, "look-ahead: audio comes out exactly 240 samples later");

        // Bypass keeps the same delay, so switching bypass doesn't jump in time.
        setValue (param (*p, peq::params::bypass), 1.0f);
        for (int i = 0; i < 10; ++i) { buf.clear(); p->processBlock (buf, midi); }
        check (impulseAt (*p, 50) == 50 + 240, "look-ahead: bypassed audio is delayed the same");
        setValue (param (*p, peq::params::bypass), 0.0f);

        // Off again: back to zero latency.
        setValue (param (*p, peq::params::lookahead), 0.0f);
        buf.clear();
        p->processBlock (buf, midi);
        p->flushLatencyUpdate();
        check (p->getLatencySamples() == 0 && impulseAt (*p, 10) == 10, "look-ahead off: zero latency again");

        // Not in presets; saved with the project (and the latency comes back).
        setValue (param (*p, peq::params::lookahead), 1.0f);
        check (! p->getPresets().capture().getChildWithProperty ("id", peq::params::lookahead).isValid(),
               "look-ahead is not saved in presets");
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        auto q = makeProcessor (48000.0, 512);
        q->setStateInformation (saved.getData(), (int) saved.getSize());
        q->prepareToPlay (48000.0, 512);
        check (q->getLatencySamples() == 240, "look-ahead: saved with the project, latency restored on load");
    }
    {
        // A dynamic band catches the start of a burst much better with look-ahead.
        auto onsetDb = [] (bool la)
        {
            auto p = makeProcessor (48000.0, 256);
            setValue (bandParam (*p, 0, BandField::used), 1.0f);
            setValue (bandParam (*p, 0, BandField::freq), 2000.0f);
            setValue (bandParam (*p, 0, BandField::dyn), 1.0f);
            setValue (bandParam (*p, 0, BandField::thr), -30.0f);
            setValue (bandParam (*p, 0, BandField::range), -12.0f);
            setValue (bandParam (*p, 0, BandField::att), 1.0f);
            setValue (param (*p, peq::params::lookahead), la ? 1.0f : 0.0f);
            const int delay = la ? 240 : 0;
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            const int onset = 4864;
            double peak = 0.0;
            for (int block = 0; block < 40; ++block)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const int t = block * 256 + i;
                    const float x = t >= onset ? (float) (0.5 * std::sin (2.0 * pi * 2000.0 * (t - onset) / 48000.0)) : 0.0f;
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                p->processBlock (buf, midi);
                for (int i = 0; i < 256; ++i)
                {
                    const int t = block * 256 + i - delay;   // time in the input's frame
                    if (t >= onset && t < onset + 96)
                        peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            return 20.0 * std::log10 (peak);
        };
        const double without = onsetDb (false), with = onsetDb (true);
        std::printf ("       first 2 ms of a -6 dBFS burst: %.2f dB without look-ahead, %.2f dB with\n", without, with);
        check (with < without - 4.0, "look-ahead: dynamic band catches the start of a burst (4+ dB lower)");
    }

    // EQ Match (1.6): learn this track against the sidechain, add bands.
    {
        auto p = makeProcessor (48000.0, 512);
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        p->setBusesLayout (l);
        p->prepareToPlay (48000.0, 512);
        check (p->isSidechainActive() && ! p->isMatchLearning() && p->applyMatch (8, 1.0) == 0,
               "EQ Match: not learning at first; nothing learned, nothing applied");

        // A band already in use must be left alone.
        setValue (bandParam (*p, 0, BandField::used), 1.0f);
        setValue (bandParam (*p, 0, BandField::freq), 5000.0f);
        setValue (bandParam (*p, 0, BandField::gain), 0.0f);

        peq::BandParameters truth;
        truth.type = peq::FilterType::LowShelf;
        truth.frequencyHz = 150.0;
        truth.gainDb = 6.0;
        truth.analogMatched = true;
        peq::EQBand shelf;
        shelf.prepare (48000.0);
        shelf.setTarget (truth);
        shelf.snapToTarget();

        p->startMatchLearning();
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        juce::Random rng (5);
        std::vector<double> ref (512);
        const int blocks = 48000 * 12 / 512;
        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float x = (rng.nextFloat() - 0.5f) * 0.5f;
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
                ref[(size_t) i] = (rng.nextDouble() - 0.5) * 0.5;
            }
            double* ch[1] { ref.data() };
            shelf.process (ch, 1, 512);
            for (int i = 0; i < 512; ++i)
            {
                buf.setSample (2, i, (float) ref[(size_t) i]);
                buf.setSample (3, i, (float) ref[(size_t) i]);
            }
            p->processBlock (buf, midi);
            if (block % 50 == 0)
                p->pumpMatch();
        }
        p->pumpMatch();
        std::printf ("       EQ Match learned %.1f s of the track and %.1f s of the reference\n",
                     p->getMatch().sourceSeconds(), p->getMatch().referenceSeconds());
        check (p->isMatchLearning() && p->getMatch().sourceSeconds() > 11.0 && p->getMatch().referenceSeconds() > 11.0,
               "EQ Match: learns both the track and the sidechain while playing");

        p->stopMatchLearning();
        p->getUndoManager().beginNewTransaction();
        const int added = p->applyMatch (8, 1.0);
        auto curveDb = [&] (double f)
        {
            double db = 0.0;
            for (int b = 1; b < peq::FilterEngine::numBands; ++b)
            {
                if (getValue (bandParam (*p, b, BandField::used)) < 0.5f)
                    continue;
                peq::BandParameters bp;
                bp.type = (peq::FilterType) juce::roundToInt (getValue (bandParam (*p, b, BandField::type)));
                bp.frequencyHz = getValue (bandParam (*p, b, BandField::freq));
                bp.gainDb = getValue (bandParam (*p, b, BandField::gain));
                bp.q = getValue (bandParam (*p, b, BandField::q));
                bp.analogMatched = true;
                peq::CascadeCoefficients c;
                peq::FilterDesign::design (bp, 48000.0, c);
                db += c.magnitudeDb (f, 48000.0);
            }
            return db;
        };
        peq::CascadeCoefficients tc;
        peq::FilterDesign::design (truth, 48000.0, tc);
        const double want = tc.magnitudeDb (50.0, 48000.0) - tc.magnitudeDb (2000.0, 48000.0);
        const double got = curveDb (50.0) - curveDb (2000.0);
        std::printf ("       EQ Match added %d band(s): 50 Hz vs 2 kHz %.2f dB (reference is %.2f dB)\n", added, got, want);
        check (added >= 1 && added <= 8 && near (got, want, 1.5), "EQ Match: the added bands follow the reference's tone");
        check (near (getValue (bandParam (*p, 0, BandField::freq)), 5000.0, 0.5)
                   && near (getValue (bandParam (*p, 0, BandField::gain)), 0.0, 0.01),
               "EQ Match: bands already in use are left alone");

        p->getUndoManager().undo();
        int used = 0;
        for (int b = 0; b < peq::FilterEngine::numBands; ++b)
            used += getValue (bandParam (*p, b, BandField::used)) >= 0.5f ? 1 : 0;
        check (used == 1, "EQ Match: one undo takes all the added bands away");
    }

    // Linear phase (1.5).
    {
        auto p = makeProcessor (48000.0, 512);
        check (getValue (param (*p, peq::params::linearPhase)) < 0.5f && ! p->isLinearPhaseActive(),
               "linear phase: off for a new instance");
        check (! param (*p, peq::params::linearPhase)->isAutomatable(), "linear phase is not automatable (it changes latency)");

        setupBell (*p, 6.0f);
        const double minimumPhase = sineGainDb (*p, 48000.0, 1000.0, 512);
        setValue (param (*p, peq::params::linearPhase), 1.0f);
        check (p->updateLinearPhaseKernel(), "linear phase: kernel designed when switched on");
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        buf.clear();
        p->processBlock (buf, midi);
        p->flushLatencyUpdate();
        const int lat = p->getLatencySamples();
        std::printf ("       linear phase latency at 48 kHz: %d samples (%.1f ms)\n", lat, lat / 48.0);
        check (p->isLinearPhaseActive() && lat == 4096 + peq::LinearPhaseFIR::partitionSize,
               "linear phase: active, latency 4352 samples (91 ms) at 48 kHz");

        // Same gain at 1 kHz as the normal mode (the bell is in the FIR now).
        const double linearGain = sineGainDb (*p, 48000.0, 1000.0, 512, 1.0);
        std::printf ("       +6 dB bell at 1 kHz: %.2f dB minimum phase, %.2f dB linear phase\n", minimumPhase, linearGain);
        check (near (linearGain, minimumPhase, 0.1), "linear phase: same gain as minimum phase on a bell");

        // Phase: a 300 Hz tone near the bell keeps its timing (exactly the latency later).
        {
            p->reset();
            const int total = 48000;
            double re = 0.0, im = 0.0;
            juce::AudioBuffer<float> b (2, 480);
            for (int pos = 0; pos < total; pos += 480)
            {
                for (int i = 0; i < 480; ++i)
                {
                    const float x = (float) (0.25 * std::sin (2.0 * pi * 300.0 * (pos + i) / 48000.0));
                    b.setSample (0, i, x);
                    b.setSample (1, i, x);
                }
                p->processBlock (b, midi);
                if (pos >= total / 2)
                    for (int i = 0; i < 480; ++i)
                    {
                        const double ph = 2.0 * pi * 300.0 * (pos + i - lat) / 48000.0;
                        re += b.getSample (0, i) * std::cos (ph);
                        im += b.getSample (0, i) * std::sin (ph);
                    }
            }
            // A sine delayed by exactly the latency projects fully onto sin(), none onto cos().
            const double phaseError = std::atan2 (re, im);
            std::printf ("       linear phase: 300 Hz phase error after removing the latency: %.4f rad\n", phaseError);
            check (std::abs (phaseError) < 0.01, "linear phase: no phase shift (300 Hz arrives exactly one latency later)");
        }

        // Dragging a band updates the FIR.
        setValue (bandParam (*p, 0, BandField::gain), -6.0f);
        p->updateLinearPhaseKernel();
        check (near (sineGainDb (*p, 48000.0, 1000.0, 512, 1.0), -6.0, 0.15), "linear phase: band changes reach the FIR");

        // A dynamic band stays minimum phase but still works.
        setValue (bandParam (*p, 0, BandField::dyn), 1.0f);
        setValue (bandParam (*p, 0, BandField::thr), -60.0f);
        setValue (bandParam (*p, 0, BandField::range), -6.0f);
        p->updateLinearPhaseKernel();
        const double dynGain = sineGainDb (*p, 48000.0, 1000.0, 512, 1.0);
        check (dynGain < -9.0, "linear phase: dynamic bands still work (IIR, before the FIR)");
        setValue (bandParam (*p, 0, BandField::dyn), 0.0f);

        // Bypass in linear mode keeps the delay (no jump in time).
        setValue (param (*p, peq::params::bypass), 1.0f);
        for (int i = 0; i < 10; ++i) { buf.clear(); p->processBlock (buf, midi); }
        int found = -1;
        for (int block = 0; block < 12 && found < 0; ++block)
        {
            buf.clear();
            if (block == 0) { buf.setSample (0, 10, 1.0f); buf.setSample (1, 10, 1.0f); }
            p->processBlock (buf, midi);
            for (int i = 0; i < 512 && found < 0; ++i)
                if (std::abs (buf.getSample (0, i)) > 0.5f)
                    found = block * 512 + i;
        }
        check (found == 10 + lat, "linear phase: bypassed audio keeps the same delay");
        setValue (param (*p, peq::params::bypass), 0.0f);

        // With look-ahead too: the latencies add up.
        setValue (param (*p, peq::params::lookahead), 1.0f);
        buf.clear();
        p->processBlock (buf, midi);
        p->flushLatencyUpdate();
        check (p->getLatencySamples() == lat + 240, "linear phase + look-ahead: latencies add up");
        setValue (param (*p, peq::params::lookahead), 0.0f);

        // Not in presets; saved with the project and active straight after loading.
        check (! p->getPresets().capture().getChildWithProperty ("id", peq::params::linearPhase).isValid(),
               "linear phase is not saved in presets");
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        auto q = makeProcessor (48000.0, 512);
        q->setStateInformation (saved.getData(), (int) saved.getSize());
        q->prepareToPlay (48000.0, 512);
        check (q->isLinearPhaseActive() && q->getLatencySamples() == lat, "linear phase: restored with the project");

        // Off again: zero latency, IIR bands back.
        setValue (param (*p, peq::params::linearPhase), 0.0f);
        buf.clear();
        p->processBlock (buf, midi);
        p->flushLatencyUpdate();
        check (! p->isLinearPhaseActive() && p->getLatencySamples() == 0
                   && near (sineGainDb (*p, 48000.0, 1000.0, 512), -6.0, 0.15),
               "linear phase off: zero latency, bands back to normal");

        // 96 kHz: longer kernel, same time.
        auto r = makeProcessor (96000.0, 512);
        setValue (param (*r, peq::params::linearPhase), 1.0f);
        r->prepareToPlay (96000.0, 512);
        check (r->getLatencySamples() == 8192 + peq::LinearPhaseFIR::partitionSize, "linear phase at 96 kHz: 8448 samples (88 ms)");
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
        mono.inputBuses.add (juce::AudioChannelSet::disabled());   // sidechain
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

    // ---------------------------------------------------------------- presets
    {
        auto p = makeProcessor();
        auto& pm = p->getPresets();
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ParaEQ-preset-test-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        folder.deleteRecursively();
        pm.setUserFolder (folder);

        const auto factory = pm.getFactoryNames();
        check (factory.size() >= 12, "at least 12 factory presets");
        bool allLoad = true;
        for (int i = 0; i < factory.size(); ++i)
            allLoad = allLoad && pm.loadFactory (i) && pm.getCurrentName() == factory[i] && ! pm.isModified();
        check (allLoad, "every factory preset loads and shows its name");

        auto usedCount = [&p]
        {
            int n = 0;
            for (int b = 0; b < peq::params::numBands; ++b)
                n += getValue (bandParam (*p, b, BandField::used)) >= 0.5f ? 1 : 0;
            return n;
        };
        pm.loadFactory (factory.indexOf ("Default (flat)"));
        check (usedCount() == 0, "Default (flat) clears all bands");
        pm.loadFactory (factory.indexOf ("Vocal - de-ess (dynamic)"));
        check (usedCount() == 2 && getValue (bandParam (*p, 1, BandField::dyn)) >= 0.5f
                   && near (getValue (bandParam (*p, 1, BandField::freq)), 6500.0, 1.0),
               "de-ess preset sets a dynamic band at 6.5 kHz");

        // Undo takes a preset load back in one step.
        pm.loadFactory (factory.indexOf ("Default (flat)"));
        setupBell (*p, 5.0f);
        p->flushUndo();
        p->getUndoManager().beginNewTransaction();
        pm.loadFactory (factory.indexOf ("Air boost"));
        p->flushUndo();
        p->getUndoManager().undo();
        check (near (getValue (bandParam (*p, 0, BandField::gain)), 5.0, 0.01)
                   && juce::roundToInt (getValue (bandParam (*p, 0, BandField::type))) == 0,
               "undo after loading a preset restores the previous settings");

        // Save, change, reload.
        setupBell (*p, 4.0f);
        check (pm.saveUser ("My vocal"), "save user preset");
        check (pm.fileForUserPreset ("My vocal").existsAsFile() && pm.getCurrentName() == "My vocal" && ! pm.isModified(),
               "preset file written, name shown, not modified");
        setValue (bandParam (*p, 0, BandField::gain), -7.0f);
        check (pm.isModified(), "changing a knob marks the preset modified");
        check (pm.loadUser ("My vocal") && near (getValue (bandParam (*p, 0, BandField::gain)), 4.0, 0.01),
               "loading the user preset restores it");

        check (pm.renameUser ("My vocal", "Lead vocal") && ! pm.userPresetExists ("My vocal")
                   && pm.userPresetExists ("Lead vocal") && pm.getCurrentName() == "Lead vocal",
               "rename user preset");
        pm.saveUser ("Other");
        check (! pm.renameUser ("Lead vocal", "Other"), "rename refuses to overwrite another preset");
        check (pm.getUserNames() == juce::StringArray ({ "Lead vocal", "Other" }), "user preset list is sorted");

        const auto exported = folder.getChildFile ("export-test.paraeq");
        pm.loadUser ("Lead vocal");
        check (pm.exportTo (exported) && exported.existsAsFile(), "export preset to a file");
        setValue (bandParam (*p, 0, BandField::gain), 0.0f);
        const auto imported = pm.importFrom (exported);
        check (imported == "Lead vocal (2)" && near (getValue (bandParam (*p, 0, BandField::gain)), 4.0, 0.01),
               "import copies into my presets (renamed if taken) and loads it");

        const auto junkFile = folder.getChildFile ("junk.paraeq");
        junkFile.replaceWithText ("not a preset");
        check (pm.importFrom (junkFile).isEmpty(), "importing a non-preset file is refused");

        check (pm.deleteUser ("Other") && ! pm.userPresetExists ("Other"), "delete user preset");

        // A preset written before dynamics existed: missing values get defaults.
        auto xml = pm.createPresetXml ("Old");
        for (auto* e = xml->getFirstChildElement(); e != nullptr;)
        {
            auto* next = e->getNextElement();
            if (e->getStringAttribute ("id").endsWith ("_dyn"))
                xml->removeChildElement (e, true);
            e = next;
        }
        const auto oldFile = folder.getChildFile ("Old.paraeq");
        xml->writeTo (oldFile);
        setValue (bandParam (*p, 0, BandField::dyn), 1.0f);
        check (pm.loadFile (oldFile) && getValue (bandParam (*p, 0, BandField::dyn)) < 0.5f,
               "older preset without dynamics loads with dynamics off");

        // Bypass is not part of a preset.
        setValue (param (*p, peq::params::bypass), 1.0f);
        pm.loadFactory (0);
        check (getValue (param (*p, peq::params::bypass)) >= 0.5f, "loading a preset leaves bypass alone");
        setValue (param (*p, peq::params::analogMatch), 0.0f);
        pm.loadFactory (1);
        check (getValue (param (*p, peq::params::analogMatch)) < 0.5f, "loading a preset leaves analog match alone");
        setValue (param (*p, peq::params::analogMatch), 1.0f);
        setValue (param (*p, peq::params::bypass), 0.0f);

        // Preset name travels with the project; not shown as modified after load.
        pm.loadUser ("Lead vocal");
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        auto q = makeProcessor();
        q->setStateInformation (saved.getData(), (int) saved.getSize());
        check (q->getPresets().getCurrentName() == "Lead vocal" && ! q->getPresets().isModified(),
               "project remembers the preset name, not marked modified");

        folder.deleteRecursively();
    }

    // ---------------------------------------------------------------- A/B
    {
        auto p = makeProcessor();
        setupBell (*p, 3.0f);
        check (p->getActiveSlot() == 0, "starts on A");
        p->switchAB();
        check (p->getActiveSlot() == 1 && near (getValue (bandParam (*p, 0, BandField::gain)), 3.0, 0.01),
               "first switch to B starts as a copy of A");
        setValue (bandParam (*p, 0, BandField::gain), -5.0f);
        p->switchAB();
        check (p->getActiveSlot() == 0 && near (getValue (bandParam (*p, 0, BandField::gain)), 3.0, 0.01), "back to A recalls A");
        p->switchAB();
        check (near (getValue (bandParam (*p, 0, BandField::gain)), -5.0, 0.01), "B recalls B");
        check (near (sineGainDb (*p, 48000.0, 1000.0, 512), -5.0, 0.15), "B is what you hear");

        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        auto q = makeProcessor();
        q->setStateInformation (saved.getData(), (int) saved.getSize());
        check (q->getActiveSlot() == 1 && near (getValue (bandParam (*q, 0, BandField::gain)), -5.0, 0.01), "project restores the active slot");
        q->switchAB();
        check (near (getValue (bandParam (*q, 0, BandField::gain)), 3.0, 0.01), "project restores the other slot too");

        q->copyActiveToOther();   // A -> B
        setValue (bandParam (*q, 0, BandField::gain), 9.0f);
        q->switchAB();
        check (near (getValue (bandParam (*q, 0, BandField::gain)), 3.0, 0.01), "copy A to B copies the settings");
    }

    // ---------------------------------------------------------------- copy/paste band
    {
        auto p = makeProcessor();
        auto refs = peq::gui::makeBandRefs (p->getState());
        setupBell (*p, 7.0f);
        setValue (bandParam (*p, 0, BandField::mode), 4.0f);    // Side
        setValue (bandParam (*p, 0, BandField::dyn), 1.0f);
        setValue (bandParam (*p, 0, BandField::thr), -33.0f);
        const auto text = peq::gui::bandToClipboardText (refs[0]);
        check (peq::gui::isBandClipboardText (text) && ! peq::gui::isBandClipboardText ("hello"), "copied band is recognised");
        check (peq::gui::pasteBandFromClipboardText (refs[9], text), "paste band");
        bool same = true;
        for (auto f : { BandField::used, BandField::on, BandField::type, BandField::freq, BandField::gain, BandField::q,
                        BandField::slope, BandField::mode, BandField::dyn, BandField::thr, BandField::range,
                        BandField::att, BandField::rel })
            same = same && near (getValue (bandParam (*p, 0, f)), getValue (bandParam (*p, 9, f)), 1e-3);
        check (same, "pasted band matches the copied one in every setting");
    }

    // ---------------------------------------------------------------- Phase 9
    // A "busy" setup: all 24 bands in use, mixed types, channel modes and
    // slopes, half of them dynamic. Used by the next three tests.
    auto busySetup = [] (ParaEQAudioProcessor& p, juce::Random& rng)
    {
        for (int b = 0; b < peq::params::numBands; ++b)
        {
            setValue (bandParam (p, b, BandField::used), 1.0f);
            setValue (bandParam (p, b, BandField::type), (float) (b % 7));
            setValue (bandParam (p, b, BandField::freq), 30.0f * std::pow (600.0f, rng.nextFloat()));
            setValue (bandParam (p, b, BandField::gain), rng.nextFloat() * 24.0f - 12.0f);
            setValue (bandParam (p, b, BandField::q), 0.3f + rng.nextFloat() * 4.0f);
            setValue (bandParam (p, b, BandField::slope), (float) rng.nextInt (8));
            setValue (bandParam (p, b, BandField::mode), (float) (b % 5));
            setValue (bandParam (p, b, BandField::dyn), b % 2 == 0 ? 1.0f : 0.0f);
            setValue (bandParam (p, b, BandField::thr), -40.0f);
        }
    };

    // No heap allocation on the audio thread, in any state (steady, while
    // every parameter moves, through bypass and back, with the analyser on).
    {
        auto p = makeProcessor (48000.0, 512);
        juce::Random rng (21);
        busySetup (*p, rng);
        p->setAnalyserWanted (true);
        juce::AudioBuffer<float> buf (2, 512);
        juce::AudioBuffer<double> dbuf (2, 512);
        juce::MidiBuffer midi;
        auto& params = p->getParameters();
        long allocations = 0;
        for (int block = 0; block < 400; ++block)
        {
            // Parameter changes happen on "another thread" (not counted).
            if (block % 3 == 0)
                for (int k = 0; k < 20; ++k)
                    params[rng.nextInt (params.size())]->setValueNotifyingHost (rng.nextFloat());
            if (block == 150) setValue (param (*p, peq::params::bypass), 1.0f);
            if (block == 200) setValue (param (*p, peq::params::bypass), 0.0f);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, rng.nextFloat() - 0.5f);

            allocationCount = 0;
            countAllocations = true;
            p->processBlock (buf, midi);
            countAllocations = false;
            allocations += allocationCount.load();
        }
        // Sanity: the counter does see allocations.
        countAllocations = true;
        allocationCount = 0;
        { std::vector<int> probe (100); juce::ignoreUnused (probe); }
        countAllocations = false;
        check (allocationCount.load() > 0, "allocation counter works (sees a test allocation)");
        check (allocations == 0, "no memory allocation on the audio thread (400 blocks, all parameters moving)");

        p->setProcessingPrecision (juce::AudioProcessor::doublePrecision);
        p->prepareToPlay (48000.0, 512);
        allocationCount = 0;
        countAllocations = true;
        for (int block = 0; block < 50; ++block)
            p->processBlock (dbuf, midi);
        countAllocations = false;
        check (allocationCount.load() == 0, "no memory allocation in 64-bit processing either");

        // Linear phase, with the kernel being redesigned between blocks (as
        // the timer does while a band is dragged) and bands moving.
        p->setProcessingPrecision (juce::AudioProcessor::singlePrecision);
        setValue (param (*p, peq::params::linearPhase), 1.0f);
        p->prepareToPlay (48000.0, 512);
        long linearAllocs = 0;
        for (int block = 0; block < 300; ++block)
        {
            if (block % 4 == 0)
            {
                setValue (bandParam (*p, 0, BandField::gain), rng.nextFloat() * 24.0f - 12.0f);
                p->updateLinearPhaseKernel();   // message thread work, not counted
            }
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, rng.nextFloat() - 0.5f);
            allocationCount = 0;
            countAllocations = true;
            p->processBlock (buf, midi);
            countAllocations = false;
            linearAllocs += allocationCount.load();
        }
        check (p->isLinearPhaseActive() && linearAllocs == 0, "no memory allocation in linear phase (kernel changing while playing)");

        // EQ Match learning (the audio thread only fills a ring buffer).
        setValue (param (*p, peq::params::linearPhase), 0.0f);
        p->startMatchLearning();
        long matchAllocs = 0;
        for (int block = 0; block < 100; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, rng.nextFloat() - 0.5f);
            allocationCount = 0;
            countAllocations = true;
            p->processBlock (buf, midi);
            countAllocations = false;
            matchAllocs += allocationCount.load();
            if (block % 10 == 0)
                p->pumpMatch();
        }
        check (p->isMatchLearning() && p->getMatch().sourceSeconds() > 0.5 && matchAllocs == 0,
               "no memory allocation while EQ Match is learning");
    }

    // Stress: random parameters every block, random block sizes (1..4096),
    // sample-rate changes, extreme input (silence, full-scale noise, DC,
    // tiny values). Output must stay finite and bounded.
    {
        auto p = makeProcessor (48000.0, 4096);
        juce::Random rng (77);
        busySetup (*p, rng);
        auto& params = p->getParameters();
        juce::AudioBuffer<float> buf (2, 4096);
        juce::MidiBuffer midi;
        bool ok = true;
        double worst = 0.0;
        const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0, 22050.0 };
        for (int round = 0; round < 5 && ok; ++round)
        {
            const double sr = rates[round];
            p->setRateAndBufferSizeDetails (sr, 4096);
            p->prepareToPlay (sr, 4096);
            for (int block = 0; block < 300 && ok; ++block)
            {
                for (int k = 0; k < 8; ++k)
                    if (auto* prm = params[rng.nextInt (params.size())]; prm != p->getBypassParameter())
                        prm->setValueNotifyingHost (rng.nextFloat());
                // Keep gains sane enough that "bounded" is meaningful.
                setValue (param (*p, peq::params::inputGain), 0.0f);
                setValue (param (*p, peq::params::outputGain), 0.0f);

                const int n = 1 + rng.nextInt (4096);
                buf.setSize (2, n, false, false, true);
                const int kind = rng.nextInt (4);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        buf.setSample (ch, i, kind == 0 ? 0.0f
                                            : kind == 1 ? rng.nextFloat() * 2.0f - 1.0f
                                            : kind == 2 ? 0.5f
                                                        : (rng.nextFloat() - 0.5f) * 1.0e-30f);
                p->processBlock (buf, midi);
                for (int ch = 0; ch < 2 && ok; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        const float y = buf.getSample (ch, i);
                        if (! std::isfinite (y)) { ok = false; break; }
                        worst = std::max (worst, (double) std::abs (y));
                    }
            }
        }
        std::printf ("       stress: largest output sample %.1f (24 bands up to +30 dB each can add a lot)\n", worst);
        // 24 bands could stack 24 x 30 dB in theory; a blow-up shows as inf/NaN
        // or astronomically large values, which is what we check against.
        check (ok && worst < 1.0e6, "stress: 1500 random blocks at 5 sample rates stay finite and stable");
    }

    // Host-like threading: the audio thread processes while the "message
    // thread" moves parameters, saves and loads state and presets, and reads
    // meters - all at the same time. Run under ThreadSanitizer to find races.
    {
        auto p = makeProcessor (48000.0, 256);
        juce::Random rng (8);
        busySetup (*p, rng);
        p->setAnalyserWanted (true);
        std::atomic<bool> running { true };
        std::atomic<bool> audioOk { true };
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            juce::Random r (9);
            while (running.load())
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        buf.setSample (ch, i, r.nextFloat() - 0.5f);
                p->processBlock (buf, midi);
                for (int i = 0; i < 256; ++i)
                    if (! std::isfinite (buf.getSample (0, i)))
                        audioOk = false;
            }
        });

        auto& params = p->getParameters();
        auto& pm = p->getPresets();
        std::vector<float> fifoOut (4096);
        for (int round = 0; round < 300; ++round)
        {
            for (int k = 0; k < 10; ++k)
                if (auto* prm = params[rng.nextInt (params.size())]; prm != p->getBypassParameter())
                    prm->setValueNotifyingHost (rng.nextFloat());
            juce::ignoreUnused (p->takeOutputPeak (0), p->getDynamicGainDb (rng.nextInt (24)));
            p->getPostFifo().pop (fifoOut.data(), fifoOut.size());
            if (round % 50 == 10)
            {
                juce::MemoryBlock mb;
                p->getStateInformation (mb);
                p->setStateInformation (mb.getData(), (int) mb.getSize());
            }
            if (round % 50 == 30)
                pm.loadFactory (rng.nextInt (pm.getFactoryNames().size()));
            if (round % 75 == 40)
                p->switchAB();
            if (round == 100)
                setValue (param (*p, peq::params::linearPhase), 1.0f);
            p->updateLinearPhaseKernel();   // as the timer does
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        running = false;
        audio.join();
        check (audioOk.load(), "audio thread and message thread working at once: no crash, output finite");
    }

    // CPU: time the busy setup (24 bands, half dynamic) in real-time units.
    {
        auto p = makeProcessor (48000.0, 256);
        juce::Random rng (5);
        busySetup (*p, rng);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                buf.setSample (ch, i, rng.nextFloat() - 0.5f);
        for (int block = 0; block < 200; ++block)
            p->processBlock (buf, midi);   // warm up, settle smoothing

        const int blocks = 3750;   // 20 s of audio
        const auto t0 = std::chrono::steady_clock::now();
        for (int block = 0; block < blocks; ++block)
            p->processBlock (buf, midi);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double audioSeconds = blocks * 256.0 / 48000.0;
        const double load = 100.0 * seconds / audioSeconds;
        std::printf ("       CPU: 24 bands (12 dynamic), stereo 48 kHz: %.2f %% of one core (%.1f ms per second of audio)\n",
                     load, 1000.0 * seconds / audioSeconds);

        // A single bell, for scale.
        auto q = makeProcessor (48000.0, 256);
        setupBell (*q, 6.0f);
        for (int block = 0; block < 200; ++block)
            q->processBlock (buf, midi);
        const auto t1 = std::chrono::steady_clock::now();
        for (int block = 0; block < blocks; ++block)
            q->processBlock (buf, midi);
        const double oneBand = 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count() / audioSeconds;
        std::printf ("       CPU: 1 bell, stereo 48 kHz: %.2f %% of one core\n", oneBand);

        // Linear phase, same busy settings.
        setValue (param (*p, peq::params::linearPhase), 1.0f);
        p->prepareToPlay (48000.0, 256);
        for (int block = 0; block < 200; ++block)
            p->processBlock (buf, midi);
        const auto t2 = std::chrono::steady_clock::now();
        for (int block = 0; block < blocks; ++block)
            p->processBlock (buf, midi);
        const double linearLoad = 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t2).count() / audioSeconds;
        std::printf ("       CPU: same, linear phase: %.2f %% of one core\n", linearLoad);
       #if defined (NDEBUG)
        check (load < 10.0, "CPU: fully loaded instance under 10 % of one core (release build)");
        check (linearLoad < 15.0, "CPU: linear phase instance under 15 % of one core (release build)");
       #else
        check (load < 100.0, "CPU: runs faster than real time (debug build; release is much faster)");
       #endif
    }

    std::printf ("\n%d checks, %d failures: %s\n", checks, failures, failures == 0 ? "ALL PLUGIN CHECKS PASSED" : "PLUGIN CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
