// TRoss Seq randomiser: writes drum-like patterns, lane by lane. Each lane gets a role (kick, snare,
// hat...) from its name or MIDI note, and each role has a step-by-step weight table per style.
// The accent row is never touched. No JUCE in here, so it can be tested on its own.
#pragma once
#include "SeqCore.h"
#include <algorithm>
#include <cctype>
#include <random>
#include <string>

namespace trossseq
{
enum class Role { Kick, Snare, Clap, ClosedHat, OpenHat, Cymbal, Tom, Perc };
// (new styles go at the end: projects store the style's position)
enum class Style { Straight, Electro, Broken, House, Techno, Detroit, MiamiBass, Breakbeat, HipHop, Dub, Latin, Industrial };
constexpr int kNumStyles = 12;
inline const char* styleName (int s)
{
    static const char* n[kNumStyles] = { "STRAIGHT", "ELECTRO", "BROKEN", "HOUSE", "TECHNO", "DETROIT", "MIAMI BASS",
                                         "BREAKBEAT", "HIP-HOP", "DUB", "LATIN", "INDUSTRIAL" };
    return n[s < 0 || s >= kNumStyles ? 0 : s];
}

// ---- which kind of drum a lane is: its name first (so renamed lanes work), then its MIDI note
inline Role roleFor (std::string name, int note)
{
    for (auto& c : name) c = (char) std::toupper ((unsigned char) c);
    auto has = [&] (const char* w) { return name.find (w) != std::string::npos; };
    auto is  = [&] (const char* w) { return name == w; };

    if (is ("CL") || has ("CLAVE")) return Role::Perc;                                   // claves, not closed hat
    if (is ("BD") || has ("KICK") || has ("BASS")) return Role::Kick;
    if (is ("SD") || has ("SNARE") || has ("SNR")) return Role::Snare;
    if (is ("CP") || is ("HC") || has ("CLAP")) return Role::Clap;
    if (is ("OH") || has ("OPEN") || has ("OP HAT") || has ("OP.HAT")) return Role::OpenHat;
    if (is ("CH") || is ("HH") || has ("CLOSED") || has ("CL HAT") || has ("HAT")) return Role::ClosedHat;
    if (is ("CY") || is ("CC") || is ("RD") || is ("RC") || has ("CRASH") || has ("RIDE") || has ("CYM")) return Role::Cymbal;
    if (has ("TOM") || is ("LT") || is ("MT") || is ("HT") || is ("LO") || is ("HI")) return Role::Tom;
    if (is ("RS") || has ("RIM") || has ("COW") || is ("CB") || has ("AGOGO") || has ("PERC") || has ("CONGA")
        || has ("BONGO") || has ("SHAK") || has ("TAMB") || has ("BLOCK")) return Role::Perc;

    switch (note)   // General MIDI drum notes
    {
        case 35: case 36:                                   return Role::Kick;
        case 38: case 40:                                   return Role::Snare;
        case 39:                                            return Role::Clap;
        case 42: case 44:                                   return Role::ClosedHat;
        case 46:                                            return Role::OpenHat;
        case 49: case 51: case 52: case 53: case 55: case 57: case 59: return Role::Cymbal;
        case 41: case 43: case 45: case 47: case 48: case 50: return Role::Tom;
        default:                                            return Role::Perc;
    }
}

// table helper: weight of step s from a 16-value list
inline float tbl (const float (&t)[16], int s) { return t[s & 15]; }

// the styles added in 0.4.0
inline float styleWeight2 (Role r, Style st, int s)
{
    const bool beat = s % 4 == 0, eighth = s % 2 == 0, back = s == 4 || s == 12, offEighth = eighth && ! beat;
    switch (st)
    {
        case Style::House:       // four on the floor, open hat on the off-beat, clap on the backbeat
            switch (r)
            {
                case Role::Kick:      return beat ? 1.0f : 0.03f;
                case Role::Snare:     return back ? 0.25f : 0.03f;
                case Role::Clap:      return back ? 1.0f : s == 15 ? 0.06f : 0.03f;
                case Role::ClosedHat: return eighth ? (beat ? 0.35f : 0.3f) : 0.55f;
                case Role::OpenHat:   return offEighth ? 0.9f : 0.02f;
                case Role::Cymbal:    return s == 0 ? 0.15f : 0.01f;
                case Role::Tom:       return s >= 12 ? 0.12f : 0.03f;
                case Role::Perc:      return eighth ? 0.05f : 0.15f;
            }
            break;
        case Style::Techno:      // driving kick, rolling 16th hats, percussion building tension
            switch (r)
            {
                case Role::Kick:      return beat ? 1.0f : (s == 14 || s == 15) ? 0.08f : 0.03f;
                case Role::Snare:     return s == 12 ? 0.2f : 0.04f;
                case Role::Clap:      return back ? 0.6f : 0.03f;
                case Role::ClosedHat: return 0.75f;
                case Role::OpenHat:   return offEighth ? 0.6f : 0.02f;
                case Role::Cymbal:    return s == 0 ? 0.25f : 0.01f;
                case Role::Tom:       return eighth ? 0.06f : 0.1f;
                case Role::Perc:      return eighth ? 0.12f : 0.3f;
            }
            break;
        case Style::Detroit:     // soulful techno: syncopated hats and claps, cowbell / clave figures
        {
            static const float perc[16] = { 0.2f, 0.06f, 0.08f, 0.45f, 0.06f, 0.08f, 0.45f, 0.06f,
                                            0.2f, 0.06f, 0.45f, 0.08f, 0.06f, 0.08f, 0.45f, 0.06f };
            switch (r)
            {
                case Role::Kick:      return s == 0 ? 1.0f : beat ? 0.95f : s == 14 ? 0.2f : s == 7 ? 0.12f : 0.04f;
                case Role::Snare:     return back ? 0.5f : 0.03f;
                case Role::Clap:      return back ? 0.7f : s == 11 ? 0.15f : 0.03f;
                case Role::ClosedHat: return eighth ? 0.7f : 0.4f;
                case Role::OpenHat:   return offEighth ? 0.45f : 0.02f;
                case Role::Cymbal:    return s == 0 ? 0.2f : 0.01f;
                case Role::Tom:       return (s == 3 || s == 11 || s >= 13) ? 0.15f : 0.04f;
                case Role::Perc:      return tbl (perc, s);
            }
            break;
        }
        case Style::MiamiBass:   // booming, bouncy kick figures, snare on 13, fast hats and toms
        {
            static const float kick[16] = { 1.0f, 0.05f, 0.05f, 0.5f, 0.05f, 0.05f, 0.55f, 0.35f,
                                            0.08f, 0.05f, 0.5f, 0.35f, 0.05f, 0.05f, 0.25f, 0.08f };
            switch (r)
            {
                case Role::Kick:      return tbl (kick, s);
                case Role::Snare:     return s == 12 ? 1.0f : s == 4 ? 0.85f : s == 15 ? 0.12f : 0.02f;
                case Role::Clap:      return s == 12 ? 0.6f : s == 4 ? 0.5f : 0.02f;
                case Role::ClosedHat: return 0.85f;
                case Role::OpenHat:   return s == 14 ? 0.3f : s == 6 ? 0.2f : 0.02f;
                case Role::Cymbal:    return s == 0 ? 0.25f : 0.01f;
                case Role::Tom:       return s >= 12 ? 0.3f : s == 9 ? 0.1f : 0.03f;
                case Role::Perc:      return (s == 2 || s == 10) ? 0.25f : eighth ? 0.04f : 0.12f;
            }
            break;
        }
        case Style::Breakbeat:   // funky break: kick on 1 and around 11, snare on 5 and 13 with ghost notes
        {
            static const float kick[16] = { 1.0f, 0.04f, 0.3f, 0.04f, 0.04f, 0.04f, 0.04f, 0.25f,
                                            0.06f, 0.04f, 0.7f, 0.2f, 0.04f, 0.04f, 0.08f, 0.06f };
            switch (r)
            {
                case Role::Kick:      return tbl (kick, s);
                case Role::Snare:     return back ? 1.0f : (s == 7 || s == 9 || s == 14 || s == 15) ? 0.22f : 0.05f;
                case Role::Clap:      return back ? 0.25f : 0.02f;
                case Role::ClosedHat: return eighth ? 0.8f : 0.35f;
                case Role::OpenHat:   return (s == 6 || s == 14) ? 0.3f : (s == 2 || s == 10) ? 0.12f : 0.02f;
                case Role::Cymbal:    return s == 0 ? 0.3f : 0.01f;
                case Role::Tom:       return s >= 13 ? 0.2f : 0.03f;
                case Role::Perc:      return 0.1f;
            }
            break;
        }
        case Style::HipHop:      // laid back: sparse kick, heavy backbeat, simple 8th hats (use SWING)
        {
            static const float kick[16] = { 1.0f, 0.03f, 0.03f, 0.2f, 0.03f, 0.03f, 0.03f, 0.35f,
                                            0.05f, 0.03f, 0.55f, 0.25f, 0.03f, 0.03f, 0.15f, 0.03f };
            switch (r)
            {
                case Role::Kick:      return tbl (kick, s);
                case Role::Snare:     return back ? 1.0f : s == 15 ? 0.12f : s == 11 ? 0.08f : 0.02f;
                case Role::Clap:      return back ? 0.35f : 0.02f;
                case Role::ClosedHat: return eighth ? 0.85f : 0.1f;
                case Role::OpenHat:   return s == 14 ? 0.2f : s == 6 ? 0.1f : 0.01f;
                case Role::Cymbal:    return s == 0 ? 0.1f : 0.01f;
                case Role::Tom:       return 0.03f;
                case Role::Perc:      return eighth ? 0.04f : 0.08f;
            }
            break;
        }
        case Style::Dub:         // sparse and spacious, rim shots instead of snares
        {
            static const float perc[16] = { 0.04f, 0.04f, 0.04f, 0.04f, 0.45f, 0.04f, 0.04f, 0.15f,
                                            0.04f, 0.04f, 0.04f, 0.04f, 0.8f, 0.04f, 0.04f, 0.15f };
            switch (r)
            {
                case Role::Kick:      return s == 0 ? 1.0f : s == 8 ? 0.6f : s == 10 ? 0.2f : s == 14 ? 0.15f : 0.03f;
                case Role::Snare:     return s == 12 ? 0.3f : 0.01f;
                case Role::Clap:      return s == 12 ? 0.2f : 0.01f;
                case Role::ClosedHat: return offEighth ? 0.5f : 0.15f;
                case Role::OpenHat:   return (s == 6 || s == 14) ? 0.25f : 0.01f;
                case Role::Cymbal:    return s == 0 ? 0.1f : 0.01f;
                case Role::Tom:       return s >= 12 ? 0.08f : 0.03f;
                case Role::Perc:      return tbl (perc, s);
            }
            break;
        }
        case Style::Latin:       // built around the son clave, with cowbell, agogo and toms interlocking
        {
            static const float clave[16] = { 0.85f, 0.08f, 0.08f, 0.85f, 0.08f, 0.08f, 0.85f, 0.08f,
                                             0.08f, 0.08f, 0.85f, 0.08f, 0.85f, 0.08f, 0.08f, 0.08f };
            switch (r)
            {
                case Role::Kick:      return s == 0 ? 1.0f : s == 8 ? 0.6f : (s == 3 || s == 11) ? 0.3f : 0.04f;
                case Role::Snare:     return s == 12 ? 0.3f : s == 7 ? 0.1f : 0.02f;
                case Role::Clap:      return back ? 0.3f : 0.02f;
                case Role::ClosedHat: return eighth ? 0.7f : 0.4f;
                case Role::OpenHat:   return (s == 6 || s == 14) ? 0.2f : 0.02f;
                case Role::Cymbal:    return s == 0 ? 0.1f : 0.01f;
                case Role::Tom:       return (s == 6 || s == 7 || s == 14 || s == 15) ? 0.35f : s == 10 ? 0.25f : 0.06f;
                case Role::Perc:      return tbl (clave, s);
            }
            break;
        }
        case Style::Industrial:  // hard and mechanical: kick and snare on the grid, few hats
            switch (r)
            {
                case Role::Kick:      return beat ? 1.0f : (s == 2 || s == 10) ? 0.15f : 0.03f;
                case Role::Snare:     return back ? 1.0f : 0.02f;
                case Role::Clap:      return back ? 0.4f : 0.02f;
                case Role::ClosedHat: return eighth ? 0.35f : 0.05f;
                case Role::OpenHat:   return 0.05f;
                case Role::Cymbal:    return s == 0 ? 0.2f : 0.01f;
                case Role::Tom:       return s >= 12 ? 0.12f : 0.06f;
                case Role::Perc:      return eighth ? 0.15f : 0.05f;
            }
            break;
        default: break;
    }
    return 0.0f;
}

// ---- chance (0..1) that a step plays, per role and style. 1.0 = anchor: always there (at any density)
inline float stepWeight (Role r, Style st, int s)
{
    const bool beat = s % 4 == 0, eighth = s % 2 == 0, back = s == 4 || s == 12;
    switch (st)
    {
        case Style::Straight:
            switch (r)
            {
                case Role::Kick:      return s == 0 ? 1.0f : beat ? 0.85f : eighth ? 0.14f : 0.06f;
                case Role::Snare:     return back ? 1.0f : s == 15 ? 0.18f : s == 14 || s == 7 ? 0.08f : 0.03f;
                case Role::Clap:      return back ? 0.45f : 0.03f;
                case Role::ClosedHat: return beat ? 0.8f : eighth ? 0.9f : 0.3f;
                case Role::OpenHat:   return (s == 2 || s == 6 || s == 10 || s == 14) ? 0.28f : 0.02f;
                case Role::Cymbal:    return s == 0 ? 0.35f : 0.02f;
                case Role::Tom:       return s >= 12 ? 0.22f : 0.03f;
                case Role::Perc:      return eighth ? 0.08f : 0.16f;
            }
            break;
        case Style::Electro:   // syncopated kick, sparse snare, busy 16th hats
            switch (r)
            {
                case Role::Kick:
                {
                    static const float k[16] = { 1.0f, 0.05f, 0.08f, 0.45f, 0.05f, 0.1f, 0.5f, 0.12f,
                                                 0.25f, 0.08f, 0.55f, 0.3f, 0.05f, 0.1f, 0.22f, 0.12f };
                    return k[s];
                }
                case Role::Snare:     return s == 12 ? 1.0f : s == 4 ? 0.8f : s == 15 ? 0.15f : 0.02f;
                case Role::Clap:      return s == 12 ? 0.55f : s == 4 ? 0.35f : s == 7 ? 0.08f : 0.02f;
                case Role::ClosedHat: return beat ? 0.85f : eighth ? 0.8f : 0.65f;
                case Role::OpenHat:   return (s == 2 || s == 10) ? 0.22f : s == 14 ? 0.3f : 0.02f;
                case Role::Cymbal:    return s == 0 ? 0.18f : 0.01f;
                case Role::Tom:       return s >= 13 ? 0.25f : s == 7 ? 0.1f : 0.03f;
                case Role::Perc:      return eighth ? 0.07f : 0.2f;
            }
            break;
        case Style::Broken:    // scattered, off the grid
            switch (r)
            {
                case Role::Kick:      return s == 0 ? 1.0f : (s == 10 || s == 7) ? 0.4f : s == 8 ? 0.25f : 0.16f;
                case Role::Snare:     return s == 12 ? 0.75f : s == 4 ? 0.5f : (s == 6 || s == 11 || s == 14) ? 0.22f : 0.06f;
                case Role::Clap:      return (s == 4 || s == 12) ? 0.3f : 0.07f;
                case Role::ClosedHat: return eighth ? 0.6f : 0.45f;
                case Role::OpenHat:   return eighth ? 0.12f : 0.08f;
                case Role::Cymbal:    return s == 0 ? 0.15f : 0.03f;
                case Role::Tom:       return 0.13f;
                case Role::Perc:      return 0.18f;
            }
            break;
        default: break;
    }
    return styleWeight2 (r, st, s);
}

// weights for the fill steps (the last four of the pattern)
inline float fillWeight (Role r)
{
    switch (r)
    {
        case Role::Tom:       return 0.5f;
        case Role::Snare:     return 0.45f;
        case Role::ClosedHat: return 0.5f;
        case Role::Kick:      return 0.3f;
        case Role::Clap:      return 0.25f;
        case Role::Perc:      return 0.3f;
        case Role::OpenHat:   return 0.12f;
        case Role::Cymbal:    return 0.0f;
    }
    return 0.0f;
}

// One lane per role-input: what the randomiser needs to know about each track
struct LaneInfo { Role role = Role::Perc; bool locked = false; };

class Randomizer
{
public:
    explicit Randomizer (unsigned seed = std::random_device{}()) : rng (seed) {}

    // density 0..100 (50 = as the tables say)
    static float scaled (float w, float density)
    {
        if (w >= 1.0f) return 1.0f;
        return std::clamp (w * density / 50.0f, 0.0f, 0.97f);
    }

    // A whole new pattern: every unlocked lane is rewritten over `len` steps. rows[0] (accent) untouched.
    void generate (uint16_t* rows, const LaneInfo* lanes, int len, Style st, float density)
    {
        for (int t = 0; t < kNumTracks; ++t)
            if (! lanes[t].locked) rows[t + 1] = lane (lanes[t].role, len, st, density);
        tidy (rows, lanes, len);
    }

    // One lane, locked or not (the user picked it)
    void generateLane (uint16_t* rows, const LaneInfo* lanes, int track, int len, Style st, float density)
    {
        rows[track + 1] = lane (lanes[track].role, len, st, density);
        LaneInfo only[kNumTracks];
        for (int t = 0; t < kNumTracks; ++t) { only[t] = lanes[t]; only[t].locked = t != track; }
        tidy (rows, only, len);
    }

    // A variation: a few steps of a few unlocked lanes change, the rest stays
    void mutate (uint16_t* rows, const LaneInfo* lanes, int len, Style st, float density)
    {
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        std::uniform_int_distribution<int> pick (0, std::max (0, len - 1));
        int changed = 0;
        for (int attempt = 0; attempt < 64 && changed < 3; ++attempt)
            for (int t = 0; t < kNumTracks; ++t)
            {
                if (lanes[t].locked || u (rng) > 0.35f) continue;
                const int s = pick (rng);
                const float w = scaled (stepWeight (lanes[t].role, st, s), density);
                if (w >= 1.0f) continue;   // keep the anchors (kick on 1, backbeat)
                const bool on = (rows[t + 1] >> s) & 1;
                const bool want = on ? u (rng) > 0.75f : u (rng) < std::max (w, 0.25f);
                if (want != on) { rows[t + 1] ^= (uint16_t) (1u << s); ++changed; }
            }
        tidy (rows, lanes, len);
    }

    // A fill: the last four steps of the pattern are rewritten for every unlocked lane
    void fill (uint16_t* rows, const LaneInfo* lanes, int len, float density)
    {
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        const int from = std::max (0, len - 4);
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (lanes[t].locked) continue;
            const float w = std::clamp (fillWeight (lanes[t].role) * density / 50.0f, 0.0f, 0.95f);
            for (int s = from; s < len; ++s)
            {
                const bool on = u (rng) < w;
                rows[t + 1] = on ? (uint16_t) (rows[t + 1] | (1u << s)) : (uint16_t) (rows[t + 1] & ~(1u << s));
            }
        }
        tidy (rows, lanes, len);
    }

private:
    std::mt19937 rng;

    uint16_t lane (Role r, int len, Style st, float density)
    {
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        uint16_t m = 0;
        for (int s = 0; s < len && s < kNumSteps; ++s)
            if (u (rng) < scaled (stepWeight (r, st, s), density)) m |= (uint16_t) (1u << s);
        // keep the hats moving even at low density
        if (r == Role::ClosedHat && m == 0 && density > 5.0f && len > 0) m = (uint16_t) (1u << (len > 2 ? 2 : 0));
        return m;
    }

    // drum-machine manners: an open hat and a closed hat don't hit on the same step;
    // nothing past the pattern length
    static void tidy (uint16_t* rows, const LaneInfo* lanes, int len)
    {
        const uint16_t inLen = (uint16_t) (len >= 16 ? 0xffff : ((1u << len) - 1));
        uint16_t open = 0, closedLocked = 0;
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (lanes[t].role == Role::OpenHat)                      open |= rows[t + 1];
            if (lanes[t].role == Role::ClosedHat && lanes[t].locked) closedLocked |= rows[t + 1];
        }
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (lanes[t].locked) continue;
            if (lanes[t].role == Role::ClosedHat) rows[t + 1] &= (uint16_t) ~open;          // open hat wins...
            if (lanes[t].role == Role::OpenHat)   rows[t + 1] &= (uint16_t) ~closedLocked;  // ...unless the closed hat is locked
            rows[t + 1] &= inLen;
        }
    }
};
} // namespace trossseq
