#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace trossseq;

namespace
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using API = juce::AudioParameterInt;

std::unique_ptr<APB> onOff (juce::String id, juce::String name, bool def) { return std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def); }
std::unique_ptr<APF> pct (juce::String id, juce::String name, float lo, float hi, float def)
{
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, 0.1f), def,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                      [] (float v, int) { return juce::String (v, std::abs (v - std::round (v)) < 0.05f ? 0 : 1) + " %"; }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout TrossSeqProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (onOff ("seqon", "Run", true));
    l.add (std::make_unique<API> (juce::ParameterID { "seqlen", 1 }, "Length", 1, 16, 16));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "pattern", 1 }, "Pattern",
                                                         juce::StringArray { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" }, 0));
    l.add (onOff ("rec", "Record", true));
    l.add (std::make_unique<API> (juce::ParameterID { "velocity", 1 }, "Velocity", 1, 127, 100));
    l.add (std::make_unique<API> (juce::ParameterID { "accvel", 1 }, "Accent Velocity", 1, 127, 127));
    l.add (pct ("gate", "Gate", 5.0f, 95.0f, 50.0f));
    l.add (pct ("swing", "Swing", 50.0f, 75.0f, 50.0f));
    l.add (std::make_unique<APF> (juce::ParameterID { "offset", 1 }, "Port Delay", juce::NormalisableRange<float> (0.0f, 250.0f, 0.5f), 0.0f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                      [] (float v, int) { return juce::String (v, 1) + " ms"; })));
    l.add (onOff ("thru", "MIDI Thru", true));
    l.add (onOff ("hostout", "Host Out", true));
    l.add (onOff ("clockout", "Clock Out", false));
    l.add (pct ("density", "Random Density", 0.0f, 100.0f, 50.0f));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "rstyle", 1 }, "Random Style",
                                                         juce::StringArray { "Straight", "Electro", "Broken" }, 0));
    for (int t = 0; t < kNumTracks; ++t)
        l.add (onOff ("mute" + juce::String (t + 1), "Track " + juce::String (t + 1) + " Mute", false));
    return l;
}

TrossSeqProcessor::TrossSeqProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TROSSSEQ", createLayout())
{
    auto p = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    for (int t = 0; t < kNumTracks; ++t) raw.mute[t] = p ("mute" + juce::String (t + 1));
    raw.seqOn = p ("seqon"); raw.seqLen = p ("seqlen"); raw.pattern = p ("pattern"); raw.rec = p ("rec");
    raw.velocity = p ("velocity"); raw.accentVel = p ("accvel"); raw.gate = p ("gate"); raw.swing = p ("swing");
    raw.thru = p ("thru"); raw.hostOut = p ("hostout"); raw.clockOut = p ("clockout"); raw.offset = p ("offset");
    raw.density = p ("density"); raw.rstyle = p ("rstyle");

    applyPreset (0);

    // the map, names and port saved with "Save as default"
    juce::PropertiesFile settings (settingsOptions());
    if (auto xml = settings.getXmlValue ("defaultMap"))
        mapFromTree (juce::ValueTree::fromXml (*xml));
    const auto portId = settings.getValue ("defaultPort");
    if (portId.isNotEmpty())
    {
        const juce::ScopedLock sl (wantedLock);
        wantedPortId = portId;
        wantedPortName = settings.getValue ("defaultPortName");
        triggerAsyncUpdate();
    }
}

TrossSeqProcessor::~TrossSeqProcessor()
{
    cancelPendingUpdate();
    directOut.close();
}

juce::PropertiesFile::Options TrossSeqProcessor::settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "TRoss Seq";
    o.filenameSuffix = ".settings";
    o.folderName = "Ticco Ross";
    o.osxLibrarySubFolder = "Application Support";
    o.commonToAllUsers = false;
    return o;
}

bool TrossSeqProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    return (mainOut == juce::AudioChannelSet::stereo() || mainOut == juce::AudioChannelSet::mono()
            || mainOut == juce::AudioChannelSet::disabled())
           && layouts.inputBuses.isEmpty();
}

void TrossSeqProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    wasPlaying = false;
    clock.stop();
    playStep = -1;
    numPending = 0;
    haveBlockTime = false;
    lastPatternParam = (int) raw.pattern->load();
    out.ensureSize ((size_t) juce::jmax (256, samplesPerBlock) * 3);
}

// ================================================================================================
void TrossSeqProcessor::applyPreset (int i)
{
    map.setPreset (i);
    const auto& p = TrackMap::preset (i);
    {
        const juce::ScopedLock sl (nameLock);
        names.clear();
        for (int t = 0; t < kNumTracks; ++t) names.add (p.labels[t]);
    }
    ++nameVersion;
}

juce::ValueTree TrossSeqProcessor::mapToTree() const
{
    juce::ValueTree t ("MAP");
    for (int i = 0; i < kNumTracks; ++i)
    {
        juce::ValueTree tr ("TRACK");
        tr.setProperty ("name", getTrackName (i), nullptr);
        tr.setProperty ("note", map.note (i), nullptr);
        tr.setProperty ("channel", map.channel (i), nullptr);
        tr.setProperty ("locked", locked[i].load(), nullptr);
        t.appendChild (tr, nullptr);
    }
    return t;
}

void TrossSeqProcessor::mapFromTree (const juce::ValueTree& t)
{
    if (! t.hasType ("MAP")) return;
    ++lockVersion;
    for (int i = 0; i < kNumTracks && i < t.getNumChildren(); ++i)
    {
        auto tr = t.getChild (i);
        map.setNote (i, (int) tr.getProperty ("note", map.note (i)));
        map.setChannel (i, (int) tr.getProperty ("channel", map.channel (i)));
        setTrackName (i, tr.getProperty ("name", getTrackName (i)).toString());
        locked[i] = (bool) tr.getProperty ("locked", false);
    }
}

void TrossSeqProcessor::saveAsDefault()
{
    juce::PropertiesFile settings (settingsOptions());
    if (auto xml = mapToTree().createXml()) settings.setValue ("defaultMap", xml.get());
    settings.setValue ("defaultPort", getPortIdentifier());
    settings.setValue ("defaultPortName", getPortName());
    settings.saveIfNeeded();
}

// ---- MIDI port
bool TrossSeqProcessor::selectPort (const juce::String& identifier)
{
    {
        const juce::ScopedLock sl (wantedLock);
        wantedPortId = identifier;
        wantedPortName = {};
    }
    if (identifier.isEmpty()) { directOut.close(); return true; }
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.identifier == identifier)
        {
            const juce::ScopedLock sl (wantedLock);
            wantedPortName = d.name;
            return directOut.open (d);
        }
    directOut.close();
    return false;
}

juce::String TrossSeqProcessor::getPortName() const
{
    if (directOut.isOpen()) return directOut.device().name;
    const juce::ScopedLock sl (wantedLock);
    return wantedPortName;   // asked for but not connected (shown as missing)
}

void TrossSeqProcessor::handleAsyncUpdate()
{
    juce::String id, name;
    {
        const juce::ScopedLock sl (wantedLock);
        id = wantedPortId; name = wantedPortName;
    }
    if (id.isEmpty()) { directOut.close(); return; }
    // same port, or one with the same name (identifiers can change between computers and reboots)
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.identifier == id || (name.isNotEmpty() && d.name == name))
        {
            if (! (directOut.isOpen() && directOut.device().identifier == d.identifier)) directOut.open (d);
            return;
        }
    directOut.close();   // not connected right now; the name stays so the editor can show it
}

// ================================================================================================
void TrossSeqProcessor::panelHit (int track, bool accent)
{
    const int w = hitWrite.load (std::memory_order_relaxed);
    const int next = (w + 1) % kHitQueue;
    if (next == hitRead.load (std::memory_order_acquire)) return;   // queue full: drop the hit
    hitQueue[w] = { track, accent };
    hitWrite.store (next, std::memory_order_release);
}

// ---- randomiser
void TrossSeqProcessor::randomise (RandomOp op, int track)
{
    const int slot = currentSlot.load();
    const int len = juce::jlimit (1, kNumSteps, (int) raw.seqLen->load());
    const auto style = (Style) juce::jlimit (0, kNumStyles - 1, (int) raw.rstyle->load());
    const float density = raw.density->load();
    const bool playing = playStep.load() >= 0;

    // start from what will be playing: a result already waiting for step 1, or the slot itself
    uint16_t rows[kNumRows];
    const int waiting = staged.load();
    const bool replaceWaiting = playing && waiting >= 0 && stagedBuf[waiting].slot == slot;
    for (int r = 0; r < kNumRows; ++r) rows[r] = replaceWaiting ? stagedBuf[waiting].rows[r] : patterns.get (slot, r);

    if (! replaceWaiting)   // remember the pattern as it was, for UNDO
    {
        UndoEntry e; e.slot = slot;
        for (int r = 0; r < kNumRows; ++r) e.rows[r] = patterns.get (slot, r);
        undoStack.push_back (e);
        if (undoStack.size() > 32) undoStack.erase (undoStack.begin());
    }

    LaneInfo lanes[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t)
        lanes[t] = { roleFor (getTrackName (t).toStdString(), map.note (t)), locked[t].load() };

    switch (op)
    {
        case RandomOp::NewPattern: rnd.generate (rows, lanes, len, style, density); break;
        case RandomOp::Mutate:     rnd.mutate (rows, lanes, len, style, density); break;
        case RandomOp::Fill:       rnd.fill (rows, lanes, len, density); break;
        case RandomOp::Lane:       if (track >= 0 && track < kNumTracks) rnd.generateLane (rows, lanes, track, len, style, density); break;
    }

    if (playing)
    {
        staged = -1;                       // the audio thread now ignores the old buffer
        auto& b = stagedBuf[stagedWrite];
        b.slot = slot;
        std::copy (rows, rows + kNumRows, b.rows);
        staged.store (stagedWrite, std::memory_order_release);
        stagedWrite = 1 - stagedWrite;
    }
    else
        for (int r = 1; r < kNumRows; ++r) patterns.set (slot, r, rows[r]);   // accent row stays as it is
}

bool TrossSeqProcessor::undoRandom()
{
    if (undoStack.empty()) return false;
    if (staged.exchange (-1) >= 0)   // still waiting for step 1: just drop it
    {
        undoStack.pop_back();
        return true;
    }
    const auto e = undoStack.back();
    undoStack.pop_back();
    for (int r = 1; r < kNumRows; ++r) patterns.set (e.slot, r, e.rows[r]);
    return true;
}

void TrossSeqProcessor::applyStaged()
{
    const int i = staged.exchange (-1, std::memory_order_acq_rel);
    if (i < 0) return;
    const auto& b = stagedBuf[i];
    for (int r = 1; r < kNumRows; ++r) patterns.set (b.slot, r, b.rows[r]);
}

// keeps a steady time base for the port output: follows the wall clock, but smooths out the
// jitter of when the host happens to call processBlock
void TrossSeqProcessor::updateBlockTime (int numSamples)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double blockMs = numSamples * 1000.0 / sampleRate;
    if (! haveBlockTime || std::abs (now - blockStartMs) > juce::jmax (40.0, 4.0 * blockMs))
    {
        blockStartMs = now;
        haveBlockTime = true;
    }
    else
        blockStartMs += 0.05 * (now - blockStartMs);
}

void TrossSeqProcessor::emit (const juce::MidiMessage& m, int sample)
{
    if (hostOutOn) out.addEvent (m, sample);
    if (directOut.isOpen()) directOut.push (m, blockStartMs + sample * 1000.0 / sampleRate + offsetMs);
}

void TrossSeqProcessor::noteOn (int track, int velocity, int sample, juce::int64 gateSamples)
{
    const int ch = map.channel (track), nn = map.note (track);
    // the same note still sounding: end it first so the drum box sees a fresh hit
    for (int i = 0; i < numPending; ++i)
        if (pending[i].channel == ch && pending[i].note == nn)
        {
            emit (juce::MidiMessage::noteOff (ch, nn), sample);
            pending[i] = pending[--numPending];
            break;
        }
    emit (juce::MidiMessage::noteOn (ch, nn, (juce::uint8) juce::jlimit (1, 127, velocity)), sample);
    if (numPending < kMaxPending) pending[numPending++] = { ch, nn, sample + juce::jmax ((juce::int64) 1, gateSamples) };
    else emit (juce::MidiMessage::noteOff (ch, nn), sample);
    lastSent[track] = juce::Time::getMillisecondCounter();
}

void TrossSeqProcessor::flushNoteOffs (int sample)
{
    for (int i = 0; i < numPending; ++i)
        emit (juce::MidiMessage::noteOff (pending[i].channel, pending[i].note), sample);
    numPending = 0;
}

// the sequencer reached `step`: a queued pattern takes over on step 1, then the step plays
void TrossSeqProcessor::doStep (int step, int sample, bool seqOn, juce::int64 gateSamples)
{
    if (step == 0)
    {
        if (pendingSlot.load() >= 0)
        {
            currentSlot = pendingSlot.load();
            pendingSlot = -1;
        }
        applyStaged();   // a randomised pattern takes over on step 1
    }
    playStep = step;
    if (! seqOn) return;
    const int slot = currentSlot.load();
    const bool accent = patterns.cell (slot, 0, step);
    const int vel = (int) (accent ? raw.accentVel->load() : raw.velocity->load());
    const int solo = momentarySolo.load();
    for (int t = 0; t < kNumTracks; ++t)
    {
        const bool audible = solo >= 0 ? t == solo : raw.mute[t]->load() < 0.5f;   // a held solo beats every mute
        if (patterns.cell (slot, t + 1, step) && audible)
            noteOn (t, vel, sample, gateSamples);
    }
}

// REC while playing: the hit goes into the nearest step (swing taken into account)
void TrossSeqProcessor::recordHit (int track, bool accent, double pos, int len)
{
    const auto n0 = (juce::int64) std::floor (pos);
    juce::int64 best = n0;
    for (auto n = n0 - 1; n <= n0 + 1; ++n)
        if (std::abs (pos - clock.stepTime (n)) < std::abs (pos - clock.stepTime (best))) best = n;
    const int step = wrapStep (best, len);
    const int slot = currentSlot.load();
    patterns.setCell (slot, track + 1, step, true);
    if (accent) patterns.setCell (slot, 0, step, true);
}

void TrossSeqProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    out.clear();
    hostOutOn = raw.hostOut->load() > 0.5f;
    offsetMs  = raw.offset->load();
    updateBlockTime (numSamples);

    const bool seqOn    = raw.seqOn->load() > 0.5f;
    const bool recOn    = raw.rec->load() > 0.5f;
    const bool thru     = raw.thru->load() > 0.5f;
    const bool clockOut = raw.clockOut->load() > 0.5f;
    const int  len      = juce::jlimit (1, kNumSteps, (int) raw.seqLen->load());
    const int  accVel   = (int) raw.accentVel->load();
    clock.swingAmount   = StepClock::swingFromPercent (raw.swing->load());

    // ---- transport
    bool playing = false, hostTransport = false;
    double ppq = 0.0, bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition())
            {
                hostTransport = true;
                playing = pos->getIsPlaying();
                ppq = *q;
            }
        }
    if (! hostTransport && isStandalone())
    {
        bpm = internalBpm.load();
        playing = internalPlay.load();
        if (! playing) internalPpq = 0.0;
        ppq = internalPpq;
    }
    bpm = juce::jlimit (20.0, 999.0, bpm);
    currentBpm.store (bpm);
    const double sixteenthsPerSample = bpm / 60.0 * 4.0 / sampleRate;
    const double samplesPerStep = 1.0 / sixteenthsPerSample;
    const auto gateSamples = (juce::int64) std::round (raw.gate->load() / 100.0 * samplesPerStep);
    if (isStandalone() && ! hostTransport && playing) internalPpq += numSamples * sixteenthsPerSample / 4.0;

    // ---- pattern selector: while playing, a new slot waits until the pattern is back at step 1
    const int sel = (int) raw.pattern->load();
    if (sel != lastPatternParam)
    {
        lastPatternParam = sel;
        if (playing) pendingSlot = (sel == currentSlot.load()) ? -1 : sel;
        else         { pendingSlot = -1; currentSlot = sel; }
    }

    // ---- stop
    if (! playing)
    {
        if (pendingSlot.load() >= 0) { currentSlot = pendingSlot.load(); pendingSlot = -1; }
        applyStaged();   // stopped before step 1 came round: write it now
        if (wasPlaying)
        {
            flushNoteOffs (0);
            if (clockOut && directOut.isOpen()) directOut.push (juce::MidiMessage::midiStop(), blockStartMs + offsetMs);
        }
        wasPlaying = false;
        clock.stop();
        playStep = -1;
    }

    const double q0 = ppq * 4.0;   // position in 16ths at the block start

    // ---- clicks on the track names since the last block: play at the block start, record if REC is on
    for (int r = hitRead.load (std::memory_order_relaxed); r != hitWrite.load (std::memory_order_acquire); r = (r + 1) % kHitQueue)
    {
        const auto hit = hitQueue[r];
        noteOn (hit.track, hit.accent ? accVel : (int) raw.velocity->load(), 0, gateSamples);
        if (playing && recOn) recordHit (hit.track, hit.accent, q0, len);
        hitRead.store ((r + 1) % kHitQueue, std::memory_order_release);
    }

    // ---- start: tell a clock-following drum box where we are
    if (playing && ! wasPlaying && clockOut && directOut.isOpen())
    {
        const double t = blockStartMs + offsetMs;
        if (ppq < 0.01) directOut.push (juce::MidiMessage::midiStart(), t);
        else
        {
            directOut.push (juce::MidiMessage::songPositionPointer (juce::jlimit (0, 16383, (int) std::floor (ppq * 4.0))), t);
            directOut.push (juce::MidiMessage::midiContinue(), t);
        }
        lastClockTick = (juce::int64) std::ceil (ppq * 24.0 - 1e-9) - 1;
    }

    // ---- run through the block: steps, clock ticks and incoming MIDI in time order
    auto midiIt = midi.cbegin();
    for (int i = 0; i < numSamples; ++i)
    {
        const double q = q0 + i * sixteenthsPerSample;
        if (playing)
        {
            const auto step = clock.advance (q);
            if (step >= 0) doStep (wrapStep (step, len), i, seqOn, gateSamples);
            if (clockOut && directOut.isOpen())
            {
                const auto tick = (juce::int64) std::floor (q * 6.0 + 1e-9);   // 24 per quarter = 6 per 16th
                if (tick != lastClockTick)
                {
                    lastClockTick = tick;
                    directOut.push (juce::MidiMessage::midiClock(), blockStartMs + i * 1000.0 / sampleRate + offsetMs);
                }
            }
        }

        for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
        {
            const auto m = (*midiIt).getMessage();
            if (m.isNoteOn())
            {
                const int learn = learnTrack.load();
                if (learn >= 0)
                {
                    map.setNote (learn, m.getNoteNumber());
                    map.setChannel (learn, m.getChannel());
                    learnTrack = -1;
                }
                const int track = map.trackFor (m.getNoteNumber(), m.getChannel());
                if (track >= 0)
                {
                    lastSent[track] = juce::Time::getMillisecondCounter();
                    if (playing && recOn) recordHit (track, m.getVelocity() > 100, q, len);
                }
            }
            if (thru && ! m.isMidiClock() && ! m.isMidiStart() && ! m.isMidiStop() && ! m.isMidiContinue())
                emit (m, i);
        }

        // note-offs that fall on this sample
        for (int p = 0; p < numPending;)
            if (pending[p].sample <= i)
            {
                emit (juce::MidiMessage::noteOff (pending[p].channel, pending[p].note), i);
                pending[p] = pending[--numPending];
            }
            else ++p;
    }
    for (int p = 0; p < numPending; ++p) pending[p].sample -= numSamples;
    if (playing) wasPlaying = true;

    midi.swapWith (out);
    blockStartMs += numSamples * 1000.0 / sampleRate;   // expected start of the next block
}

// ---- state: parameters + the 12 patterns + playing slot + track map + MIDI port
void TrossSeqProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto pats = state.getOrCreateChildWithName ("PATTERNS", nullptr);
    pats.setProperty ("current", currentSlot.load(), nullptr);
    for (int s = 0; s < kNumSlots; ++s)
    {
        juce::StringArray rows;
        for (int r = 0; r < kNumRows; ++r) rows.add (juce::String::toHexString ((int) patterns.get (s, r)));
        pats.setProperty ("slot" + juce::String (s), rows.joinIntoString (" "), nullptr);
    }
    state.removeChild (state.getChildWithName ("MAP"), nullptr);
    state.appendChild (mapToTree(), nullptr);
    {
        const juce::ScopedLock sl (wantedLock);
        state.setProperty ("portId", wantedPortId, nullptr);
        state.setProperty ("portName", wantedPortName, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void TrossSeqProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    auto pats = state.getChildWithName ("PATTERNS");
    if (pats.isValid())
    {
        for (int s = 0; s < kNumSlots; ++s)
        {
            juce::StringArray rows;
            rows.addTokens (pats.getProperty ("slot" + juce::String (s)).toString(), " ", "");
            for (int r = 0; r < kNumRows; ++r)
                patterns.set (s, r, (uint16_t) (r < rows.size() ? rows[r].getHexValue32() & 0xffff : 0));
        }
        state.removeChild (pats, nullptr);
    }
    auto mapTree = state.getChildWithName ("MAP");
    if (mapTree.isValid()) { mapFromTree (mapTree); state.removeChild (mapTree, nullptr); }
    {
        const juce::ScopedLock sl (wantedLock);
        wantedPortId = state.getProperty ("portId").toString();
        wantedPortName = state.getProperty ("portName").toString();
    }
    state.removeProperty ("portId", nullptr);
    state.removeProperty ("portName", nullptr);
    triggerAsyncUpdate();

    apvts.replaceState (state);
    const int sel = (int) raw.pattern->load();
    currentSlot = pats.isValid() ? juce::jlimit (0, kNumSlots - 1, (int) pats.getProperty ("current", sel)) : sel;
    pendingSlot = -1;
    lastPatternParam = sel;
}

juce::AudioProcessorEditor* TrossSeqProcessor::createEditor() { return new TrossSeqEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TrossSeqProcessor(); }
