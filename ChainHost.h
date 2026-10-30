#pragma once
#include <juce_core/juce_core.h>
#include "ChainChunk.h"

// Accès à la chaîne d'effets de la piste qui héberge Octopus.
// Les indices sont ceux de REAPER (Octopus compris). Toutes les méthodes s'appellent sur le thread de messages
// (jamais depuis le thread audio).
struct FxInfo
{
    juce::String name;        // nom affiché par REAPER (renommage de l'utilisateur compris)
    juce::String original;    // nom d'origine "VST3: Holyverb (Studio Wiga)"
    juce::String guid;        // identifiant unique et stable de l'instance dans le projet
    bool enabled = true;
    bool offline = false;
    bool open = false;        // fenêtre flottante ouverte
    bool isSelf = false;      // c'est cette instance d'Octopus
    int parallel = 0;         // 1 = traité en parallèle avec l'effet précédent (REAPER 7)

    bool sameAs (const FxInfo& o) const
    {
        return name == o.name && original == o.original && guid == o.guid && enabled == o.enabled
            && offline == o.offline && open == o.open && isSelf == o.isSelf && parallel == o.parallel;
    }
};

struct InstalledFx
{
    juce::String name;        // "VST3: Holyverb (Studio Wiga)" : c'est ce nom qu'on passe pour l'ajouter
    juce::String ident;
};

struct EntryInsert
{
    juce::String entry;       // effet au format texte de REAPER (ChainChunk)
    int at = 0;               // position finale dans la chaîne (indices REAPER, Octopus compris)
};

// presets d'un plugin (ceux de la liste de presets de REAPER : usine + utilisateur)
struct PluginPresets
{
    juce::StringArray names;      // dans l'ordre de REAPER (index de TrackFX_SetPresetByIndex)
    juce::Array<bool> user;       // preset utilisateur (fichier .ini de REAPER) ou d'usine
    int current = -1;             // preset actif (-1 = réglages modifiés / aucun)
    int count = 0;
    bool byName = false;          // noms sans index fiables : charger par le nom
};

class ChainHost
{
public:
    virtual ~ChainHost() = default;

    // false hors de REAPER ou si la piste n'est pas (encore) connue ; statusText dit pourquoi
    virtual bool ready() = 0;
    virtual juce::String statusText() = 0;
    virtual juce::String trackName() = 0;
    virtual bool isInputChain() = 0;         // Octopus est dans les effets d'entrée (enregistrement) de la piste
    virtual bool tokenClash() { return false; }

    // lecture de la chaîne (celle où se trouve Octopus)
    virtual int count() = 0;
    virtual FxInfo info (int index) = 0;

    // actions
    virtual void setEnabled (int index, bool on) = 0;
    virtual void setOffline (int index, bool off) = 0;
    virtual void setParallel (int index, int mode) = 0;
    virtual int  add (const juce::String& fxName, int insertAt) = 0;   // renvoie le nouvel indice ou -1
    virtual bool remove (int index) = 0;
    virtual void move (int from, int to) = 0;                          // to = indice final voulu
    virtual void showWindow (int index, bool show) = 0;
    virtual void beginUndo() = 0;
    virtual void endUndo (const juce::String& what) = 0;

    // paramètres d'un effet (panneau à l'écran)
    virtual int numParams (int index) = 0;
    virtual juce::String paramName (int index, int p) = 0;
    virtual juce::String paramText (int index, int p) = 0;
    virtual double getParam (int index, int p) = 0;
    virtual void setParam (int index, int p, double v) = 0;
    virtual int paramSteps (int index, int p) = 0;      // 0 = continu, 2 = interrupteur, n = n positions (modes...)

    // presets : état complet de la chaîne au format texte de REAPER (sans Octopus)
    //   capture : effets dans l'ordre, et position d'Octopus parmi eux (0 = Octopus en premier)
    virtual bool capture (juce::StringArray& entries, int& selfPos, juce::String& error) = 0;
    //   insère des effets complets (réglages compris) aux positions données (triées par position croissante)
    virtual bool insertEntries (const juce::Array<EntryInsert>& items, juce::String& error) = 0;

    // presets du plugin : probe = lire aussi les noms (REAPER ne donne que le nombre : chaque preset est chargé
    // un instant puis l'état exact du plugin est rétabli)
    virtual PluginPresets presets (int index, bool probe) = 0;
    virtual juce::String presetName (int index) = 0;
    virtual bool loadPreset (int index, int presetIndex, const juce::String& name) = 0;
    virtual bool stepPreset (int index, int dir) = 0;

    // état d'un seul effet au format texte de REAPER (ChainChunk)
    virtual juce::String fxChunk (int index) = 0;

    // effets installés (liste de REAPER)
    virtual juce::Array<InstalledFx> installed() = 0;
};

// Chaîne simulée : sert à l'outil de rendu sous Linux et aux tests (aucun REAPER). Elle produit et relit le même
// format texte que REAPER, pour tester la logique des presets.
class DemoChainHost : public ChainHost
{
public:
    DemoChainHost()
    {
        chain.add (make ("VST3: Octopus (Studio Wiga)", true, true));
        for (auto* n : { "VST3: CHROMA (Studio Wiga)", "VST3: NGATE (Studio Wiga)", "VST3: Crycat (Studio Wiga)",
                         "VST3: Doorn (Studio Wiga)", "VST3: Wharry (Studio Wiga)", "VST3: Small Tone (Studio Wiga)",
                         "VST3: Genome (Two notes Audio Engineering)", "VST3: ReaEQ (Cockos)",
                         "VST3: ECHOFLEX (Studio Wiga)", "VST3: Holyverb (Studio Wiga)" })
            chain.add (make (n, true, false));
        chain.getReference (1).enabled = false;
        chain.getReference (3).enabled = false;
        chain.getReference (6).enabled = false;
        chain.getReference (10).enabled = false;
    }

    bool ready() override                { return true; }
    juce::String statusText() override   { return "REAPER (demo)"; }
    juce::String trackName() override    { return "Guitare 7 cordes"; }
    bool isInputChain() override         { return false; }
    int count() override                 { return chain.size(); }
    FxInfo info (int i) override         { return juce::isPositiveAndBelow (i, chain.size()) ? chain[i] : FxInfo(); }
    void setEnabled (int i, bool on) override { if (ok (i)) chain.getReference (i).enabled = on; }
    void setOffline (int i, bool off) override { if (ok (i)) chain.getReference (i).offline = off; }
    void setParallel (int i, int m) override  { if (ok (i)) chain.getReference (i).parallel = m; }

    int add (const juce::String& n, int at) override
    {
        at = juce::jlimit (0, chain.size(), at);
        chain.insert (at, make (n, true, false));
        return at;
    }
    bool remove (int i) override
    {
        if (! ok (i) || chain[i].isSelf) return false;
        chain.remove (i);
        return true;
    }
    void move (int from, int to) override
    {
        if (! ok (from)) return;
        chain.move (from, juce::jlimit (0, chain.size() - 1, to));
    }
    void showWindow (int i, bool show) override { if (ok (i)) chain.getReference (i).open = show; }
    void beginUndo() override {}
    void endUndo (const juce::String&) override {}

    bool isChroma (int i) const { return ok (i) && chain[i].original.containsIgnoreCase ("CHROMA"); }
    static constexpr int kDemoParams = 16;
    int numParams (int i) override { return ok (i) ? (isChroma (i) ? 3 : kDemoParams) : 0; }
    juce::String paramName (int i, int p) override
    {
        if (isChroma (i)) { static const char* c[] = { "Mute", "Mode", "Reference A4" }; return c[juce::jlimit (0, 2, p)]; }
        static const char* n[kDemoParams] = { "Level", "Tone", "Gain", "Mix", "Time", "Feedback", "Mode", "Tempo Sync", "Rate", "Depth",
                                              "MIDI Channel", "MIDI Pedal Invert", "MIDI CC 0|0", "Bypass", "Wet", "Delta" };
        return n[juce::jlimit (0, kDemoParams - 1, p)];
    }
    int paramSteps (int i, int p) override
    {
        if (isChroma (i)) return p == 0 ? 2 : (p == 1 ? 3 : 21);
        return p == 6 ? 4 : (p == 7 || p == 11 || p == 13 ? 2 : (p == 10 ? 17 : 0));
    }
    juce::String paramText (int i, int p) override
    {
        const double v = getParam (i, p);
        const int st = paramSteps (i, p);
        if (st >= 2)
        {
            const int k = juce::roundToInt (v * (st - 1));
            if (! isChroma (i) && p == 6) { static const char* m[4] = { "Digital", "Analog", "Tape", "Reverse" }; return m[k]; }
            if (st == 2) return k ? "On" : "Off";
            return p == 10 ? (k == 0 ? juce::String ("Omni") : juce::String (k)) : juce::String (k + 1);
        }
        return juce::String (juce::roundToInt (v * 100.0)) + " %";
    }
    double getParam (int i, int p) override
    {
        auto k = chain[i].guid + "/" + juce::String (p);
        double v = values.contains (k) ? (double) values[k] : 0.25 + 0.06 * (double) ((p * 7 + i * 3) % 10);
        const int st = paramSteps (i, p);
        if (st >= 2) v = std::round (v * (st - 1)) / (st - 1);
        return v;
    }
    void setParam (int i, int p, double v) override { values.set (chain[i].guid + "/" + juce::String (p), v); }

    bool capture (juce::StringArray& entries, int& selfPos, juce::String&) override
    {
        entries.clear(); selfPos = 0;
        for (auto& f : chain)
        {
            if (f.isSelf) { selfPos = entries.size(); continue; }
            juce::String e;
            e << "BYPASS " << (f.enabled ? 0 : 1) << " " << (f.offline ? 1 : 0) << " 0\n"
              << "<VST \"" << f.original << "\" demo.vst3 0 \"\" 0<00> \"\"\n"
              << juce::Base64::toBase64 (f.original + "|" + settingsOf (f.guid)) << "\n>\n"
              << "FLOATPOS 0 0 0 0\nFXID " << f.guid << "\nWAK 0 0\n";
            entries.add (e);
        }
        return true;
    }

    bool insertEntries (const juce::Array<EntryInsert>& items, juce::String&) override
    {
        for (auto& it : items)
        {
            auto f = make (ChainChunk::displayName (it.entry), ! ChainChunk::bypassed (it.entry), false);
            f.offline = ChainChunk::offline (it.entry);
            chain.insert (juce::jlimit (0, chain.size(), it.at), f);
        }
        return true;
    }

    PluginPresets presets (int i, bool probe) override
    {
        PluginPresets p;
        if (! ok (i) || isChroma (i)) return p;
        static const char* f[] = { "Default", "Clean Ambient", "Crunch", "Lead Boost", "Doom", "Wide Stereo", "Vintage", "Subtle" };
        p.count = 10; p.current = presetIdx.contains (chain[i].guid) ? (int) presetIdx[chain[i].guid] : 0;
        if (probe)
        {
            for (auto* n : f) { p.names.add (n); p.user.add (false); }
            p.names.add ("My live sound"); p.user.add (true);
            p.names.add ("Studio 2026"); p.user.add (true);
        }
        return p;
    }
    juce::String presetName (int i) override
    {
        const auto p = presets (i, true);
        return juce::isPositiveAndBelow (p.current, p.names.size()) ? p.names[p.current] : juce::String();
    }
    bool loadPreset (int i, int idx, const juce::String&) override
    {
        if (! ok (i)) return false;
        presetIdx.set (chain[i].guid, idx);
        for (int k = 0; k < 6; ++k) values.set (chain[i].guid + "/" + juce::String (k), std::fmod (0.17 * (idx + 1) + 0.09 * k, 1.0));
        return true;
    }
    bool stepPreset (int i, int dir) override
    {
        if (! ok (i)) return false;
        const auto p = presets (i, false);
        return loadPreset (i, (p.current + dir + p.count) % p.count, {});
    }
    juce::String fxChunk (int i) override
    {
        if (! ok (i)) return {};
        juce::String xml = "<?xml version=\"1.0\"?><TunaState tuningKey=\"factory|GUITARE 7 CORDES|Sol standard (7)\" "
                           "tuningNotes=\"G1 D2 G2 C3 F3 A3 D4\" tuningName=\"G standard (7)\"><PARAM id=\"mode\" value=\"0.0\"/></TunaState>";
        return "BYPASS 0 0 0\n<VST \"" + chain[i].original + "\" demo.vst3 0 \"\" 0<00> \"\"\n"
               + juce::Base64::toBase64 ("VC2!" + xml) + "\n>\nFLOATPOS 0 0 0 0\nWAK 0 0\n";
    }

    juce::Array<InstalledFx> installed() override
    {
        juce::Array<InstalledFx> a;
        for (auto* n : { "VST3: Holyverb (Studio Wiga)", "VST3: ECHOFLEX (Studio Wiga)", "VST3: Eight Echo (Studio Wiga)",
                         "VST3: Doorn (Studio Wiga)", "VST3: Peachy (Studio Wiga)", "VST3: Wharry (Studio Wiga)",
                         "VST3: FOG Z (Studio Wiga)", "VST3: Ring Think (Studio Wiga)", "VST3: Textural (Studio Wiga)",
                         "VST3: Pipeline (Studio Wiga)", "VST3: Small Tone (Studio Wiga)", "VST3: CHROMA (Studio Wiga)",
                         "VST3: Crycat (Studio Wiga)", "VST3: NGATE (Studio Wiga)", "VST3: Fantomine (Studio Wiga)",
                         "VST3: Genome (Two notes Audio Engineering)", "VST3: ReaEQ (Cockos)", "VST3: ReaComp (Cockos)",
                         "VST3: ReaDelay (Cockos)", "VST3: ReaVerbate (Cockos)", "VST3: ReaGate (Cockos)",
                         "VST: ReaPitch (Cockos)", "JS: 3-Band EQ", "JS: Chorus", "JS: Volume Adjustment",
                         "CLAP: Surge XT Effects (Surge Synth Team)" })
            a.add ({ n, n });
        return a;
    }

private:
    juce::Array<FxInfo> chain;
    juce::NamedValueSet values, presetIdx;
    int nextId = 1;

    bool ok (int i) const { return juce::isPositiveAndBelow (i, chain.size()); }
    juce::String settingsOf (const juce::String& guid)
    {
        juce::String s;
        for (int p = 0; p < kDemoParams; ++p) { auto k = guid + "/" + juce::String (p); if (values.contains (k)) s << p << "=" << values[k].toString() << ";"; }
        return s;
    }

    FxInfo make (const juce::String& n, bool on, bool self)
    {
        FxInfo f;
        f.name = n; f.original = n; f.enabled = on; f.isSelf = self;
        f.guid = "{DEMO-" + juce::String (nextId++) + "}";
        return f;
    }
};
