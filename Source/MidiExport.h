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
                        const auto vel = (juce::uint8) juce::jlimit (1, 127, accent ? s.accentVelocity : s.velocity);
                        seq.addEvent (juce::MidiMessage::noteOn (map.channel (tr), map.note (tr), vel), t);
                        seq.addEvent (juce::MidiMessage::noteOff (map.channel (tr), map.note (tr)), t + gateTicks);
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
