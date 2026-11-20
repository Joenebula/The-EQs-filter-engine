# Testing ParaEQ in Cubase

A short checklist to try the plugin in a real Cubase project. Each step says
what you should see. If something is different, write down the step number
and what happened (a screenshot helps).

## 0. Install

1. Download the build: https://github.com/Joenebula/The-EQs-filter-engine/releases/download/preview/ParaEQ-VST3-Windows-x64.zip
2. Close Cubase. Unzip, and copy the `ParaEQ.vst3` folder into
   `C:\Program Files\Common Files\VST3\` (replace the old one).
3. Start Cubase. If ParaEQ is missing, open **Studio > VST Plug-in Manager** and
   click **Rescan**. It is listed under **EQ**, vendor **Home Studio**.

## 1. Loading

1. Put ParaEQ on an audio track that has music on it (stereo).
   *Expected:* the window opens with an empty graph and the analyser moving
   while you play.
2. Put it on a mono track too.
   *Expected:* it loads and works the same way.
3. Put three or four copies on different tracks and play.
   *Expected:* no crackles, each copy works on its own.

## 2. Basic EQ

1. Click the graph at about 3 kHz to add a band, then drag the dot up and down.
   *Expected:* you hear the change straight away, with no clicks.
2. Turn the mouse wheel over the dot.
   *Expected:* the curve gets wider or narrower.
3. Right-click the dot, set **Channels** to **Side**.
   *Expected:* the change now only affects the stereo sides.
4. Press **Bypass**.
   *Expected:* the sound goes back to the original, with no click.

## 3. Dynamic EQ

1. Add a bell at a harsh frequency (for example 6 kHz on a vocal), leave its
   gain at 0 dB and click **Dynamic** in the panel at the bottom.
2. Set **Threshold** so the live bar under the Dynamic button moves when the
   loud parts play, and **Range** to about -6 dB.
   *Expected:* the band only cuts on the loud, harsh parts. On the graph you
   see the band's curve dip and come back as the music plays.

## 4. Automation

1. Turn on **Write** automation for the track, play, and move a band's gain
   knob for a few seconds. Stop, switch to **Read**, and play again.
   *Expected:* the knob and the dot move on their own, and you hear it.
2. Open the track's automation lane and choose a parameter such as
   "Band 1 Freq".
   *Expected:* all parameters are listed by band ("Band 1 Gain", etc.).

## 5. Saving and loading

1. Set up a few bands (one dynamic, one on Side), change the window size and
   the analyser settings, then save the project and close Cubase.
2. Open the project again.
   *Expected:* everything is exactly as you left it: bands, sound, window
   size, analyser settings.
3. Make a few changes, then click **Undo** in the plugin a few times, then
   **Redo**.
   *Expected:* your changes are undone one by one, then come back. (The undo
   list starts fresh each time a project is opened.)

## 6. Sample rate and export

1. Change the project sample rate (**Project > Project Setup**) from 48 kHz to
   44.1 kHz or 96 kHz and play.
   *Expected:* it still sounds the same, with no noise or silence.
2. Export the mix (**File > Export > Audio Mixdown**).
   *Expected:* the exported file sounds the same as playback.

## 7. 64-bit processing (optional)

**Studio > Studio Setup > Audio System > Processing Precision: 64-bit float.**
*Expected:* no change in sound. ParaEQ works in 64-bit internally either way.
