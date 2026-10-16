# TRoss Seq

16-step MIDI drum sequencer for playing drum boxes, hardware and software, plug-ins or standalone apps (AU / VST3 / LV2) by Ticco Ross, 2026.

Thirteen drum tracks plus an accent row, 12 pattern slots, and each track sends its own MIDI note on its own channel,
so one TRoss Seq can play one drum box, or several drum boxes at once.

## Getting the notes to your drum machine

There are two outputs, and you can use either or both:

| Output | How it works | Use it when |
|---|---|---|
| **MIDI OUT PORT** | TRoss Seq sends straight to the MIDI port you pick (a USB/DIN MIDI interface, a drum box with USB MIDI, or a software drum box app) | Hardware drum boxes in every DAW, and standalone software drum boxes |
| **HOST OUT** | The notes go to the host's MIDI out, and you route them to the drum box plug-in's track | Software drum box plug-ins: Ableton Live (VST3), Logic 11+, Bitwig, Reaper, Cubase, Studio One... |

If both send to the same drum box, you'll hear every note twice. Turn **HOST OUT** off, or set
**MIDI OUT PORT** to **NONE**.

**Ableton Live:** use the VST3. Hardware: pick the interface under MIDI OUT PORT. Software plug-in: on a second MIDI track
set **MIDI From** to the TRoss Seq track, the chooser below to **TRoss Seq**, and Monitor to **In** (Live can't take MIDI out of AU plug-ins).

**Logic Pro (11+):** on the drum plug-in's track, Track Inspector → **Internal MIDI In** → Instrument Output → the TRoss Seq track, with Input Monitoring on.

**Bitwig Studio:** on the drum plug-in's track, set the note input to the TRoss Seq track (or use a Note Receiver device).

The manual's chapter 3 has the full steps.

**Windows:** most classic Windows MIDI drivers let only one program use a port at a time. If the port shows
an error, disable it in your DAW's MIDI settings so TRoss Seq can open it.

**PORT DELAY** shifts everything sent to the port from −250 to +250 ms, to line the drum box up with your
DAW's audio. Positive sends it later (start at about the size of your audio buffer); negative sends it
earlier, for a drum box that's late. HOST OUT always stays on the grid.

**CLOCK OUT** sends MIDI clock (24 per quarter), start / continue / stop and song position to the
port, so drum boxes with their own sequencers or tempo-synced effects follow along.

## Using it

| Control | What it does |
|---|---|
| VELOCITY / ACCENT | Note velocity for normal steps and for steps with the ACCENT row set |
| GATE | How long each note is held: 5–95 % of a 16th (most drum boxes only care about the note-on) |
| FLAM / FLAM LVL | How far before a flammed step the grace notes start (5–80 ms), and how loud they are (10–100 %). **Shift-click** a pad to add or remove a flam; **right-click** a pad for drag (2 grace notes), ruff (3), its own spacing and level, or to use it on the whole lane |
| HELP | Top right: switches the hover tips on or off (remembered for every copy) |
| SWING | 50 % = straight, 66.7 % = triplet feel, up to 75 %. Pushes every second 16th later |
| MIDI THRU | Passes your keyboard / pad controller through to the outputs, so you can play the drum box live |
| Track name strips | Click to play that drum now (Cmd/Ctrl-click = accent); the strip flashes only for your clicks. With REC on while the song plays, the hit is recorded. **Shift-click** to rename, Alt-click to lock, right-click for MIDI learn and to **clear the lane** (this pattern or all 12, steps or only flams) |
| NOTE / CH | Each track's MIDI note and channel: drag the red read-outs up or down. Note names use Ableton's naming (C1 = 36) |
| S | Click: solo. While any S is lit only the soloed tracks play (even muted ones); light several to hear them together |
| M | Click: mutes the track. **Shift + hold:** solos the track for as long as you hold the mouse button (it plays even if it's muted, every other track goes quiet). Let go and everything is back as it was; nothing gets toggled |
| LEARN | Click, then hit a key or pad: the track takes that note and channel |
| PRESETS | Drum map library: built-in maps (General MIDI; hardware: Roland TR-06, TR-08, TR-09, TR-8 / TR-8S, Korg volca beats; software: UVI Prime 8, AudioRealism ADM), your own saved maps (Save map as…, Save, Rename, Delete, Show maps folder: `Documents/TRoss Seq/Maps`), all lanes on one channel, and save as default. The panel shows the current map and marks it * EDITED after changes |
| Step pads | 14 rows (ACCENT + 13 tracks) × 16 steps, in groups of four. Click to toggle, or drag along a row |
| RUN | Sequencer on/off (MIDI THRU and the name strips still play) |
| LENGTH | Pattern length, 1–16 steps: drag the red read-out up/down |
| CLEAR | Clears the playing pattern (UNDO brings it back). Right-click the ACCENT label to clear the accent row |
| REC | While playing, incoming notes that match a track are written into the nearest step. Velocity above 100 also sets the accent |
| PATTERN 1–12 | While playing, a new slot waits until the current pattern reaches step 1. Right-click a slot to copy it to another slot or clear it |
| RANDOM | Writes a new pattern into the playing slot, lane by lane, the way each drum is usually played (kick on the beat, snare on the backbeat, busy hats, sparse toms and percussion). The ACCENT row is never changed. **Shift-click** for a small variation of the current pattern instead. While the song plays, the new pattern starts on step 1 (the RANDOM light blinks until then) |
| FILL | Randomises only the last four steps of the pattern, as a fill |
| UNDO | Goes back to the pattern before the last RANDOM / FILL (up to 32 steps back). While a result is still waiting for step 1, UNDO cancels it |
| DENSITY | How busy random patterns are |
| RANDOM STYLE | Straight, Electro, Broken, House, Techno, Detroit, Miami Bass, Breakbeat, Hip-Hop, Dub, Latin, Industrial. The randomiser never adds flams |
| Lane locks | **Alt-click** (Option-click on a Mac) a track name to lock it: a padlock shows and the randomiser leaves that lane alone. Right-click a name → **Randomise this lane** to roll just that one |
| EXPORT MIDI | Click to export the playing pattern, all patterns in a row, or all as separate `.mid` files (with each track's note and channel, swing and gate). Drag the key onto a MIDI track to drop the playing pattern in as a clip |

Incoming notes are matched to tracks by note and channel first, then by note alone, so a keyboard on
channel 1 can still record into tracks that send on channel 10.

The preset maps follow each maker's published default note assignments. Check them against your drum box's manual
or MIDI settings if anything doesn't trigger, and fix any track with LEARN.

Everything (patterns, track names, notes, channels, the MIDI port) is saved with your project. If the saved port
isn't connected when the project opens, the display says so, and TRoss Seq reconnects to a port with the same name
when you pick it again.


The randomiser recognises each lane by its name (BD, KICK, SNARE, CH, OPEN HAT, TOM, RIM, COWBELL...) and,
if the name doesn't say, by its General MIDI note. Rename a lane and it's treated as that drum.

The full manual is in `docs/TRoss Seq Manual.pdf`.

## Build on your Mac (macOS 12+)

1. **Compiler:** `xcode-select --install`
2. **CMake 3.22 or newer:** download from https://cmake.org/download/, then
   `sudo "/Applications/CMake.app/Contents/bin/cmake-gui" --install`
3. **Build** from inside this folder:

   ```sh
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
   cmake --build build --config Release -j8
   ```

   The plug-ins are copied to `~/Library/Audio/Plug-Ins/Components/TRoss Seq.component` and
   `~/Library/Audio/Plug-Ins/VST3/TRoss Seq.vst3`.
4. **Validate the AU (optional):** `auval -v aumu TSeq Tcro`

To build the installer, run `./tools/make_pkg.sh` after the build (signing and notarizing use
`DEV_ID_APP`, `DEV_ID_INSTALLER`, `NOTARY_PROFILE`, as for TRoss 606).

## Automatic builds (GitHub Actions)

Each push to `main` builds everything on macOS, Windows and Linux and runs the tests on Linux.
The files appear under the run's **Artifacts** on the **Actions** tab:

| Platform | Files |
|---|---|
| macOS | `TRossSeq-<v>-macOS.pkg` (AU + VST3), plus AU and VST3 zips and the manual |
| Windows | `TRossSeq-<v>-Windows-Setup.exe` (VST3), plus a VST3 zip and the manual |
| Linux | `TRossSeq-<v>-Linux-x86_64.tar.gz` (VST3 + LV2, with `install.sh`), and the manual |

**Publishing a release:** raise the version in `CMakeLists.txt`, commit, then `git tag v0.2.0 && git push --tags`.

## Tests

```sh
cmake -B build -DTSEQ_BUILD_TESTS=ON && cmake --build build --target TRossSeqTest
./build/TRossSeqTest_artefacts/Release/TRossSeqTest out
```

Covers note timing against the host transport, velocity and accent, gate, swing, pattern queueing, LENGTH,
recording, MIDI learn, mute, solo buttons, Shift-held solo, negative port delay (early port, host on the grid, loops), all 13 lanes, the randomiser (12 styles, density, locks, mutate, fill, undo), flams, the drum map library, thru / host out, loops, stop flushing, presets, MIDI export, state save/restore,
and an editor snapshot.
