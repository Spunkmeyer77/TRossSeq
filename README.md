# TRoss Seq

16-step MIDI drum sequencer for playing hardware drum machines (AU / VST3 / LV2 / Standalone) by Ticco Ross, 2026.

Thirteen drum tracks plus an accent row, 12 pattern slots, and each track sends its own MIDI note on its own channel,
so one TRoss Seq can play one drum box, or several drum boxes at once.

## Getting the notes to your drum machine

There are two outputs, and you can use either or both:

| Output | How it works | Use it when |
|---|---|---|
| **MIDI OUT PORT** | TRoss Seq sends straight to the MIDI port you pick (a USB/DIN MIDI interface, or a drum box with USB MIDI) | Works in every host and in the standalone app. The easy route in Ableton Live and Logic |
| **HOST OUT** | The notes go to the host's MIDI out, and you route them to the drum machine there | Hosts that pass MIDI out of plug-ins (Bitwig, Reaper, Cubase, Studio One...) |

If both send to the same drum machine, you'll hear every note twice. Turn **HOST OUT** off, or set
**MIDI OUT PORT** to **NONE**.

**Ableton Live:** put TRoss Seq on a MIDI track and pick your interface under **MIDI OUT PORT**.
Live doesn't route MIDI coming out of AU/VST3 plug-ins, so the port output is the way to go there.

**Windows:** most classic Windows MIDI drivers let only one program use a port at a time. If the port shows
an error, disable it in your DAW's MIDI settings so TRoss Seq can open it.

**PORT DELAY** shifts everything sent to the port later (0–250 ms), to line the drum box up with your
DAW's audio. Start at about the size of your audio buffer and adjust by ear.

**CLOCK OUT** sends MIDI clock (24 per quarter), start / continue / stop and song position to the
port, so drum boxes with their own sequencers or tempo-synced effects follow along.

## Using it

| Control | What it does |
|---|---|
| VELOCITY / ACCENT | Note velocity for normal steps and for steps with the ACCENT row set |
| GATE | How long each note is held: 5–95 % of a 16th (most drum boxes only care about the note-on) |
| SWING | 50 % = straight, 66.7 % = triplet feel, up to 75 %. Pushes every second 16th later |
| MIDI THRU | Passes your keyboard / pad controller through to the outputs, so you can play the drum box live |
| Track name strips | Click to play that drum now (Shift-click = accent). With REC on while the song plays, the hit is recorded. Double-click to rename, right-click for MIDI learn |
| NOTE / CH | Each track's MIDI note and channel: drag the red read-outs up or down. Note names use Ableton's naming (C1 = 36) |
| M | Click: mutes the track. **Shift + hold:** solos the track for as long as you hold the mouse button (it plays even if it's muted, every other track goes quiet). Let go and everything is back as it was; nothing gets toggled |
| LEARN | Click, then hit a key or pad: the track takes that note and channel |
| PRESETS | Note maps for General MIDI drums, Roland TR-8 / TR-8S and Korg volca beats (the default GM lanes: BD SD LT HT CP CH OH CY RS MT RD CB CL); all tracks on one channel; and **Save map + MIDI port as default**, which new copies of TRoss Seq then start with |
| Step pads | 14 rows (ACCENT + 13 tracks) × 16 steps, in groups of four. Click to toggle, or drag along a row |
| RUN | Sequencer on/off (MIDI THRU and the name strips still play) |
| LENGTH | Pattern length, 1–16 steps: drag the red read-out up/down |
| CLEAR | Clears the playing pattern |
| REC | While playing, incoming notes that match a track are written into the nearest step. Velocity above 100 also sets the accent |
| PATTERN 1–12 | While playing, a new slot waits until the current pattern reaches step 1. Right-click a slot to copy it to another slot or clear it |
| EXPORT MIDI | Click to export the playing pattern, all patterns in a row, or all as separate `.mid` files (with each track's note and channel, swing and gate). Drag the key onto a MIDI track to drop the playing pattern in as a clip |

Incoming notes are matched to tracks by note and channel first, then by note alone, so a keyboard on
channel 1 can still record into tracks that send on channel 10.

The preset maps follow each maker's published default note assignments. Check them against your drum box's manual
or MIDI settings if anything doesn't trigger, and fix any track with LEARN.

Everything (patterns, track names, notes, channels, the MIDI port) is saved with your project. If the saved port
isn't connected when the project opens, the display says so, and TRoss Seq reconnects to a port with the same name
when you pick it again.

The **standalone app** has its own **PLAY** button and BPM read-out. Its Options → Audio/MIDI Settings
window has a MIDI output too, which is the HOST OUT route.

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
| macOS | `TRossSeq-<v>-macOS.pkg` (AU + VST3 + app), plus AU and VST3 zips |
| Windows | `TRossSeq-<v>-Windows-Setup.exe` (VST3 + app), plus a VST3 zip |
| Linux | `TRossSeq-<v>-Linux-x86_64.tar.gz` (VST3 + LV2 + app, with `install.sh`) |

**Publishing a release:** raise the version in `CMakeLists.txt`, commit, then `git tag v0.2.0 && git push --tags`.

## Tests

```sh
cmake -B build -DTSEQ_BUILD_TESTS=ON && cmake --build build --target TRossSeqTest
./build/TRossSeqTest_artefacts/Release/TRossSeqTest out
```

Covers note timing against the host transport, velocity and accent, gate, swing, pattern queueing, LENGTH,
recording, MIDI learn, mute, Shift-held solo, all 13 lanes, thru / host out, loops, stop flushing, presets, MIDI export, state save/restore,
and an editor snapshot.
