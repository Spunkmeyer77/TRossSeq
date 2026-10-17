// TRoss Seq core: pattern memory, the drum-track map (note + channel per row) and the step clock.
// No JUCE in here, so it can be tested on its own.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace trossseq
{
// sequencer rows: 0 = accent, 1..8 = drum tracks (row = track + 1)
constexpr int kNumTracks = 13, kNumRows = kNumTracks + 1, kNumSteps = 16, kNumSlots = 12;

// ---- 12 slots x 9 rows x 16 steps, one 16-bit mask per row (same layout as TRoss 606).
// ---- how a flam plays, per step: one byte.
//   bits 0-1: grace notes  (0 = flam: 1, 1 = flam: 1, 2 = drag: 2, 3 = ruff: 3)
//   bits 2-4: spacing      (0 = FLAM knob, 1..4 = the kSpacingMs presets)
//   bits 5-7: grace level  (0 = FLAM LEVEL knob, 1..4 = the kLevelPct presets)
struct FlamStyle
{
    static constexpr int kNumSpacings = 5, kNumLevels = 5;
    static constexpr float kSpacingMs[kNumSpacings] = { 0.0f, 12.0f, 25.0f, 40.0f, 60.0f };   // [0] unused (knob)
    static constexpr float kLevelPct[kNumLevels]    = { 0.0f, 30.0f, 50.0f, 75.0f, 100.0f };  // [0] unused (knob)
    static constexpr float kMaxSpacingMs = 80.0f;   // the FLAM knob's top; sets how far ahead grace notes are looked for

    int graces = 1, spacing = 0, level = 0;

    static FlamStyle decode (uint8_t b)
    {
        FlamStyle f;
        f.graces = (b & 3) <= 1 ? 1 : (b & 3);
        f.spacing = std::min ((b >> 2) & 7, kNumSpacings - 1);
        f.level = std::min ((b >> 5) & 7, kNumLevels - 1);
        return f;
    }
    uint8_t encode() const
    {
        const int g = graces <= 1 ? 0 : std::min (graces, 3);
        return (uint8_t) (g | (std::clamp (spacing, 0, kNumSpacings - 1) << 2) | (std::clamp (level, 0, kNumLevels - 1) << 5));
    }
    float spacingMs (float knobMs) const  { return spacing == 0 ? knobMs : kSpacingMs[spacing]; }
    float levelPct (float knobPct) const  { return level == 0 ? knobPct : kLevelPct[level]; }
};

// Edited from the UI thread, read and recorded into from the audio thread, so every row is atomic.
class PatternMemory
{
public:
    uint16_t get (int slot, int row) const          { return masks[slot][row].load (std::memory_order_relaxed); }
    // writing a row also drops flams on steps that are now off
    void set (int slot, int row, uint16_t m)
    {
        masks[slot][row].store (m, std::memory_order_relaxed);
        const auto before = flams[slot][row].load (std::memory_order_relaxed);
        flams[slot][row].store ((uint16_t) (before & m), std::memory_order_relaxed);
        resetStyles (slot, row, (uint16_t) (before & ~m));
        ++version;
    }

    // ---- flams: a grace note just before the step (drum rows only; a flam needs its step on)
    uint16_t getFlams (int slot, int row) const     { return flams[slot][row].load (std::memory_order_relaxed); }
    void setFlams (int slot, int row, uint16_t m)
    {
        const auto before = flams[slot][row].load (std::memory_order_relaxed);
        const auto now = (uint16_t) (m & get (slot, row));
        flams[slot][row].store (now, std::memory_order_relaxed);
        resetStyles (slot, row, (uint16_t) (before & ~now));   // a flam that's gone forgets its style
        ++version;
    }
    // the flam's type / spacing / grace level (see FlamStyle)
    uint8_t getFlamStyle (int slot, int row, int step) const  { return styles[slot][row][step].load (std::memory_order_relaxed); }
    void setFlamStyle (int slot, int row, int step, uint8_t b) { styles[slot][row][step].store (b, std::memory_order_relaxed); ++version; }
    FlamStyle flamStyle (int slot, int row, int step) const   { return FlamStyle::decode (getFlamStyle (slot, row, step)); }
    bool flam (int slot, int row, int step) const  { return (getFlams (slot, row) >> step) & 1; }
    void setFlam (int slot, int row, int step, bool on)
    {
        if (row == 0) return;   // no flams on the accent row
        auto m = getFlams (slot, row);
        m = on ? (uint16_t) (m | (1u << step)) : (uint16_t) (m & ~(1u << step));
        setFlams (slot, row, m);
    }
    bool cell (int slot, int row, int step) const  { return (get (slot, row) >> step) & 1; }
    void setCell (int slot, int row, int step, bool on)
    {
        auto m = get (slot, row);
        m = on ? (uint16_t) (m | (1u << step)) : (uint16_t) (m & ~(1u << step));
        set (slot, row, m);
    }
    void clear (int slot) { for (int r = 0; r < kNumRows; ++r) set (slot, r, 0); }
    void copySlot (int from, int to)
    {
        for (int r = 0; r < kNumRows; ++r)
        {
            set (to, r, get (from, r));
            setFlams (to, r, getFlams (from, r));
            for (int s = 0; s < kNumSteps; ++s) setFlamStyle (to, r, s, getFlamStyle (from, r, s));
        }
    }
    bool isEmpty (int slot) const
    {
        for (int r = 1; r < kNumRows; ++r)   // the accent row alone plays nothing
            if (get (slot, r) != 0) return false;
        return true;
    }
    int getVersion() const { return version.load(); }

private:
    std::atomic<uint16_t> masks[kNumSlots][kNumRows] {};
    std::atomic<uint16_t> flams[kNumSlots][kNumRows] {};
    std::atomic<uint8_t> styles[kNumSlots][kNumRows][kNumSteps] {};
    std::atomic<int> version { 0 };

    void resetStyles (int slot, int row, uint16_t gone)
    {
        for (int s = 0; s < kNumSteps; ++s)
            if ((gone >> s) & 1) styles[slot][row][s].store (0, std::memory_order_relaxed);
    }
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

    // Built-in maps. kind: 0 = General MIDI, 1 = hardware, 2 = software.
    // Lanes a machine doesn't have are named "---" (note 0): spare, and skipped by the randomiser.
    // Sources: each maker's MIDI implementation chart / manual (see the manual's reference section).
    struct Preset { const char* name; int kind; int channel; int notes[kNumTracks]; const char* labels[kNumTracks]; };
    static constexpr int kNumPresets = 9;
    static const Preset& preset (int i)
    {
        static const Preset p[kNumPresets] = {
            { "General MIDI drums", 0, 10, { 36, 38, 45, 50, 39, 42, 46, 49, 37, 47, 51, 56, 75 },
              { "BD", "SD", "LT", "HT", "CP", "CH", "OH", "CY", "RS", "MT", "RD", "CB", "CL" } },
            { "Roland TR-8 / TR-8S", 1, 10, { 36, 38, 43, 50, 39, 42, 46, 49, 37, 47, 51, 56, 75 },
              { "BD", "SD", "LT", "HT", "HC", "CH", "OH", "CC", "RS", "MT", "RC", "CB", "CL" } },
            { "Korg volca beats", 1, 10, { 36, 38, 43, 50, 39, 42, 46, 49, 75, 67, 37, 47, 51 },
              { "KICK", "SNARE", "LO TOM", "HI TOM", "CLAP", "CL HAT", "OP HAT", "CRASH", "CLAVES", "AGOGO", "RIM", "MID TOM", "RIDE" } },
            // Roland TR-06 MIDI implementation chart: ch 10
            { "Roland TR-06", 1, 10, { 36, 38, 47, 50, 0, 42, 46, 49, 0, 0, 0, 0, 0 },
              { "BD", "SD", "LT", "HT", "---", "CH", "OH", "CY", "---", "---", "---", "---", "---" } },
            // Roland TR-08 MIDI implementation chart: ch 1. Congas (HC 62, MC 63, LC 64) left out: set a lane to them by hand
            { "Roland TR-08", 1, 1, { 36, 38, 43, 50, 39, 42, 46, 49, 37, 47, 70, 56, 75 },
              { "BD", "SD", "LT", "HT", "CP", "CH", "OH", "CY", "RS", "MT", "MA", "CB", "CL" } },
            // Roland TR-09 MIDI implementation chart: ch 10
            { "Roland TR-09", 1, 10, { 36, 38, 43, 50, 39, 42, 46, 49, 37, 47, 51, 0, 0 },
              { "BD", "SD", "LT", "HT", "HC", "CH", "OH", "CC", "RS", "MT", "RC", "---", "---" } },
            // UVI Prime 8 / Prime 8+ manual, MIDI mapping (C1 = 36). Software: set its MIDI channel to match
            { "UVI Prime 8", 2, 1, { 36, 38, 41, 45, 39, 42, 46, 49, 37, 43, 0, 47, 0 },
              { "BD", "SD", "LT", "HT", "CP", "CH", "OH", "CY", "RS", "MT", "---", "CB", "---" } },
            // AudioRealism ADM manual, "Using Note Mode" (MIDI key numbers). Unaccented keys; TRoss Seq's accent
            // velocity (above 100) plays ADM's accent. PCF = Pattern Controlled Fx trigger (not randomised)
            { "AudioRealism ADM (ADM1X)", 2, 1, { 36, 40, 43, 50, 39, 44, 46, 51, 37, 47, 0, 49, 52 },
              { "BD", "SD", "LT", "HT", "CP", "CH", "OH", "CY", "RS", "MT", "---", "CB", "PCF" } },
            // Ableton Live Drum Rack: the 16 pads sit on C1..D#2 (36..51), laid out like General MIDI.
            // E1 (40) and B1 (47) are the two pads left over. Channel 1 (a Drum Rack listens on every channel)
            { "Ableton Drum Rack", 2, 1, { 36, 38, 41, 48, 39, 42, 46, 49, 37, 45, 51, 43, 44 },
              { "BD", "SD", "LT", "HT", "CP", "CH", "OH", "CY", "RS", "MT", "RD", "TOM", "PD HAT" } },
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

    // the first step at or after `pos`
    long long firstAt (double pos) const
    {
        long long n = (long long) std::floor (pos) - 1;
        while (stepTime (n) < pos - 1e-7) ++n;
        return n;
    }

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
    // `slack`: how far ahead of the song the clock may run (negative port delay) before a jump is assumed.
    // `resyncAt`: where to pick up after a jump (the song position, so no step is skipped; NaN = pos).
    long long advance (double pos, double slack = 0.0, double resyncAt = std::numeric_limits<double>::quiet_NaN())
    {
        const double at = std::isnan (resyncAt) ? pos : resyncAt;
        if (! running) syncTo (at);
        const double prev = stepTime (nextStep - 1);
        if (pos < prev - 1e-6 || pos > stepTime (nextStep) + 1.0 + slack) syncTo (at);   // jumped
        if (stepTime (nextStep) <= pos + 1e-9) return nextStep++;
        return -1;
    }
};

inline int wrapStep (long long x, int len) { return (int) (((x % len) + len) % len); }
} // namespace trossseq
