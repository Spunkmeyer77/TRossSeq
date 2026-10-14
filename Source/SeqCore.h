// TRoss Seq core: pattern memory, the drum-track map (note + channel per row) and the step clock.
// No JUCE in here, so it can be tested on its own.
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>

namespace trossseq
{
// sequencer rows: 0 = accent, 1..8 = drum tracks (row = track + 1)
constexpr int kNumTracks = 8, kNumRows = kNumTracks + 1, kNumSteps = 16, kNumSlots = 12;

// ---- 12 slots x 9 rows x 16 steps, one 16-bit mask per row (same layout as TRoss 606).
// Edited from the UI thread, read and recorded into from the audio thread, so every row is atomic.
class PatternMemory
{
public:
    uint16_t get (int slot, int row) const          { return masks[slot][row].load (std::memory_order_relaxed); }
    void set (int slot, int row, uint16_t m)       { masks[slot][row].store (m, std::memory_order_relaxed); ++version; }
    bool cell (int slot, int row, int step) const  { return (get (slot, row) >> step) & 1; }
    void setCell (int slot, int row, int step, bool on)
    {
        auto m = get (slot, row);
        m = on ? (uint16_t) (m | (1u << step)) : (uint16_t) (m & ~(1u << step));
        set (slot, row, m);
    }
    void clear (int slot) { for (int r = 0; r < kNumRows; ++r) set (slot, r, 0); }
    void copySlot (int from, int to) { for (int r = 0; r < kNumRows; ++r) set (to, r, get (from, r)); }
    bool isEmpty (int slot) const
    {
        for (int r = 1; r < kNumRows; ++r)   // the accent row alone plays nothing
            if (get (slot, r) != 0) return false;
        return true;
    }
    int getVersion() const { return version.load(); }

private:
    std::atomic<uint16_t> masks[kNumSlots][kNumRows] {};
    std::atomic<int> version { 0 };
};

// ---- which MIDI note and channel each track plays. Atomic: set from the UI (or MIDI learn on the
// audio thread), read on the audio thread.
class TrackMap
{
public:
    TrackMap() { setPreset (0); }

    int  note (int t) const        { return notes[t].load (std::memory_order_relaxed); }
    int  channel (int t) const     { return channels[t].load (std::memory_order_relaxed); }   // 1..16
    void setNote (int t, int n)    { notes[t].store (n < 0 ? 0 : (n > 127 ? 127 : n)); ++version; }
    void setChannel (int t, int c) { channels[t].store (c < 1 ? 1 : (c > 16 ? 16 : c)); ++version; }
    void setAllChannels (int c)    { for (int t = 0; t < kNumTracks; ++t) setChannel (t, c); }
    int  getVersion() const        { return version.load(); }

    // the track that an incoming note belongs to: same note and channel first, then same note on any channel
    int trackFor (int noteNumber, int ch) const
    {
        for (int t = 0; t < kNumTracks; ++t) if (note (t) == noteNumber && channel (t) == ch) return t;
        for (int t = 0; t < kNumTracks; ++t) if (note (t) == noteNumber) return t;
        return -1;
    }

    struct Preset { const char* name; int channel; int notes[kNumTracks]; const char* labels[kNumTracks]; };
    static constexpr int kNumPresets = 3;
    static const Preset& preset (int i)
    {
        static const Preset p[kNumPresets] = {
            { "General MIDI drums", 10, { 36, 38, 45, 50, 39, 42, 46, 49 },
              { "BD", "SD", "LT", "HT", "CP", "CH", "OH", "CY" } },
            { "Roland TR-8 / TR-8S", 10, { 36, 38, 43, 50, 39, 42, 46, 49 },
              { "BD", "SD", "LT", "HT", "CP", "CH", "OH", "CC" } },
            { "Korg volca beats", 10, { 36, 38, 43, 50, 39, 42, 46, 49 },
              { "KICK", "SNARE", "LO TOM", "HI TOM", "CLAP", "CL HAT", "OP HAT", "CRASH" } },
        };
        return p[i < 0 || i >= kNumPresets ? 0 : i];
    }
    void setPreset (int i)
    {
        const auto& p = preset (i);
        for (int t = 0; t < kNumTracks; ++t) { notes[t].store (p.notes[t]); channels[t].store (p.channel); }
        ++version;
    }

private:
    std::atomic<int> notes[kNumTracks] {}, channels[kNumTracks] {};
    std::atomic<int> version { 0 };
};

// ---- step clock with swing. Positions are in 16ths (ppq * 4). With swing, every second 16th
// (steps 2, 4, ...) is pushed later: 50 % = straight, 66.7 % = triplet feel, 75 % = maximum.
struct StepClock
{
    double swingAmount = 0.0;            // 0 .. 0.5 of a 16th
    long long nextStep = 0;
    bool running = false;

    static double swingFromPercent (double pct) { return std::fmax (0.0, std::fmin (0.5, (pct - 50.0) / 50.0)); }
    double stepTime (long long n) const { return (double) n + ((n & 1) ? swingAmount : 0.0); }

    void stop() { running = false; }

    // the transport is at `pos`; the next step to fire is the first one at or after it
    void syncTo (double pos)
    {
        long long n = (long long) std::floor (pos) - 1;
        while (stepTime (n) < pos - 1e-7) ++n;
        nextStep = n;
        running = true;
    }

    // Called once per sample (or per position): returns the step that fires at `pos`, or -1.
    // Handles loops and jumps by re-syncing when the position moves backwards or skips ahead.
    long long advance (double pos)
    {
        if (! running) syncTo (pos);
        const double prev = stepTime (nextStep - 1);
        if (pos < prev - 1e-6 || pos > stepTime (nextStep) + 1.0) syncTo (pos);   // jumped
        if (stepTime (nextStep) <= pos + 1e-9) return nextStep++;
        return -1;
    }
};

inline int wrapStep (long long x, int len) { return (int) (((x % len) + len) % len); }
} // namespace trossseq
