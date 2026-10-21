// Turns TRoss Seq patterns into standard MIDI files, using each track's note and channel.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SeqCore.h"

namespace trossseq
{
struct MidiExport
{
    static constexpr int kTicksPerQuarter = 96, kTicksPerStep = 24;

    struct Settings
    {
        int length = 16;
        double bpm = 120.0;
        int velocity = 100, accentVelocity = 127;
        float gate = 0.5f;     // fraction of a step
        float swing = 50.0f;   // %
        float flamMs = 25.0f;     // FLAM knob: grace notes this long before a flammed step
        float flamLevel = 60.0f;  // FLAM LEVEL knob: grace velocity, % of the step's
    };

    // One track: the given slots one after another, each `length` steps long.
    static juce::MidiFile make (const PatternMemory& p, const TrackMap& map, const std::vector<int>& slots,
                                const Settings& s, const juce::String& name)
    {
        juce::MidiMessageSequence seq;
        auto meta = [&] (juce::MidiMessage m) { m.setTimeStamp (0); seq.addEvent (m); };
        meta (juce::MidiMessage::textMetaEvent (3, name));
        meta (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / juce::jlimit (20.0, 999.0, s.bpm))));
        meta (juce::MidiMessage::timeSignatureMetaEvent (4, 4));

        const double sw = StepClock::swingFromPercent (s.swing);
        auto msToTicks = [&] (float ms) { return juce::jlimit (1, kTicksPerStep / 2, juce::roundToInt (ms / 1000.0 * s.bpm / 60.0 * kTicksPerQuarter)); };
        const int gateTicks = juce::jlimit (1, kTicksPerStep - 1, juce::roundToInt (s.gate * kTicksPerStep));
        int start = 0;
        for (int slot : slots)
        {
            for (int st = 0; st < s.length; ++st)
            {
                const bool accent = p.cell (slot, 0, st);
                const int t = start + juce::roundToInt ((st + ((st & 1) ? sw : 0.0)) * kTicksPerStep);
                for (int tr = 0; tr < kNumTracks; ++tr)
                    if (p.cell (slot, tr + 1, st))
                    {
                        // parameter locks: velocity, gate, pitch, nudge, ratchet and CCs go into the clip;
                        // CHANCE can't be written into a clip, so a step plays unless its chance is 0 %
                        auto lk = [&] (int q) { return p.getLock (slot, tr, st, q); };
                        if (lk (kLockChance) == 0) continue;
                        int vel = juce::jlimit (1, 127, accent ? s.accentVelocity : s.velocity);
                        if (lk (kLockVel) != kNoLock) vel = lk (kLockVel);
                        const int nn = juce::jlimit (0, 127, map.note (tr) + (lk (kLockPitch) != kNoLock ? lk (kLockPitch) : 0));
                        const int tn = juce::jmax (0, t + (lk (kLockNudge) != kNoLock ? juce::roundToInt (lk (kLockNudge) / 100.0 * kTicksPerStep) : 0));
                        int gt = lk (kLockGate) != kNoLock ? juce::jlimit (1, kTicksPerStep - 1, juce::roundToInt (lk (kLockGate) / 100.0 * kTicksPerStep)) : gateTicks;
                        for (int k = 0; k < 4; ++k)
                            if (map.ccNumber (tr, k) >= 0 && lk (kLockCC1 + k) != kNoLock)
                                seq.addEvent (juce::MidiMessage::controllerEvent (map.channel (tr), map.ccNumber (tr, k), lk (kLockCC1 + k)), tn);
                        const int hits = lk (kLockRatchet) != kNoLock ? juce::jlimit (1, 4, (int) lk (kLockRatchet)) : 1;
                        if (p.flam (slot, tr + 1, st))   // grace notes just before, softer (flam 1, drag 2, ruff 3)
                        {
                            const auto fs = p.flamStyle (slot, tr + 1, st);
                            const int span = msToTicks (fs.spacingMs (s.flamMs));
                            const int gap = juce::jmax (1, span / fs.graces);
                            const auto gv = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (vel * fs.levelPct (s.flamLevel) / 100.0f));
                            for (int k = 0; k < fs.graces; ++k)
                            {
                                const int g = juce::jmax (0, t - (span - k * gap));
                                seq.addEvent (juce::MidiMessage::noteOn (map.channel (tr), map.note (tr), gv), g);
                                seq.addEvent (juce::MidiMessage::noteOff (map.channel (tr), map.note (tr)), g + juce::jmax (1, gap * 3 / 4));
                            }
                        }
                        const int spacing = kTicksPerStep / hits;
                        if (hits > 1) gt = juce::jmin (gt, juce::jmax (1, spacing * 3 / 4));
                        for (int k = 0; k < hits; ++k)
                        {
                            seq.addEvent (juce::MidiMessage::noteOn (map.channel (tr), nn, (juce::uint8) vel), tn + k * spacing);
                            seq.addEvent (juce::MidiMessage::noteOff (map.channel (tr), nn), tn + k * spacing + gt);
                        }
                    }
            }
            start += s.length * kTicksPerStep;
        }
        seq.updateMatchedPairs();
        seq.addEvent (juce::MidiMessage::endOfTrack(), start);

        juce::MidiFile file;
        file.setTicksPerQuarterNote (kTicksPerQuarter);
        file.addTrack (seq);
        return file;
    }

    static bool write (const juce::MidiFile& f, const juce::File& dest)
    {
        dest.deleteFile();
        juce::FileOutputStream out (dest);
        return out.openedOk() && f.writeTo (out, 0);
    }

    static juce::String fileName (int slot) { return "TRoss Seq Pattern " + juce::String (slot + 1) + ".mid"; }
};
} // namespace trossseq
