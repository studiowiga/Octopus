#include "OctopusEngine.h"
#include "FxCategory.h"

OctopusEngine::OctopusEngine (ChainHost& h) : host (h)
{
    cc.fill (-1);
    latch.fill (false);
    ccPrev.fill (false);
    paramPrev.fill (false);
    paramLast.fill (-1.0f);
}

juce::String OctopusEngine::targetName (int t)
{
    static const char* n[numTargets] = { "Footswitch A", "Footswitch B", "Footswitch C", "Footswitch D", "Bank down", "Bank up", "PRESET / STOMP mode" };
    return n[juce::jlimit (0, numTargets - 1, t)];
}

juce::String OctopusEngine::label (int idx)
{
    idx = juce::jlimit (0, kPresets - 1, idx);
    return juce::String (idx / kSlots + 1) + juce::String::charToString ((juce::juce_wchar) ('A' + idx % kSlots));
}

juce::String OctopusEngine::displayName (int idx) const
{
    const auto& p = preset (idx);
    if (! p.saved) return "(empty)";
    return p.name.isNotEmpty() ? p.name : "Preset " + label (idx);
}

void OctopusEngine::say (const juce::String& m, double ms)
{
    msg = m;
    msgUntil = juce::Time::getMillisecondCounterHiRes() + ms;
    ++gen;
}

juce::String OctopusEngine::message() const
{
    return juce::Time::getMillisecondCounterHiRes() < msgUntil ? msg : juce::String();
}

void OctopusEngine::changed()
{
    ++gen;
    if (onStateChanged) onStateChanged();
}

// ------------------------------------------------------------------ image de la chaîne
void OctopusEngine::tick()
{
    // relecture de la chaîne ~7 fois par seconde : ce que l'utilisateur fait directement dans REAPER apparaît aussi
    if (++tickCount % 4 == 0) refreshNow();
    if (! msg.isEmpty() && juce::Time::getMillisecondCounterHiRes() >= msgUntil) { msg = {}; ++gen; }
}

void OctopusEngine::refreshNow()
{
    const bool r = host.ready();
    juce::Array<FxInfo> now;
    if (r)
    {
        const int n = host.count();
        for (int i = 0; i < n; ++i) now.add (host.info (i));
    }
    const auto st = host.statusText();
    const auto tn = r ? host.trackName() : juce::String();

    bool diff = r != ready || st != status || tn != track || now.size() != fx.size();
    for (int i = 0; ! diff && i < now.size(); ++i) diff = ! now.getReference (i).sameAs (fx.getReference (i));
    if (diff)
    {
        ready = r; status = st; track = tn;
        fx = now;
        blk.clearQuick();
        for (int i = 0; i < fx.size(); ++i) if (! fx.getReference (i).isSelf) blk.add (i);
        ++gen;
    }
    if (r && host.tokenClash() && onTokenClash) onTokenClash();
    if (r && sigPending) { sigPending = false; sigLoaded = signature(); ++gen; }
}

juce::String OctopusEngine::signature() const
{
    juce::String s;
    for (auto i : blk) { const auto& f = fx.getReference (i); s << f.original << (f.enabled ? "+" : "-") << (f.offline ? "o" : "") << f.parallel << "|"; }
    return s;
}

int OctopusEngine::blockOf (const juce::String& guid) const
{
    for (int k = 0; k < blk.size(); ++k) if (fx.getReference (blk[k]).guid == guid) return k;
    return -1;
}

int OctopusEngine::chainIndexForBlock (int blockPos) const
{
    if (blockPos < 0) blockPos = 0;
    if (blockPos >= blk.size()) return fx.size();          // à la fin de la chaîne
    return blk[blockPos];                                    // juste avant ce bloc
}

// ------------------------------------------------------------------ actions sur les blocs
void OctopusEngine::setEnabled (int i, bool on)
{
    if (! ready || ! juce::isPositiveAndBelow (i, fx.size()) || fx.getReference (i).isSelf) return;
    host.beginUndo();
    host.setEnabled (i, on);
    host.endUndo (juce::String (on ? "enable " : "bypass ") + FxCat::shortName (fx.getReference (i).name));
    refreshNow();
}

void OctopusEngine::toggle (int i)
{
    if (juce::isPositiveAndBelow (i, fx.size())) setEnabled (i, ! fx.getReference (i).enabled);
}

void OctopusEngine::remove (int i)
{
    if (! ready || ! juce::isPositiveAndBelow (i, fx.size()) || fx.getReference (i).isSelf) return;
    const auto guid = fx.getReference (i).guid;
    const auto name = FxCat::shortName (fx.getReference (i).name);
    host.beginUndo();
    const bool ok = host.remove (i);
    host.endUndo ("remove " + name);
    if (ok)
    {
        for (auto& s : stompGuids) s.removeString (guid);
        say ("Removed " + name);
        changed();
    }
    refreshNow();
}

void OctopusEngine::moveBlock (int i, int toBlockPos)
{
    if (! ready || ! juce::isPositiveAndBelow (i, fx.size()) || fx.getReference (i).isSelf) return;
    // ordre final de la chaîne complète : on retire l'effet, puis on le replace devant le bloc visé
    juce::Array<int> rest;
    for (int k = 0; k < fx.size(); ++k) if (k != i) rest.add (k);
    juce::Array<int> restBlocks;
    for (auto k : rest) if (! fx.getReference (k).isSelf) restBlocks.add (k);
    int finalIndex = rest.size();
    if (juce::isPositiveAndBelow (toBlockPos, restBlocks.size())) finalIndex = rest.indexOf (restBlocks[toBlockPos]);
    if (finalIndex == i) return;
    host.beginUndo();
    host.move (i, finalIndex);
    host.endUndo ("move " + FxCat::shortName (fx.getReference (i).name));
    refreshNow();
}

bool OctopusEngine::addFx (const InstalledFx& f, int blockPos)
{
    if (! ready) { say (status); return false; }
    const int at = chainIndexForBlock (blockPos);
    host.beginUndo();
    int pos = host.add (f.name, at);
    if (pos < 0 && f.name.startsWith ("JS") && f.ident.isNotEmpty())    // JS : par son chemin
        pos = host.add ("JS:" + f.ident, at);
    host.endUndo ("add " + FxCat::shortName (f.name));
    refreshNow();
    if (pos < 0) { say ("REAPER could not load " + FxCat::shortName (f.name)); return false; }
    say ("Added " + FxCat::shortName (f.name));
    return true;
}

int OctopusEngine::addFxList (const juce::Array<InstalledFx>& list, int blockPos)
{
    if (! ready) { say (status); return 0; }
    if (list.size() == 1) return addFx (list.getReference (0), blockPos) ? 1 : 0;
    int at = chainIndexForBlock (blockPos), added = 0;
    juce::StringArray failed;
    host.beginUndo();
    for (auto& f : list)
    {
        int pos = host.add (f.name, at);
        if (pos < 0 && f.name.startsWith ("JS") && f.ident.isNotEmpty()) pos = host.add ("JS:" + f.ident, at);
        if (pos < 0) { failed.add (FxCat::shortName (f.name)); continue; }
        at = pos + 1;                                             // le suivant juste après, dans l'ordre choisi
        ++added;
    }
    host.endUndo ("add " + juce::String (added) + " effects");
    refreshNow();
    if (failed.isEmpty()) say ("Added " + juce::String (added) + " effects");
    else say ("Added " + juce::String (added) + ", REAPER could not load " + failed.joinIntoString (", "), 4500.0);
    return added;
}

bool OctopusEngine::isChroma (const FxInfo& f)
{
    const auto n = f.original.isNotEmpty() ? f.original : f.name;
    return (n.containsIgnoreCase ("CHROMA") || n.containsIgnoreCase ("Tuna")) && n.containsIgnoreCase ("Studio Wiga");
}

int OctopusEngine::chromaIndex() const
{
    if (! ready) return -1;
    for (int i = 0; i < fx.size(); ++i)
    {
        const auto& f = fx.getReference (i);
        if (! f.isSelf && f.enabled && ! f.offline && isChroma (f)) return i;
    }
    return -1;
}

void OctopusEngine::openWindow (int i, bool show)
{
    if (ready && juce::isPositiveAndBelow (i, fx.size())) { host.showWindow (i, show); refreshNow(); }
}

void OctopusEngine::setParallel (int i, bool on)
{
    if (! ready || ! juce::isPositiveAndBelow (i, fx.size())) return;
    host.beginUndo();
    host.setParallel (i, on ? 1 : 0);
    host.endUndo ("parallel");
    refreshNow();
}

const juce::Array<InstalledFx>& OctopusEngine::installedList (bool reload)
{
    if (reload || ! installedLoaded)
    {
        installedCache = host.installed();
        installedLoaded = installedCache.size() > 0;
        std::sort (installedCache.begin(), installedCache.end(), [] (const InstalledFx& a, const InstalledFx& b)
                   { return FxCat::shortName (a.name).compareNatural (FxCat::shortName (b.name)) < 0; });
    }
    return installedCache;
}

// ------------------------------------------------------------------ footswitchs
void OctopusEngine::setMode (Mode m)
{
    if (m == md) return;
    md = m;
    say (m == presetMode ? "PRESET mode : footswitches recall presets" : "STOMP mode : footswitches switch blocks");
    changed();
}

void OctopusEngine::press (int f)
{
    if (! juce::isPositiveAndBelow (f, kFs)) return;
    if (md == presetMode)
    {
        const int idx = viewBank * kSlots + f;
        if (idx == cur && ! modified()) { say (label (idx) + " already loaded"); return; }
        load (idx);
        return;
    }
    // STOMP : bascule le groupe de blocs commandés (tous OFF si l'un est ON, sinon tous ON)
    juce::Array<int> idx;
    bool anyOn = false;
    for (auto& g : stompGuids[f])
        for (int i = 0; i < fx.size(); ++i)
            if (fx.getReference (i).guid == g) { idx.add (i); anyOn = anyOn || fx.getReference (i).enabled; }
    if (idx.isEmpty()) { say ("Footswitch " + juce::String::charToString ((juce::juce_wchar) ('A' + f)) + " : no block assigned (right-click a block)"); return; }
    host.beginUndo();
    for (auto i : idx) host.setEnabled (i, ! anyOn);
    host.endUndo ("footswitch " + juce::String::charToString ((juce::juce_wchar) ('A' + f)));
    refreshNow();
}

juce::String OctopusEngine::fsTitle (int f) const
{
    if (md == presetMode) return label (viewBank * kSlots + f);
    if (stompGuids[f].isEmpty()) return "STOMP " + juce::String::charToString ((juce::juce_wchar) ('A' + f));
    const int k = fsKind (f);
    return juce::String (FxCat::label (k)).toUpperCase();
}

juce::String OctopusEngine::fsText (int f) const
{
    if (md == presetMode) return displayName (viewBank * kSlots + f);
    juce::StringArray names;
    for (auto& g : stompGuids[f])
    {
        const int b = blockOf (g);
        if (b >= 0) names.add (FxCat::shortName (fx.getReference (blk[b]).name));
    }
    return names.isEmpty() ? juce::String ("-") : names.joinIntoString (" + ");
}

bool OctopusEngine::fsLit (int f) const
{
    if (md == presetMode) return cur == viewBank * kSlots + f;
    for (auto& g : stompGuids[f])
    {
        const int b = blockOf (g);
        if (b >= 0 && fx.getReference (blk[b]).enabled) return true;
    }
    return false;
}

int OctopusEngine::fsKind (int f) const
{
    for (auto& g : stompGuids[f])
    {
        const int b = blockOf (g);
        if (b >= 0) return FxCat::classify (fx.getReference (blk[b]).original);
    }
    return -1;
}

void OctopusEngine::toggleAssign (int f, const juce::String& guid)
{
    if (! juce::isPositiveAndBelow (f, kFs) || guid.isEmpty()) return;
    if (stompGuids[f].contains (guid)) stompGuids[f].removeString (guid);
    else stompGuids[f].add (guid);
    changed();
}

void OctopusEngine::clearAssign (int f)
{
    if (juce::isPositiveAndBelow (f, kFs)) { stompGuids[f].clear(); changed(); }
}

// ------------------------------------------------------------------ banks / presets
void OctopusEngine::setBank (int b)
{
    b = (b % kBanks + kBanks) % kBanks;
    if (b == viewBank) return;
    viewBank = b;
    say ("BANK " + juce::String (b + 1));
    changed();
}

void OctopusEngine::bankStep (int d) { setBank (viewBank + d); }

bool OctopusEngine::save (int idx)
{
    idx = juce::jlimit (0, kPresets - 1, idx);
    juce::StringArray entries; int selfPos = 0; juce::String err;
    refreshNow();
    if (! host.capture (entries, selfPos, err)) { say ("Save failed : " + err, 4000.0); return false; }
    auto& p = presets[(size_t) idx];
    p.entries.clear();
    for (auto& e : entries) p.entries.add (ChainChunk::forPreset (e));
    p.selfPos = selfPos;
    for (int f = 0; f < kFs; ++f)
    {
        p.stomp[f].clear();
        for (auto& g : stompGuids[f]) { const int b = blockOf (g); if (b >= 0) p.stomp[f].add (b); }
    }
    p.saved = true;
    if (p.name.isEmpty()) p.name = "Preset " + label (idx);
    cur = idx;
    viewBank = idx / kSlots;
    sigLoaded = signature();
    say ("Saved " + label (idx) + "  " + p.name);
    changed();
    return true;
}

// Rappel d'un preset sans recréer ce qui n'a pas changé :
//   - même suite d'effets (mêmes plugins, même ordre) : seuls les blocs dont les réglages diffèrent sont rechargés,
//     les autres reçoivent juste leur état ON / OFF -> passage quasi instantané entre presets d'une même chaîne ;
//   - sinon la chaîne est entièrement remplacée (Octopus reste en place).
bool OctopusEngine::load (int idx)
{
    idx = juce::jlimit (0, kPresets - 1, idx);
    const auto& p = presets[(size_t) idx];
    if (! ready) { say (status); return false; }
    if (! p.saved)
    {
        // preset vide : écran vide comme au démarrage (tous les effets retirés, sauf Octopus), prêt à être construit puis enregistré
        host.beginUndo();
        for (int i = host.count(); --i >= 0;)
            if (! host.info (i).isSelf) host.remove (i);
        host.endUndo ("empty preset " + label (idx));
        refreshNow();
        for (auto& g : stompGuids) g.clear();
        cur = idx;
        viewBank = idx / kSlots;
        sigLoaded = signature();
        sigPending = true;
        say (label (idx) + "  empty preset : add effects, then SAVE");
        changed();
        return true;
    }

    juce::StringArray now; int nowSelf = 0; juce::String err;
    if (! host.capture (now, nowSelf, err)) { say ("Load failed : " + err, 4000.0); return false; }

    const int target = juce::jlimit (0, p.entries.size(), p.selfPos);
    bool sameShape = now.size() == p.entries.size() && nowSelf == target;
    for (int i = 0; sameShape && i < now.size(); ++i)
        sameShape = ChainChunk::typeLine (now[i]) == ChainChunk::typeLine (p.entries[i]);

    auto chainIdx = [target] (int i) { return i < target ? i : i + 1; };
    host.beginUndo();
    juce::Array<int> reload;
    if (sameShape)
    {
        for (int i = 0; i < now.size(); ++i)
        {
            if (ChainChunk::settings (now[i]) != ChainChunk::settings (p.entries[i])) { reload.add (i); continue; }
            const bool on = ! ChainChunk::bypassed (p.entries[i]), off = ChainChunk::offline (p.entries[i]);
            if (ChainChunk::bypassed (now[i]) == on) host.setEnabled (chainIdx (i), on);
            if (ChainChunk::offline (now[i]) != off) host.setOffline (chainIdx (i), off);
        }
        for (int k = reload.size(); --k >= 0;) host.remove (chainIdx (reload[k]));
    }
    else
    {
        for (int i = host.count(); --i >= 0;)
            if (! host.info (i).isSelf) host.remove (i);
        for (int i = 0; i < p.entries.size(); ++i) reload.add (i);
    }

    juce::Array<EntryInsert> ins;
    for (auto i : reload) ins.add ({ p.entries[i], chainIdx (i) });
    const bool ok = host.insertEntries (ins, err);
    host.endUndo ("preset " + label (idx));

    refreshNow();
    // footswitchs du mode STOMP : positions du preset -> blocs réellement présents
    for (int f = 0; f < kFs; ++f)
    {
        stompGuids[f].clear();
        for (auto b : p.stomp[f])
            if (juce::isPositiveAndBelow (b, blk.size())) stompGuids[f].add (fx.getReference (blk[b]).guid);
    }
    cur = idx;
    viewBank = idx / kSlots;
    sigLoaded = signature();
    sigPending = true;
    if (! ok) say (label (idx) + " : " + err, 5000.0);
    else say (label (idx) + "  " + displayName (idx) + (reload.size() < p.entries.size() ? "  (instant)" : ""));
    changed();
    return ok;
}

void OctopusEngine::rename (int idx, const juce::String& n)
{
    auto& p = presets[(size_t) juce::jlimit (0, kPresets - 1, idx)];
    p.name = n.trim().substring (0, 40);
    changed();
}

void OctopusEngine::clear (int idx)
{
    idx = juce::jlimit (0, kPresets - 1, idx);
    presets[(size_t) idx] = Preset();
    if (cur == idx) { cur = -1; sigLoaded = {}; }
    say ("Cleared " + label (idx));
    changed();
}

void OctopusEngine::paste (int idx)
{
    if (! hasClip) return;
    idx = juce::jlimit (0, kPresets - 1, idx);
    presets[(size_t) idx] = clip;
    say ("Pasted into " + label (idx));
    changed();
}

void OctopusEngine::swapPresets (int a, int b)
{
    a = juce::jlimit (0, kPresets - 1, a); b = juce::jlimit (0, kPresets - 1, b);
    if (a == b) return;
    std::swap (presets[(size_t) a], presets[(size_t) b]);
    if (cur == a) cur = b; else if (cur == b) cur = a;
    changed();
}

// ------------------------------------------------------------------ MIDI
void OctopusEngine::resetMidi()
{
    cc.fill (-1);
    latch.fill (false);
    learnTarget = -1;
    programChange = true;
    changed();
}

void OctopusEngine::trigger (int t)
{
    if (t >= fsA && t <= fsD) press (t - fsA);
    else if (t == bankDown) bankStep (-1);
    else if (t == bankUp) bankStep (1);
    else if (t == modeToggle) setMode (md == presetMode ? stompMode : presetMode);
}

// momentané : chaque appui envoie ON puis OFF -> action au passage à ON ;
// à accrochage : chaque appui inverse l'état -> action à chaque changement
void OctopusEngine::input (int t, bool on, std::array<bool, numTargets>& prev, bool first)
{
    const bool was = prev[(size_t) t];
    prev[(size_t) t] = on;
    if (first) return;
    if (latch[(size_t) t] ? (on != was) : (on && ! was)) trigger (t);
}

void OctopusEngine::midiCc (int, int number, int value)
{
    if (learnTarget >= 0)
    {
        for (auto& c : cc) if (c == number) c = -1;           // un CC ne commande qu'une chose
        cc[(size_t) learnTarget] = number;
        say (targetName (learnTarget) + " = CC " + juce::String (number));
        ccPrev[(size_t) learnTarget] = value >= 64;
        learnTarget = -1;
        changed();
        return;
    }
    for (int t = 0; t < numTargets; ++t)
        if (cc[(size_t) t] == number) input (t, value >= 64, ccPrev, false);
}

void OctopusEngine::midiProgram (int, int program)
{
    if (! programChange || ! juce::isPositiveAndBelow (program, kPresets)) return;
    load (program);
}

void OctopusEngine::paramInput (int t, float v)
{
    if (! juce::isPositiveAndBelow (t, (int) numTargets)) return;
    const bool first = paramLast[(size_t) t] < 0.0f;
    if (! first && std::abs (v - paramLast[(size_t) t]) < 1.0e-4f) return;
    paramLast[(size_t) t] = v;
    input (t, v >= 0.5f, paramPrev, first);
}

// ------------------------------------------------------------------ état
juce::ValueTree OctopusEngine::presetToTree (const Preset& p, int idx)
{
    juce::ValueTree t ("PRESET");
    t.setProperty ("idx", idx, nullptr);
    t.setProperty ("name", p.name, nullptr);
    t.setProperty ("self", p.selfPos, nullptr);
    for (int f = 0; f < kFs; ++f)
    {
        juce::StringArray s;
        for (auto b : p.stomp[f]) s.add (juce::String (b));
        t.setProperty ("stomp" + juce::String (f), s.joinIntoString (","), nullptr);
    }
    for (auto& e : p.entries)
    {
        juce::ValueTree fxT ("FX");
        fxT.setProperty ("chunk", e, nullptr);
        t.appendChild (fxT, nullptr);
    }
    return t;
}

OctopusEngine::Preset OctopusEngine::presetFromTree (const juce::ValueTree& t)
{
    Preset p;
    p.saved = true;
    p.name = t["name"].toString();
    p.selfPos = (int) t["self"];
    for (int f = 0; f < kFs; ++f)
    {
        auto s = juce::StringArray::fromTokens (t["stomp" + juce::String (f)].toString(), ",", "");
        for (auto& x : s) if (x.isNotEmpty()) p.stomp[f].add (x.getIntValue());
    }
    for (auto c : t)
        if (c.hasType ("FX")) p.entries.add (c["chunk"].toString());
    return p;
}

juce::ValueTree OctopusEngine::banksTree() const
{
    juce::ValueTree t ("OCTOPUS_BANKS");
    t.setProperty ("version", 1, nullptr);
    for (int i = 0; i < kPresets; ++i)
        if (presets[(size_t) i].saved) t.appendChild (presetToTree (presets[(size_t) i], i), nullptr);
    return t;
}

bool OctopusEngine::importBanks (const juce::ValueTree& t)
{
    if (! t.hasType ("OCTOPUS_BANKS") && ! t.hasType ("OCTOPUS")) return false;
    for (auto& p : presets) p = Preset();
    for (auto c : t)
        if (c.hasType ("PRESET"))
        {
            const int i = (int) c["idx"];
            if (juce::isPositiveAndBelow (i, kPresets)) presets[(size_t) i] = presetFromTree (c);
        }
    cur = -1; sigLoaded = {};
    say ("Banks imported");
    changed();
    return true;
}

juce::ValueTree OctopusEngine::toTree() const
{
    auto t = banksTree();
    t = t.createCopy();
    juce::ValueTree root ("OCTOPUS");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("mode", (int) md, nullptr);
    root.setProperty ("bank", viewBank, nullptr);
    root.setProperty ("current", cur, nullptr);
    root.setProperty ("sig", sigLoaded, nullptr);
    root.setProperty ("pc", programChange, nullptr);
    juce::StringArray c, l;
    for (int i = 0; i < numTargets; ++i) { c.add (juce::String (cc[(size_t) i])); l.add (latch[(size_t) i] ? "1" : "0"); }
    root.setProperty ("cc", c.joinIntoString (","), nullptr);
    root.setProperty ("latch", l.joinIntoString (","), nullptr);
    for (int f = 0; f < kFs; ++f) root.setProperty ("stompGuids" + juce::String (f), stompGuids[f].joinIntoString (";"), nullptr);
    for (int f = 0; f < kFs; ++f) root.setProperty ("fsColour" + juce::String (f), juce::String::toHexString ((int) fsCol[(size_t) f]), nullptr);
    while (t.getNumChildren() > 0) { auto ch = t.getChild (0); t.removeChild (0, nullptr); root.appendChild (ch, nullptr); }
    return root;
}

void OctopusEngine::fromTree (const juce::ValueTree& root)
{
    if (! root.hasType ("OCTOPUS")) return;
    for (auto& p : presets) p = Preset();
    for (auto c : root)
        if (c.hasType ("PRESET"))
        {
            const int i = (int) c["idx"];
            if (juce::isPositiveAndBelow (i, kPresets)) presets[(size_t) i] = presetFromTree (c);
        }
    md = (int) root["mode"] == 1 ? stompMode : presetMode;
    viewBank = juce::jlimit (0, kBanks - 1, (int) root["bank"]);
    cur = juce::jlimit (-1, kPresets - 1, (int) root.getProperty ("current", -1));
    sigLoaded = root["sig"].toString();
    programChange = (bool) root.getProperty ("pc", true);
    auto c = juce::StringArray::fromTokens (root["cc"].toString(), ",", "");
    auto l = juce::StringArray::fromTokens (root["latch"].toString(), ",", "");
    for (int i = 0; i < numTargets; ++i)
    {
        cc[(size_t) i] = i < c.size() && c[i].isNotEmpty() ? c[i].getIntValue() : -1;
        latch[(size_t) i] = i < l.size() && l[i] == "1";
    }
    for (int f = 0; f < kFs; ++f)
        stompGuids[f] = juce::StringArray::fromTokens (root["stompGuids" + juce::String (f)].toString(), ";", "");
    for (auto& s : stompGuids) s.removeEmptyStrings();
    for (int f = 0; f < kFs; ++f)
    {
        const auto hex = root["fsColour" + juce::String (f)].toString();
        fsCol[(size_t) f] = hex.isNotEmpty() ? (juce::uint32) hex.getHexValue64() : kDefaultFsColour;
    }
    ++gen;
}
