#include "EQGraph.h"
#include "GraphMath.h"
#include "Theme.h"
#include "../PluginProcessor.h"

#include <cmath>

namespace peq::gui
{

namespace
{
    constexpr float nodeRadius = 7.5f;
    constexpr float hitRadius = 12.0f;
    constexpr float rightMargin = 36.0f;   // dB labels
    constexpr float bottomMargin = 20.0f;  // frequency labels
    constexpr float topMargin = 8.0f;

    /** Default settings for a band created by clicking at a frequency: low
        cut near the bottom, high cut near the top, a bell elsewhere. */
    FilterType typeForNewBand (double hz) noexcept
    {
        if (hz <= 25.0)    return FilterType::HighPass;
        if (hz >= 18000.0) return FilterType::LowPass;
        return FilterType::Bell;
    }
}

EQGraph::EQGraph (ParaEQAudioProcessor& p, AllBands& b)
    : processor (p), bands (b)
{
    analogMatchParam = processor.getState().getParameter (params::analogMatch);
    gainScaleParam   = processor.getState().getParameter (params::gainScale);
    setOpaque (true);
    setWantsKeyboardFocus (false);
    bandCurves.resize ((size_t) FilterEngine::numBands);
    refreshFromParameters (true);
    startTimerHz (30);
}

EQGraph::~EQGraph()
{
    endDrag();
}

//==============================================================================
juce::Rectangle<float> EQGraph::plotArea() const
{
    return getLocalBounds().toFloat()
        .withTrimmedRight (rightMargin)
        .withTrimmedBottom (bottomMargin)
        .withTrimmedTop (topMargin);
}

float EQGraph::xForFreq (double hz) const
{
    const auto plot = plotArea();
    return plot.getX() + (float) graph::freqToX (hz, plot.getWidth());
}

float EQGraph::yForDb (double db) const
{
    const auto plot = plotArea();
    return plot.getY() + (float) graph::dbToY (db, plot.getHeight(), rangeDb);
}

juce::Point<float> EQGraph::nodePosition (int band) const
{
    const auto& r = bands[(size_t) band];
    const double hz = (double) BandParamRefs::get (r.freq);
    const double db = typeHasGain (r.filterType()) ? (double) BandParamRefs::get (r.gain) * gainScaleFactor() : 0.0;
    const auto plot = plotArea();
    return { xForFreq (hz), juce::jlimit (plot.getY(), plot.getBottom(), yForDb (db)) };
}

int EQGraph::bandAt (juce::Point<float> position) const
{
    int best = -1;
    float bestDist = hitRadius;

    // Prefer the selected band when nodes overlap.
    if (selected >= 0 && bands[(size_t) selected].isUsed()
        && nodePosition (selected).getDistanceFrom (position) <= hitRadius)
        return selected;

    for (int b = 0; b < FilterEngine::numBands; ++b)
    {
        if (! bands[(size_t) b].isUsed())
            continue;
        const float d = nodePosition (b).getDistanceFrom (position);
        if (d <= bestDist)
        {
            bestDist = d;
            best = b;
        }
    }
    return best;
}

//==============================================================================
void EQGraph::setSelectedBand (int band)
{
    if (band >= 0 && ! bands[(size_t) band].isUsed())
        band = -1;
    if (band == selected)
        return;
    selected = band;
    repaint();
    if (onSelectionChanged)
        onSelectionChanged (selected);
}

void EQGraph::setDisplayRange (double newRange)
{
    rangeDb = juce::jlimit (3.0, 30.0, newRange);
    recomputeCurves();
    repaint();
}

int EQGraph::addBandAt (juce::Point<float> plotPosition)
{
    int band = -1;
    for (int b = 0; b < FilterEngine::numBands; ++b)
        if (! bands[(size_t) b].isUsed())
        {
            band = b;
            break;
        }
    if (band < 0)
        return -1;

    const auto plot = plotArea();
    const double hz = juce::jlimit (limits::minFrequency, limits::maxFrequency,
                                    graph::xToFreq (plotPosition.x - plot.getX(), plot.getWidth()));
    const auto type = typeForNewBand (hz);
    const double db = typeHasGain (type)
                          ? juce::jlimit (limits::minGainDb, limits::maxGainDb,
                                          graph::yToDb (plotPosition.y - plot.getY(), plot.getHeight(), rangeDb))
                          : 0.0;

    processor.beginUndoStep ("Add band");
    auto& r = bands[(size_t) band];
    BandParamRefs::setWithGesture (r.type, (float) type);
    BandParamRefs::setWithGesture (r.freq, (float) hz);
    BandParamRefs::setWithGesture (r.gain, (float) db);
    BandParamRefs::setWithGesture (r.q, type == FilterType::Bell ? 1.0f : (float) limits::defaultQ);
    BandParamRefs::setWithGesture (r.slope, type == FilterType::Bell ? 1.0f : 3.0f);  // 12 or 24 dB/oct
    BandParamRefs::setWithGesture (r.mode, (float) ChannelMode::Stereo);
    // A reused band starts fresh: dynamics off and at their defaults.
    for (auto* p : { r.dyn, r.thr, r.range, r.att, r.rel })
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
    BandParamRefs::setWithGesture (r.on, 1.0f);
    BandParamRefs::setWithGesture (r.used, 1.0f);

    processor.flushUndo();
    refreshFromParameters (true);
    setSelectedBand (band);
    return band;
}

void EQGraph::copyBand (int band) const
{
    if (band >= 0 && bands[(size_t) band].isUsed())
        juce::SystemClipboard::copyTextToClipboard (bandToClipboardText (bands[(size_t) band]));
}

bool EQGraph::clipboardHasBand()
{
    return isBandClipboardText (juce::SystemClipboard::getTextFromClipboard());
}

int EQGraph::pasteBand (int target)
{
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    if (! isBandClipboardText (text))
        return -1;

    if (target < 0)
    {
        for (int b = 0; b < FilterEngine::numBands && target < 0; ++b)
            if (! bands[(size_t) b].isUsed())
                target = b;
        if (target < 0)
            return -1;
    }

    processor.beginUndoStep ("Paste band");
    pasteBandFromClipboardText (bands[(size_t) target], text);
    processor.flushUndo();
    refreshFromParameters (true);
    setSelectedBand (target);
    repaint();
    return target;
}

void EQGraph::deleteBand (int band)
{
    if (band < 0 || ! bands[(size_t) band].isUsed())
        return;
    processor.beginUndoStep ("Delete band");
    if (processor.getSoloBand() == band)
        processor.setSoloBand (-1);
    BandParamRefs::setWithGesture (bands[(size_t) band].used, 0.0f);
    processor.flushUndo();
    if (selected == band)
        setSelectedBand (-1);
    refreshFromParameters (true);
}

void EQGraph::resetGain (int band)
{
    if (band < 0)
        return;
    processor.beginUndoStep ("Reset gain");
    BandParamRefs::setWithGesture (bands[(size_t) band].gain, 0.0f);
    processor.flushUndo();
    refreshFromParameters (true);
}

void EQGraph::adjustQ (int band, double wheelSteps)
{
    if (band < 0 || ! bands[(size_t) band].isUsed())
        return;

    // Group a burst of wheel movement into one undo step.
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastWheelTime > 600)
        processor.beginUndoStep ("Change Q");
    lastWheelTime = now;

    auto* q = bands[(size_t) band].q;
    const double newQ = graph::qAfterWheel ((double) BandParamRefs::get (q), wheelSteps,
                                            limits::minQ, limits::maxQ);
    BandParamRefs::setWithGesture (q, (float) newQ);
    processor.flushUndo();
    refreshFromParameters (true);
}

double EQGraph::gainScaleFactor() const noexcept
{
    return gainScaleParam != nullptr ? juce::jlimit (0.0, 2.0, (double) gainScaleParam->convertFrom0to1 (gainScaleParam->getValue()) / 100.0)
                                     : 1.0;
}

void EQGraph::nudgeBand (int band, double octaves, double db)
{
    if (band < 0 || band >= FilterEngine::numBands || ! bands[(size_t) band].isUsed())
        return;
    auto& r = bands[(size_t) band];

    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastNudgeTime > 600)
        processor.beginUndoStep ("Nudge band");
    lastNudgeTime = now;

    if (! juce::exactlyEqual (octaves, 0.0))
    {
        const double hz = (double) BandParamRefs::get (r.freq) * std::pow (2.0, octaves);
        BandParamRefs::setWithGesture (r.freq, (float) juce::jlimit (limits::minFrequency, limits::maxFrequency, hz));
    }
    if (! juce::exactlyEqual (db, 0.0) && typeHasGain (r.filterType()))
    {
        const double g = std::round (((double) BandParamRefs::get (r.gain) + db) * 10.0) / 10.0;
        BandParamRefs::setWithGesture (r.gain, (float) juce::jlimit (limits::minGainDb, limits::maxGainDb, g));
    }
    processor.flushUndo();
    refreshFromParameters (true);
    repaint();
}

juce::String EQGraph::cursorReadout (juce::Point<float> position) const
{
    const auto plot = plotArea();
    const double hz = graph::xToFreq ((double) (position.x - plot.getX()), (double) plot.getWidth());
    const double db = graph::yToDb ((double) (position.y - plot.getY()), (double) plot.getHeight(), rangeDb);

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int nearest = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - nearest) * 100.0);
    const int octave = nearest / 12 - 1;
    const juce::String note = juce::String (names[((nearest % 12) + 12) % 12]) + juce::String (octave)
                            + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + juce::String (cents) + "c");

    const juce::String freq = hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz"
                                           : juce::String (hz, hz >= 100.0 ? 0 : 1) + " Hz";
    return freq + "   " + note + "   " + (db >= 0.0 ? "+" : "") + juce::String (db, 1) + " dB";
}

void EQGraph::toggleSolo (int band)
{
    processor.setSoloBand (processor.getSoloBand() == band ? -1 : band);
    repaint();
    if (onSelectionChanged)
        onSelectionChanged (selected);  // lets the band panel update its solo button
}

//==============================================================================
void EQGraph::setAnalyserSettings (const AnalyserSettings& newSettings)
{
    analyser = newSettings;
    processor.setAnalyserWanted (analyser.enabled());
    configureAnalysers();
    for (auto* a : { &preAnalyser, &postAnalyser, &sidechainAnalyser })
    {
        a->setReleaseSeconds (analyser.releaseSeconds);
        a->setPeakHold (analyser.peakHold);
    }
    repaint();
}

void EQGraph::configureAnalysers()
{
    const double sr = processor.getCurrentSampleRateOrDefault();
    if (juce::exactlyEqual (sr, analyserRate) && analyser.fftOrder == analyserOrder)
        return;
    analyserRate = sr;
    analyserOrder = analyser.fftOrder;
    for (auto* a : { &preAnalyser, &postAnalyser, &sidechainAnalyser })
    {
        a->configure (analyserOrder, sr);
        a->setReleaseSeconds (analyser.releaseSeconds);
        a->setPeakHold (analyser.peakHold);
    }
}

bool EQGraph::updateAnalyser (double elapsedSeconds)
{
    configureAnalysers();
    bool fresh = false;
    if (fifoScratch.size() < 8192)
        fifoScratch.resize (8192);

    auto drain = [this] (SampleFifo& fifo, SpectrumAnalyser& a, bool use)
    {
        for (;;)
        {
            const size_t n = fifo.pop (fifoScratch.data(), fifoScratch.size());
            if (n == 0)
                break;
            if (use)
                a.addSamples (fifoScratch.data(), (int) n);
        }
    };
    drain (processor.getPreFifo(), preAnalyser, analyser.showPre());
    drain (processor.getPostFifo(), postAnalyser, analyser.showPost());
    const bool sc = analyser.enabled() && analyser.showSidechain;
    drain (processor.getSidechainFifo(), sidechainAnalyser, sc);

    if (analyser.showPre())  fresh |= preAnalyser.computeIfReady (elapsedSeconds);
    if (analyser.showPost()) fresh |= postAnalyser.computeIfReady (elapsedSeconds);
    if (sc && sidechainAnalyser.computeIfReady (elapsedSeconds))
    {
        ++sidechainFrames;
        fresh = true;
    }
    return fresh;
}

void EQGraph::timerCallback()
{
    if (analyser.enabled())
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const double dt = lastAnalyserTime > 0.0 ? juce::jlimit (0.0, 0.5, now - lastAnalyserTime) : 0.0;
        lastAnalyserTime = now;
        updateAnalyser (dt);
        repaint();
    }

    // Polling is simpler and safer than parameter listeners here: host
    // automation arrives on the audio thread, and 30 checks a second of ~200
    // floats costs nothing. The curves are only recomputed when a value moved.
    pollParameters();
}

bool EQGraph::pollParameters()
{
    if (! refreshFromParameters (false))
        return false;
    if (selected >= 0 && ! bands[(size_t) selected].isUsed())
        setSelectedBand (-1);
    repaint();
    return true;
}

bool EQGraph::refreshFromParameters (bool force)
{
    std::vector<float> values;
    values.reserve ((size_t) FilterEngine::numBands * 11);
    liveDynGain.resize ((size_t) FilterEngine::numBands, 0.0f);
    for (int b = 0; b < FilterEngine::numBands; ++b)
    {
        const auto& r = bands[(size_t) b];
        for (auto* p : { r.used, r.on, r.type, r.freq, r.gain, r.q, r.slope, r.mode, r.dyn, r.range })
            values.push_back (p->getValue());
        if (b == 0)
        {
            values.push_back (analogMatchParam != nullptr ? analogMatchParam->getValue() : 0.0f);
            values.push_back (gainScaleParam != nullptr ? gainScaleParam->getValue() : 0.5f);
        }

        // Live dynamic gain, in 0.1 dB steps so the curves are only redrawn
        // when the change is visible.
        const float live = r.dynamicsActive() && r.isEnabled() ? processor.getDynamicGainDb (b) : 0.0f;
        liveDynGain[(size_t) b] = live;
        values.push_back (std::round (live * 10.0f));
    }

    const double sr = processor.getCurrentSampleRateOrDefault();
    if (! force && values == lastValues && juce::exactlyEqual (sr, lastSampleRate))
        return false;

    lastValues = std::move (values);
    lastSampleRate = sr;
    recomputeCurves();
    return true;
}

void EQGraph::recomputeCurves()
{
    const auto plot = plotArea();
    curveWidth = juce::jmax (1, (int) std::ceil (plot.getWidth()));
    curveSampleRate = processor.getCurrentSampleRateOrDefault();

    totalCurve.assign ((size_t) curveWidth, 0.0f);
    liveCurves.resize (bandCurves.size());
    rangeCurves.resize (bandCurves.size());
    liveDynGain.resize ((size_t) FilterEngine::numBands, 0.0f);

    auto compute = [this] (const BandParameters& params, std::vector<float>& out)
    {
        CascadeCoefficients c;
        auto p = params;
        p.analogMatched = analogMatchParam != nullptr && analogMatchParam->getValue() >= 0.5f;
        FilterDesign::design (p, curveSampleRate, c);
        out.resize ((size_t) curveWidth);
        for (int x = 0; x < curveWidth; ++x)
        {
            const double hz = graph::xToFreq ((double) x, (double) curveWidth);
            out[(size_t) x] = hz < 0.5 * curveSampleRate ? (float) c.magnitudeDb (hz, curveSampleRate)
                                                         : out[(size_t) juce::jmax (0, x - 1)];
        }
    };

    for (int b = 0; b < FilterEngine::numBands; ++b)
    {
        auto& curve = bandCurves[(size_t) b];
        auto& live = liveCurves[(size_t) b];
        auto& reach = rangeCurves[(size_t) b];
        const auto& r = bands[(size_t) b];
        live.clear();
        reach.clear();
        if (! r.isUsed())
        {
            curve.clear();
            continue;
        }

        auto params = r.toBandParameters();
        params.gainDb *= gainScaleFactor();
        compute (params, curve);

        const float dynGain = liveDynGain[(size_t) b];
        if (r.dynamicsActive())
        {
            auto end = params;
            end.gainDb = juce::jlimit (limits::minGainDb, limits::maxGainDb,
                                       params.gainDb + (double) BandParamRefs::get (r.range));
            compute (end, reach);

            if (std::abs (dynGain) >= 0.05f)
            {
                auto now = params;
                now.gainDb = juce::jlimit (limits::minGainDb, limits::maxGainDb, params.gainDb + (double) dynGain);
                compute (now, live);
            }
        }

        // The combined curve shows what is applied right now.
        if (r.isEnabled())
        {
            const auto& heard = live.empty() ? curve : live;
            for (int x = 0; x < curveWidth; ++x)
                totalCurve[(size_t) x] += heard[(size_t) x];
        }
    }
}

//==============================================================================
void EQGraph::resized()
{
    recomputeCurves();
}

void EQGraph::setMatchCurve (std::vector<double> frequencies, std::vector<double> db)
{
    if (frequencies.size() != db.size())
        frequencies.clear();
    if (frequencies.empty())
        db.clear();
    if (frequencies == matchFreqs && db == matchDb)
        return;
    matchFreqs = std::move (frequencies);
    matchDb = std::move (db);
    repaint();
}

void EQGraph::paint (juce::Graphics& g)
{
    g.fillAll (theme::graphBg);
    const auto plot = plotArea();
    drawGrid (g, plot);
    if (analyser.enabled())
        drawSpectrum (g, plot);
    drawCurves (g, plot);
    if (! matchFreqs.empty())
    {
        juce::Path line;
        for (size_t i = 0; i < matchFreqs.size(); ++i)
        {
            const juce::Point<float> pt (xForFreq (matchFreqs[i]), yForDb (matchDb[i]));
            if (i == 0) line.startNewSubPath (pt);
            else        line.lineTo (pt);
        }
        juce::Path dashed;
        const float dashes[] = { 7.0f, 4.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (theme::warning.withAlpha (0.9f));
        g.fillPath (dashed);
    }
    drawNodes (g);

    const int readoutBand = dragBand >= 0 ? dragBand : (hovered >= 0 ? hovered : selected);
    if (readoutBand >= 0 && bands[(size_t) readoutBand].isUsed())
        drawReadout (g, readoutBand);

    if (cursor.x >= 0.0f && dragBand < 0)
    {
        g.setColour (theme::textDim.brighter (0.2f));
        g.setFont (theme::font (12.0f));
        g.drawText (cursorReadout (cursor), plot.reduced (10.0f, 6.0f), juce::Justification::bottomLeft);
    }

    if (processor.getSoloBand() >= 0)
    {
        g.setColour (theme::warning);
        g.setFont (theme::font (12.0f, true));
        g.drawText ("SOLO: band " + juce::String (processor.getSoloBand() + 1),
                    plot.reduced (10.0f, 6.0f), juce::Justification::topLeft);
    }
    if (processor.getDeltaListen())
    {
        g.setColour (theme::warning);
        g.setFont (theme::font (12.0f, true));
        g.drawText ("DELTA: hearing only what the EQ changes",
                    plot.reduced (10.0f, 6.0f).withTrimmedTop (processor.getSoloBand() >= 0 ? 16.0f : 0.0f),
                    juce::Justification::topLeft);
    }

    bool anyUsed = false;
    for (const auto& r : bands)
        anyUsed = anyUsed || r.isUsed();
    if (! anyUsed)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (15.0f));
        g.drawText ("Click anywhere on the graph to add a band",
                    plot.withTrimmedTop (plot.getHeight() * 0.25f).removeFromTop (30.0f),
                    juce::Justification::centred);
    }
}

void EQGraph::drawGrid (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    // Frequency lines: every 1-2-3...9 x decade, labels at musically useful points.
    g.setFont (theme::font (11.0f));
    for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
    {
        for (int m = 1; m <= 9; ++m)
        {
            const double hz = decade * m;
            if (hz < graph::minDisplayHz || hz > graph::maxDisplayHz)
                continue;
            const float x = xForFreq (hz);
            g.setColour (m == 1 ? theme::gridMajor : theme::gridMinor);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }
    }

    g.setColour (theme::gridText);
    for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = xForFreq (hz);
        const juce::String label = hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, bottomMargin - 4.0f),
                    juce::Justification::centred);
    }

    // dB lines.
    const double step = rangeDb <= 6.0 ? 2.0 : rangeDb <= 12.0 ? 3.0 : rangeDb <= 18.0 ? 6.0 : 10.0;
    for (double db = -rangeDb; db <= rangeDb + 1.0e-9; db += step)
    {
        const float y = yForDb (db);
        g.setColour (std::abs (db) < 1.0e-9 ? theme::gridMajor.brighter (0.25f) : theme::gridMajor);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (theme::gridText);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db),
                    juce::Rectangle<float> (plot.getRight() + 2.0f, y - 8.0f, rightMargin - 6.0f, 16.0f),
                    juce::Justification::centredRight);
    }

    // Above Nyquist the filters do not exist: shade it.
    const double nyquist = 0.5 * processor.getCurrentSampleRateOrDefault();
    if (nyquist < graph::maxDisplayHz)
    {
        const float x = xForFreq (nyquist);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x, plot.getY(), plot.getRight() - x, plot.getHeight()));
    }
}

void EQGraph::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    const double nyquist = 0.5 * preAnalyser.sampleRate();
    const float lastX = juce::jmin (plot.getRight(), xForFreq (nyquist));
    const double binWidth = preAnalyser.sampleRate() / (double) preAnalyser.fftSize();
    const float alpha = juce::jlimit (0.0f, 1.0f, analyser.opacity);

    auto yForLevel = [&] (double dbfs)
    {
        const double t = (0.0 - dbfs) / (0.0 - analyserFloorDb);
        return juce::jlimit (plot.getY(), plot.getBottom(), plot.getY() + (float) t * plot.getHeight());
    };

    auto buildPath = [&] (const std::vector<double>& bins, bool closed)
    {
        std::vector<double> prefix;
        SpectrumAnalyser::buildPowerPrefix (bins, prefix);
        juce::Path p;
        bool started = false;
        for (float x = plot.getX(); x <= lastX; x += 2.0f)
        {
            const double hz = graph::xToFreq ((double) (x - plot.getX()), (double) plot.getWidth());
            const double level = SpectrumAnalyser::levelAtPrefix (prefix, hz, binWidth, analyser.smoothingOctaves)
                                 + analyser.tiltDbPerOctave * std::log2 (hz / 1000.0);
            const float y = yForLevel (level);
            if (! started)
            {
                if (closed) { p.startNewSubPath (x, plot.getBottom()); p.lineTo (x, y); }
                else        p.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                p.lineTo (x, y);
            }
        }
        if (closed && started)
        {
            p.lineTo (lastX, plot.getBottom());
            p.closeSubPath();
        }
        return p;
    };

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    if (analyser.showPost())
    {
        auto fill = buildPath (postAnalyser.levelsDb(), true);
        const auto colour = juce::Colour (0xff3d7fa6);
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.55f * alpha), 0.0f, plot.getY(),
                                                 colour.withAlpha (0.12f * alpha), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);
        g.setColour (colour.brighter (0.4f).withAlpha (0.7f * alpha));
        g.strokePath (buildPath (postAnalyser.levelsDb(), false), juce::PathStrokeType (1.0f));
        if (analyser.peakHold)
        {
            g.setColour (colour.brighter (0.8f).withAlpha (0.6f * alpha));
            g.strokePath (buildPath (postAnalyser.peaksDb(), false), juce::PathStrokeType (0.8f));
        }
    }

    if (analyser.showPre())
    {
        g.setColour (juce::Colour (0xff8a93a3).withAlpha (0.55f * alpha));
        g.strokePath (buildPath (preAnalyser.levelsDb(), false), juce::PathStrokeType (1.0f));
        if (analyser.peakHold && ! analyser.showPost())
        {
            g.setColour (juce::Colour (0xffb0b8c6).withAlpha (0.5f * alpha));
            g.strokePath (buildPath (preAnalyser.peaksDb(), false), juce::PathStrokeType (0.8f));
        }
    }

    if (analyser.showSidechain && sidechainFrames > 0)
    {
        // The side-chain (another track): an amber line, so overlaps with
        // this track's spectrum stand out.
        g.setColour (theme::warning.withAlpha (0.85f * alpha));
        g.strokePath (buildPath (sidechainAnalyser.levelsDb(), false), juce::PathStrokeType (1.4f));
        g.setFont (theme::font (11.0f, true));
        g.drawText ("SIDE-CHAIN", plot.reduced (12.0f, 6.0f).withTrimmedRight (40.0f), juce::Justification::topRight);
    }

    // dBFS scale for the analyser, small, on the left edge.
    g.setFont (theme::font (10.0f));
    g.setColour (theme::gridText.withAlpha (0.8f));
    for (double db = -30.0; db > analyserFloorDb; db -= 30.0)
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() + 3.0f, yForLevel (db) - 7.0f, 30.0f, 14.0f),
                    juce::Justification::centredLeft);

    g.restoreState();
}

void EQGraph::drawCurves (juce::Graphics& g, juce::Rectangle<float> plot) const
{
    if (curveWidth <= 1)
        return;

    const float zeroY = yForDb (0.0);
    const float nyquistX = xForFreq (0.5 * curveSampleRate);
    const int lastX = juce::jmin (curveWidth - 1, (int) (nyquistX - plot.getX()));

    auto yFor = [&] (float db) { return juce::jlimit (plot.getY() - 2.0f, plot.getBottom() + 2.0f, yForDb ((double) db)); };

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    for (int b = 0; b < FilterEngine::numBands; ++b)
    {
        const auto& curve = bandCurves[(size_t) b];
        if (curve.empty())
            continue;
        const auto& r = bands[(size_t) b];
        const bool active = r.isEnabled();
        const bool isSelected = b == selected;
        const auto colour = active ? theme::bandColour (b) : theme::textDim;

        juce::Path line, fill;
        for (int x = 0; x <= lastX; ++x)
        {
            const float px = plot.getX() + (float) x;
            const float py = yFor (curve[(size_t) x]);
            if (x == 0)
            {
                line.startNewSubPath (px, py);
                fill.startNewSubPath (px, zeroY);
                fill.lineTo (px, py);
            }
            else
            {
                line.lineTo (px, py);
                fill.lineTo (px, py);
            }
        }
        fill.lineTo (plot.getX() + (float) lastX, zeroY);
        fill.closeSubPath();

        if (active)
        {
            g.setColour (colour.withAlpha (isSelected ? 0.22f : 0.09f));
            g.fillPath (fill);
        }

        g.setColour (colour.withAlpha (isSelected ? 0.95f : 0.55f));
        const bool dashed = ! active || r.channelMode() != ChannelMode::Stereo;
        if (dashed)
        {
            juce::Path dashedLine;
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.3f).createDashedStroke (dashedLine, line, dashes, 2);
            g.fillPath (dashedLine);
        }
        else
        {
            g.strokePath (line, juce::PathStrokeType (isSelected ? 1.8f : 1.2f));
        }

        // Dynamic band: where the range can take it (dotted), and where the
        // dynamics have it right now (bright, with the area between shaded).
        auto pathOf = [&] (const std::vector<float>& c)
        {
            juce::Path p;
            for (int x = 0; x <= lastX; ++x)
            {
                const float px = plot.getX() + (float) x, py = yFor (c[(size_t) x]);
                if (x == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            return p;
        };

        const auto& reach = rangeCurves.size() > (size_t) b ? rangeCurves[(size_t) b] : curve;
        if (active && &reach != &curve && ! reach.empty())
        {
            juce::Path dotted;
            const float dots[] = { 1.5f, 3.5f };
            juce::PathStrokeType (1.2f).createDashedStroke (dotted, pathOf (reach), dots, 2);
            g.setColour (colour.withAlpha (isSelected ? 0.75f : 0.4f));
            g.fillPath (dotted);
        }

        const auto& live = liveCurves.size() > (size_t) b ? liveCurves[(size_t) b] : curve;
        if (active && &live != &curve && ! live.empty())
        {
            juce::Path between;
            for (int x = 0; x <= lastX; ++x)
            {
                const float px = plot.getX() + (float) x;
                if (x == 0) between.startNewSubPath (px, yFor (curve[0]));
                else        between.lineTo (px, yFor (curve[(size_t) x]));
            }
            for (int x = lastX; x >= 0; --x)
                between.lineTo (plot.getX() + (float) x, yFor (live[(size_t) x]));
            between.closeSubPath();
            g.setColour (colour.withAlpha (0.28f));
            g.fillPath (between);
            g.setColour (colour.brighter (0.4f));
            g.strokePath (pathOf (live), juce::PathStrokeType (1.6f));
        }
    }

    // Combined response.
    juce::Path total;
    for (int x = 0; x <= lastX; ++x)
    {
        const float px = plot.getX() + (float) x;
        const float py = yFor (totalCurve[(size_t) x]);
        if (x == 0) total.startNewSubPath (px, py);
        else        total.lineTo (px, py);
    }
    g.setColour (theme::curve.withAlpha (0.25f));
    g.strokePath (total, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (theme::curve);
    g.strokePath (total, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.restoreState();
}

void EQGraph::drawNodes (juce::Graphics& g) const
{
    const int solo = processor.getSoloBand();

    for (int pass = 0; pass < 2; ++pass)  // selected band drawn last, on top
    {
        for (int b = 0; b < FilterEngine::numBands; ++b)
        {
            const auto& r = bands[(size_t) b];
            if (! r.isUsed() || ((b == selected) != (pass == 1)))
                continue;

            const auto pos = nodePosition (b);
            const bool active = r.isEnabled();
            const bool isSelected = b == selected;
            const bool isHot = b == hovered || b == dragBand;
            const float radius = nodeRadius + (isHot ? 1.5f : 0.0f);
            const auto colour = active ? theme::bandColour (b) : theme::textDim;

            if (isSelected || b == solo)
            {
                g.setColour ((b == solo ? theme::warning : theme::curve).withAlpha (0.9f));
                g.drawEllipse (juce::Rectangle<float> (radius * 2.0f + 6.0f, radius * 2.0f + 6.0f).withCentre (pos), 1.6f);
            }

            if (r.dynamicsActive() && active)
            {
                // Range bar from the node to where the range ends, and a
                // marker at the gain the dynamics apply right now.
                const double gain = (double) BandParamRefs::get (r.gain);
                const float yEnd = juce::jlimit (plotArea().getY(), plotArea().getBottom(),
                                                 yForDb (gain + (double) BandParamRefs::get (r.range)));
                g.setColour (colour.withAlpha (0.35f));
                g.fillRoundedRectangle (juce::Rectangle<float> (pos.x - 2.5f, juce::jmin (pos.y, yEnd), 5.0f, std::abs (yEnd - pos.y)), 2.5f);

                const float live = (size_t) b < liveDynGain.size() ? liveDynGain[(size_t) b] : 0.0f;
                const float yLive = juce::jlimit (plotArea().getY(), plotArea().getBottom(), yForDb (gain + (double) live));
                g.setColour (colour.brighter (0.5f));
                g.fillRoundedRectangle (juce::Rectangle<float> (pos.x - 7.0f, yLive - 1.5f, 14.0f, 3.0f), 1.5f);
            }

            g.setColour (colour.withAlpha (active ? 1.0f : 0.6f));
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (pos));

            g.setColour (theme::graphBg);
            g.setFont (theme::font (10.0f, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (pos),
                        juce::Justification::centred);

            const juce::String tag = modeTag (r.channelMode());
            if (tag.isNotEmpty())
            {
                g.setColour (colour);
                g.setFont (theme::font (10.0f, true));
                g.drawText (tag, juce::Rectangle<float> (14.0f, 12.0f).withCentre (pos.translated (radius + 6.0f, -radius - 4.0f)),
                            juce::Justification::centred);
            }
        }
    }
}

void EQGraph::drawReadout (juce::Graphics& g, int band) const
{
    const auto& r = bands[(size_t) band];
    const auto type = r.filterType();

    juce::String line1 = "Band " + juce::String (band + 1) + "  " + typeNames()[(int) type];
    if (r.channelMode() != ChannelMode::Stereo)
        line1 << "  (" << modeNames()[(int) r.channelMode()] << ")";
    if (! r.isEnabled())
        line1 << "  [off]";

    juce::String line2 = formatFrequency ((double) BandParamRefs::get (r.freq));
    if (typeHasGain (type))
        line2 << "   " << formatGain ((double) BandParamRefs::get (r.gain));
    line2 << "   Q " << juce::String ((double) BandParamRefs::get (r.q), 2);
    if (typeHasSlope (type))
        line2 << "   " << juce::String (r.slopeDbPerOct()) << " dB/oct";
    if (r.dynamicsActive())
        line1 << "  Dyn " << formatGain ((double) BandParamRefs::get (r.range))
              << " @ " << juce::String ((double) BandParamRefs::get (r.thr), 1) << " dB"
              << (BandParamRefs::get (r.sc) >= 0.5f ? "  (SC)" : "");

    const auto f1 = theme::font (12.0f, true), f2 = theme::font (12.0f);
    const float w = juce::jmax (juce::GlyphArrangement::getStringWidth (f1, line1),
                                juce::GlyphArrangement::getStringWidth (f2, line2)) + 16.0f;
    const float h = 38.0f;

    const auto plot = plotArea();
    const auto pos = nodePosition (band);
    auto box = juce::Rectangle<float> (w, h).withCentre (pos).translated (0.0f, -(h * 0.5f + nodeRadius + 10.0f));
    if (box.getY() < plot.getY() + 2.0f)
        box.setY (pos.y + nodeRadius + 10.0f);
    box = box.constrainedWithin (plot.reduced (2.0f));

    g.setColour (theme::panel.withAlpha (0.92f));
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (theme::bandColour (band).withAlpha (0.8f));
    g.drawRoundedRectangle (box, 5.0f, 1.0f);

    g.setColour (theme::text);
    g.setFont (f1);
    g.drawText (line1, box.withTrimmedLeft (8.0f).withHeight (h * 0.5f).translated (0.0f, 2.0f), juce::Justification::centredLeft);
    g.setFont (f2);
    g.setColour (theme::textDim.brighter (0.3f));
    g.drawText (line2, box.withTrimmedLeft (8.0f).withTrimmedTop (h * 0.5f).translated (0.0f, -2.0f), juce::Justification::centredLeft);
}

//==============================================================================
void EQGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = bandAt (e.position);
    const auto newCursor = (h < 0 && plotArea().contains (e.position)) ? e.position : juce::Point<float> { -1.0f, -1.0f };
    if (newCursor != cursor)
    {
        cursor = newCursor;
        repaint();
    }
    if (h != hovered)
    {
        hovered = h;
        repaint();
    }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
}

void EQGraph::mouseExit (const juce::MouseEvent&)
{
    if (cursor.x >= 0.0f)
    {
        cursor = { -1.0f, -1.0f };
        repaint();
    }
    if (hovered >= 0)
    {
        hovered = -1;
        repaint();
    }
}

void EQGraph::beginDrag (int band, juce::Point<float> mousePos, bool adjustQ)
{
    endDrag();
    auto& r = bands[(size_t) band];
    dragBand = band;
    dragStartMouse = mousePos;
    dragStartLogFreq = std::log ((double) BandParamRefs::get (r.freq));
    dragStartGain = (double) BandParamRefs::get (r.gain);
    dragStartLogQ = std::log ((double) BandParamRefs::get (r.q));
    dragMovesQ = adjustQ;                       // Alt-drag: up/down changes Q
    dragMovesGain = ! adjustQ && typeHasGain (r.filterType());

    processor.beginUndoStep (adjustQ ? "Change Q" : "Move band");
    r.freq->beginChangeGesture();
    if (dragMovesGain)
        r.gain->beginChangeGesture();
    if (dragMovesQ)
        r.q->beginChangeGesture();
}

void EQGraph::endDrag()
{
    if (dragBand < 0)
        return;
    auto& r = bands[(size_t) dragBand];
    r.freq->endChangeGesture();
    if (dragMovesGain)
        r.gain->endChangeGesture();
    if (dragMovesQ)
        r.q->endChangeGesture();
    dragBand = -1;
    processor.flushUndo();
}

void EQGraph::mouseDown (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);

    if (e.mods.isPopupMenu())
    {
        if (band >= 0)
        {
            setSelectedBand (band);
            showBandMenu (band);
        }
        return;
    }

    if (band >= 0)
    {
        setSelectedBand (band);
        beginDrag (band, e.position, e.mods.isAltDown());
        return;
    }

    if (plotArea().contains (e.position))
    {
        const int added = addBandAt (e.position);
        if (added >= 0)
            beginDrag (added, nodePosition (added));
    }
}

void EQGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;

    auto& r = bands[(size_t) dragBand];
    const auto plot = plotArea();
    const float sensitivity = e.mods.isShiftDown() ? 0.15f : 1.0f;
    const auto delta = (e.position - dragStartMouse) * sensitivity;

    // Horizontal: frequency, in the log domain (same feel across the range).
    const double logSpan = std::log (graph::maxDisplayHz / graph::minDisplayHz);
    const double newHz = std::exp (dragStartLogFreq + logSpan * (double) delta.x / (double) plot.getWidth());
    BandParamRefs::set (r.freq, (float) juce::jlimit (limits::minFrequency, limits::maxFrequency, newHz));

    // Vertical: gain (only for types that have one).
    if (dragMovesGain)
    {
        // The node is drawn at gain x Gain Scale, so follow the mouse in that
        // scaled space (with a floor so a tiny scale doesn't make it jumpy).
        const double dbPerPixel = 2.0 * rangeDb / (double) plot.getHeight() / std::max (gainScaleFactor(), 0.25);
        const double newDb = dragStartGain - dbPerPixel * (double) delta.y;
        BandParamRefs::set (r.gain, (float) juce::jlimit (limits::minGainDb, limits::maxGainDb, newDb));
    }
    if (dragMovesQ)
    {
        // Up = narrower. 100 pixels = a factor of 2.
        const double newQ = std::exp (dragStartLogQ - std::log (2.0) * (double) delta.y / 100.0);
        BandParamRefs::set (r.q, (float) juce::jlimit (limits::minQ, limits::maxQ, newQ));
    }

    refreshFromParameters (true);
    repaint();
}

void EQGraph::mouseUp (const juce::MouseEvent&)
{
    endDrag();
    repaint();
}

void EQGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);
    if (band >= 0 && ! e.mods.isPopupMenu())
    {
        endDrag();
        resetGain (band);
        repaint();
    }
}

void EQGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int band = bandAt (e.position);
    if (band < 0)
        band = selected;
    if (band < 0)
        return;

    const double steps = (double) (std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX)
                         * (wheel.isReversed ? -1.0 : 1.0) * 4.0;
    if (std::abs (steps) < 1.0e-6)
        return;
    adjustQ (band, e.mods.isShiftDown() ? steps * 0.25 : steps);
    repaint();
}

void EQGraph::showBandMenu (int band)
{
    auto& r = bands[(size_t) band];
    const auto type = r.filterType();
    const int slopeIndex = juce::roundToInt (BandParamRefs::get (r.slope));
    const auto mode = r.channelMode();

    enum { typeBase = 100, slopeBase = 200, modeBase = 300, enableId = 1, soloId = 2, resetId = 3, deleteId = 4, copyId = 5, pasteId = 6 };

    juce::PopupMenu typeMenu, slopeMenu, modeMenu;
    for (int i = 0; i < typeNames().size(); ++i)
        typeMenu.addItem (typeBase + i, typeNames()[i], true, i == (int) type);

    for (int i = 0; i < (int) limits::slopesDbPerOct.size(); ++i)
    {
        const int s = limits::slopesDbPerOct[(size_t) i];
        const bool shelf = type == FilterType::LowShelf || type == FilterType::HighShelf;
        const bool allowed = typeHasSlope (type) && (! shelf || s <= 12)
                             && ! (type == FilterType::BandPass && s > 48);
        slopeMenu.addItem (slopeBase + i, juce::String (s) + " dB/oct", allowed, i == slopeIndex);
    }

    for (int i = 0; i < modeNames().size(); ++i)
        modeMenu.addItem (modeBase + i, modeNames()[i], true, i == (int) mode);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Band " + juce::String (band + 1));
    menu.addSubMenu ("Type", typeMenu);
    menu.addSubMenu ("Slope", slopeMenu, typeHasSlope (type));
    menu.addSubMenu ("Channels", modeMenu);
    menu.addSeparator();
    menu.addItem (enableId, "Enabled", true, r.isEnabled());
    menu.addItem (soloId, "Solo (listen to this band)", true, processor.getSoloBand() == band);
    menu.addItem (resetId, "Reset gain", typeHasGain (type));
    menu.addSeparator();
    menu.addItem (copyId, "Copy band (Ctrl+C)");
    menu.addItem (pasteId, "Paste onto this band", clipboardHasBand());
    menu.addSeparator();
    menu.addItem (deleteId, "Delete band");

    juce::Component::SafePointer<EQGraph> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe, band] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& self = *safe;
        auto& br = self.bands[(size_t) band];

        if (result >= typeBase && result < typeBase + 100)
        {
            self.processor.beginUndoStep ("Change type");
            BandParamRefs::setWithGesture (br.type, (float) (result - typeBase));
        }
        else if (result >= slopeBase && result < slopeBase + 100)
        {
            self.processor.beginUndoStep ("Change slope");
            BandParamRefs::setWithGesture (br.slope, (float) (result - slopeBase));
        }
        else if (result >= modeBase && result < modeBase + 100)
        {
            self.processor.beginUndoStep ("Change channels");
            BandParamRefs::setWithGesture (br.mode, (float) (result - modeBase));
        }
        else if (result == enableId)
        {
            self.processor.beginUndoStep ("Enable band");
            BandParamRefs::setWithGesture (br.on, br.isEnabled() ? 0.0f : 1.0f);
        }
        else if (result == soloId)   self.toggleSolo (band);
        else if (result == resetId)  self.resetGain (band);
        else if (result == deleteId) self.deleteBand (band);
        else if (result == copyId)   self.copyBand (band);
        else if (result == pasteId)  self.pasteBand (band);

        self.processor.flushUndo();
        self.refreshFromParameters (true);
        if (self.onSelectionChanged)
            self.onSelectionChanged (self.selected);
        self.repaint();
    });
}

} // namespace peq::gui
