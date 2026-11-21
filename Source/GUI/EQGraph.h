#pragma once

#include "BandParams.h"
#include "../DSP/SpectrumAnalyser.h"
#include "../DSP/Resonances.h"
#include "../DSP/Clashes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

class ParaEQAudioProcessor;

namespace peq::gui
{

/** How the spectrum analyser behind the curves is shown. */
struct AnalyserSettings
{
    enum class Show { off = 0, post, preAndPost, pre };
    Show show = Show::preAndPost;
    int fftOrder = 13;              // 2^13 = 8192 points
    double releaseSeconds = 0.6;    // per 20 dB fall ("speed")
    bool peakHold = false;
    double smoothingOctaves = 1.0 / 6.0;
    float opacity = 0.75f;
    double tiltDbPerOctave = 4.5;   // pivot at 1 kHz; makes typical music look flat
    bool showSidechain = false;     // 1.8: also draw the side-chain input (another track)
    bool findResonances = false;    // 1.9: mark rings in the spectrum; click one to cut it
    bool showClashes = false;       // 2.0: shade ranges where the side-chain track masks this one
    int channel = 0;                // 2.1: 0 stereo (L+R), 1 left, 2 right, 3 side (L-R)

    bool showPre() const noexcept  { return show == Show::pre || show == Show::preAndPost; }
    bool showPost() const noexcept { return show == Show::post || show == Show::preAndPost; }
    bool enabled() const noexcept  { return show != Show::off; }
};

/** The interactive frequency-response graph.

    Draws a log-frequency / dB grid, each band's own curve (filled, in its
    colour), the combined curve and a draggable node per band. Dynamic bands
    also show how far their range reaches (faint dotted curve and a bar from
    the node) and, live, the curve they are applying right now.

    Mouse
    - Click empty space: add a band there (then keep dragging to place it).
    - Drag a node: horizontal = frequency, vertical = gain. Shift = fine.
    - Mouse wheel over a node (or anywhere, for the selected band): Q.
    - Double-click a node: reset its gain to 0 dB.
    - Right-click a node: type, slope, channels, enable, solo, delete.

    Every change goes through the host parameters with proper gestures, so it
    is automatable and undoable.

    The combined curve adds up all active bands as if they ran on the same
    signal. Left/right/mid/side bands only act on part of the signal, so
    their nodes carry a letter tag and their own curves are drawn dashed.
*/
class EQGraph final : public juce::Component,
                      private juce::Timer
{
public:
    EQGraph (ParaEQAudioProcessor& processor, AllBands& bands);
    ~EQGraph() override;

    /** -1 = none. */
    int getSelectedBand() const noexcept { return selected; }
    void setSelectedBand (int band);
    std::function<void (int)> onSelectionChanged;

    /** Vertical scale: +/- this many dB (6, 12, 18 or 30). */
    void setDisplayRange (double rangeDb);
    double getDisplayRange() const noexcept { return rangeDb; }

    // Actions used by the mouse handlers; public so they can be tested.
    /** Adds a band at a point in the plot; returns its index or -1 if all
        bands are in use. */
    int addBandAt (juce::Point<float> plotPosition);
    void deleteBand (int band);
    void resetGain (int band);
    void adjustQ (int band, double wheelSteps);
    /** Moves a band by a number of octaves and dB (arrow keys). One undo step
        per burst of presses. */
    void nudgeBand (int band, double octaves, double db);
    /** Gain Scale as a factor (1 = 100 %). */
    double gainScaleFactor() const noexcept;
    /** Text shown for the mouse position on an empty part of the graph,
        e.g. "1.00 kHz  C6 -14c  +3.0 dB". Public for tests. */
    juce::String cursorReadout (juce::Point<float> position) const;
    void toggleSolo (int band);

    /** Copies a band's settings to the system clipboard. */
    void copyBand (int band) const;
    /** Pastes copied settings onto a band, or into the first free band when
        target is -1. Returns the band pasted into, or -1. */
    int pasteBand (int target);
    static bool clipboardHasBand();

    /** Where a band's node is drawn, in this component's coordinates. */
    juce::Point<float> nodePosition (int band) const;

    juce::Rectangle<float> plotArea() const;

    void setAnalyserSettings (const AnalyserSettings&);
    const AnalyserSettings& getAnalyserSettings() const noexcept { return analyser; }

    /** Pulls audio from the processor and updates the analyser (called by the
        timer; public for tests). Returns true if a new spectrum was made. */
    bool updateAnalyser (double elapsedSeconds);

    /** Re-reads parameters and live dynamic gains; redraws if anything moved
        (called by the timer; public for tests). */
    bool pollParameters();

    /** Analyser display range: 0 dBFS at the top, this at the bottom. */
    static constexpr double analyserFloorDb = -90.0;

    /** EQ Match: the correction learned so far (dB at each frequency),
        drawn as a dashed line. Empty = not shown. */
    void setMatchCurve (std::vector<double> frequencies, std::vector<double> db);
    bool isShowingMatchCurve() const noexcept { return ! matchFreqs.empty(); }
    /** Resonance finder (1.9): the rings found in the long-term spectrum. */
    const std::vector<Resonance>& getResonances() const noexcept { return resonances; }
    /** Adds a narrow cut at resonance `index` (one undo step). Returns the band, or -1. */
    int addBandAtResonance (int index);
    /** The marker under a point, or -1. */
    int resonanceAt (juce::Point<float> position) const;
    juce::Point<float> resonanceMarkerPosition (int index) const;

    /** Clash view (2.0): ranges where the side-chain track is about as loud
        as this one, from the long-term spectra of both. */
    const std::vector<Clash>& getClashes() const noexcept { return clashes; }
    /** Adds a gentle, wide cut across clash `index` (one undo step). Returns the band, or -1. */
    int addBandAtClash (int index);
    /** The clash bar under a point, or -1. */
    int clashAt (juce::Point<float> position) const;
    /** The clickable bar along the bottom of the graph for clash `index`. */
    juce::Rectangle<float> clashBarArea (int index) const;

    /** Listen while dragging (2.2): while a node is dragged, solo that
        band; the previous solo comes back on release. Not saved by the graph:
        the editor stores it through onListenWhileDraggingChanged. */
    void setListenWhileDragging (bool on);
    bool getListenWhileDragging() const noexcept { return listenWhileDragging; }
    bool isAuditioning() const noexcept { return auditioning; }
    std::function<void (bool)> onListenWhileDraggingChanged;
    /** Starts / ends a node drag as the mouse would (tests). */
    void beginDragForTests (int band) { beginDrag (band, nodePosition (band)); }
    void endDragForTests() { endDrag(); }

    /** The output analyser, as drawn (tests). */
    const SpectrumAnalyser& getPostAnalyser() const noexcept { return postAnalyser; }

    /** True once the side-chain line has something to draw (tests). */
    bool hasSidechainSpectrum() const noexcept { return sidechainFrames > 0; }

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    std::vector<double> matchFreqs, matchDb;
    int sidechainFrames = 0;
    std::vector<double> longTermPower;           // per bin, ~2 s average (resonance finder)
    std::vector<Resonance> resonances;
    std::vector<double> resonanceLevels;         // dBFS at each resonance, for drawing
    double resonanceClock = 0.0;
    void updateResonances (const SpectrumAnalyser& source, double elapsedSeconds);
    std::vector<double> clashTrackPower, clashOtherPower;   // per bin, ~3 s averages (clash view)
    std::vector<Clash> clashes;
    double clashClock = 0.0;
    void updateClashes (const SpectrumAnalyser& track, double elapsedSeconds);
    bool bandCutsNear (double hz) const;   // an active cut within 1/3 octave
    /** Adds a static Stereo bell (one undo step) and selects it. Returns the band, or -1 if all are used. */
    int addCutBand (double hz, double gainDb, double q, const juce::String& undoName);
    double spectrumYForLevel (double dbfs, double hz) const;

    /** Re-reads the parameters; recomputes the curves only if anything moved. */
    bool refreshFromParameters (bool force);
    void recomputeCurves();

    int bandAt (juce::Point<float> position) const;
    void showBandMenu (int band);
    void beginDrag (int band, juce::Point<float> mousePos, bool adjustQ = false);
    void endDrag();

    void drawGrid (juce::Graphics&, juce::Rectangle<float> plot) const;
    void drawCurves (juce::Graphics&, juce::Rectangle<float> plot) const;
    void drawNodes (juce::Graphics&) const;
    void drawReadout (juce::Graphics&, int band) const;
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float> plot) const;
    void configureAnalysers();

    float xForFreq (double hz) const;
    float yForDb (double db) const;

    ParaEQAudioProcessor& processor;
    juce::RangedAudioParameter* analogMatchParam = nullptr;
    juce::RangedAudioParameter* gainScaleParam = nullptr;
    AllBands& bands;

    int selected = -1;
    int hovered = -1;
    juce::Point<float> cursor { -1.0f, -1.0f }; // mouse over empty graph, or (-1,-1)
    double rangeDb = 18.0;

    // Snapshot of the parameter values the curves were computed from.
    std::vector<float> lastValues;
    double lastSampleRate = 0.0;

    // Curves in dB, one value per pixel column of the plot.
    std::vector<std::vector<float>> bandCurves;
    // Dynamic bands: the curve at the gain the dynamics apply right now, and
    // at the end of their range (empty when not dynamic).
    std::vector<std::vector<float>> liveCurves, rangeCurves;
    std::vector<float> liveDynGain;   // dB, per band, as last read
    std::vector<float> totalCurve;
    int curveWidth = 0;
    double curveSampleRate = 48000.0;

    // Drag state.
    int dragBand = -1;
    bool listenWhileDragging = false, auditioning = false;
    int soloBeforeAudition = -1;
    juce::Point<float> dragStartMouse;
    double dragStartLogFreq = 0.0, dragStartGain = 0.0, dragStartLogQ = 0.0;
    bool dragMovesGain = false, dragMovesQ = false;
    juce::uint32 lastNudgeTime = 0;
    juce::uint32 lastWheelTime = 0;

    // Spectrum analyser.
    AnalyserSettings analyser;
    SpectrumAnalyser preAnalyser, postAnalyser, sidechainAnalyser;
    std::vector<float> fifoScratch;
    double analyserRate = 0.0;
    int analyserOrder = 0;
    double lastAnalyserTime = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQGraph)
};

} // namespace peq::gui
