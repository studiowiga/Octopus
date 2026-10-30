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

    // presets : état complet de la chaîne au format texte de REAPER (sans Octopus)
    //   capture : effets dans l'ordre, et position d'Octopus parmi eux (0 = Octopus en premier)
    virtual bool capture (juce::StringArray& entries, int& selfPos, juce::String& error) = 0;
    //   insère des effets complets (réglages compris) aux positions données (triées par position croissante)
    virtual bool insertEntries (const juce::Array<EntryInsert>& items, juce::String& error) = 0;

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

    int numParams (int i) override { return ok (i) ? 11 : 0; }
    juce::String paramName (int, int p) override
    {
        static const char* n[] = { "Level", "Tone", "Gain", "Mix", "Time", "Feedback", "Rate", "Depth", "Bypass", "Wet", "Delta" };
        return n[juce::jlimit (0, 10, p)];
    }
    juce::String paramText (int i, int p) override { return juce::String (juce::roundToInt (getParam (i, p) * 100.0)) + " %"; }
    double getParam (int i, int p) override
    {
        auto k = chain[i].guid + "/" + juce::String (p);
        return values.contains (k) ? (double) values[k] : 0.25 + 0.06 * (double) ((p * 7 + i * 3) % 10);
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
    juce::NamedValueSet values;
    int nextId = 1;

    bool ok (int i) const { return juce::isPositiveAndBelow (i, chain.size()); }
    juce::String settingsOf (const juce::String& guid)
    {
        juce::String s;
        for (int p = 0; p < 11; ++p) { auto k = guid + "/" + juce::String (p); if (values.contains (k)) s << p << "=" << values[k].toString() << ";"; }
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
