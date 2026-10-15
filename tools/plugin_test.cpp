// End-to-end check of the plug-in: host-synced MIDI output (timing, velocity, gate, swing), pattern
// queueing, keyboard recording, MIDI learn, mute / thru / host-out switches, loops, state save/restore,
// MIDI export, and an editor snapshot.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/MidiExport.h"
#include "../Source/PluginEditor.h"
#include "../Source/Randomizer.h"

using namespace trossseq;

struct FakeHost : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        return p;
    }
};

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

struct Ev { juce::int64 t; juce::MidiMessage m; };

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    const double sr = 48000.0;
    const int block = 480;   // 10 ms; a 16th at 120 bpm = 125 ms = 6000 samples

    TrossSeqProcessor proc;
    FakeHost host;
    proc.setPlayHead (&host);
    proc.setRateAndBufferSizeDetails (sr, block);
    proc.prepareToPlay (sr, block);
    std::printf ("params: %d\n", (int) proc.getParameters().size());
    check (proc.apvts.getParameter ("pattern")->getNumSteps() == 12, "12 pattern slots");
    check (proc.map.note (0) == 36 && proc.map.channel (0) == 10 && proc.getTrackName (0) == "BD", "default map: track 1 = BD, note 36, channel 10");
    check (proc.producesMidi() && proc.acceptsMidi(), "plug-in takes and sends MIDI");

    auto setP = [&] (const char* id, float value)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    auto& pats = proc.patterns;
    auto put = [&] (int slot, int row, const char* s) { for (int i = 0; i < 16; ++i) pats.setCell (slot, row, i, s[i] == 'x'); };
    put (0, 0, "x...............");   // accent on step 1
    put (0, 1, "x...x...x...x...");   // BD
    put (0, 2, "....x.......x...");   // SD
    put (0, 6, ".x..............");   // CH on step 2 (for swing)
    put (1, 1, "x...............");
    put (1, 5, "x...............");   // CP

    juce::AudioBuffer<float> buf (2, block);
    juce::int64 now = 0;
    std::vector<Ev> evs;
    int lastStep = -1;
    std::vector<int> slotAtStep0;

    auto run = [&] (int numBlocks, std::function<void (int, juce::MidiBuffer&)> perBlock = {})
    {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::MidiBuffer midi;
            if (perBlock) perBlock (b, midi);
            proc.processBlock (buf, midi);
            for (const auto md : midi) evs.push_back ({ now + md.samplePosition, md.getMessage() });
            if (host.playing) host.ppq += block / sr * host.bpm / 60.0;
            now += block;
            const int st = proc.getPlayStep();
            if (st != lastStep && st == 0) slotAtStep0.push_back (proc.getCurrentSlot());
            lastStep = st;
        }
    };
    auto ons = [&] (int note = -1) { std::vector<Ev> r; for (auto& e : evs) if (e.m.isNoteOn() && (note < 0 || e.m.getNoteNumber() == note)) r.push_back (e); return r; };
    auto offs = [&] (int note) { std::vector<Ev> r; for (auto& e : evs) if (e.m.isNoteOff() && e.m.getNoteNumber() == note) r.push_back (e); return r; };

    // 1. stopped: nothing comes out
    run (20);
    check (evs.empty() && proc.getPlayStep() == -1, "no MIDI while the transport is stopped");

    // 2. one bar of pattern 1 at 120 BPM: BD on 1,5,9,13 -> samples 0, 24000, 48000, 72000
    host.playing = true; host.ppq = 0.0;
    const auto t0 = now;
    evs.clear();
    run (200);   // 2 s = 1 bar
    {
        auto bd = ons (36);
        bool timing = bd.size() == 4;
        for (size_t i = 0; i < bd.size() && timing; ++i) timing = bd[i].t - t0 == (juce::int64) i * 24000;
        check (timing, "BD notes land exactly on steps 1, 5, 9, 13 (got " + juce::String ((int) bd.size()) + " notes)");
        check (! bd.empty() && bd[0].m.getVelocity() == 127 && bd[0].m.getChannel() == 10 && (bd.size() < 2 || bd[1].m.getVelocity() == 100),
               "accented step at velocity 127, others at 100, channel 10");
        auto bdOff = offs (36);
        check (! bdOff.empty() && bdOff[0].t - t0 == 3000, "note-off after the gate: 50 % of a 16th = 3000 samples (got "
               + juce::String (bdOff.empty() ? -1 : (int) (bdOff[0].t - t0)) + ")");
        check (ons (38).size() == 2 && ons (42).size() == 1, "SD x2 and CH x1 in the bar");
        auto ch = ons (42);
        check (! ch.empty() && ch[0].t - t0 == 6000, "CH on step 2 straight (no swing) at 6000");
    }

    // 3. swing 66.7 %: step 2 moves a third of a 16th later (8000)
    setP ("swing", 66.7f);
    host.ppq = 0.0; evs.clear();
    const auto t1 = now;
    run (100);
    {
        auto ch = ons (42);
        const auto pos = ch.empty() ? -1 : ch[0].t - t1;
        check (std::abs ((double) pos - 8004.0) <= 2.0, "swing 66.7 %: step 2 at ~8004 samples (got " + juce::String ((int) pos) + ")");
        auto bd = ons (36);
        check (bd.size() >= 2 && bd[1].t - t1 == 24000, "swing leaves step 5 on the beat");
    }
    setP ("swing", 50.0f);

    // 4. pick pattern 2 in the middle of a bar: it must wait for step 1
    host.ppq = 1.5;
    lastStep = -1; slotAtStep0.clear();
    setP ("pattern", 1.0f);
    run (10);
    check (proc.getCurrentSlot() == 0 && proc.getPendingSlot() == 1, "pattern 2 queued while pattern 1 finishes");
    evs.clear();
    run (300);
    check (! slotAtStep0.empty() && slotAtStep0.front() == 1 && proc.getPendingSlot() == -1, "pattern 2 starts on step 1");
    check (! ons (39).empty(), "pattern 2 plays its clap");

    // 5. LENGTH 12: steps wrap at 12
    setP ("seqlen", 12.0f);
    int maxStep = 0;
    run (20);
    for (int b = 0; b < 300; ++b) { run (1); maxStep = juce::jmax (maxStep, proc.getPlayStep()); }
    check (maxStep == 11, "LENGTH 12 plays steps 1..12 only (highest " + juce::String (maxStep + 1) + ")");
    setP ("seqlen", 16.0f);

    // 6. record: snare (note 38) hit just before step 7 at velocity 120 -> SD + accent on step 7 of pattern 2
    host.ppq = 0.0;
    run (1);
    const int samplesTo7 = (int) std::round ((6.0 / 4.0 - 0.06) * 60.0 / host.bpm * sr);
    const int hitBlock = samplesTo7 / block;
    run (hitBlock + 2, [&] (int b, juce::MidiBuffer& m)
    {
        if (b == hitBlock - 1) m.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 120), samplesTo7 % block);
    });
    check (pats.cell (1, 2, 6) && pats.cell (1, 0, 6), "recorded snare + accent into the nearest step (7)");
    check (! pats.cell (0, 2, 6), "recording went into the playing pattern only");

    // 7. MIDI thru on (default): the keyboard note comes out too; off: it doesn't
    {
        evs.clear();
        run (1, [] (int, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (1, 70, (juce::uint8) 90), 5); });
        check (ons (70).size() == 1, "MIDI THRU passes the keyboard on");
        setP ("thru", 0.0f);
        evs.clear();
        run (1, [] (int, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (1, 71, (juce::uint8) 90), 5); });
        check (ons (71).empty(), "MIDI THRU off blocks it");
        setP ("thru", 1.0f);
    }

    // 8. mute track 5 (clap): pattern 2's clap stops
    setP ("mute5", 1.0f);
    host.ppq = 0.0; evs.clear();
    run (210);
    check (ons (39).empty() && ! ons (36).empty(), "muted track sends nothing, others keep playing");
    setP ("mute5", 0.0f);

    // 8b. momentary solo (Shift + held M): only that track plays, even if it is muted itself
    proc.momentarySolo = 4;   // clap
    setP ("mute5", 1.0f);
    host.ppq = 0.0; evs.clear();
    run (210);
    check (! ons (39).empty() && ons (36).empty(), "held solo on the clap: clap plays (despite its mute), BD silent");
    proc.momentarySolo = -1;
    setP ("mute5", 0.0f);
    host.ppq = 0.0; evs.clear();
    run (210);
    check (! ons (39).empty() && ! ons (36).empty(), "solo released: everything plays again");

    // 8c. the five extra lanes: lane 13 (CL, note 75) plays
    check (kNumTracks == 13 && proc.map.note (12) == 75 && proc.getTrackName (12) == "CL", "13 lanes, lane 13 = CL on note 75");
    put (1, 13, "....x...........");
    host.ppq = 0.0; evs.clear();
    run (210);
    check (ons (75).size() == 1 && ons (75)[0].m.getChannel() == 10, "lane 13 sends note 75 on channel 10");
    put (1, 13, "................");

    // 9. MIDI learn on track 4: next note (60 on channel 2) becomes its note and channel; steps follow it
    host.playing = false;
    run (2);
    setP ("pattern", 0.0f);
    run (2);
    proc.learnTrack = 3;
    run (1, [] (int, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), 0); });
    check (proc.map.note (3) == 60 && proc.map.channel (3) == 2 && proc.learnTrack.load() == -1, "LEARN: track 4 = note 60, channel 2");
    put (0, 4, "..x.............");
    host.playing = true; host.ppq = 0.0; evs.clear();
    run (60);
    {
        auto e = ons (60);
        check (e.size() == 1 && e[0].m.getChannel() == 2, "learned track plays note 60 on channel 2");
    }

    // 10. stop flushes hanging notes; HOST OUT off sends nothing to the host
    {
        setP ("gate", 95.0f);
        host.ppq = 0.0; evs.clear();
        run (5);   // step 1 fired, its notes still on
        host.playing = false;
        run (1);
        int open = 0;
        for (auto& e : evs) { if (e.m.isNoteOn()) ++open; if (e.m.isNoteOff()) --open; }
        check (open == 0, "stopping ends every note that was still on");
        setP ("gate", 50.0f);

        setP ("hostout", 0.0f);
        host.playing = true; host.ppq = 0.0; evs.clear();
        run (100);
        check (evs.empty() && proc.getPlayStep() >= 0, "HOST OUT off: sequencer runs, nothing to the host");
        setP ("hostout", 1.0f);
    }

    // 11. a host loop (position jumps back) keeps the steps going
    {
        host.ppq = 3.9; evs.clear();
        run (1);
        lastStep = -1;
        run (5);
        host.ppq = 0.0;   // loop back to bar start
        run (2);
        check (! ons (36).empty() && proc.getPlayStep() == 0, "after a loop back the sequencer restarts on step 1");
    }

    // 12. a click on a track name plays it at once (stopped: not recorded)
    {
        host.playing = false;
        run (2);
        const auto before = pats.get (0, 6);
        evs.clear();
        proc.panelHit (5, true);   // closed hat, Shift = accent
        run (1);
        auto e = ons (42);
        check (e.size() == 1 && e[0].m.getVelocity() == 127 && pats.get (0, 6) == before, "name click sends the note (accent), no recording while stopped");
    }

    // 13. drum map presets + all-channels
    proc.applyPreset (2);
    check (proc.map.note (2) == 43 && proc.getTrackName (0) == "KICK", "volca beats map: LO TOM = 43, names follow");
    proc.map.setAllChannels (3);
    check (proc.map.channel (7) == 3, "all tracks on channel 3");
    proc.setTrackName (6, "ride");
    proc.map.setNote (6, 51);

    // 14. MIDI export with the track map
    {
        PatternMemory pm;
        TrackMap tm;
        tm.setNote (1, 40); tm.setChannel (1, 5);
        auto put2 = [&] (int slot, int row, const char* s) { for (int i = 0; i < 16; ++i) pm.setCell (slot, row, i, s[i] == 'x'); };
        put2 (2, 0, "x...............");
        put2 (2, 1, "x.......x.......");
        put2 (2, 2, "....x.......x...");
        MidiExport::Settings s; s.bpm = 128.0;
        auto f = outDir.getChildFile ("export_test.mid");
        check (MidiExport::write (MidiExport::make (pm, tm, { 2 }, s, "Pattern 3"), f), "MIDI file written");
        juce::MidiFile back;
        juce::FileInputStream in (f);
        const bool read = in.openedOk() && back.readFrom (in);
        int n = 0, sd = 0, acc = 0; double tempo = 0, lastTick = 0;
        if (read)
            for (auto* e : *back.getTrack (0))
            {
                const auto& m = e->message;
                lastTick = juce::jmax (lastTick, m.getTimeStamp());
                if (m.isTempoMetaEvent()) tempo = 60.0 / m.getTempoSecondsPerQuarterNote();
                if (! m.isNoteOn()) continue;
                ++n;
                if (m.getNoteNumber() == 40 && m.getChannel() == 5) ++sd;
                if (m.getVelocity() == 127) ++acc;
            }
        check (read && n == 4 && sd == 2 && acc == 1, "export: 4 notes, track 2 on note 40 / channel 5, one accent");
        check (std::abs (tempo - 128.0) < 0.01 && std::abs (lastTick - 16 * 24) < 0.5, "export: 128 BPM, 16 steps long");
    }

    // 14b. randomiser
    {
        // lane roles from names, then notes
        check (roleFor ("BD", 36) == Role::Kick && roleFor ("CL", 75) == Role::Perc && roleFor ("CL HAT", 42) == Role::ClosedHat
               && roleFor ("OP HAT", 46) == Role::OpenHat && roleFor ("LO TOM", 43) == Role::Tom && roleFor ("Kick 2", 0) == Role::Kick
               && roleFor ("X", 46) == Role::OpenHat && roleFor ("RD", 51) == Role::Cymbal, "lane roles from names, then MIDI notes");

        // engine on its own: 200 patterns per style
        Randomizer rz (1234);
        LaneInfo lanes[kNumTracks];
        const char* nm[kNumTracks] = { "BD", "SD", "LT", "HT", "CP", "CH", "OH", "CY", "RS", "MT", "RD", "CB", "CL" };
        const int nt[kNumTracks] = { 36, 38, 45, 50, 39, 42, 46, 49, 37, 47, 51, 56, 75 };
        for (int t = 0; t < kNumTracks; ++t) lanes[t] = { roleFor (nm[t], nt[t]), false };
        lanes[1].locked = true;   // SD locked
        bool ok = true, hatsClash = false, pastLen = false, lockedKept = true, accentKept = true;
        int kickHits = 0, hatHits = 0, tomHits = 0;
        for (int st = 0; st < kNumStyles; ++st)
            for (int i = 0; i < 200; ++i)
            {
                uint16_t rows[kNumRows] {};
                rows[0] = 0x0101; rows[2] = 0x1234;
                rz.generate (rows, lanes, 12, (Style) st, 50.0f);
                ok &= (rows[1] & 1) != 0;                       // kick on step 1, always
                hatsClash |= (rows[6] & rows[7]) != 0;          // CH (row 6) and OH (row 7)
                for (int r = 1; r < kNumRows; ++r) if (r != 2) pastLen |= (rows[r] & 0xF000) != 0;
                lockedKept &= rows[2] == 0x1234;
                accentKept &= rows[0] == 0x0101;
                kickHits += __builtin_popcount (rows[1]); hatHits += __builtin_popcount (rows[6]); tomHits += __builtin_popcount (rows[3]);
            }
        check (ok, "random: kick always on step 1");
        check (! hatsClash, "random: open and closed hat never on the same step");
        check (! pastLen, "random: nothing past LENGTH (12)");
        check (lockedKept, "random: locked lane (SD) untouched");
        check (accentKept, "random: accent row untouched");
        check (hatHits > kickHits && kickHits > tomHits, "random: hats busiest, toms sparsest (CH " + juce::String (hatHits)
               + ", BD " + juce::String (kickHits) + ", LT " + juce::String (tomHits) + ")");

        lanes[1].locked = false;
        int lowHits = 0, highHits = 0; bool straightSnare = true;
        for (int i = 0; i < 100; ++i)
        {
            uint16_t a[kNumRows] {}, b[kNumRows] {};
            rz.generate (a, lanes, 16, Style::Straight, 10.0f);
            rz.generate (b, lanes, 16, Style::Straight, 90.0f);
            for (int r = 1; r < kNumRows; ++r) { lowHits += __builtin_popcount (a[r]); highHits += __builtin_popcount (b[r]); }
            straightSnare &= (a[2] & 0x1010) == 0x1010;
        }
        check (highHits > lowHits * 2, "DENSITY: 90 % gives far busier patterns than 10 % (" + juce::String (highHits) + " vs " + juce::String (lowHits) + ")");
        check (straightSnare, "STRAIGHT: snare always on the backbeat (steps 5 and 13)");

        // mutate: a few steps change, locked lanes don't
        int diffTotal = 0, maxDiff = 0; bool mutLock = true;
        lanes[0].locked = true;
        for (int i = 0; i < 100; ++i)
        {
            uint16_t r0[kNumRows] {};
            rz.generate (r0, lanes, 16, Style::Electro, 50.0f);
            r0[1] = 0x1111;
            uint16_t r1[kNumRows]; std::copy (r0, r0 + kNumRows, r1);
            rz.mutate (r1, lanes, 16, Style::Electro, 50.0f);
            int d = 0;
            for (int r = 1; r < kNumRows; ++r) d += __builtin_popcount (r0[r] ^ r1[r]);
            diffTotal += d; maxDiff = juce::jmax (maxDiff, d);
            mutLock &= r1[1] == 0x1111;
        }
        check (diffTotal > 0 && maxDiff <= 8, "MUTATE: small changes (at most " + juce::String (maxDiff) + " steps)");
        check (mutLock, "MUTATE: locked lane untouched");
        lanes[0].locked = false;

        // fill: only the last four steps change
        bool fillOnly = true; int fillChanged = 0;
        for (int i = 0; i < 100; ++i)
        {
            uint16_t r0[kNumRows] {};
            rz.generate (r0, lanes, 16, Style::Straight, 50.0f);
            uint16_t r1[kNumRows]; std::copy (r0, r0 + kNumRows, r1);
            rz.fill (r1, lanes, 16, 50.0f);
            for (int r = 1; r < kNumRows; ++r) { fillOnly &= ((r0[r] ^ r1[r]) & 0x0FFF) == 0; fillChanged += __builtin_popcount (r0[r] ^ r1[r]); }
        }
        check (fillOnly && fillChanged > 0, "FILL: only steps 13-16 change");

        // through the plug-in: stopped = at once, playing = waits for step 1, UNDO
        host.playing = false;
        run (2);
        const int slot = proc.getCurrentSlot();
        pats.clear (slot);
        pats.setCell (slot, 0, 3, true);   // accent on step 4
        pats.setCell (slot, 11, 9, true);  // RD on step 10
        proc.toggleLock (10);              // lock RD
        proc.randomise (TrossSeqProcessor::RandomOp::NewPattern);
        check (pats.cell (slot, 1, 0) && pats.get (slot, 0) == (1 << 3) && pats.get (slot, 11) == (1 << 9),
               "RANDOM while stopped: written at once, accent and locked lane kept");
        const auto afterFirst = pats.get (slot, 6);
        juce::uint16 before[kNumRows];
        for (int r = 0; r < kNumRows; ++r) before[r] = pats.get (slot, r);

        host.playing = true; host.ppq = 1.0;   // beat 2 of the bar
        run (2);
        proc.randomise (TrossSeqProcessor::RandomOp::NewPattern);
        bool unchanged = true;
        for (int r = 0; r < kNumRows; ++r) unchanged &= pats.get (slot, r) == before[r];
        check (proc.isRandomPending() && unchanged, "RANDOM while playing: waits, pattern unchanged mid-bar");
        lastStep = -1;
        run (200);
        check (! proc.isRandomPending(), "the random pattern takes over on step 1");
        proc.undoRandom();
        bool restored = true;
        for (int r = 0; r < kNumRows; ++r) restored &= pats.get (slot, r) == before[r];
        check (restored, "UNDO brings back the previous pattern");
        proc.randomise (TrossSeqProcessor::RandomOp::NewPattern);
        proc.randomise (TrossSeqProcessor::RandomOp::NewPattern);   // second click while still waiting
        check (proc.isRandomPending() && proc.undoRandom() && ! proc.isRandomPending(), "UNDO while waiting cancels it");
        restored = true;
        for (int r = 0; r < kNumRows; ++r) restored &= pats.get (slot, r) == before[r];
        check (restored, "...and the pattern is as it was");
        proc.undoRandom();
        juce::ignoreUnused (afterFirst);
        check (pats.get (slot, 1) == 0 && pats.get (slot, 11) == (1 << 9) && ! proc.canUndo(), "UNDO again goes back to before the first RANDOM");
        host.playing = false;
        run (2);
        proc.randomise (TrossSeqProcessor::RandomOp::Lane, 5);   // CH only
        check (pats.get (slot, 6) != 0 && pats.get (slot, 1) == 0, "Randomise this lane: only CH written");
    }

    // 15. state round trip: patterns, map, names, slot
    juce::MemoryBlock state; proc.getStateInformation (state);
    {
        TrossSeqProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (int s = 0; s < kNumSlots; ++s)
            for (int r = 0; r < kNumRows; ++r)
                same &= other.patterns.get (s, r) == pats.get (s, r);
        check (same, "all 12 patterns restored");
        check (other.locked[10].load() && ! other.locked[0].load(), "lane locks restored");
        check (other.map.note (6) == 51 && other.map.channel (0) == 3 && other.getTrackName (6) == "RIDE"
               && other.getTrackName (0) == "KICK", "track notes, channels and names restored");
    }
    setP ("velocity", 20.0f);
    proc.setStateInformation (state.getData(), (int) state.getSize());
    check ((int) proc.apvts.getRawParameterValue ("velocity")->load() == 100, "parameters restored");

    // editor snapshot at 1x and 2x: an Electro random pattern, RD locked
    proc.applyPreset (0);
    setP ("rstyle", 1.0f);
    pats.clear (0);
    pats.setCell (0, 0, 0, true); pats.setCell (0, 0, 12, true);
    { juce::Random r (7); for (int i = 0; i < 3; ++i) proc.randomise (TrossSeqProcessor::RandomOp::NewPattern); }
    juce::Thread::sleep (400);   // let the name-strip flashes fade
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());

    // the M button with the mouse: Shift-press = momentary solo, nothing toggled; plain click = mute toggle
    {
        std::vector<MuteButton*> mutes;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            for (auto* ch : c.getChildren()) { if (auto* m = dynamic_cast<MuteButton*> (ch)) mutes.push_back (m); find (*ch); }
        };
        find (*ed);
        check (mutes.size() == 13, "13 M buttons on the panel");
        if (mutes.size() == 13)
        {
            auto* mb = mutes[2];   // LT
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            auto ev = [&] (juce::ModifierKeys mods)
            {
                return juce::MouseEvent (src, { 5.0f, 5.0f }, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, mb, mb,
                                         juce::Time::getCurrentTime(), { 5.0f, 5.0f }, juce::Time::getCurrentTime(), 1, false);
            };
            const auto shiftLeft = juce::ModifierKeys (juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier);
            mb->mouseDown (ev (shiftLeft));
            const bool heldSolo = proc.momentarySolo.load() == 2;
            mb->mouseUp (ev (juce::ModifierKeys (juce::ModifierKeys::shiftModifier)));
            check (heldSolo && proc.momentarySolo.load() == -1 && proc.apvts.getRawParameterValue ("mute3")->load() < 0.5f,
                   "Shift + hold on M: solo while held, gone on release, mute not toggled");
        }
    }
    std::printf ("editor size %d x %d\n", ed->getWidth(), ed->getHeight());
    juce::PNGImageFormat png;
    const int baseW = ed->getWidth(), baseH = ed->getHeight();
    for (float scale : { 1.0f, 2.0f })
    {
        ed->setSize ((int) (baseW * scale), (int) (baseH * scale));
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto pngFile = outDir.getChildFile ("editor_" + juce::String ((int) scale) + "x.png");
        pngFile.deleteFile();
        if (auto os = pngFile.createOutputStream()) png.writeImageToStream (img, *os);
        ed->setSize (baseW, baseH);
    }
    ed.reset();

    std::printf ("%s\n", failures ? "SOME TESTS FAILED" : "all tests passed");
    return failures ? 1 : 0;
}
