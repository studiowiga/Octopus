#pragma once
#include <functional>
#include "ChainHost.h"

namespace Steinberg { class FUnknown; }

// Pont vers REAPER : REAPER donne aux plugins VST3 une interface spéciale (IReaperHostApplication, SDK officiel de
// REAPER, reaper_vst3_interfaces.h) qui donne la piste qui héberge le plugin et toute l'API de REAPER (les fonctions
// des scripts ReaScript). Toutes les méthodes s'appellent sur le thread de messages.
class ReaperBridge : public ChainHost
{
public:
    // token : valeur actuelle du paramètre interne "instance" de CETTE instance d'Octopus (sert à se reconnaître
    // dans la chaîne, même s'il y a plusieurs Octopus sur la piste)
    explicit ReaperBridge (std::function<double()> token);
    ~ReaperBridge() override;

    // appelé par l'enveloppe VST3 de JUCE (VST3ClientExtensions::setIHostApplication)
    void attach (Steinberg::FUnknown* hostApplication);
    bool isReaper() const noexcept { return reaper != nullptr; }
    juce::String reaperVersion();

    bool ready() override;
    juce::String statusText() override { return status; }
    juce::String trackName() override;
    bool isInputChain() override { return chainFlag != 0; }
    bool tokenClash() override { return clash; }
    int count() override;
    FxInfo info (int index) override;
    void setEnabled (int index, bool on) override;
    void setOffline (int index, bool off) override;
    void setParallel (int index, int mode) override;
    int  add (const juce::String& fxName, int insertAt) override;
    bool remove (int index) override;
    void move (int from, int to) override;
    void showWindow (int index, bool show) override;
    void beginUndo() override;
    void endUndo (const juce::String& what) override;
    int numParams (int index) override;
    juce::String paramName (int index, int p) override;
    juce::String paramText (int index, int p) override;
    double getParam (int index, int p) override;
    void setParam (int index, int p, double v) override;
    bool capture (juce::StringArray& entries, int& selfPos, juce::String& error) override;
    bool insertEntries (const juce::Array<EntryInsert>& items, juce::String& error) override;
    juce::Array<InstalledFx> installed() override;

private:
    struct Api;                               // pointeurs vers les fonctions de l'API de REAPER
    std::unique_ptr<Api> api;
    void* reaper = nullptr;                   // IReaperHostApplication* (référence tenue)
    std::function<double()> tokenFn;
    void* track = nullptr;                    // MediaTrack*
    void* project = nullptr;                  // ReaProject*
    int chainFlag = 0;                        // 0 = chaîne normale, 0x1000000 = effets d'entrée
    int selfRaw = -1;                         // position d'Octopus dans sa chaîne (-1 = pas trouvé)
    int undoDepth = 0;
    bool undoTouchedTracks = false;           // une piste temporaire a été créée : point d'annulation complet
    bool clash = false;
    juce::String status { "Octopus works inside REAPER only" };

    void resolve();
    bool locateSelf();
    bool looksLikeOctopus (int raw);
    int rawCount (bool rec);
    bool validIndex (int i);
    bool trackChunk (void* tr, juce::String& out);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReaperBridge)
};
