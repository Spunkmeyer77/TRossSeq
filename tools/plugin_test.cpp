// End-to-end check of the plug-in: host-synced MIDI output (timing, velocity, gate, swing), pattern
// queueing, keyboard recording, MIDI learn, mute / thru / host-out switches, loops, state save/restore,
// MIDI export, and an editor snapshot.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/MidiExport.h"
#include "../Source/PluginEditor.h"
#include "../Source/Randomizer.h"
#include <set>

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

    // 14a. flams: grace note FLAM ms before the step at 60 % velocity, main hit on the grid
    {
        host.playing = false;
        run (2);
        setP ("pattern", 3.0f);   // an empty slot
        run (2);
        const int fs = proc.getCurrentSlot();
        pats.clear (fs);
        proc.map.setNote (1, 38);
        pats.setCell (fs, 2, 4, true);       // SD on step 5 ...
        pats.setFlam (fs, 2, 4, true);       // ... with a flam
        pats.setCell (fs, 2, 12, true);      // SD on step 13, no flam
        setP ("flamtime", 25.0f);
        setP ("swing", 50.0f);
        host.playing = true; host.ppq = 0.0; evs.clear();
        const auto tf = now;
        run (200);
        auto sd = ons (38);
        const juce::int64 flamS = (juce::int64) std::round (0.025 * sr);   // 1200 samples
        check (sd.size() == 3, "flammed step sends 2 notes, plain step 1 (got " + juce::String ((int) sd.size()) + ")");
        if (sd.size() == 3)
        {
            check (std::abs ((sd[0].t - tf) - (24000 - flamS)) <= 1 && sd[1].t - tf == 24000,
                   "grace note 25 ms before step 5, main hit exactly on step 5 (grace at " + juce::String ((int) (sd[0].t - tf)) + ")");
            check (sd[0].m.getVelocity() == 60 && sd[1].m.getVelocity() == 100, "grace at 60 % velocity (60), main at 100");
            check (sd[2].t - tf == 72000, "step 13 without flam: one hit on the grid");
        }
        int open = 0, maxOpen = 0;
        for (auto& e : evs) if (e.m.getNoteNumber() == 38) { if (e.m.isNoteOn()) ++open; if (e.m.isNoteOff()) --open; maxOpen = juce::jmax (maxOpen, open); }
        check (maxOpen == 1, "grace note ends before the main hit starts");

        // a flam on step 1 still plays after the loop wraps
        pats.setCell (fs, 2, 0, true); pats.setFlam (fs, 2, 0, true);
        host.ppq = 0.0; evs.clear();
        run (420);   // a bit over 2 bars
        int onStep1 = 0;
        for (auto& e : ons (38)) if (e.m.getVelocity() == 60) ++onStep1;
        check (onStep1 >= 3, "flams play every bar, including step 1 after the wrap (" + juce::String (onStep1) + " grace notes)");

        // switching a step off drops its flam; state keeps flams
        pats.setCell (fs, 2, 0, false);
        check (! pats.flam (fs, 2, 0), "switching a step off removes its flam");
        juce::MemoryBlock st; proc.getStateInformation (st);
        TrossSeqProcessor other;
        other.setStateInformation (st.getData(), (int) st.getSize());
        check (other.patterns.flam (fs, 2, 4) && ! other.patterns.flam (fs, 2, 12), "flams saved and restored with the project");

        // MIDI export writes the grace note before the step
        MidiExport::Settings xs; xs.bpm = 120.0; xs.flamMs = 25.0f;
        auto mf = MidiExport::make (pats, proc.map, { fs }, xs, "flam");
        std::vector<double> sdTimes;
        for (auto* e : *mf.getTrack (0)) if (e->message.isNoteOn() && e->message.getNoteNumber() == 38) sdTimes.push_back (e->message.getTimeStamp());
        check (sdTimes.size() == 3 && sdTimes[0] < 96 && sdTimes[1] == 96, "export: grace note just before step 5 (tick " + juce::String (sdTimes.empty() ? -1.0 : sdTimes[0]) + "), main on 96");
        // drag (2 graces) and ruff (3) with their own spacing and level; FLAM LEVEL knob for the rest
        {
            pats.clear (fs);
            pats.setCell (fs, 2, 4, true); pats.setFlam (fs, 2, 4, true);
            FlamStyle ruff; ruff.graces = 3; ruff.spacing = 3; ruff.level = 2;   // 40 ms, 50 %
            pats.setFlamStyle (fs, 2, 4, ruff.encode());
            pats.setCell (fs, 2, 12, true); pats.setFlam (fs, 2, 12, true);
            FlamStyle drag; drag.graces = 2;                                    // knob spacing and level
            pats.setFlamStyle (fs, 2, 12, drag.encode());
            setP ("flamlevel", 40.0f);
            host.playing = true; host.ppq = 0.0; evs.clear();
            const auto t0r = now;
            run (200);
            auto n = ons (38);
            const juce::int64 s40 = (juce::int64) std::round (0.040 * sr), s25 = (juce::int64) std::round (0.025 * sr);
            bool ok = n.size() == 7;
            if (ok)
            {
                ok &= n[0].t - t0r == 24000 - s40 && n[1].t - t0r == 24000 - s40 + s40 / 3 && n[2].t - t0r == 24000 - s40 + 2 * (s40 / 3);
                ok &= n[3].t - t0r == 24000 && n[0].m.getVelocity() == 50 && n[3].m.getVelocity() == 100;
                ok &= n[4].t - t0r == 72000 - s25 && n[5].t - t0r == 72000 - s25 + s25 / 2 && n[6].t - t0r == 72000;
                ok &= n[4].m.getVelocity() == 40;
            }
            check (ok, "ruff: 3 grace notes over 40 ms at 50 %; drag: 2 over the FLAM knob's 25 ms at FLAM LEVEL 40 % (" + juce::String ((int) n.size()) + " notes)");
            int open = 0, maxOpen = 0;
            for (auto& e : evs) if (e.m.getNoteNumber() == 38) { if (e.m.isNoteOn()) ++open; if (e.m.isNoteOff()) --open; maxOpen = juce::jmax (maxOpen, open); }
            check (maxOpen == 1, "every grace note ends before the next one starts");
            setP ("flamlevel", 60.0f);

            // styles survive save / load, and the export writes all grace notes
            juce::MemoryBlock st2; proc.getStateInformation (st2);
            TrossSeqProcessor o2; o2.setStateInformation (st2.getData(), (int) st2.getSize());
            check (o2.patterns.flamStyle (fs, 2, 4).graces == 3 && o2.patterns.flamStyle (fs, 2, 4).spacing == 3 && o2.patterns.flamStyle (fs, 2, 12).graces == 2,
                   "flam types and spacings saved with the project");
            MidiExport::Settings xs2; xs2.bpm = 120.0;
            auto mf2 = MidiExport::make (pats, proc.map, { fs }, xs2, "ruff");
            int sdOn = 0;
            for (auto* e : *mf2.getTrack (0)) if (e->message.isNoteOn() && e->message.getNoteNumber() == 38) ++sdOn;
            check (sdOn == 7, "export: ruff and drag grace notes written (" + juce::String (sdOn) + " snare notes)");

            // removing a flam forgets its style
            pats.setFlam (fs, 2, 4, false);
            pats.setFlam (fs, 2, 4, true);
            check (pats.flamStyle (fs, 2, 4).graces == 1 && pats.flamStyle (fs, 2, 4).spacing == 0, "a flam that's removed and added again starts as a plain flam");

            // one flam style on every hit of a lane
            pats.setCell (fs, 2, 0, true); pats.setCell (fs, 2, 8, true);
            proc.flamWholeRow (2, ruff.encode());
            check (pats.getFlams (fs, 2) == pats.get (fs, 2) && pats.flamStyle (fs, 2, 8).graces == 3, "Use this flam on every hit in this lane");
            proc.undoRandom();
            check (! pats.flam (fs, 2, 8), "...and UNDO takes it back");
            host.playing = false;
            run (2);
        }

        // clearing lanes, with UNDO
        {
            pats.clear (fs);
            pats.setCell (fs, 1, 0, true); pats.setCell (fs, 2, 4, true); pats.setFlam (fs, 2, 4, true);
            pats.setCell (fs, 0, 0, true);
            pats.setCell (5, 2, 7, true);   // SD in another slot
            proc.clearRowFlams (2, false);
            check (pats.cell (fs, 2, 4) && ! pats.flam (fs, 2, 4), "Clear this lane's flams: steps stay, flams go");
            proc.undoRandom();
            check (pats.flam (fs, 2, 4), "UNDO brings the flams back");
            proc.clearRow (2, false);
            check (pats.get (fs, 2) == 0 && pats.get (fs, 1) != 0 && pats.cell (5, 2, 7), "Clear this lane: only this lane, only this pattern");
            proc.undoRandom();
            proc.clearRow (2, true);
            check (pats.get (fs, 2) == 0 && ! pats.cell (5, 2, 7) && pats.get (fs, 1) != 0, "Clear this lane in all 12 patterns");
            proc.undoRandom();
            check (pats.cell (5, 2, 7) && pats.cell (fs, 2, 4) && pats.flam (fs, 2, 4), "one UNDO restores the lane in every pattern");
            proc.clearRow (0, false);
            check (pats.get (fs, 0) == 0 && pats.get (fs, 1) != 0, "Clear the accent row");
            proc.undoRandom();
            proc.clearCurrentPattern();
            check (pats.isEmpty (fs), "CLEAR empties the pattern");
            proc.undoRandom();
            check (pats.cell (fs, 1, 0) && pats.cell (fs, 0, 0) && pats.flam (fs, 2, 4), "CLEAR can be undone");
            pats.clear (5);
        }
        setP ("pattern", 0.0f);
        run (2);
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

        // every style: kick on 1, nothing past LENGTH, no open/closed hat clash, a sensible amount of notes
        {
            check (kNumStyles == 12 && juce::String (styleName (6)) == "MIAMI BASS" && juce::String (styleName (11)) == "INDUSTRIAL", "12 random styles");
            bool allOk = true;
            juce::String report;
            for (int st = 0; st < kNumStyles; ++st)
            {
                int notes = 0; bool kick1 = true, clash = false;
                for (int i = 0; i < 100; ++i)
                {
                    uint16_t rows[kNumRows] {};
                    rz.generate (rows, lanes, 16, (Style) st, 50.0f);
                    kick1 &= (rows[1] & 1) != 0;
                    clash |= (rows[6] & rows[7]) != 0;
                    for (int r = 1; r < kNumRows; ++r) notes += __builtin_popcount (rows[r]);
                }
                const double perBar = notes / 100.0;
                report << styleName (st) << " " << juce::String (perBar, 1) << "  ";
                allOk &= kick1 && ! clash && perBar > 6 && perBar < 70;
            }
            std::printf ("notes per bar: %s\n", report.toRawUTF8());
            check (allOk, "every style: kick on step 1, hats don't clash, sensible note count");

            // style character checks (200 rolls each)
            auto avg = [&] (Style st, int row, int step)
            {
                int on = 0;
                for (int i = 0; i < 200; ++i) { uint16_t rows[kNumRows] {}; rz.generate (rows, lanes, 16, st, 50.0f); on += (rows[row] >> step) & 1; }
                return on / 200.0;
            };
            check (avg (Style::House, 1, 4) == 1.0 && avg (Style::House, 1, 12) == 1.0 && avg (Style::House, 7, 2) > 0.7,
                   "HOUSE: kick on every beat, open hat on the off-beats");
            check (avg (Style::Industrial, 2, 4) == 1.0 && avg (Style::Industrial, 1, 8) == 1.0, "INDUSTRIAL: kick and snare locked to the grid");
            check (avg (Style::Latin, 13, 3) > 0.7 && avg (Style::Latin, 13, 6) > 0.7 && avg (Style::Latin, 13, 1) < 0.2, "LATIN: claves lane follows the clave");
            check (avg (Style::Dub, 9, 12) > 0.6 && avg (Style::Dub, 2, 4) < 0.1, "DUB: rim shot on 13 instead of a snare backbeat");
            check (avg (Style::HipHop, 2, 4) == 1.0 && avg (Style::HipHop, 6, 1) < 0.2, "HIP-HOP: heavy backbeat, simple 8th hats");
        }

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

        // flams: the randomiser never adds any; RANDOM drops them on the lanes it rewrites, locked lanes keep theirs
        {
            pats.clear (slot);
            pats.setCell (slot, 11, 9, true); pats.setFlam (slot, 11, 9, true);   // RD (locked) with a flam
            pats.setCell (slot, 2, 4, true);  pats.setFlam (slot, 2, 4, true);    // snare flam, unlocked lane
            for (int st = 0; st < kNumStyles; ++st)
            {
                setP ("rstyle", (float) st);
                proc.randomise (TrossSeqProcessor::RandomOp::NewPattern);
            }
            int flamCount = 0;
            for (int r = 1; r < kNumRows; ++r) flamCount += __builtin_popcount (pats.getFlams (slot, r));
            check (flamCount == 1 && pats.flam (slot, 11, 9), "RANDOM adds no flams, drops them on rewritten lanes, locked lane keeps its flam");
            proc.undoRandom();
            for (int i = 0; i < 11; ++i) proc.undoRandom();
            check (pats.flam (slot, 2, 4), "UNDO brings the hand-set flam back");
            setP ("rstyle", 0.0f);
        }
    }

    // 14b2. built-in maps from the makers' MIDI charts; spare lanes are skipped by the randomiser
    {
        TrossSeqProcessor bp;
        auto find = [] (const char* n) { for (int i = 0; i < TrackMap::kNumPresets; ++i) if (juce::String (TrackMap::preset (i).name) == n) return i; return -1; };
        const int tr06 = find ("Roland TR-06"), tr08 = find ("Roland TR-08"), tr09 = find ("Roland TR-09"), uvi = find ("UVI Prime 8");
        const int adm = find ("AudioRealism ADM (ADM1X)");
        check (tr06 >= 0 && tr08 >= 0 && tr09 >= 0 && uvi >= 0 && adm >= 0 && TrackMap::kNumPresets == 14, "built-in maps: TR-06, TR-08, TR-09, UVI Prime 8, ADM added");
        bp.applyPreset (adm);
        check (bp.map.note (1) == 40 && bp.map.note (5) == 44 && bp.map.note (7) == 51 && bp.map.note (11) == 49 && bp.map.note (12) == 52
               && bp.getTrackName (12) == "PCF", "ADM: SD 40, CH 44, CY 51, CB 49, PCF 52 (unaccented keys)");
        for (int i = 0; i < 20; ++i) bp.randomise (TrossSeqProcessor::RandomOp::NewPattern);
        check (bp.patterns.get (0, 13) == 0 && bp.patterns.get (0, 11) == 0, "RANDOM never triggers ADM's PCF lane or the spare lane");
        bp.applyPreset (tr08);
        check (bp.map.note (0) == 36 && bp.map.note (8) == 37 && bp.map.note (10) == 70 && bp.map.note (11) == 56 && bp.map.note (12) == 75
               && bp.map.channel (0) == 1 && bp.getTrackName (10) == "MA", "TR-08: BD 36, RS 37, MA 70, CB 56, CL 75 on channel 1");
        bp.applyPreset (tr06);
        check (bp.map.note (2) == 47 && bp.map.note (3) == 50 && bp.map.channel (0) == 10 && bp.getTrackName (4) == "---", "TR-06: LT 47, HT 50, ch 10, spare lanes");
        bp.applyPreset (tr09);
        check (bp.map.note (9) == 47 && bp.map.note (10) == 51 && bp.getTrackName (11) == "---", "TR-09: MT 47, RC 51, two spare lanes");
        bp.applyPreset (uvi);
        check (bp.map.note (2) == 41 && bp.map.note (9) == 43 && bp.map.note (3) == 45 && bp.map.note (11) == 47 && bp.map.note (7) == 49,
               "UVI Prime 8: LT F1 41, MT G1 43, HT A1 45, CB B1 47, CY C#2 49");
        const int abl = find ("Ableton Drum Rack");
        bp.applyPreset (abl);
        bool inRack = true; std::set<int> used;
        for (int t = 0; t < kNumTracks; ++t) { inRack &= bp.map.note (t) >= 36 && bp.map.note (t) <= 51; used.insert (bp.map.note (t)); }
        check (abl >= 0 && inRack && used.size() == (size_t) kNumTracks && bp.map.note (0) == 36 && bp.map.note (10) == 51 && bp.getTrackName (10) == "RD",
               "Ableton Drum Rack: 13 different pads, all on C1..D#2 (BD C1, RD D#2)");
        const int rd8 = find ("Behringer RD-8"), rd9 = find ("Behringer RD-9"), rd6 = find ("Behringer RD-6"), tr6s = find ("Roland TR-6S"), dt2 = find ("Elektron Digitakt II");
        check (rd8 >= 0 && rd9 >= 0 && rd6 >= 0 && tr6s >= 0 && dt2 >= 0, "built-in maps: RD-8, RD-9, RD-6, TR-6S, Digitakt II");
        bp.applyPreset (rd8);
        check (bp.map.note (1) == 40 && bp.map.note (11) == 56 && bp.map.note (7) == 51 && bp.map.channel (0) == 1, "RD-8: SD 40, CB 56, CY 51, channel 1");
        bp.applyPreset (rd9);
        check (bp.map.note (7) == 49 && bp.map.note (10) == 51 && bp.getTrackName (11) == "---", "RD-9: CC 49, RC 51, no cowbell");
        bp.applyPreset (rd6);
        check (bp.map.note (2) == 45 && bp.map.note (4) == 39 && bp.getTrackName (8) == "---", "RD-6: LT 45, CP 39, spare lanes");
        bp.applyPreset (tr6s);
        check (bp.map.note (2) == 43 && bp.map.channel (0) == 10 && bp.getTrackName (3) == "---", "TR-6S: six instruments on channel 10");
        bp.applyPreset (dt2);
        bool perLane = true;
        for (int t = 0; t < kNumTracks; ++t) perLane &= bp.map.channel (t) == t + 1 && bp.map.note (t) == 60;
        check (perLane, "Digitakt II: lane N on channel N, note 60");
        bool branded = true;
        for (int i = 0; i < TrackMap::kNumPresets; ++i) branded &= juce::String (TrackMap::preset (i).brand).isNotEmpty();
        check (branded, "every built-in map has a brand for the menu");
        bp.applyPreset (tr06);
        bp.patterns.clear (0);
        for (int i = 0; i < 20; ++i) bp.randomise (TrossSeqProcessor::RandomOp::NewPattern);
        bool spareEmpty = true;
        for (int t : { 4, 8, 9, 10, 11, 12 }) spareEmpty &= bp.patterns.get (0, t + 1) == 0;
        check (spareEmpty && bp.patterns.get (0, 1) != 0, "RANDOM leaves the TR-06's spare lanes empty");
    }

    // 14d. pattern chain, locked to the song position
    {
        host.playing = false; run (2);
        setP ("seqlen", 16.0f);
        TrossSeqProcessor cp;
        FakeHost h2; cp.setPlayHead (&h2);
        cp.setRateAndBufferSizeDetails (sr, block); cp.prepareToPlay (sr, block);
        // patterns 1..3 each have a kick on a different step, so the notes tell which pattern played
        cp.patterns.clear (0); cp.patterns.clear (1); cp.patterns.clear (2);
        cp.patterns.setCell (0, 1, 0, true);   // pattern 1: BD step 1
        cp.patterns.setCell (1, 1, 4, true);   // pattern 2: BD step 5
        cp.patterns.setCell (2, 1, 8, true);   // pattern 3: BD step 9
        cp.chainAppend (0); cp.chainAppend (0); cp.chainAppend (1); cp.chainAppend (2);
        check (cp.getChainLength() == 4 && cp.getChainSlot (2) == 1, "chain built: 1 > 1 > 2 > 3");
        juce::AudioBuffer<float> b2 (2, block);
        juce::int64 t = 0; std::vector<juce::int64> kicks;
        auto runCp = [&] (int blocks)
        {
            for (int k = 0; k < blocks; ++k)
            {
                juce::MidiBuffer m; cp.processBlock (b2, m);
                for (const auto md : m) if (md.getMessage().isNoteOn() && md.getMessage().getNoteNumber() == 36) kicks.push_back (t + md.samplePosition);
                if (h2.playing) h2.ppq += block / sr * h2.bpm / 60.0;
                t += block;
            }
        };
        h2.playing = true; h2.ppq = 0.0;
        runCp (800);   // 8 s = 4 bars at 120 BPM
        // expected kick positions (bar = 96000 samples): bar1 step1, bar2 step1, bar3 step5, bar4 step9
        const std::vector<juce::int64> want { 0, 96000, 2 * 96000 + 24000, 3 * 96000 + 48000 };
        check (kicks == want, "chain plays 1, 1, 2, 3 bar by bar (" + juce::String ((int) kicks.size()) + " kicks)");
        // jump into bar 3 of the song: the chain picks pattern 2 straight away
        kicks.clear(); t = 0;
        h2.ppq = 8.0;   // start of bar 3
        runCp (200);
        check (kicks.size() == 1 && kicks[0] == 24000 && cp.getCurrentSlot() == 1 && cp.getChainPos() == 2,
               "jumping to bar 3 lands on chain step 3 (pattern 2)");
        // saved with the project; UNDO; a plain click clears it
        juce::MemoryBlock ms; cp.getStateInformation (ms);
        TrossSeqProcessor o3; o3.setStateInformation (ms.getData(), (int) ms.getSize());
        check (o3.getChain() == std::vector<int> { 0, 0, 1, 2 }, "chain saved with the project");
        cp.chainRemoveLast();
        check (cp.getChainLength() == 3, "remove the last chain step");
        cp.undoRandom();
        check (cp.getChainLength() == 4, "UNDO brings the chain step back");
        cp.chainClear();
        check (cp.getChainLength() == 0, "clear the chain");
        h2.playing = false;
        juce::MidiBuffer m; cp.processBlock (b2, m);
        MidiExport::Settings xs3;
        auto mf3 = MidiExport::make (o3.patterns, o3.map, o3.getChain(), xs3, "chain");
        double endT = 0; for (auto* e : *mf3.getTrack (0)) endT = juce::jmax (endT, e->message.getTimeStamp());
        check (std::abs (endT - 4 * 16 * 24) < 0.5, "export: the chain as one 4-bar clip");
    }

    // 14e. SOLO buttons: any S lit = only the soloed lanes play (even if muted); the held Shift-solo beats them
    {
        TrossSeqProcessor sp;
        FakeHost h3; sp.setPlayHead (&h3);
        sp.setRateAndBufferSizeDetails (sr, block); sp.prepareToPlay (sr, block);
        for (int sl = 0; sl < 2; ++sl) sp.patterns.clear (sl);
        sp.patterns.setCell (0, 1, 0, true);   // BD
        sp.patterns.setCell (0, 2, 4, true);   // SD
        sp.patterns.setCell (0, 5, 8, true);   // CP
        auto sP = [&] (const char* id, float v) { auto* q = sp.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        juce::AudioBuffer<float> b3 (2, block);
        auto bar = [&]
        {
            std::vector<int> notes;
            h3.playing = true; h3.ppq = 0.0;
            for (int k = 0; k < 200; ++k)
            {
                juce::MidiBuffer m; sp.processBlock (b3, m);
                for (const auto md : m) if (md.getMessage().isNoteOn()) notes.push_back (md.getMessage().getNoteNumber());
                h3.ppq += block / sr * h3.bpm / 60.0;
            }
            h3.playing = false; juce::MidiBuffer m; sp.processBlock (b3, m);
            return notes;
        };
        check (sp.apvts.getParameter ("solo1") != nullptr && sp.apvts.getParameter ("solo13") != nullptr, "13 SOLO parameters");
        check (bar() == std::vector<int> { 36, 38, 39 }, "no solo: BD, SD, CP play");
        sP ("solo2", 1.0f);
        check (bar() == std::vector<int> { 38 }, "S on the snare: only the snare plays");
        sP ("solo5", 1.0f); sP ("mute5", 1.0f);
        check (bar() == std::vector<int> { 38, 39 }, "two lanes soloed (one of them muted): both play, the solo beats the mute");
        sp.momentarySolo = 0;
        check (bar() == std::vector<int> { 36 }, "Shift-held M on BD beats the S buttons");
        sp.momentarySolo = -1;
        sP ("solo2", 0.0f); sP ("solo5", 0.0f);
        check (bar() == std::vector<int> { 36, 38 }, "solos off: the mute counts again");
        sP ("mute5", 0.0f);
    }

    // 14f. negative PORT DELAY: the port gets every note early, the host still gets it on the grid,
    // no step lost at the start and none doubled when the host loops
    {
        TrossSeqProcessor np;
        FakeHost h4; np.setPlayHead (&h4);
        np.setRateAndBufferSizeDetails (sr, block); np.prepareToPlay (sr, block);
        np.patterns.clear (0);
        for (int st : { 0, 4, 8, 12 }) np.patterns.setCell (0, 1, st, true);   // BD on 1, 5, 9, 13
        np.patterns.setCell (0, 2, 4, true); np.patterns.setFlam (0, 2, 4, true);   // flammed snare on 5
        auto* off = np.apvts.getParameter ("offset");
        check (off->getNormalisableRange().start <= -250.0f && off->getNormalisableRange().end >= 250.0f, "PORT DELAY goes from -250 to +250 ms");
        off->setValueNotifyingHost (off->convertTo0to1 (-50.0f));   // 2400 samples early
        std::vector<juce::int64> port, hostK, portSd, hostSd;
        juce::int64 t = 0;
        np.portSpy = [&] (const juce::MidiMessage& m, juce::int64 at)
        {
            if (m.isNoteOn() && m.getNoteNumber() == 36) port.push_back (at);
            if (m.isNoteOn() && m.getNoteNumber() == 38) portSd.push_back (at);
        };
        // the plug-in's own sample counter started at prepareToPlay; line it up with ours
        juce::AudioBuffer<float> b4 (2, block);
        h4.playing = true; h4.ppq = 0.0;
        int open = 0;
        auto runN = [&] (int blocks, double loopAt)   // loopAt: the host jumps back to 0 when it gets there
        {
            for (int k = 0; k < blocks; ++k)
            {
                juce::MidiBuffer m; np.processBlock (b4, m);
                for (const auto md : m)
                {
                    const auto mm = md.getMessage();
                    if (mm.isNoteOn()) ++open; else if (mm.isNoteOff()) --open;
                    if (mm.isNoteOn() && mm.getNoteNumber() == 36) hostK.push_back (t + md.samplePosition);
                    if (mm.isNoteOn() && mm.getNoteNumber() == 38) hostSd.push_back (t + md.samplePosition);
                }
                h4.ppq += block / sr * h4.bpm / 60.0;
                if (h4.ppq >= loopAt - 1e-9) h4.ppq -= loopAt;
                t += block;
            }
        };
        runN (600, 4.0);   // 3 times round a 1-bar loop
        std::vector<juce::int64> wantHost, wantPort;
        for (int b = 0; b < 3; ++b)
            for (int st : { 0, 4, 8, 12 })
            {
                wantHost.push_back (b * 96000 + st * 6000);
                wantPort.push_back (b * 96000 + st * 6000 - 2400);
            }
        wantPort[0] = 0;   // the very first step can't go out before the start
        wantPort.push_back (3 * 96000 - 2400);   // and the 4th time round is already on its way when we stop listening
        // the port spy counts from prepareToPlay, our t from the same moment
        auto list = [] (const std::vector<juce::int64>& v) { juce::String r; for (auto x : v) r << (int) x << " "; return r; };
        check (hostK == wantHost, "HOST OUT: kicks still on the grid, once each, over 3 loops (" + list (hostK) + ")");
        check (port == wantPort, "MIDI port: every kick 50 ms (2400 samples) early, once each, over 3 loops (" + list (port) + ")");
        check (portSd.size() == 6 && hostSd.size() == 6, "flammed snare: grace + main, once per loop, on both outputs ("
               + juce::String ((int) portSd.size()) + " / " + juce::String ((int) hostSd.size()) + ")");
        if (portSd.size() == 6 && hostSd.size() == 6)
            check (portSd[1] == 24000 - 2400 && hostSd[1] == 24000 && hostSd[0] - portSd[0] == 2400, "flam timing moves early with the port");
        // start in the middle of the song: the step at the start position plays, on time for the host
        h4.playing = false; { juce::MidiBuffer m; np.processBlock (b4, m); } t += block;
        port.clear(); hostK.clear();
        h4.ppq = 1.0; h4.playing = true;   // = step 5
        const auto startT = t;
        runN (50, 1000.0);   // just short of step 9 on the grid (24000)
        check (hostK.size() == 1 && hostK[0] == startT && port.size() == 2 && port[0] == startT && port[1] == startT + 24000 - 2400,
               "starting on step 5: it plays at once, step 9 goes to the port early (host " + list (hostK) + "/ port " + list (port) + "from " + juce::String ((int) startT) + ")");
        h4.playing = false; { juce::MidiBuffer m; np.processBlock (b4, m); }
        check (open == 0, "stopping ends every note, the held-back ones too");
        // positive delay still the same: host on the grid
        off->setValueNotifyingHost (off->convertTo0to1 (20.0f));
        hostK.clear(); t = 0; h4.ppq = 0.0; h4.playing = true;
        runN (200, 1000.0);
        check (hostK == std::vector<juce::int64> { 0, 24000, 48000, 72000 }, "positive PORT DELAY: host notes untouched");
    }

    // 14g. STUTTER: hold = the slice under the play position repeats; let go = back where the song is
    {
        TrossSeqProcessor st;
        FakeHost h5; st.setPlayHead (&h5);
        st.setRateAndBufferSizeDetails (sr, block); st.prepareToPlay (sr, block);
        st.patterns.clear (0);
        for (int x = 0; x < 16; ++x) st.patterns.setCell (0, 6, x, true);   // CH on every step: a clock to count with
        st.patterns.setCell (0, 1, 4, true);                                // BD on step 5
        st.patterns.setCell (0, 2, 5, true);                                // SD on step 6
        st.patterns.setCell (0, 1, 12, true);                               // BD on step 13
        auto sp = [&] (const char* id, float v) { auto* q = st.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        juce::AudioBuffer<float> b5 (2, block);
        std::vector<std::pair<juce::int64, int>> notes;   // (time, note)
        juce::int64 t = 0;
        auto runS = [&] (int blocks, std::function<void (juce::int64)> at = {})
        {
            for (int k = 0; k < blocks; ++k)
            {
                if (at) at (t);
                juce::MidiBuffer m; st.processBlock (b5, m);
                for (const auto md : m) if (md.getMessage().isNoteOn()) notes.push_back ({ t + md.samplePosition, md.getMessage().getNoteNumber() });
                h5.ppq += block / sr * h5.bpm / 60.0;
                t += block;
            }
        };
        check (st.apvts.getParameter ("stutter") != nullptr && st.apvts.getParameter ("sturate") != nullptr && st.apvts.getParameter ("sturoll") != nullptr,
               "STUTTER, Stutter Length and Stutter Roll parameters");
        // 1/8 (2 steps), pressed on step 5 (sample 24000), let go on step 13 (72000)
        sp ("sturate", 2.0f);
        h5.playing = true; h5.ppq = 0.0;
        runS (200, [&] (juce::int64 now) { if (now == 24000) sp ("stutter", 1.0f); if (now == 72000) sp ("stutter", 0.0f); });
        std::vector<juce::int64> bd, sd, ch;
        for (auto& n : notes) { if (n.second == 36) bd.push_back (n.first); if (n.second == 38) sd.push_back (n.first); if (n.second == 42) ch.push_back (n.first); }
        auto list = [] (const std::vector<juce::int64>& v) { juce::String r; for (auto x : v) r << (int) (x / 6000) << " "; return r; };
        check (bd == std::vector<juce::int64> { 24000, 36000, 48000, 60000, 72000 } && sd == std::vector<juce::int64> { 30000, 42000, 54000, 66000 },
               "1/8 stutter on steps 5-6 repeats them 4 times, then step 13 plays on time (BD at steps " + list (bd) + "/ SD " + list (sd) + ")");
        check (ch.size() == 16, "the hi-hat keeps every 16th through the stutter (" + juce::String ((int) ch.size()) + ")");
        // ROLL with 1/4: 4,4 then 2,2 then 1,1 then 1/32 ... hits get closer together
        notes.clear(); t = 0;
        h5.playing = false; { juce::MidiBuffer m; st.processBlock (b5, m); }
        sp ("sturate", 1.0f); sp ("sturoll", 1.0f);
        h5.playing = true; h5.ppq = 0.0;
        runS (260, [&] (juce::int64 now) { if (now == 24000) sp ("stutter", 1.0f); if (now == 120000) sp ("stutter", 0.0f); });
        bd.clear(); for (auto& n : notes) if (n.second == 36 && n.first >= 24000) bd.push_back (n.first);
        bool closer = bd.size() >= 6;
        for (size_t k = 2; closer && k < bd.size(); ++k) closer &= (bd[k] - bd[k - 1]) <= (bd[k - 1] - bd[k - 2]);
        check (closer && bd.size() >= 6 && bd[1] - bd[0] == 24000 && bd.back() - bd[bd.size() - 2] == 3000,
               "ROLL: the repeats get shorter, down to 1/32 (" + juce::String ((int) bd.size()) + " kicks)");
        h5.playing = false; { juce::MidiBuffer m; st.processBlock (b5, m); }
        check (! st.isStuttering(), "stopping ends the stutter");
        sp ("sturoll", 0.0f);
    }

    // 14h. TR-808 fill: Shift + FILL, then a pattern: it plays once from the next step 1, then back
    {
        TrossSeqProcessor fp;
        FakeHost h6; fp.setPlayHead (&h6);
        fp.setRateAndBufferSizeDetails (sr, block); fp.prepareToPlay (sr, block);
        for (int sl = 0; sl < 3; ++sl) fp.patterns.clear (sl);
        fp.patterns.setCell (0, 1, 0, true);   // pattern 1: BD
        fp.patterns.setCell (1, 2, 0, true);   // pattern 2: SD (the fill)
        fp.patterns.setCell (2, 5, 0, true);   // pattern 3: CP
        juce::AudioBuffer<float> b6 (2, block);
        std::vector<int> bars;   // the note on step 1 of each bar
        auto runF = [&] (int blocks, std::function<void (int)> at = {})
        {
            for (int k = 0; k < blocks; ++k)
            {
                if (at) at (k);
                juce::MidiBuffer m; fp.processBlock (b6, m);
                for (const auto md : m) if (md.getMessage().isNoteOn()) bars.push_back (md.getMessage().getNoteNumber());
                h6.ppq += block / sr * h6.bpm / 60.0;
            }
        };
        h6.playing = true; h6.ppq = 0.0;
        fp.fillArmed = true;
        runF (800, [&] (int k) { if (k == 50) fp.queueFill (1); });   // picked in the middle of bar 1
        check (bars == std::vector<int> { 36, 38, 36, 36 }, "fill pattern 2 plays once in bar 2, then pattern 1 again");
        check (! fp.fillArmed.load() && fp.getFillQueued() == -1 && ! fp.isFillPlaying() && fp.getCurrentSlot() == 0, "fill done: nothing left waiting");
        // with a chain (1 > 3): the fill takes one bar, the chain carries on from the song position
        bars.clear();
        h6.playing = false; { juce::MidiBuffer m; fp.processBlock (b6, m); }
        fp.chainAppend (0); fp.chainAppend (2);
        h6.playing = true; h6.ppq = 0.0;
        runF (800, [&] (int k) { if (k == 50) fp.queueFill (1); });
        check (bars == std::vector<int> { 36, 38, 36, 39 }, "with a chain: bar 2 is the fill, bars 3-4 follow the chain");
        fp.chainClear();
        // stopping in the middle of a fill goes back to the pattern
        bars.clear();
        h6.playing = false; { juce::MidiBuffer m; fp.processBlock (b6, m); }
        fp.selectSlot (0);
        h6.playing = true; h6.ppq = 0.0;
        fp.queueFill (2);
        runF (100);
        const bool wasFill = fp.isFillPlaying() && fp.getCurrentSlot() == 2;
        h6.playing = false; { juce::MidiBuffer m; fp.processBlock (b6, m); }
        check (wasFill && ! fp.isFillPlaying() && fp.getCurrentSlot() == 0, "stop during a fill: back to pattern 1");
    }

    // 14i. parameter locks: velocity, gate, pitch, chance, ratchet, nudge (early / late) and CCs, one step at a time
    {
        TrossSeqProcessor lp;
        FakeHost h7; lp.setPlayHead (&h7);
        lp.setRateAndBufferSizeDetails (sr, block); lp.prepareToPlay (sr, block);
        lp.patterns.clear (0);
        for (int st : { 0, 4, 8, 12 }) lp.patterns.setCell (0, 1, st, true);   // BD on 1, 5, 9, 13
        juce::AudioBuffer<float> b7 (2, block);
        struct E { juce::int64 t; juce::MidiMessage m; };
        std::vector<E> ev;
        juce::int64 t = 0;
        auto bar = [&]
        {
            ev.clear(); t = 0;
            h7.playing = false; { juce::MidiBuffer m; lp.processBlock (b7, m); }
            h7.playing = true; h7.ppq = 0.0;
            for (int k = 0; k < 200; ++k)
            {
                juce::MidiBuffer m; lp.processBlock (b7, m);
                for (const auto md : m) ev.push_back ({ t + md.samplePosition, md.getMessage() });
                h7.ppq += block / sr * h7.bpm / 60.0; t += block;
            }
        };
        auto ons = [&] { std::vector<E> r; for (auto& e : ev) if (e.m.isNoteOn()) r.push_back (e); return r; };
        bar();
        check (ons().size() == 4 && ons()[1].m.getVelocity() == 100, "no locks: 4 kicks at the knob velocity");
        lp.patterns.setLock (0, 0, 4, kLockVel, 37);
        lp.patterns.setLock (0, 0, 8, kLockPitch, 5);
        lp.patterns.setLock (0, 0, 12, kLockGate, 90);
        bar();
        {
            auto o = ons();
            juce::int64 off13 = -1;
            for (auto& e : ev) if (e.m.isNoteOff() && e.t >= 72000) { off13 = e.t; break; }
            check (o.size() == 4 && o[1].m.getVelocity() == 37 && o[0].m.getVelocity() == 100 && o[2].m.getNoteNumber() == 41 && o[3].m.getNoteNumber() == 36
                   && off13 == 72000 + 5400, "VELOCITY 37 on step 5, PITCH +5 on step 9 (note 41), GATE 90 % on step 13 (5400 samples)");
        }
        lp.patterns.clearLocks (0);
        lp.patterns.setLock (0, 0, 4, kLockChance, 0);
        lp.patterns.setLock (0, 0, 8, kLockChance, 100);
        bar();
        check (ons().size() == 3, "CHANCE 0 % never plays, 100 % always does");
        lp.patterns.setLock (0, 0, 4, kLockChance, 50);
        lp.seedChance (1234);
        int played = 0;
        for (int r = 0; r < 40; ++r) { bar(); for (auto& e : ons()) if (e.t == 24000) ++played; }
        check (played > 8 && played < 32, "CHANCE 50 %: plays about half the time (" + juce::String (played) + " of 40)");
        lp.patterns.clearLocks (0);
        lp.patterns.setLock (0, 0, 4, kLockRatchet, 3);
        bar();
        {
            std::vector<juce::int64> at; for (auto& e : ons()) at.push_back (e.t);
            check (at == std::vector<juce::int64> { 0, 24000, 26000, 28000, 48000, 72000 }, "RATCHET 3: three hits spread over step 5");
        }
        lp.patterns.clearLocks (0);
        lp.patterns.setLock (0, 0, 4, kLockNudge, 25);
        lp.patterns.setLock (0, 0, 8, kLockNudge, -40);
        bar();
        {
            std::vector<juce::int64> at; for (auto& e : ons()) at.push_back (e.t);
            check (at == std::vector<juce::int64> { 0, 24000 + 1500, 48000 - 2400, 72000 }, "NUDGE +25 % plays step 5 late, -40 % plays step 9 early");
        }
        lp.patterns.clearLocks (0);
        lp.map.setCcNumber (0, 0, 74); lp.map.setCcNumber (0, 2, 71);
        lp.patterns.setLock (0, 0, 4, kLockCC1, 99);
        lp.patterns.setLock (0, 0, 4, kLockCC3, 12);
        lp.patterns.setLock (0, 0, 8, kLockCC2, 64);   // CC 2 has no number: nothing sent
        bar();
        {
            std::vector<juce::String> cc; int noteAt5 = -1, idx = 0;
            for (auto& e : ev)
            {
                if (e.m.isController()) cc.push_back (juce::String (e.m.getControllerNumber()) + "=" + juce::String (e.m.getControllerValue()) + "@" + juce::String ((int) e.t));
                if (e.m.isNoteOn() && e.t == 24000 && noteAt5 < 0) noteAt5 = idx;
                ++idx;
            }
            int lastCc = -1; idx = 0; for (auto& e : ev) { if (e.m.isController()) lastCc = idx; ++idx; }
            check (cc == std::vector<juce::String> { "74=99@24000", "71=12@24000" } && lastCc < noteAt5 && ev[(size_t) lastCc].m.getChannel() == 10,
                   "CC 1 (74) and CC 3 (71) sent on step 5, just before the note, on the lane's channel");
        }
        lp.patterns.setLock (0, 0, 4, kLockVel, 50);
        juce::MemoryBlock mb; lp.getStateInformation (mb);
        TrossSeqProcessor lr; lr.setStateInformation (mb.getData(), (int) mb.getSize());
        check (lr.patterns.getLock (0, 0, 4, kLockVel) == 50 && lr.patterns.getLock (0, 0, 4, kLockCC1) == 99 && lr.map.ccNumber (0, 0) == 74
               && ! lr.patterns.isLocked (0, 0, 5, kLockVel), "locks and CC numbers saved with the project");
        lp.patterns.copySlot (0, 3);
        check (lp.patterns.getLock (3, 0, 4, kLockVel) == 50, "copying a pattern copies its locks");
        {   // EXPORT MIDI writes the locks
            TrossSeqProcessor xp;
            xp.patterns.clear (0);
            xp.patterns.setCell (0, 1, 0, true); xp.patterns.setCell (0, 1, 4, true); xp.patterns.setCell (0, 1, 8, true);
            xp.patterns.setLock (0, 0, 0, kLockVel, 33);
            xp.patterns.setLock (0, 0, 4, kLockRatchet, 2);
            xp.patterns.setLock (0, 0, 8, kLockPitch, -3);
            xp.patterns.setLock (0, 0, 8, kLockNudge, 50);
            xp.map.setCcNumber (0, 0, 74); xp.patterns.setLock (0, 0, 0, kLockCC1, 90);
            MidiExport::Settings xs;
            auto mf = MidiExport::make (xp.patterns, xp.map, { 0 }, xs, "locks");
            std::vector<juce::String> got;
            for (auto* e : *mf.getTrack (0))
            {
                const auto& m = e->message;
                if (m.isNoteOn()) got.push_back ("N" + juce::String (m.getNoteNumber()) + "v" + juce::String (m.getVelocity()) + "@" + juce::String ((int) m.getTimeStamp()));
                if (m.isController()) got.push_back ("C" + juce::String (m.getControllerNumber()) + "=" + juce::String (m.getControllerValue()) + "@" + juce::String ((int) m.getTimeStamp()));
            }
            juce::String all; for (auto& g2 : got) all << g2 << " ";
            check (got == std::vector<juce::String> { "C74=90@0", "N36v33@0", "N36v100@96", "N36v100@108", "N33v100@204" },
                   "EXPORT MIDI: velocity, CC, ratchet, pitch and nudge locks in the clip (" + all.trim() + ")");
        }
        lp.patterns.setLock (0, 0, 4, kLockVel, 500);
        check (lp.patterns.getLock (0, 0, 4, kLockVel) == 127, "lock values stay in range");
    }

    // 14c. drum map library
    {
        auto dir = outDir.getChildFile ("maps");
        dir.deleteRecursively();
        TrossSeqProcessor::mapsFolderOverride = dir;
        TrossSeqProcessor mp;
        check (mp.getMapName() == "General MIDI drums" && ! mp.isMapModified(), "a new instance starts on the GM map, unedited");
        mp.map.setNote (0, 35);
        check (mp.isMapModified(), "changing a note marks the map as edited");
        mp.setTrackName (0, "KICK2");
        mp.toggleLock (3);
        check (mp.saveMap ("My Drum Box") && mp.getMapName() == "My Drum Box" && ! mp.isMapModified(), "Save map as: file written, map named, no longer edited");
        check (TrossSeqProcessor::listMaps() == juce::StringArray { "My Drum Box" }, "the map shows up in the list");
        mp.applyPreset (1);
        check (mp.getMapName() == "Roland TR-8 / TR-8S" && mp.map.note (0) == 36, "picking a built-in map renames and resets the lanes");
        check (mp.loadMap ("My Drum Box") && mp.map.note (0) == 35 && mp.getTrackName (0) == "KICK2" && mp.locked[3].load()
               && ! mp.isMapModified(), "loading the saved map brings back notes, names and locks");
        check (TrossSeqProcessor::renameMap ("My Drum Box", "RD-8") && TrossSeqProcessor::listMaps() == juce::StringArray { "RD-8" },
               "rename a map");
        check (! TrossSeqProcessor::renameMap ("RD-8", "RD-8") && ! TrossSeqProcessor::renameMap ("nope", "x"), "rename refuses clashes and missing maps");
        mp.saveMap ("Second");
        mp.map.setChannel (2, 5);
        juce::MemoryBlock ms; mp.getStateInformation (ms);
        TrossSeqProcessor other;
        other.setStateInformation (ms.getData(), (int) ms.getSize());
        check (other.getMapName() == "Second" && other.isMapModified() && other.map.channel (2) == 5, "map name and the edited mark are saved with the project");
        check (TrossSeqProcessor::deleteMap ("RD-8") && TrossSeqProcessor::listMaps() == juce::StringArray { "Second" }, "delete a map");
        TrossSeqProcessor::mapsFolderOverride = juce::File();
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
    proc.chainAppend (0); proc.chainAppend (0); proc.chainAppend (1);   // a chain for the snapshot
    juce::Thread::sleep (400);   // let the name-strip flashes fade
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());

    // Shift-click on a pad: flam on / off; and the HELP switch
    {
        StepGrid* grid = nullptr;
        juce::TextButton* help = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            for (auto* ch : c.getChildren())
            {
                if (auto* gr = dynamic_cast<StepGrid*> (ch)) grid = gr;
                if (auto* tb = dynamic_cast<juce::TextButton*> (ch)) if (tb->getButtonText() == "HELP") help = tb;
                find (*ch);
            }
        };
        find (*ed);
        check (grid != nullptr && help != nullptr, "step grid and HELP button found");
        if (grid != nullptr)
        {
            const int sl = proc.getCurrentSlot();
            pats.setCell (sl, 3, 7, false);
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const auto pos = StepGrid::padRect (3, 7).getCentre();
            auto click = [&] (juce::ModifierKeys mods)
            {
                juce::MouseEvent e (src, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, grid, grid, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
                grid->mouseDown (e); grid->mouseUp (e);
            };
            const auto shift = juce::ModifierKeys (juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier);
            click (shift);
            check (pats.cell (sl, 3, 7) && pats.flam (sl, 3, 7), "Shift-click on an empty pad: step on with a flam");
            click (shift);
            check (pats.cell (sl, 3, 7) && ! pats.flam (sl, 3, 7), "Shift-click again: flam gone, step stays");
            click (shift);
            click (juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
            check (! pats.cell (sl, 3, 7) && ! pats.flam (sl, 3, 7), "plain click switches the step (and its flam) off");
            pats.setCell (sl, 3, 7, true); pats.setFlam (sl, 3, 7, true);   // leave some showing for the snapshot
            pats.setCell (sl, 2, 14, true); pats.setFlam (sl, 2, 14, true);
            { FlamStyle d; d.graces = 2; pats.setFlamStyle (sl, 2, 14, d.encode()); }
            pats.setCell (sl, 4, 15, true); pats.setFlam (sl, 4, 15, true);
            { FlamStyle r3; r3.graces = 3; r3.spacing = 3; pats.setFlamStyle (sl, 4, 15, r3.encode()); }
        }
        // name strips: Shift-click renames (double-click doesn't), a plain click plays and flashes, the lane playing doesn't flash
        {
            NameStrip* strip = nullptr;
            std::function<void (juce::Component&)> findStrip = [&] (juce::Component& c)
            {
                for (auto* ch : c.getChildren()) { if (auto* ns = dynamic_cast<NameStrip*> (ch)) { if (strip == nullptr) strip = ns; } findStrip (*ch); }
            };
            findStrip (*ed);
            check (strip != nullptr, "name strip found");
            if (strip != nullptr)
            {
                auto src = juce::Desktop::getInstance().getMainMouseSource();
                auto press = [&] (juce::ModifierKeys mods, int clicks)
                {
                    juce::MouseEvent e (src, { 5.0f, 5.0f }, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, strip, strip, juce::Time::getCurrentTime(), { 5.0f, 5.0f }, juce::Time::getCurrentTime(), clicks, false);
                    strip->mouseDown (e);
                };
                auto editorShown = [&] { for (auto* ch : strip->getChildren()) if (auto* te = dynamic_cast<juce::TextEditor*> (ch)) return te->isVisible(); return false; };
                const auto lb = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
                strip->clickTime = 0;
                proc.lastSent[0] = juce::Time::getMillisecondCounter();   // as if the lane just played
                check (strip->clickTime == 0, "a lane playing doesn't make its name flash");
                press (lb, 2);
                check (! editorShown() && strip->clickTime != 0, "double-click just plays (no rename)");
                press (juce::ModifierKeys (juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier), 1);
                check (editorShown(), "Shift-click opens the rename box");
                for (auto* ch : strip->getChildren()) if (auto* te = dynamic_cast<juce::TextEditor*> (ch)) { te->setText ("KICK", false); te->onEscapeKey(); }
                strip->clickTime = 0;
            }
        }

        if (help != nullptr)
        {
            auto countTips = [&] { int n = 0; for (auto* ch : ed->getChildren()) if (dynamic_cast<juce::TooltipWindow*> (ch)) ++n; return n; };
            const bool wasOn = help->getToggleState();
            if (! wasOn) { help->setToggleState (true, juce::dontSendNotification); help->onClick(); }
            const int on = countTips();
            help->setToggleState (false, juce::sendNotificationSync);
            help->onClick();
            const int off = countTips();
            help->setToggleState (true, juce::sendNotificationSync);
            help->onClick();
            check (on == 1 && off == 0 && countTips() == 1, "HELP off removes the hover tips, on brings them back");
            if (! wasOn) { help->setToggleState (false, juce::dontSendNotification); help->onClick(); }
        }
    }

    // About window: click the logo to open, click to close; snapshot it
    {
        LogoButton* logo = nullptr; AboutOverlay* about = nullptr;
        std::function<void (juce::Component&)> findA = [&] (juce::Component& c)
        {
            for (auto* ch : c.getChildren())
            {
                if (auto* l = dynamic_cast<LogoButton*> (ch)) logo = l;
                if (auto* a = dynamic_cast<AboutOverlay*> (ch)) about = a;
                findA (*ch);
            }
        };
        findA (*ed);
        check (logo != nullptr && about != nullptr && ! about->isVisible(), "logo click area and hidden About window present");
        if (logo != nullptr && about != nullptr)
        {
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            juce::MouseEvent e (src, { 20.0f, 20.0f }, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, logo, logo,
                                juce::Time::getCurrentTime(), { 20.0f, 20.0f }, juce::Time::getCurrentTime(), 1, false);
            logo->mouseUp (e);
            check (about->isVisible(), "clicking the TRoss SEQ logo opens About");
            check (AboutOverlay::formatAndHost (proc).isNotEmpty(), "About shows the format: " + AboutOverlay::formatAndHost (proc));
            check (about->manualBtn.isVisible() && about->card().contains (about->manualBtn.getBounds().toFloat()), "OPEN THE MANUAL button inside the About card");
            {
                const auto mf = AboutOverlay::writeManual();
                juce::MemoryBlock head; mf.loadFileAsData (head);
                check (mf.existsAsFile() && mf.getSize() > 100000 && head.getSize() > 5 && std::memcmp (head.getData(), "%PDF-", 5) == 0,
                       "built-in manual written as a PDF (" + juce::String (mf.getSize() / 1024) + " KB) to " + mf.getFileName());
            }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            auto f = outDir.getChildFile ("about_2x.png"); f.deleteFile();
            if (auto os = f.createOutputStream()) juce::PNGImageFormat().writeImageToStream (img, *os);
            check (juce::String (AboutOverlay::kContact) == "ticcoross@gmail.com" && about->contactLink.isVisible()
                   && about->card().contains (about->contactLink.getBounds().toFloat())
                   && about->contactLink.getURL().toString (false) == "mailto:ticcoross@gmail.com",
                   "About: contact ticcoross@gmail.com, a mail link inside the card");
            about->mouseDown (e);
            check (! about->isVisible(), "clicking closes About");
        }
    }

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
    // the S buttons: one per lane, next to M, each switches its lane's solo
    {
        std::vector<juce::Button*> solos;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            for (auto* ch : c.getChildren()) { if (ch->getComponentID().startsWith ("solo")) if (auto* b = dynamic_cast<juce::Button*> (ch)) solos.push_back (b); find (*ch); }
        };
        find (*ed);
        check (solos.size() == 13, "13 S buttons on the panel (" + juce::String ((int) solos.size()) + ")");
        if (solos.size() == 13)
        {
            solos[3]->setToggleState (true, juce::sendNotificationSync);
            const bool on = proc.apvts.getRawParameterValue ("solo4")->load() > 0.5f;
            solos[3]->setToggleState (false, juce::sendNotificationSync);
            check (on && proc.apvts.getRawParameterValue ("solo4")->load() < 0.5f, "S on: lane 4 soloed, S off: not");
            solos[1]->setToggleState (true, juce::sendNotificationSync);   // leave one lit for the snapshot
        }
    }
    // armed FILL: the next pattern button picks the fill (no pattern change, no chain)
    {
        PatternPad* pad = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        { for (auto* ch : c.getChildren()) { if (auto* pp = dynamic_cast<PatternPad*> (ch)) pad = pp; find (*ch); } };
        find (*ed);
        if (pad != nullptr)
        {
            const auto selBefore = proc.apvts.getRawParameterValue ("pattern")->load();
            const int chainBefore = proc.getChainLength();
            proc.fillArmed = true;
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const auto pos = PatternPad::buttonRect (4).getCentre();
            juce::MouseEvent e (src, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, pad, pad,
                                juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
            pad->mouseDown (e);
            check (proc.getFillQueued() == 4 && ! proc.fillArmed.load() && proc.apvts.getRawParameterValue ("pattern")->load() == selBefore
                   && proc.getChainLength() == chainBefore, "FILL armed, click pattern 5: it is queued as the fill, the playing pattern stays (" + juce::String (proc.getFillQueued()) + " " + juce::String ((int) proc.fillArmed.load()) + " " + juce::String (proc.getChainLength()) + ")");
            proc.cancelFill();
        }
        else check (false, "pattern pad found");
    }
    // the LOCKS page: STEPS / LOCKS keys swap the zone; drag on a bar locks, double-click unlocks; CC numbers per lane
    if (auto* te = dynamic_cast<TrossSeqEditor*> (ed.get()))
    {
        te->locksBtn.onClick();
        check (te->isLockMode() && te->lockPage.isVisible() && ! te->lockPage.getBounds().isEmpty(), "LOCKS key: the lock page replaces the grid");
        auto& lpg = te->lockPage;
        const int sl = proc.getCurrentSlot();
        proc.patterns.clearLocks (sl);
        lpg.lane = 5; lpg.param = kLockVel;
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto mev = [&] (juce::Point<float> p, juce::ModifierKeys mods, int clicks = 1)
        {
            return juce::MouseEvent (src, p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &lpg, &lpg, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), clicks, false);
        };
        const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
        const auto b2 = LockPage::barRect (2), b6 = LockPage::barRect (6);
        lpg.mouseDown (mev ({ b2.getCentreX(), b2.getY() + 5.0f }, left));          // top of the bar: 127
        lpg.mouseDrag (mev ({ b6.getCentreX(), b6.getBottom() - 5.0f }, left));     // drawn across to the bottom: 1
        lpg.mouseUp (mev ({ b6.getCentreX(), b6.getBottom() - 5.0f }, left));
        bool drawn = true;
        for (int s2 = 2; s2 <= 6; ++s2) drawn &= proc.patterns.getLock (sl, 5, s2, kLockVel) == 1;
        check (drawn && ! proc.patterns.isLocked (sl, 5, 1, kLockVel) && ! proc.patterns.isLocked (sl, 5, 7, kLockVel),
               "drag across the bars draws locks on steps 3-7");
        lpg.mouseDown (mev ({ b2.getCentreX(), b2.getY() + 5.0f }, left));
        lpg.mouseUp (mev ({ b2.getCentreX(), b2.getY() + 5.0f }, left));
        check (proc.patterns.getLock (sl, 5, 2, kLockVel) == 127, "click at the top of a bar: VELOCITY 127");
        lpg.mouseDoubleClick (mev ({ b2.getCentreX(), b2.getCentreY() }, left, 2));
        check (! proc.patterns.isLocked (sl, 5, 2, kLockVel), "double-click a bar: back to the knob");
        proc.undoRandom();
        check (proc.patterns.getLock (sl, 5, 2, kLockVel) == 1, "UNDO takes back the last lock edit");
        // CC: no number = nothing drawn; drag the CC box up to pick one
        lpg.param = kLockCC1;
        proc.map.setCcNumber (5, 0, -1);
        lpg.mouseDown (mev ({ b2.getCentreX(), b2.getY() + 5.0f }, left)); lpg.mouseUp (mev ({ b2.getCentreX(), b2.getY() + 5.0f }, left));
        const bool noCc = ! proc.patterns.isLocked (sl, 5, 2, kLockCC1);
        const auto cr = LockPage::ccDisplayRect (0);
        lpg.mouseDown (mev (cr.getCentre(), left));
        lpg.mouseDrag (mev (cr.getCentre().translated (0.0f, -24.0f), left));
        lpg.mouseUp (mev (cr.getCentre().translated (0.0f, -24.0f), left));
        const int picked = proc.map.ccNumber (5, 0);
        lpg.mouseDown (mev ({ b2.getCentreX(), b2.getY() + 5.0f }, left)); lpg.mouseUp (mev ({ b2.getCentreX(), b2.getY() + 5.0f }, left));
        check (noCc && picked >= 0 && proc.patterns.getLock (sl, 5, 2, kLockCC1) == 127, "CC 1 needs a number first (dragged to " + juce::String (picked) + "), then it locks");
        proc.map.setCcNumber (5, 0, 74); proc.map.setCcNumber (5, 1, 71);
        lpg.param = kLockVel;
        for (int s2 = 0; s2 < 16; s2 += 2) proc.patterns.setLock (sl, 5, s2, kLockVel, (int16_t) (30 + s2 * 6));
        lpg.touched = 4;
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = outDir.getChildFile ("locks_2x.png"); f.deleteFile();
        if (auto os = f.createOutputStream()) juce::PNGImageFormat().writeImageToStream (img, *os);
        te->stepsBtn.onClick();
        check (! te->isLockMode() && ! te->lockPage.isVisible(), "STEPS key: back to the grid");
    }
    // STUTTER button: on while held, off on release
    if (auto* te = dynamic_cast<TrossSeqEditor*> (ed.get()))
    {
        te->stutterBtn.setState (juce::Button::buttonDown);
        const bool on = proc.apvts.getRawParameterValue ("stutter")->load() > 0.5f;
        te->stutterBtn.setState (juce::Button::buttonNormal);
        check (on && proc.apvts.getRawParameterValue ("stutter")->load() < 0.5f && te->stutRateBox.getText() == "1/8",
               "STUTTER button: on while held, off when let go; length 1/8 by default");
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
