#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SeqCore.h"
#include "DirectMidiOut.h"

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
    int  getPlayStep() const    { return playStep.load(); }      // step being played, -1 = stopped
    void clearCurrentPattern()  { patterns.clear (currentSlot.load()); }

    // track names (message thread; locked because hosts may save state from another thread)
    juce::String getTrackName (int t) const              { const juce::ScopedLock sl (nameLock); return names[t]; }
    void setTrackName (int t, const juce::String& n)     { { const juce::ScopedLock sl (nameLock); names.set (t, n.toUpperCase().substring (0, 10)); } ++nameVersion; }
    void applyPreset (int i);
    int  getNameVersion() const { return nameVersion.load(); }

    // ---- Shift + mouse held on a track's M button: only that track plays until the button is released
    std::atomic<int> momentarySolo { -1 };

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

    // Standalone app only: no host transport, so it gets its own play button and tempo
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
    std::atomic<bool>  internalPlay { false };
    std::atomic<float> internalBpm { 120.0f };

    std::atomic<double> currentBpm { 120.0 };   // the tempo seen in the last audio block

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
    struct PendingOff { int channel = 0, note = 0; juce::int64 sample = 0; };
    static constexpr int kMaxPending = 128;
    PendingOff pending[kMaxPending];
    int numPending = 0;

    // transport following
    trossseq::StepClock clock;
    bool wasPlaying = false;
    juce::int64 lastClockTick = 0;
    double internalPpq = 0.0;

    // time base for the port output: where this block starts, in Time::getMillisecondCounterHiRes
    double blockStartMs = 0.0;
    bool   haveBlockTime = false;

    // per-block output
    juce::MidiBuffer out;
    bool hostOutOn = true;
    float offsetMs = 0.0f;

    struct Raw
    {
        std::atomic<float>* mute[trossseq::kNumTracks];
        std::atomic<float> *seqOn, *seqLen, *pattern, *rec, *velocity, *accentVel, *gate, *swing,
                           *thru, *hostOut, *clockOut, *offset;
    } raw {};

    void emit (const juce::MidiMessage& m, int sample);
    void noteOn (int track, int velocity, int sample, juce::int64 gateSamples);
    void flushNoteOffs (int sample);
    void doStep (int step, int sample, bool seqOn, juce::int64 gateSamples);
    void recordHit (int track, bool accent, double sixteenthPos, int len);
    void updateBlockTime (int numSamples);

    static juce::PropertiesFile::Options settingsOptions();
    juce::ValueTree mapToTree() const;
    void mapFromTree (const juce::ValueTree&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrossSeqProcessor)
};
