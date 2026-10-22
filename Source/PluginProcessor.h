#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <climits>
#include "SeqCore.h"
#include "DirectMidiOut.h"
#include "Randomizer.h"

// TRoss Seq: the TRoss 606 step sequencer on its own, playing external drum machines over MIDI.
// Output goes to the host's MIDI out (HOST OUT) and/or straight to a MIDI port (MIDI OUT selector).
class TrossSeqProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    TrossSeqProcessor();
    ~TrossSeqProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // ---- pattern memory / track map, shared with the editor
    trossseq::PatternMemory patterns;
    trossseq::TrackMap map;
    int  getCurrentSlot() const { return currentSlot.load(); }   // the slot that plays and is edited
    int  getPendingSlot() const { return pendingSlot.load(); }   // queued slot, -1 = none

    // FILL pattern (TR-808 style): Shift + FILL arms it, then a pattern button picks the fill. It plays once, from the
    // next step 1, then the pattern (or chain) that was playing carries on.
    std::atomic<bool> fillArmed { false };
    void queueFill (int slot) { fillQueued = slot; fillArmed = false; }
    void cancelFill() { fillQueued = -1; fillArmed = false; }
    int  getFillQueued() const { return fillQueued.load(); }
    bool isFillPlaying() const { return fillPlaying.load(); }
    int  getPlayStep() const    { return playStep.load(); }      // step being played, -1 = stopped
    void clearCurrentPattern();                                  // CLEAR key (can be undone)
    // ---- pattern chain (message thread edits, audio thread plays). Locked to the song position:
    // the chain step = (song position in steps / LENGTH) mod chain length, so jumps and loops land right.
    static constexpr int kMaxChain = 32;
    void chainAppend (int slot);                 // Shift-click a pattern button
    void chainClear();
    // a plain click on a pattern button: queue it (playing) or switch at once (stopped)
    void selectSlot (int slot) { if (playStep.load() >= 0) pendingSlot = (slot == currentSlot.load()) ? -1 : slot; else { pendingSlot = -1; currentSlot = slot; } }
    void chainRemoveLast();
    int  getChainLength() const          { return chainLen.load(); }
    int  getChainSlot (int i) const      { return chain[i].load(); }
    int  getChainPos() const             { return chainPos.load(); }   // step of the chain that's playing
    bool isInChain (int slot) const      { for (int i = 0; i < chainLen.load(); ++i) if (chain[i].load() == slot) return true; return false; }
    std::vector<int> getChain() const    { std::vector<int> v; for (int i = 0; i < chainLen.load(); ++i) v.push_back (chain[i].load()); return v; }
    int  getChainVersion() const         { return chainVersion.load(); }

    // ---- clearing lanes (message thread, can be undone). row 0 = ACCENT, 1..13 = lanes
    void clearRow (int row, bool allPatterns);
    void clearRowFlams (int row, bool allPatterns);
    // apply one flam style to every hit of a lane in the playing pattern (adds flams to all its steps)
    void flamWholeRow (int row, uint8_t style);

    // track names (message thread; locked because hosts may save state from another thread)
    juce::String getTrackName (int t) const              { const juce::ScopedLock sl (nameLock); return names[t]; }
    void setTrackName (int t, const juce::String& n)     { { const juce::ScopedLock sl (nameLock); names.set (t, n.toUpperCase().substring (0, 10)); } ++nameVersion; }
    void applyPreset (int i);
    int  getNameVersion() const { return nameVersion.load(); }

    // ---- Shift + mouse held on a track's M button: only that track plays until the button is released
    std::atomic<int> momentarySolo { -1 };

    // ---- randomiser (message thread). While the song plays, the result waits for step 1;
    // while stopped it's written at once. The accent row is never touched.
    enum class RandomOp { NewPattern, Mutate, Fill, Lane };
    void randomise (RandomOp op, int track = -1);
    bool undoRandom();                 // back to the pattern before the last randomise; false = nothing to undo
    bool canUndo() const { return ! undoStack.empty(); }
    bool isRandomPending() const { return staged.load() >= 0; }
    std::atomic<bool> locked[trossseq::kNumTracks] {};   // Alt-click a lane name: the randomiser leaves it alone
    std::atomic<int> lockVersion { 0 };
    void toggleLock (int t) { locked[t] = ! locked[t].load(); ++lockVersion; }

    // ---- MIDI learn: the next note that comes in sets this track's note and channel
    std::atomic<int> learnTrack { -1 };

    // a click on a track's name: plays its note now (accent = Shift-click) and, with REC on while
    // playing, records it. Called from the UI thread; handled in the next block.
    void panelHit (int track, bool accent);

    // last time each track sent a note (Time::getMillisecondCounter), for the row lights
    std::atomic<juce::uint32> lastSent[trossseq::kNumTracks] {};

    // ---- MIDI port output (message thread)
    trossseq::DirectMidiOut directOut;
    bool selectPort (const juce::String& identifier);   // empty = none
    juce::String getPortIdentifier() const { return directOut.isOpen() ? directOut.device().identifier : juce::String(); }
    juce::String getPortName() const;
    juce::String getPortError() const { return directOut.getLastError(); }

    // the map + names + port remembered for new instances
    void saveAsDefault();

    // ---- drum map library (message thread): maps are small files in Documents/TRoss Seq/Maps
    static juce::File mapsFolder();
    static inline juce::File mapsFolderOverride;   // tests only
    static juce::StringArray listMaps();           // saved map names, sorted
    bool saveMap (const juce::String& name);       // current names / notes / channels / locks -> file
    bool loadMap (const juce::String& name);
    static bool renameMap (const juce::String& from, const juce::String& to);
    static bool deleteMap (const juce::String& name);
    static bool mapExists (const juce::String& name) { return mapFile (name).existsAsFile(); }
    juce::String getMapName() const { const juce::ScopedLock sl (nameLock); return mapName; }
    bool isMapModified() const;                    // changed since it was loaded / saved / picked
    bool isUserMap() const { return mapExists (getMapName()); }

    // Standalone app only: no host transport, so it gets its own play button and tempo
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
    std::atomic<bool>  internalPlay { false };
    std::atomic<float> internalBpm { 120.0f };

    std::atomic<double> currentBpm { 120.0 };   // the tempo seen in the last audio block

    // tests only: sees everything sent to the MIDI port, with its position in samples since the start
    std::function<void (const juce::MidiMessage&, juce::int64)> portSpy;

private:
    double sampleRate = 44100.0;
    std::atomic<int> currentSlot { 0 }, pendingSlot { -1 }, playStep { -1 };

    juce::CriticalSection nameLock;
    juce::StringArray names;
    std::atomic<int> nameVersion { 0 };

    // port the saved state asked for, opened on the message thread
    juce::CriticalSection wantedLock;
    juce::String wantedPortId, wantedPortName;
    void handleAsyncUpdate() override;

    // name clicks: single-producer / single-consumer queue (UI thread -> audio thread)
    static constexpr int kHitQueue = 64;
    struct PanelHit { int track = 0; bool accent = false; };
    PanelHit hitQueue[kHitQueue];
    std::atomic<int> hitWrite { 0 }, hitRead { 0 };
    int lastPatternParam = 0;

    // notes waiting for their note-off
    struct PendingOff { int channel = 0, note = 0; juce::int64 sample = 0; bool fromSeq = false; };
    static constexpr int kMaxPending = 128;
    PendingOff pending[kMaxPending];
    int numPending = 0;

    // transport following
    trossseq::StepClock clock;
    trossseq::StepClock graceClock;   // runs a flam's length ahead of `clock`, to play grace notes before their step
    bool wasPlaying = false;
    juce::int64 lastClockTick = 0;
    double internalPpq = 0.0;

    // time base for the port output: where this block starts, in Time::getMillisecondCounterHiRes
    double blockStartMs = 0.0;
    bool   haveBlockTime = false;

    // randomiser: a finished pattern waiting for step 1 (double buffered: UI writes one half, audio reads the other)
    struct Staged { int slot = 0; uint16_t rows[trossseq::kNumRows] {}; uint16_t clearFlams = 0; };   // clearFlams: bit r = drop row r's flams
    Staged stagedBuf[2];
    int stagedWrite = 0;                    // message thread
    std::atomic<int> staged { -1 };         // index of the buffer waiting, -1 = none
    void applyStaged();                     // audio thread
    struct SlotSnap { int slot = 0; uint16_t rows[trossseq::kNumRows] {}, flams[trossseq::kNumRows] {};
                      uint8_t styles[trossseq::kNumRows][trossseq::kNumSteps] {};
                      int16_t locks[trossseq::kNumTracks][trossseq::kNumSteps][trossseq::kNumLockParams] {}; };
    struct UndoEntry { std::vector<SlotSnap> slots; bool hasChain = false; std::vector<int> chain; };
    void pushChainUndo();
    void setChain (const std::vector<int>&);
    int chainSlotFor (long long absStep, int len) const;   // -1 = no chain
    std::atomic<int> chain[kMaxChain] {};
    std::atomic<int> chainLen { 0 }, chainPos { -1 }, chainVersion { 0 };
    std::vector<UndoEntry> undoStack;       // message thread
    void pushUndo (std::initializer_list<int> slots);
    void pushUndoAllSlots();
    SlotSnap snap (int slot) const;
    void restore (const SlotSnap&);
    trossseq::Randomizer rnd;

    // per-block output
    juce::MidiBuffer out;
    bool hostOutOn = true;
    float offsetMs = 0.0f;
    // negative PORT DELAY: the sequencer runs this far ahead of the song; the port gets its notes that
    // early, and the host's notes are held back by the same amount so they stay in time
    juce::int64 aheadSamples = 0;
    double aheadMsPort = 0.0;   // positive part of PORT DELAY (added to port times)
    struct Held { juce::uint8 bytes[3] {}; int size = 0; juce::int64 due = 0; };
    static constexpr int kMaxHeld = 1024;
    Held held[kMaxHeld];
    int numHeld = 0;
    // running ahead (negative PORT DELAY, and the flam look-ahead) over a loop: the steps already played
    // past the loop end are the ones the loop start brings back - skip them once
    long long lastFiredStep = LLONG_MIN, lastGraceStep = LLONG_MIN;
    int skipSteps = 0, skipGrace = 0;
    double lastQEnd = 0.0;          // song position (16ths) the last block ended at
    juce::int64 blockCounter = 0;   // samples since the plug-in started (tests)

    struct Raw
    {
        std::atomic<float>* mute[trossseq::kNumTracks];
        std::atomic<float>* solo[trossseq::kNumTracks];
        std::atomic<float>* ccBase[trossseq::kNumTracks][4];   // each lane's CC track values (Elektron style)
        std::atomic<float> *seqOn, *seqLen, *pattern, *rec, *velocity, *accentVel, *gate, *swing,
                           *thru, *hostOut, *clockOut, *offset, *density, *rstyle, *flamTime, *flamLevel, *stutter, *stutRate, *stutRoll;
    } raw {};

    void emit (const juce::MidiMessage& m, int sample, bool fromSeq = false, juce::int64 hostSample = -1);
    void noteOn (int track, int velocity, int sample, juce::int64 gateSamples, bool fromSeq = false, juce::int64 hostSample = -1, int pitch = 0);
    void flushNoteOffs (int sample);
    void doStep (int step, long long absStep, int len, int sample, bool seqOn, juce::int64 gateSamples, juce::int64 hostSample = -1);
    void doGrace (int step, long long absStep, int len, int sample, juce::int64 lookSamples);
    void playNotes (int step, int sample, juce::int64 gateSamples, juce::int64 hostSample, bool skipNudged = true);
    void playTrack (int slot, int step, int track, int sample, juce::int64 gateSamples, juce::int64 hostSample);
    // what the drum box was last sent per lane and CC (-1 = unknown), so CCs only go out when they change
    int ccSentVal[trossseq::kNumTracks][4], ccSentNum[trossseq::kNumTracks][4], ccBaseSeen[trossseq::kNumTracks][4];
    void forgetSentCcs() { for (auto& a : ccSentVal) for (auto& v : a) v = -1; }
public:
    static juce::String ccBaseId (int track, int k) { return "ccbase" + juce::String (track + 1) + "_" + juce::String (k + 1); }
    int ccBase (int track, int k) const { return (int) raw.ccBase[track][k]->load(); }
    void setCcBase (int track, int k, int v);   // message thread
private:
    int  slotForStep (long long absStep, int len) const;   // the pattern that will play at absStep (for look-ahead)
    double curSamplesPerStep = 6000.0;
    trossseq::StepClock nudgeClock;
    long long lastNudgeStep = LLONG_MIN;
    int skipNudge = 0;
    juce::Random chanceRnd;
public:
    // tests only: rolls for CHANCE come from here
    void seedChance (juce::int64 s) { chanceRnd.setSeed (s); }
    // undo point for edits on the LOCKS page (message thread)
    void pushLockUndo() { pushUndo ({ currentSlot.load() }); }
private:
    std::atomic<int> fillQueued { -1 };
    std::atomic<bool> fillPlaying { false };
    int fillReturn = -1;   // the pattern to go back to after the fill
    // STUTTER (beat repeat): while held, the slice that starts at stutStart (in 16ths) plays over and over
    bool stutActive = false;
    double stutStart = 0.0, stutLen = 2.0, stutSlice = 0.0, stutLastO = 0.0;
    int stutCount = 0;
public:
    static double stutterLength (int rateIndex);   // 0 = 1/2 bar ... 4 = 1/32, in 16ths
    bool isStuttering() const { return stutShown.load(); }
private:
    std::atomic<bool> stutShown { false };
    // grace notes waiting to be played (flam / drag / ruff), sample positions relative to this block
    // kind 0 = a plain hit (grace note, ratchet repeat); kind 1 = a nudged step: plays the whole step (locks, chance) when due
    struct SchedNote { int track = 0, velocity = 0; juce::int64 due = 0, gate = 1; int pitch = 0, kind = 0, slot = 0, step = 0; };
    static constexpr int kMaxSched = 128;
    SchedNote sched[kMaxSched];
    int numSched = 0;
    bool audible (int track) const;
    void recordHit (int track, bool accent, double sixteenthPos, int len);
    void updateBlockTime (int numSamples);

public:
    static juce::PropertiesFile::Options settingsOptions();   // also holds editor settings (help on/off)
private:
    juce::ValueTree mapToTree() const;
    void mapFromTree (const juce::ValueTree&);
    static juce::File mapFile (const juce::String& name);
    juce::String mapSignature() const;             // names, notes, channels, locks as one string
    void setMapIdentity (const juce::String& name) { { const juce::ScopedLock sl (nameLock); mapName = name; mapSnapshot = mapSignature(); } ++nameVersion; }
    juce::String mapName, mapSnapshot;             // guarded by nameLock

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrossSeqProcessor)
};
