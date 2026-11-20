# ParaEQ user manual

ParaEQ is a 24-band parametric EQ plugin (VST3, 64-bit Windows) made for
Cubase. It has a large interactive EQ graph, a real-time spectrum analyser,
dynamic EQ on any bell or shelf, left/right/mid/side processing per band,
presets, A/B compare and full automation.

Version 1.2 · Vendor "Home Studio" · Personal use.

---

## 1. Installing

### With the installer (recommended)

1. Close Cubase.
2. Download **ParaEQ-Setup-Windows-x64.exe** and run it. Windows may warn
   that the app is from an unknown publisher (the installer is not code-signed).
   Click **More info > Run anyway**.
3. Click through the installer. It puts:
   - the plugin in `C:\Program Files\Common Files\VST3\ParaEQ.vst3`
   - this manual in `C:\Program Files\Home Studio\ParaEQ`, with a Start menu
     shortcut.
4. Start Cubase. ParaEQ appears in the plugin list under **EQ**, vendor
   **Home Studio**. If it doesn't, open **Studio > VST Plug-in Manager** and
   click **Rescan**.

### By hand (zip file)

Unzip **ParaEQ-VST3-Windows-x64.zip** and copy the `ParaEQ.vst3` folder into
`C:\Program Files\Common Files\VST3\`. Then rescan in Cubase as above.

### Updating

Close Cubase and run the new installer (or copy the new `ParaEQ.vst3` over the
old one). Your projects and presets are kept. Projects saved with an older
ParaEQ open with the same sound.

### Uninstalling

**Windows Settings > Apps > ParaEQ > Uninstall**. Your own presets stay in
`%APPDATA%\Home Studio\ParaEQ\Presets`; delete that folder if you don't want
them.

---

## 2. The window

![ParaEQ window](screenshot-phase8.png)

From left to right along the top:

| Control | What it does |
|---|---|
| **Undo / Redo** | Steps back or forward through your changes (Ctrl+Z, Ctrl+Y). |
| **< Preset name >** | Click the name for the preset menu; the arrows step through presets. A **\*** after the name means you've changed something since loading it. |
| **A** / **B** | Switches between two complete settings to compare them. |
| **+/- 18 dB** | Vertical scale of the graph (6, 12, 18 or 30 dB). Display only. |
| **Analyser** | Spectrum analyser settings. |
| **Analog** | Analog-matched curves (see below). On by default. |
| Scale knob | **Gain Scale**: makes every band's boost or cut bigger or smaller together, 0 to 200 %. 100 % = as set, 0 % = flat. Double-click for 100 %. Saved in presets. |
| **IN / OUT** | Input and output gain, -24 to +24 dB. Double-click to reset. |
| Meter | Output peak level. Red light = went over 0 dBFS; click to reset. |
| **Bypass** | Turns the whole EQ off (click-free). |

The window can be resized from its bottom-right corner. Its size is saved with
the project.

### Analog-matched curves

A normal digital EQ "squeezes" bands near the top of the spectrum: at 44.1
or 48 kHz a wide bell at 12 kHz comes out narrower than it should, and a high
shelf can't keep rising past about 18 kHz. With **Analog** on (the default),
every band keeps the shape an analogue EQ would have, right up to 20 kHz. There
is no added latency and no extra CPU worth measuring.

Turn it off for the classic digital curves. Projects made with ParaEQ 1.0 open
with it off, so they sound exactly as they did. It is not part of a preset.

---

## 3. Working with bands

### Adding and moving

- **Click** an empty spot on the graph to add a band there. Near 20 Hz you
  get a low cut, near 20 kHz a high cut, anywhere else a bell.
- **Drag** a dot: left/right changes the frequency, up/down the gain. Hold
  **Shift** for fine moves. Hold **Alt** while you start the drag and up/down
  changes the Q instead (up = narrower).
- **Arrow keys** move the selected band: left/right one semitone, up/down
  0.5 dB (with Shift: a quarter semitone and 0.1 dB).
- Hover over an empty spot to see the frequency, the musical note and the
  level at the mouse (bottom left of the graph).
- **Mouse wheel** over a dot changes the Q (width). Higher Q = narrower.
- **Double-click** a dot to reset its gain to 0 dB.
- **Right-click** a dot for type, slope, channels, enable, solo, copy,
  paste and delete.
- **Delete** key removes the selected band.

### The band panel

The strip at the bottom shows every setting of the selected band:

- **Type**, **Slope**, **Channels** (see below).
- **Dynamic** switch and its four knobs (section 4).
- **Freq**, **Gain**, **Q** knobs.
- **On** (bypass just this band), **Solo**, **Delete**.

Double-click a knob to reset it. Drag up/down or left/right to turn it.

### Filter types

| Type | Use | Gain | Slope |
|---|---|---|---|
| Bell | Boost or cut around a frequency | yes | - |
| Low Shelf | Boost or cut everything below | yes | 6 or 12 dB/oct |
| High Shelf | Boost or cut everything above | yes | 6 or 12 dB/oct |
| High-pass (low cut) | Remove rumble and low end | - | 6 to 96 dB/oct |
| Low-pass (high cut) | Remove highs | - | 6 to 96 dB/oct |
| Notch | Remove one narrow frequency (hum, ring) | - | - |
| Band-pass | Keep only a region | - | 6 to 48 dB/oct |

### Channels (per band)

| Setting | The band works on |
|---|---|
| Stereo | Both channels together (normal) |
| Left / Right | One channel only |
| Mid | What's in the centre (vocals, kick, bass, snare) |
| Side | What's at the sides (width, reverb, wide guitars) |

Bands that aren't Stereo show a small letter (L, R, M, S) next to their dot
and a dashed curve. The white combined curve treats all bands as if they were
Stereo, so for L/R/M/S bands it's a guide, not exact.

### Solo

**Solo** (panel button or right-click) plays only the frequency region of
that band. It's a quick way to find a harsh or muddy area. Click again to
stop. Solo is a listening aid; it isn't saved or automated.

### Copy and paste

Right-click a dot and choose **Copy band**, or select it and press **Ctrl+C**.
**Ctrl+V** pastes it as a new band; right-click another dot and choose
**Paste onto this band** to replace it. This also works between two ParaEQs
in the same project.

(Ctrl+C / Ctrl+V only reach ParaEQ when its window has keyboard focus; click
the graph first. The right-click menu always works.)

---

## 4. Dynamic EQ

A dynamic band only acts when its frequency region gets loud. It works on
bells and shelves.

1. Select a bell or shelf and click **Dynamic** in the band panel.
2. **Threshold**: the level (in dBFS of that region) where it starts to act.
   The live bar under the Dynamic button shows the gain change as it happens;
   set the threshold so it moves on the loud parts only.
3. **Range**: the most the gain may move. Negative turns the band **down**
   when loud (taming harsh "s" sounds, a boomy note). Positive turns it **up**
   when loud.
4. **Attack**: how fast it reacts (0.1 to 200 ms).
   **Release**: how fast it lets go (5 ms to 2 s).

The band's normal **Gain** still applies: the dynamics move away from it. For
example, Gain +3 dB with Range -6 dB gives +3 dB on quiet parts and down to
-3 dB on loud parts. Gain Scale scales the Gain, not the Range.

On the graph a dynamic band shows:

- a bar from its dot to where its range ends,
- a dotted curve for the full range,
- live, the curve it is applying right now (shaded).

How it works: each dynamic band listens to its own frequency region (a
band-pass at the band's frequency and Q for a bell, low or high frequencies
for a shelf). L/R/M/S bands listen only to their own channel. Above the
threshold the gain moves 1 dB for every 2 dB the region goes over, with a
soft knee, up to the range.

---

## 5. Spectrum analyser

The analyser draws what's playing behind the EQ curves. The filled shape is
the **output** (after the EQ), the grey line the **input** (before it). The
scale on the left is dBFS (0 at the top, -90 at the bottom).

Settings (**Analyser** button):

| Setting | Choices |
|---|---|
| Show | Off, Post-EQ, Pre + Post, Pre-EQ |
| Resolution | 2048 to 16384 points. Higher = more detail in the lows, slower |
| Speed | How fast the display falls back |
| Smoothing | Off, 1/12, 1/6 or 1/3 octave |
| Tilt | 0, 3 or 4.5 dB/oct (makes typical music look flat; display only) |
| Opacity | 25 to 100 % |
| Peak hold | A thin line showing recent peaks |

The analyser only runs while the window is open.

---

## 6. Presets

Click the preset name for the menu:

- **Factory presets**: 14 starting points (vocals, drums, bass, guitar,
  master, effects). Load one and adjust it to your track.
- **My presets**: presets you saved.
- **Save** (overwrite the current user preset), **Save as...** (new name),
  **Rename...**, **Delete** (goes to the Recycle Bin).
- **Import...** / **Export...**: `.paraeq` files, to share or back up.
- **Show presets folder**: `%APPDATA%\Home Studio\ParaEQ\Presets`.

A preset holds all bands, dynamics and the IN/OUT gain. It doesn't change
Bypass, the window size or the analyser settings. Loading a preset can be
undone with Undo.

Cubase's own preset menu (at the top of the plugin window) also works; it saves
everything, including the window and analyser settings.

## 7. A/B compare

Click **A** to switch to **B**. The first time, B starts as a copy of A. Make
changes, then switch back and forth to compare. **Copy A to B** (or B to A)
is in the preset menu. Both settings are saved with the project.

---

## 8. Using ParaEQ in Cubase

- **Insert it** on any mono or stereo audio track, group, FX channel or the
  stereo out.
- **Automation**: every setting can be automated. Use **Write** (W) on the
  track and move a knob or a dot. The parameters are named "Band 1 Freq",
  "Band 1 Gain", and so on, grouped by band.
- **Latency**: zero. ParaEQ adds no delay, so it's safe for recording and
  live monitoring.
- **64-bit processing**: supported (Studio > Studio Setup > Audio System >
  Processing Precision). ParaEQ works in 64-bit inside either way.
- **Sample rates**: anything from 22.05 to 192 kHz. Bands above what the
  sample rate allows are limited automatically.
- **CPU**: very light. A fully loaded instance (24 bands, half of them
  dynamic) measured about 1.5 % of one CPU core on the test machine.
- **Projects** save all settings, the preset name, A/B, window size and
  analyser settings.

## 9. Keyboard shortcuts

| Keys | Action |
|---|---|
| Ctrl+Z | Undo |
| Ctrl+Y or Ctrl+Shift+Z | Redo |
| Delete / Backspace | Delete the selected band |
| Ctrl+C | Copy the selected band |
| Ctrl+V | Paste a copied band as a new band |
| Shift + drag | Fine adjustment |
| Alt + drag | Change Q (up = narrower) |
| Left / Right arrow | Selected band down / up a semitone (Shift: 1/4) |
| Up / Down arrow | Selected band +/- 0.5 dB (Shift: 0.1 dB) |

## 10. Troubleshooting

| Problem | Try this |
|---|---|
| ParaEQ isn't in the plugin list | Check `C:\Program Files\Common Files\VST3\ParaEQ.vst3` exists, then Studio > VST Plug-in Manager > Rescan. Make sure Cubase is 64-bit. |
| Cubase lists it as blocked | In the Plug-in Manager, select ParaEQ in the blocklist and click "Reactivate". |
| No sound change | Is **Bypass** on? Is the band's **On** button on? Is the band set to Side on a mono source? |
| The analyser is flat | It only shows while audio plays and the window is open. Check Analyser > Show isn't Off. |
| Ctrl+Z undoes something in Cubase instead | Click inside the ParaEQ window first, or use the Undo button. |
| Output meter shows red | The output went over 0 dBFS. Lower **OUT** or the boosts. |

## 11. Known limitations

- Automation is applied once per audio block and glided over about 20 ms, so
  it's smooth but not sample-accurate.
- Dynamic EQ has no look-ahead, so the very start of a sharp hit can pass
  before a band reacts. The ratio (1 dB per 2 dB) and knee are fixed.
- No external sidechain input.
- The combined curve is exact only for Stereo bands.
- Analog-matched curves are very close to analogue but not perfect for
  bands centred right next to the top of the spectrum (about 1 dB on an
  everyday bell or shelf, more for extreme settings).
- The installer and plugin are not code-signed, so Windows shows a warning
  the first time.
