#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>
#include <functional>

namespace peq::gui
{

/** Stereo output peak meter: two bars (left, right) from -60 to +6 dBFS,
    falling at 24 dB/s, with a peak-hold tick (1.5 s) and a clip light above
    0 dBFS that stays on until clicked. Polls a peak source at 30 Hz. */
class OutputMeter final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::Timer
{
public:
    /** source (channel) returns the linear peak since the last call. */
    explicit OutputMeter (std::function<float (int)> source)
        : takePeak (std::move (source))
    {
        setTooltip ("Output level (peak). Red = over 0 dBFS; click to reset.");
        startTimerHz (30);
    }

    static constexpr float minDb = -60.0f, maxDb = 6.0f;

    /** Current displayed level of a channel in dBFS (for tests). */
    float levelDb (int ch) const noexcept { return level[(size_t) ch]; }
    bool clipped() const noexcept { return clip; }

    /** Pulls the latest peaks (called by the timer; public for tests). */
    void update (float elapsedSeconds)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float peak = takePeak ? takePeak (ch) : 0.0f;
            const float db = peak > 0.0f ? juce::jlimit (-120.0f, 24.0f, 20.0f * std::log10 (peak)) : -120.0f;
            auto& l = level[(size_t) ch];
            l = db >= l ? db : juce::jmax (db, l - fallDbPerSecond * elapsedSeconds);

            auto& h = hold[(size_t) ch];
            auto& ht = holdTime[(size_t) ch];
            if (db >= h) { h = db; ht = 0.0f; }
            else if ((ht += elapsedSeconds) > holdSeconds) h = l;

            if (db > 0.0f)
                clip = true;
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto clipLight = r.removeFromTop (5.0f);
        r.removeFromTop (2.0f);

        g.setColour (clip ? juce::Colour (0xffff4d4d) : theme::graphBg);
        g.fillRoundedRectangle (clipLight, 1.5f);

        const float barW = (r.getWidth() - 2.0f) * 0.5f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (r.getX() + (float) ch * (barW + 2.0f), r.getY(), barW, r.getHeight());
            g.setColour (theme::graphBg);
            g.fillRect (bar);

            const float top = yFor (level[(size_t) ch], bar);
            const float zero = yFor (0.0f, bar);
            const float warn = yFor (-6.0f, bar);

            // Green up to -6 dBFS, amber to 0, red above.
            auto fillPart = [&] (float from, float to, juce::Colour c)
            {
                const float y1 = juce::jmax (top, to), y2 = from;
                if (y2 > y1)
                {
                    g.setColour (c);
                    g.fillRect (bar.getX(), y1, bar.getWidth(), y2 - y1);
                }
            };
            fillPart (bar.getBottom(), warn, juce::Colour (0xff3fcf7a));
            fillPart (warn, zero, juce::Colour (0xffe8c547));
            fillPart (zero, bar.getY(), juce::Colour (0xffff4d4d));

            const float hy = yFor (hold[(size_t) ch], bar);
            if (hold[(size_t) ch] > minDb)
            {
                g.setColour (theme::text);
                g.fillRect (bar.getX(), hy, bar.getWidth(), 1.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        clip = false;
        hold = { minDb - 1.0f, minDb - 1.0f };
        repaint();
    }

private:
    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float dt = last > 0.0 ? (float) juce::jlimit (0.0, 0.5, now - last) : 0.0f;
        last = now;
        update (dt);
    }

    static float yFor (float db, juce::Rectangle<float> bar) noexcept
    {
        const float t = (juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb);
        return bar.getBottom() - t * bar.getHeight();
    }

    static constexpr float fallDbPerSecond = 24.0f;
    static constexpr float holdSeconds = 1.5f;

    std::function<float (int)> takePeak;
    std::array<float, 2> level { -120.0f, -120.0f }, hold { -120.0f, -120.0f }, holdTime { 0.0f, 0.0f };
    bool clip = false;
    double last = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputMeter)
};

} // namespace peq::gui
