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
    l.add (std::make_unique<APF> (juce::ParameterID { "offset", 1 }, "Port Delay", juce::NormalisableRange<float> (-250.0f, 250.0f, 0.5f), 0.0f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                      [] (float v, int) { return juce::String (v, 1) + " ms"; })));
    l.add (onOff ("thru", "MIDI Thru", true));
    l.add (onOff ("hostout", "Host Out", true));
    l.add (onOff ("clockout", "Clock Out", false));
    l.add (std::make_unique<APF> (juce::ParameterID { "flamtime", 1 }, "Flam Time", juce::NormalisableRange<float> (5.0f, 80.0f, 0.5f), 25.0f,
                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                      [] (float v, int) { return juce::String (v, 1) + " ms"; })));
    l.add (pct ("flamlevel", "Flam Level", 10.0f, 100.0f, 60.0f));
    l.add (pct ("density", "Random Density", 0.0f, 100.0f, 50.0f));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "rstyle", 1 }, "Random Style",
                                                         juce::StringArray { "Straight", "Electro", "Broken", "House", "Techno", "Detroit", "Miami Bass",
                                                                             "Breakbeat", "Hip-Hop", "Dub", "Latin", "Industrial" }, 0));
    for (int t = 0; t < kNumTracks; ++t)
        l.add (onOff ("mute" + juce::String (t + 1), "Track " + juce::String (t + 1) + " Mute", false));
    for (int t = 0; t < kNumTracks; ++t)
        l.add (onOff ("solo" + juce::String (t + 1), "Track " + juce::String (t + 1) + " Solo", false));
    for (int t = 0; t < kNumTracks; ++t)
        for (int k = 0; k < 4; ++k)
            l.add (std::make_unique<API> (juce::ParameterID { ccBaseId (t, k), 1 },
                                          "Lane " + juce::String (t + 1) + " CC " + juce::String (k + 1) + " Base", 0, 127, 64));
    l.add (onOff ("stutter", "Stutter", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "sturate", 1 }, "Stutter Length",
                                                         juce::StringArray { "1/2 bar", "1/4", "1/8", "1/16", "1/32" }, 2));
    l.add (onOff ("sturoll", "Stutter Roll", false));
    return l;
}

TrossSeqProcessor::TrossSeqProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TROSSSEQ", createLayout())
{
    auto p = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    for (int t = 0; t < kNumTracks; ++t) { raw.mute[t] = p ("mute" + juce::String (t + 1)); raw.solo[t] = p ("solo" + juce::String (t + 1)); }
    raw.seqOn = p ("seqon"); raw.seqLen = p ("seqlen"); raw.pattern = p ("pattern"); raw.rec = p ("rec");
    raw.velocity = p ("velocity"); raw.accentVel = p ("accvel"); raw.gate = p ("gate"); raw.swing = p ("swing");
    raw.thru = p ("thru"); raw.hostOut = p ("hostout"); raw.clockOut = p ("clockout"); raw.offset = p ("offset");
    raw.density = p ("density"); raw.rstyle = p ("rstyle"); raw.flamTime = p ("flamtime"); raw.flamLevel = p ("flamlevel");
    for (int t = 0; t < kNumTracks; ++t)
        for (int k = 0; k < 4; ++k) { raw.ccBase[t][k] = p (ccBaseId (t, k)); ccSentNum[t][k] = -1; ccBaseSeen[t][k] = -1; }
    forgetSentCcs();
    raw.stutter = p ("stutter"); raw.stutRate = p ("sturate"); raw.stutRoll = p ("sturoll");

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
    graceClock.stop();
    nudgeClock.stop();
    numSched = 0;
    numHeld = 0;
    skipSteps = skipGrace = 0;
    lastFiredStep = lastGraceStep = lastNudgeStep = LLONG_MIN; skipNudge = 0;
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
    for (int t = 0; t < kNumTracks; ++t) for (int k = 0; k < 4; ++k) setCcBase (t, k, 64);
    const auto& p = TrackMap::preset (i);
    {
        const juce::ScopedLock sl (nameLock);
        names.clear();
        for (int t = 0; t < kNumTracks; ++t) names.add (p.labels[t]);
    }
    ++nameVersion;
    setMapIdentity (p.name);
}

// ---- drum map library
juce::File TrossSeqProcessor::mapsFolder()
{
    if (mapsFolderOverride != juce::File()) return mapsFolderOverride;
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TRoss Seq").getChildFile ("Maps");
}

juce::File TrossSeqProcessor::mapFile (const juce::String& name)
{
    return mapsFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".tsmap");
}

juce::StringArray TrossSeqProcessor::listMaps()
{
    juce::StringArray names;
    for (auto& f : mapsFolder().findChildFiles (juce::File::findFiles, false, "*.tsmap"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::String TrossSeqProcessor::mapSignature() const
{
    juce::String sig;
    for (int t = 0; t < kNumTracks; ++t)
        sig << getTrackName (t) << '|' << map.note (t) << '|' << map.channel (t) << '|' << (locked[t].load() ? 1 : 0)
            << '|' << map.ccNumber (t, 0) << ',' << map.ccNumber (t, 1) << ',' << map.ccNumber (t, 2) << ',' << map.ccNumber (t, 3)
            << '|' << ccBase (t, 0) << ',' << ccBase (t, 1) << ',' << ccBase (t, 2) << ',' << ccBase (t, 3) << ';';
    return sig;
}

bool TrossSeqProcessor::isMapModified() const
{
    const auto sig = mapSignature();
    const juce::ScopedLock sl (nameLock);
    return sig != mapSnapshot;
}

bool TrossSeqProcessor::saveMap (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    const auto f = mapFile (clean);
    f.getParentDirectory().createDirectory();
    juce::ValueTree root ("TROSSSEQMAP");
    root.setProperty ("name", clean, nullptr);
    root.setProperty ("version", 1, nullptr);
    root.appendChild (mapToTree(), nullptr);
    auto xml = root.createXml();
    if (xml == nullptr || ! xml->writeTo (f)) return false;
    setMapIdentity (clean);
    return true;
}

bool TrossSeqProcessor::loadMap (const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (mapFile (name));
    if (xml == nullptr || ! xml->hasTagName ("TROSSSEQMAP")) return false;
    const auto root = juce::ValueTree::fromXml (*xml);
    const auto m = root.getChildWithName ("MAP");
    if (! m.isValid()) return false;
    mapFromTree (m);
    setMapIdentity (mapFile (name).getFileNameWithoutExtension());
    return true;
}

bool TrossSeqProcessor::renameMap (const juce::String& from, const juce::String& to)
{
    const auto src = mapFile (from), dst = mapFile (to);
    if (! src.existsAsFile() || dst.existsAsFile() || juce::File::createLegalFileName (to.trim()).isEmpty()) return false;
    auto xml = juce::XmlDocument::parse (src);
    if (xml == nullptr) return false;
    xml->setAttribute ("name", dst.getFileNameWithoutExtension());
    return xml->writeTo (dst) && src.deleteFile();
}

bool TrossSeqProcessor::deleteMap (const juce::String& name)
{
    const auto f = mapFile (name);
    return f.existsAsFile() && (f.moveToTrash() || f.deleteFile());
}

juce::ValueTree TrossSeqProcessor::mapToTree() const
{
    juce::ValueTree t ("MAP");
    {
        const juce::ScopedLock sl (nameLock);
        t.setProperty ("mapName", mapName, nullptr);
        t.setProperty ("snapshot", mapSnapshot, nullptr);
    }
    for (int i = 0; i < kNumTracks; ++i)
    {
        juce::ValueTree tr ("TRACK");
        tr.setProperty ("name", getTrackName (i), nullptr);
        tr.setProperty ("note", map.note (i), nullptr);
        tr.setProperty ("channel", map.channel (i), nullptr);
        tr.setProperty ("locked", locked[i].load(), nullptr);
        tr.setProperty ("cc", juce::String (map.ccNumber (i, 0)) + " " + juce::String (map.ccNumber (i, 1)) + " "
                              + juce::String (map.ccNumber (i, 2)) + " " + juce::String (map.ccNumber (i, 3)), nullptr);
        tr.setProperty ("ccbase", juce::String (ccBase (i, 0)) + " " + juce::String (ccBase (i, 1)) + " "
                                  + juce::String (ccBase (i, 2)) + " " + juce::String (ccBase (i, 3)), nullptr);
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
        juce::StringArray cc; cc.addTokens (tr.getProperty ("cc", "-1 -1 -1 -1").toString(), " ", "");
        for (int k = 0; k < 4; ++k) map.setCcNumber (i, k, k < cc.size() ? cc[k].getIntValue() : -1);
        juce::StringArray cb; cb.addTokens (tr.getProperty ("ccbase", "64 64 64 64").toString(), " ", "");
        for (int k = 0; k < 4; ++k) setCcBase (i, k, k < cb.size() ? cb[k].getIntValue() : 64);
    }
    if (t.hasProperty ("mapName"))   // which map this is, and how it looked when picked (for the * mark)
    {
        { const juce::ScopedLock sl (nameLock); mapName = t["mapName"].toString(); mapSnapshot = t["snapshot"].toString(); }
        ++nameVersion;
    }
    else
        setMapIdentity ({});   // older projects: an unnamed map
}

void TrossSeqProcessor::setCcBase (int t, int k, int v)
{
    if (auto* p = apvts.getParameter (ccBaseId (t, k)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, 127, v)));
        p->endChangeGesture();
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

    if (! replaceWaiting) pushUndo ({ slot });   // remember the pattern as it was, for UNDO

    LaneInfo lanes[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto role = roleFor (getTrackName (t).toStdString(), map.note (t));
        lanes[t] = { role, locked[t].load() || (role == Role::Unused && op != RandomOp::Lane) };   // spare lanes are left alone
    }

    // a new pattern (or lane) drops the flams on the lanes it rewrites; flams are only ever set by hand
    uint16_t clearFlams = 0;
    if (op == RandomOp::NewPattern)
        for (int t = 0; t < kNumTracks; ++t) if (! lanes[t].locked) clearFlams |= (uint16_t) (1u << (t + 1));
    if (op == RandomOp::Lane && track >= 0 && track < kNumTracks) clearFlams = (uint16_t) (1u << (track + 1));
    if (replaceWaiting) clearFlams |= stagedBuf[waiting].clearFlams;

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
        b.clearFlams = clearFlams;
        staged.store (stagedWrite, std::memory_order_release);
        stagedWrite = 1 - stagedWrite;
    }
    else
        for (int r = 1; r < kNumRows; ++r)   // accent row stays as it is
        {
            patterns.set (slot, r, rows[r]);
            if ((clearFlams >> r) & 1) patterns.setFlams (slot, r, 0);
        }
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
    for (auto& sn : e.slots) restore (sn);
    if (e.hasChain) setChain (e.chain);
    return true;
}

// ---- undo snapshots: steps, flams and flam styles of whole slots
TrossSeqProcessor::SlotSnap TrossSeqProcessor::snap (int slot) const
{
    SlotSnap sn; sn.slot = slot;
    for (int r = 0; r < kNumRows; ++r)
    {
        sn.rows[r] = patterns.get (slot, r);
        sn.flams[r] = patterns.getFlams (slot, r);
        for (int s = 0; s < kNumSteps; ++s) sn.styles[r][s] = patterns.getFlamStyle (slot, r, s);
    }
    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < kNumSteps; ++s)
            for (int p = 0; p < kNumLockParams; ++p) sn.locks[t][s][p] = patterns.getLock (slot, t, s, p);
    return sn;
}

void TrossSeqProcessor::restore (const SlotSnap& sn)
{
    for (int r = 0; r < kNumRows; ++r)
    {
        patterns.set (sn.slot, r, sn.rows[r]);
        patterns.setFlams (sn.slot, r, sn.flams[r]);
        for (int s = 0; s < kNumSteps; ++s) patterns.setFlamStyle (sn.slot, r, s, sn.styles[r][s]);
    }
    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < kNumSteps; ++s)
            for (int p = 0; p < kNumLockParams; ++p) patterns.setLock (sn.slot, t, s, p, sn.locks[t][s][p]);
}

void TrossSeqProcessor::pushUndo (std::initializer_list<int> slots)
{
    UndoEntry e;
    for (int sl : slots) e.slots.push_back (snap (sl));
    undoStack.push_back (std::move (e));
    if (undoStack.size() > 32) undoStack.erase (undoStack.begin());
}

void TrossSeqProcessor::pushUndoAllSlots()
{
    UndoEntry e;
    for (int sl = 0; sl < kNumSlots; ++sl) e.slots.push_back (snap (sl));
    undoStack.push_back (std::move (e));
    if (undoStack.size() > 32) undoStack.erase (undoStack.begin());
}

// ---- clearing
void TrossSeqProcessor::clearCurrentPattern()
{
    const int slot = currentSlot.load();
    if (patterns.isEmpty (slot) && patterns.get (slot, 0) == 0) return;
    pushUndo ({ slot });
    patterns.clear (slot);
}

void TrossSeqProcessor::clearRow (int row, bool allPatterns)
{
    if (row < 0 || row >= kNumRows) return;
    if (allPatterns) pushUndoAllSlots(); else pushUndo ({ currentSlot.load() });
    for (int sl = 0; sl < kNumSlots; ++sl)
        if (allPatterns || sl == currentSlot.load())
        {
            patterns.set (sl, row, 0);
            if (row > 0) patterns.clearLocks (sl, row - 1);   // the lane's parameter locks go too
        }
}

void TrossSeqProcessor::clearRowFlams (int row, bool allPatterns)
{
    if (row <= 0 || row >= kNumRows) return;
    if (allPatterns) pushUndoAllSlots(); else pushUndo ({ currentSlot.load() });
    for (int sl = 0; sl < kNumSlots; ++sl)
        if (allPatterns || sl == currentSlot.load()) patterns.setFlams (sl, row, 0);
}

void TrossSeqProcessor::flamWholeRow (int row, uint8_t style)
{
    if (row <= 0 || row >= kNumRows) return;
    const int slot = currentSlot.load();
    pushUndo ({ slot });
    const auto steps = patterns.get (slot, row);
    patterns.setFlams (slot, row, steps);
    for (int s = 0; s < kNumSteps; ++s)
        if ((steps >> s) & 1) patterns.setFlamStyle (slot, row, s, style);
}

void TrossSeqProcessor::applyStaged()
{
    const int i = staged.exchange (-1, std::memory_order_acq_rel);
    if (i < 0) return;
    const auto& b = stagedBuf[i];
    for (int r = 1; r < kNumRows; ++r)
    {
        patterns.set (b.slot, r, b.rows[r]);
        if ((b.clearFlams >> r) & 1) patterns.setFlams (b.slot, r, 0);
    }
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

// fromSeq = the sequencer made it (steps, flams and their note-offs). With a negative PORT DELAY the
// sequencer runs ahead: the port gets it now, the host gets it held back to its place in the song.
void TrossSeqProcessor::emit (const juce::MidiMessage& m, int sample, bool fromSeq, juce::int64 hostSample)
{
    if (hostOutOn)
    {
        const auto at = fromSeq && aheadSamples > 0 ? (hostSample >= 0 ? hostSample : sample + aheadSamples) : (juce::int64) sample;
        if (at <= sample || at < 0) out.addEvent (m, sample);
        else if (numHeld < kMaxHeld && m.getRawDataSize() <= 3)
        {
            auto& h = held[numHeld++];
            h.size = m.getRawDataSize();
            std::memcpy (h.bytes, m.getRawData(), (size_t) h.size);
            h.due = at;
        }
    }
    if (directOut.isOpen()) directOut.push (m, blockStartMs + sample * 1000.0 / sampleRate + aheadMsPort);
    if (portSpy) portSpy (m, blockCounter + sample);
}

void TrossSeqProcessor::noteOn (int track, int velocity, int sample, juce::int64 gateSamples, bool fromSeq, juce::int64 hostSample, int pitch)
{
    const int ch = map.channel (track), nn = juce::jlimit (0, 127, map.note (track) + pitch);
    // the same note still sounding: end it first so the drum box sees a fresh hit
    for (int i = 0; i < numPending; ++i)
        if (pending[i].channel == ch && pending[i].note == nn)
        {
            emit (juce::MidiMessage::noteOff (ch, nn), sample, pending[i].fromSeq,
                  hostSample >= 0 ? hostSample : -1);
            pending[i] = pending[--numPending];
            break;
        }
    emit (juce::MidiMessage::noteOn (ch, nn, (juce::uint8) juce::jlimit (1, 127, velocity)), sample, fromSeq, hostSample);
    if (numPending < kMaxPending) pending[numPending++] = { ch, nn, sample + juce::jmax ((juce::int64) 1, gateSamples), fromSeq };
    else emit (juce::MidiMessage::noteOff (ch, nn), sample, fromSeq, hostSample);
    lastSent[track] = juce::Time::getMillisecondCounter();
}

void TrossSeqProcessor::flushNoteOffs (int sample)
{
    // notes held back for the host: send their note-offs now, drop the note-ons that hadn't gone out
    for (int i = 0; i < numHeld; ++i)
    {
        juce::MidiMessage m (held[i].bytes, held[i].size);
        if (m.isNoteOff()) out.addEvent (m, sample);
    }
    numHeld = 0;
    for (int i = 0; i < numPending; ++i)
        emit (juce::MidiMessage::noteOff (pending[i].channel, pending[i].note), sample);
    numPending = 0;
}

// the sequencer reached `step`: a queued pattern takes over on step 1, then the step plays
// ---- pattern chain
int TrossSeqProcessor::chainSlotFor (long long absStep, int len) const
{
    const int n = chainLen.load();
    if (n <= 0) return -1;
    long long bar = absStep >= 0 ? absStep / len : -((-absStep + len - 1) / len);
    const int idx = (int) (((bar % n) + n) % n);
    return chain[idx].load();
}

void TrossSeqProcessor::setChain (const std::vector<int>& c)
{
    const int n = juce::jmin ((int) c.size(), kMaxChain);
    chainLen = 0;                                   // audio thread sees no chain while it's rewritten
    for (int i = 0; i < n; ++i) chain[i] = juce::jlimit (0, kNumSlots - 1, c[(size_t) i]);
    chainLen = n;
    if (n == 0) chainPos = -1;
    else if (playStep.load() < 0) { currentSlot = chain[0].load(); chainPos = 0; }   // stopped: show the first one
    ++chainVersion;
}

void TrossSeqProcessor::pushChainUndo()
{
    UndoEntry e; e.hasChain = true; e.chain = getChain();
    undoStack.push_back (std::move (e));
    if (undoStack.size() > 32) undoStack.erase (undoStack.begin());
}

void TrossSeqProcessor::chainAppend (int slot)
{
    if (chainLen.load() >= kMaxChain) return;
    pushChainUndo();
    auto c = getChain(); c.push_back (slot);
    setChain (c);
}

void TrossSeqProcessor::chainClear()
{
    if (chainLen.load() == 0) return;
    pushChainUndo();
    setChain ({});
}

void TrossSeqProcessor::chainRemoveLast()
{
    if (chainLen.load() == 0) return;
    pushChainUndo();
    auto c = getChain(); c.pop_back();
    setChain (c);
}

void TrossSeqProcessor::doStep (int step, long long absStep, int len, int sample, bool seqOn, juce::int64 gateSamples, juce::int64 hostSample)
{
    const int chained = chainSlotFor (absStep, len);
    if (step == 0 && fillPlaying.load())   // the fill has played its bar: back to what was playing
    {
        fillPlaying = false;
        if (chained < 0) currentSlot = fillReturn;
    }
    if (chained >= 0)   // a chain decides which pattern plays, from the song position
    {
        if (! fillPlaying.load()) currentSlot = chained;
        pendingSlot = -1;
        const int n = chainLen.load();
        const long long bar = absStep >= 0 ? absStep / len : -((-absStep + len - 1) / len);
        chainPos = (int) (((bar % n) + n) % n);
        if (step == 0) applyStaged();
    }
    else if (step == 0)
    {
        if (pendingSlot.load() >= 0)
        {
            currentSlot = pendingSlot.load();
            pendingSlot = -1;
        }
        applyStaged();   // a randomised pattern takes over on step 1
    }
    if (step == 0 && fillQueued.load() >= 0)   // a fill was picked: it plays this bar, once
    {
        fillReturn = currentSlot.load();
        currentSlot = fillQueued.load();
        fillQueued = -1;
        fillPlaying = true;
    }
    if (! stutActive) playStep = step;   // while stuttering the LEDs follow the repeated steps
    if (! seqOn) return;
    playNotes (step, sample, gateSamples, hostSample);
}

double TrossSeqProcessor::stutterLength (int i)
{
    static const double len[5] = { 8.0, 4.0, 2.0, 1.0, 0.5 };
    return len[juce::jlimit (0, 4, i)];
}

// the step's notes. skipNudged: lanes with a NUDGE lock on this step are left to the nudge clock
void TrossSeqProcessor::playNotes (int step, int sample, juce::int64 gateSamples, juce::int64 hostSample, bool skipNudged)
{
    const int slot = currentSlot.load();
    for (int t = 0; t < kNumTracks; ++t)
        if (patterns.cell (slot, t + 1, step) && audible (t))
        {
            if (skipNudged && patterns.isLocked (slot, t, step, kLockNudge) && patterns.getLock (slot, t, step, kLockNudge) != 0) continue;
            playTrack (slot, step, t, sample, gateSamples, hostSample);
        }
}

// one lane's hit on one step, with its parameter locks: CHANCE, VELOCITY, GATE, PITCH, RATCHET and the 4 CCs
void TrossSeqProcessor::playTrack (int slot, int step, int t, int sample, juce::int64 gateSamples, juce::int64 hostSample)
{
    auto lock = [&] (int p) { return patterns.getLock (slot, t, step, p); };
    if (const auto c = lock (kLockChance); c != kNoLock && chanceRnd.nextInt (100) >= c) return;   // not this time
    const bool accent = patterns.cell (slot, 0, step);
    int vel = (int) (accent ? raw.accentVel->load() : raw.velocity->load());
    if (const auto v = lock (kLockVel); v != kNoLock) vel = v;
    if (const auto g = lock (kLockGate); g != kNoLock) gateSamples = (juce::int64) std::round (g / 100.0 * curSamplesPerStep);
    const int pitch = lock (kLockPitch) != kNoLock ? lock (kLockPitch) : 0;
    const int ch = map.channel (t);
    // CCs, Elektron style: a locked step sends its value, an unlocked step puts the lane's base value back.
    // Only sent when it differs from what the drum box last got, so lanes without locks send nothing extra.
    for (int k = 0; k < 4; ++k)
        if (const int n = map.ccNumber (t, k); n >= 0)
        {
            const auto lv = lock (kLockCC1 + k);
            const int v = juce::jlimit (0, 127, lv != kNoLock ? (int) lv : ccBase (t, k));
            if (ccSentNum[t][k] != n) { ccSentNum[t][k] = n; ccSentVal[t][k] = -1; }
            if (v != ccSentVal[t][k])
            {
                emit (juce::MidiMessage::controllerEvent (ch, n, v), sample, true, hostSample);
                ccSentVal[t][k] = v;
            }
        }
    const int hits = lock (kLockRatchet) != kNoLock ? juce::jlimit (1, 4, (int) lock (kLockRatchet)) : 1;
    if (hits > 1)
    {
        const auto spacing = (juce::int64) std::round (curSamplesPerStep / hits);
        gateSamples = juce::jmin (gateSamples, juce::jmax ((juce::int64) 1, spacing * 3 / 4));
        for (int k = 1; k < hits && numSched < kMaxSched; ++k)
        {
            SchedNote n; n.track = t; n.velocity = vel; n.due = sample + k * spacing; n.gate = gateSamples; n.pitch = pitch; n.kind = 0;
            sched[numSched++] = n;
        }
    }
    noteOn (t, vel, sample, gateSamples, true, hostSample, pitch);
}

// which pattern will play at absStep (the nudge clock looks half a step ahead, across pattern changes)
int TrossSeqProcessor::slotForStep (long long absStep, int len) const
{
    const int w = wrapStep (absStep, len);
    const int chained = chainSlotFor (absStep, len);
    if (w == 0 && fillQueued.load() >= 0) return fillQueued.load();
    if (fillPlaying.load())
    {
        if (w != 0) return currentSlot.load();
        if (chained >= 0) return chained;
        return pendingSlot.load() >= 0 ? pendingSlot.load() : fillReturn;
    }
    if (chained >= 0) return chained;
    if (w == 0 && pendingSlot.load() >= 0) return pendingSlot.load();
    return currentSlot.load();
}

bool TrossSeqProcessor::audible (int t) const
{
    const int held = momentarySolo.load();
    if (held >= 0) return t == held;            // Shift-held M beats everything
    bool anySolo = false;
    for (int i = 0; i < kNumTracks; ++i) anySolo |= raw.solo[i]->load() > 0.5f;
    if (anySolo) return raw.solo[t]->load() > 0.5f;   // S buttons: only soloed lanes play (even if muted)
    return raw.mute[t]->load() < 0.5f;
}

// `step` is coming up in lookSamples: schedule the grace notes of its flams (1 = flam, 2 = drag, 3 = ruff),
// spread evenly before the step and all over before the main hit
void TrossSeqProcessor::doGrace (int step, long long absStep, int len, int sample, juce::int64 lookSamples)
{
    // the pattern that will be playing at that step: the chain's, or a queued slot / random result on step 1
    int slot = currentSlot.load();
    const Staged* st = nullptr;
    const int chained = chainSlotFor (absStep, len);
    if (chained >= 0) slot = chained;
    if (step == 0)
    {
        if (chained < 0 && pendingSlot.load() >= 0) slot = pendingSlot.load();
        const int i = staged.load (std::memory_order_acquire);
        if (i >= 0 && stagedBuf[i].slot == slot) st = &stagedBuf[i];
    }
    auto rowOf = [&] (int r) { return st != nullptr ? st->rows[r] : patterns.get (slot, r); };
    const bool accent = (rowOf (0) >> step) & 1;
    const int vel = (int) (accent ? raw.accentVel->load() : raw.velocity->load());
    const float knobMs = raw.flamTime->load(), knobLevel = raw.flamLevel->load();
    for (int t = 0; t < kNumTracks; ++t)
    {
        const uint16_t fl = (st != nullptr && ((st->clearFlams >> (t + 1)) & 1)) ? 0 : patterns.getFlams (slot, t + 1);
        if (! (((rowOf (t + 1) & fl) >> step) & 1) || ! audible (t)) continue;
        const auto fs = patterns.flamStyle (slot, t + 1, step);
        const auto spacing = juce::jlimit ((juce::int64) 2, lookSamples,
                                           (juce::int64) std::round (fs.spacingMs (knobMs) / 1000.0 * sampleRate));
        const int graceVel = juce::jlimit (1, 127, juce::roundToInt (vel * fs.levelPct (knobLevel) / 100.0f));
        const auto gap = juce::jmax ((juce::int64) 1, spacing / fs.graces);
        for (int k = 0; k < fs.graces && numSched < kMaxSched; ++k)
        {
            const auto before = spacing - k * gap;   // first grace furthest from the step
            sched[numSched++] = { t, graceVel, sample + (lookSamples - before), juce::jmax ((juce::int64) 1, gap * 3 / 4) };
        }
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
    aheadSamples = offsetMs < 0.0f ? (juce::int64) std::llround (-offsetMs / 1000.0 * sampleRate) : 0;
    aheadMsPort  = juce::jmax (0.0, (double) offsetMs);
    updateBlockTime (numSamples);

    const bool seqOn    = raw.seqOn->load() > 0.5f;
    const bool recOn    = raw.rec->load() > 0.5f;
    const bool thru     = raw.thru->load() > 0.5f;
    const bool clockOut = raw.clockOut->load() > 0.5f;
    const int  len      = juce::jlimit (1, kNumSteps, (int) raw.seqLen->load());
    const int  accVel   = (int) raw.accentVel->load();
    clock.swingAmount   = StepClock::swingFromPercent (raw.swing->load());
    graceClock.swingAmount = clock.swingAmount;

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
    curSamplesPerStep = samplesPerStep;
    nudgeClock.swingAmount = StepClock::swingFromPercent (raw.swing->load());
    const auto gateSamples = (juce::int64) std::round (raw.gate->load() / 100.0 * samplesPerStep);
    // grace notes are looked for this far ahead: the longest flam spacing, but never more than half a step
    const double graceLook16 = juce::jmin (0.45, FlamStyle::kMaxSpacingMs / 1000.0 * sampleRate * sixteenthsPerSample);
    const auto graceLookSamples = (juce::int64) std::floor (graceLook16 / sixteenthsPerSample);
    const double ahead16 = (double) aheadSamples * sixteenthsPerSample;   // negative PORT DELAY, in 16ths
    if (isStandalone() && ! hostTransport && playing) internalPpq += numSamples * sixteenthsPerSample / 4.0;

    // ---- pattern selector: while playing, a new slot waits until the pattern is back at step 1
    const int sel = (int) raw.pattern->load();
    if (sel != lastPatternParam)
    {
        lastPatternParam = sel;
        if (chainLen.load() > 0) {}   // a chain is running: it picks the patterns (the editor clears it on a plain click)
        else if (playing) pendingSlot = (sel == currentSlot.load()) ? -1 : sel;
        else         { pendingSlot = -1; currentSlot = sel; }
    }

    // ---- stop
    if (! playing)
    {
        if (pendingSlot.load() >= 0) { currentSlot = pendingSlot.load(); pendingSlot = -1; }
        if (const int chained = chainSlotFor ((long long) std::floor (ppq * 4.0 + 1e-9), len); chained >= 0)
        {
            currentSlot = chained;   // stopped: show the chain step at the song position
            const int n = chainLen.load();
            const long long b = (long long) std::floor (ppq * 4.0 / len + 1e-9);
            chainPos = (int) (((b % n) + n) % n);
        }
        applyStaged();   // stopped before step 1 came round: write it now
        if (wasPlaying)
        {
            flushNoteOffs (0);
            if (clockOut && directOut.isOpen()) directOut.push (juce::MidiMessage::midiStop(), blockStartMs + aheadMsPort);
        }
        wasPlaying = false;
        clock.stop();
        graceClock.stop();
        nudgeClock.stop();
        numSched = 0;
        numHeld = 0;
        playStep = -1;
        stutActive = false; stutShown = false;
        if (fillPlaying.load()) { fillPlaying = false; currentSlot = fillReturn; }   // stopped mid-fill: back to the pattern
        skipSteps = skipGrace = 0;
        lastFiredStep = lastGraceStep = lastNudgeStep = LLONG_MIN; skipNudge = 0;
    }

    const double q0 = ppq * 4.0;   // position in 16ths at the block start

    // ---- the song jumped back (a loop) while the clocks were running ahead of it
    if (playing && wasPlaying && q0 < lastQEnd - 1e-6)
    {
        skipSteps = lastFiredStep == LLONG_MIN ? 0 : (int) juce::jlimit (0LL, 64LL, lastFiredStep - clock.firstAt (lastQEnd) + 1);
        skipGrace = lastGraceStep == LLONG_MIN ? 0 : (int) juce::jlimit (0LL, 64LL, lastGraceStep - clock.firstAt (lastQEnd + graceLook16) + 1);
        skipNudge = lastNudgeStep == LLONG_MIN ? 0 : (int) juce::jlimit (0LL, 64LL, lastNudgeStep - clock.firstAt (lastQEnd + 0.5) + 1);
        const auto ticksAhead = juce::jmax ((juce::int64) 0, lastClockTick - ((juce::int64) std::ceil (lastQEnd * 6.0 - 1e-9) - 1));
        lastClockTick = (juce::int64) std::ceil (q0 * 6.0 - 1e-9) - 1 + ticksAhead;
    }
    lastQEnd = q0 + numSamples * sixteenthsPerSample;

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
        const double t = blockStartMs + aheadMsPort;
        if (ppq < 0.01) directOut.push (juce::MidiMessage::midiStart(), t);
        else
        {
            directOut.push (juce::MidiMessage::songPositionPointer (juce::jlimit (0, 16383, (int) std::floor (ppq * 4.0))), t);
            directOut.push (juce::MidiMessage::midiContinue(), t);
        }
        lastClockTick = (juce::int64) std::ceil (ppq * 24.0 - 1e-9) - 1;
    }

    // ---- CC base values: sent once when the song starts, and straight away when one is moved (like a knob)
    {
        const bool starting = playing && ! wasPlaying;
        for (int t = 0; t < kNumTracks; ++t)
            for (int k = 0; k < 4; ++k)
            {
                const int n = map.ccNumber (t, k), b = ccBase (t, k);
                const bool moved = ccBaseSeen[t][k] >= 0 && b != ccBaseSeen[t][k];
                ccBaseSeen[t][k] = b;
                if (n < 0) continue;
                if (ccSentNum[t][k] != n) { ccSentNum[t][k] = n; ccSentVal[t][k] = -1; }
                if (starting || (moved && ccSentVal[t][k] != b))   // the start always sends: every pass begins from the same sound
                {
                    emit (juce::MidiMessage::controllerEvent (map.channel (t), n, b), 0, false);
                    ccSentVal[t][k] = b;
                }
            }
    }

    // ---- run through the block: steps, clock ticks and incoming MIDI in time order
    auto midiIt = midi.cbegin();
    for (int i = 0; i < numSamples; ++i)
    {
        const double q = q0 + i * sixteenthsPerSample;
        if (playing)
        {
            // ---- STUTTER: from the press, the slice it falls in plays over and over (snapped to the grid); the song
            // goes on underneath, so letting go lands exactly where the DAW is. ROLL halves the slice every 2 repeats.
            const bool stutWanted = raw.stutter->load() > 0.5f;
            const double qa = q + ahead16;
            if (stutWanted && ! stutActive)
            {
                stutActive = true;
                stutLen = stutterLength ((int) raw.stutRate->load());
                stutStart = stutSlice = std::floor (qa / stutLen + 1e-9) * stutLen;
                stutLastO = qa - stutSlice - 1e-6;   // steps before the press already played; one right on it plays now
                stutCount = 0;
            }
            else if (! stutWanted && stutActive) stutActive = false;
            stutShown = stutActive;
            auto grace = graceClock.advance (q + ahead16 + graceLook16, ahead16, q + graceLook16);
            if (grace >= 0 && skipGrace > 0) { --skipGrace; grace = -1; }
            if (grace >= 0) lastGraceStep = grace;
            if (grace >= 0 && seqOn && ! stutActive) doGrace (wrapStep (grace, len), grace, len, i, graceLookSamples);
            // ---- NUDGE: a clock half a step ahead hands nudged lanes to the scheduler, early or late
            auto nstep = nudgeClock.advance (q + ahead16 + 0.5, ahead16, q + 0.5);
            if (nstep >= 0 && skipNudge > 0) { --skipNudge; nstep = -1; }
            if (nstep >= 0)
            {
                lastNudgeStep = nstep;
                if (seqOn && ! stutActive)
                {
                    const int ns = wrapStep (nstep, len), nslot = slotForStep (nstep, len);
                    for (int t = 0; t < kNumTracks && numSched < kMaxSched; ++t)
                        if (patterns.cell (nslot, t + 1, ns) && patterns.isLocked (nslot, t, ns, kLockNudge))
                            if (const auto nu = patterns.getLock (nslot, t, ns, kLockNudge); nu != 0)
                            {
                                SchedNote n; n.track = t; n.kind = 1; n.slot = nslot; n.step = ns;
                                n.due = i + (juce::int64) std::round ((clock.stepTime (nstep) - (q + ahead16) + nu / 100.0) * samplesPerStep);
                                sched[numSched++] = n;
                            }
                }
            }
            for (int n = 0; n < numSched;)   // grace notes, ratchet repeats and nudged steps due now
                if (sched[n].due <= i)
                {
                    const auto sn = sched[n];
                    sched[n] = sched[--numSched];
                    if (sn.kind == 1) { if (audible (sn.track)) playTrack (sn.slot, sn.step, sn.track, i, gateSamples, -1); }
                    else noteOn (sn.track, sn.velocity, i, sn.gate, true, -1, sn.pitch);
                }
                else ++n;
            auto step = clock.advance (q + ahead16, ahead16, q);
            if (step >= 0 && skipSteps > 0) { --skipSteps; step = -1; }   // already played before the loop
            if (step >= 0)
            {
                lastFiredStep = step;
                juce::int64 hostAt = -1;   // the host's copy goes at the step's own place in the song
                if (aheadSamples > 0)
                    hostAt = juce::jmax ((juce::int64) i, (juce::int64) std::llround ((clock.stepTime (step) - q0) / sixteenthsPerSample));
                doStep (wrapStep (step, len), step, len, i, seqOn && ! stutActive, gateSamples, hostAt);
            }
            if (stutActive)
            {
                while (qa >= stutSlice + stutLen - 1e-9)
                {
                    stutSlice += stutLen;
                    stutLastO = -1e-6;   // a new repeat: its first step plays again
                    if (++stutCount % 2 == 0 && raw.stutRoll->load() > 0.5f && stutLen > 0.5 + 1e-9) stutLen *= 0.5;
                }
                const double o = qa - stutSlice;
                for (long long n = (long long) std::floor (stutStart); clock.stepTime (n) < stutStart + stutLen - 1e-9; ++n)
                {
                    const double at = clock.stepTime (n) - stutStart;
                    if (at >= -1e-9 && at > stutLastO + 1e-12 && at <= o + 1e-9)
                    {
                        playStep = wrapStep (n, len);
                        if (seqOn) playNotes (wrapStep (n, len), i, juce::jmin (gateSamples, (juce::int64) (stutLen * samplesPerStep * 0.9)), -1, false);
                    }
                }
                stutLastO = o;
            }
            if (clockOut && directOut.isOpen())
            {
                const auto tick = (juce::int64) std::floor ((q + ahead16) * 6.0 + 1e-9);   // 24 per quarter = 6 per 16th
                for (int k = 0; tick > lastClockTick && k < 48; ++k)                          // every tick, none skipped
                {
                    ++lastClockTick;
                    directOut.push (juce::MidiMessage::midiClock(), blockStartMs + i * 1000.0 / sampleRate + aheadMsPort);
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
                emit (juce::MidiMessage::noteOff (pending[p].channel, pending[p].note), i, pending[p].fromSeq);
                pending[p] = pending[--numPending];
            }
            else ++p;
    }
    for (int p = 0; p < numPending; ++p) pending[p].sample -= numSamples;
    for (int n = 0; n < numSched; ++n) sched[n].due -= numSamples;
    // notes held back for the host (negative PORT DELAY): out when their time comes
    for (int h = 0; h < numHeld;)
        if (held[h].due < numSamples)
        {
            out.addEvent (juce::MidiMessage (held[h].bytes, held[h].size), (int) juce::jmax ((juce::int64) 0, held[h].due));
            held[h] = held[--numHeld];
        }
        else { held[h].due -= numSamples; ++h; }
    blockCounter += numSamples;
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
    {
        juce::StringArray ch;
        for (int s : getChain()) ch.add (juce::String (s));
        pats.setProperty ("chain", ch.joinIntoString (" "), nullptr);
    }
    for (int s = 0; s < kNumSlots; ++s)
    {
        juce::StringArray rows;
        for (int r = 0; r < kNumRows; ++r) rows.add (juce::String::toHexString ((int) patterns.get (s, r)));
        pats.setProperty ("slot" + juce::String (s), rows.joinIntoString (" "), nullptr);
        juce::StringArray fl;
        bool any = false;
        for (int r = 0; r < kNumRows; ++r) { fl.add (juce::String::toHexString ((int) patterns.getFlams (s, r))); any |= patterns.getFlams (s, r) != 0; }
        if (any) pats.setProperty ("flams" + juce::String (s), fl.joinIntoString (" "), nullptr);
        juce::StringArray st;
        bool anyStyle = false;
        for (int r = 0; r < kNumRows; ++r)
        {
            juce::String hex;
            for (int k = 0; k < kNumSteps; ++k) { const auto b = patterns.getFlamStyle (s, r, k); hex << juce::String::toHexString ((int) b).paddedLeft ('0', 2); anyStyle |= b != 0; }
            st.add (hex);
        }
        if (anyStyle) pats.setProperty ("fstyle" + juce::String (s), st.joinIntoString (" "), nullptr);
        juce::StringArray lk;   // parameter locks: "lane.step.param=value"
        for (int t = 0; t < kNumTracks; ++t)
            for (int k = 0; k < kNumSteps; ++k)
                for (int p = 0; p < kNumLockParams; ++p)
                    if (patterns.isLocked (s, t, k, p))
                        lk.add (juce::String (t) + "." + juce::String (k) + "." + juce::String (p) + "=" + juce::String (patterns.getLock (s, t, k, p)));
        if (! lk.isEmpty()) pats.setProperty ("locks" + juce::String (s), lk.joinIntoString (" "), nullptr);
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
            juce::StringArray fl;
            fl.addTokens (pats.getProperty ("flams" + juce::String (s)).toString(), " ", "");
            for (int r = 0; r < kNumRows; ++r)
                patterns.setFlams (s, r, (uint16_t) (r < fl.size() ? fl[r].getHexValue32() & 0xffff : 0));
            juce::StringArray st;
            st.addTokens (pats.getProperty ("fstyle" + juce::String (s)).toString(), " ", "");
            for (int r = 0; r < kNumRows; ++r)
                for (int k = 0; k < kNumSteps; ++k)
                    patterns.setFlamStyle (s, r, k, (uint8_t) (r < st.size() && st[r].length() >= 2 * kNumSteps
                                                                   ? st[r].substring (2 * k, 2 * k + 2).getHexValue32() & 0xff : 0));
            patterns.clearLocks (s);
            juce::StringArray lk;
            lk.addTokens (pats.getProperty ("locks" + juce::String (s)).toString(), " ", "");
            for (auto& e : lk)
            {
                juce::StringArray f; f.addTokens (e.replaceCharacter ('=', '.'), ".", "");
                if (f.size() != 4) continue;
                const int t = f[0].getIntValue(), k = f[1].getIntValue(), p = f[2].getIntValue();
                if (t >= 0 && t < kNumTracks && k >= 0 && k < kNumSteps && p >= 0 && p < kNumLockParams)
                    patterns.setLock (s, t, k, p, (int16_t) f[3].getIntValue());
            }
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
    {
        std::vector<int> c;
        juce::StringArray ch;
        ch.addTokens (pats.isValid() ? pats.getProperty ("chain").toString() : juce::String(), " ", "");
        for (auto& t : ch) if (t.isNotEmpty()) c.push_back (t.getIntValue());
        setChain (c);
    }
}

juce::AudioProcessorEditor* TrossSeqProcessor::createEditor() { return new TrossSeqEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TrossSeqProcessor(); }
