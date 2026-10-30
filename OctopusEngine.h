#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "ChainHost.h"

// Cerveau d'Octopus (thread de messages uniquement) : image de la chaîne de la piste, actions sur les blocs,
// banks / presets, mode PRESET / STOMP des footswitchs, MIDI learn. Fonctionne aussi fenêtre fermée
// (piloté par le timer du processeur).
class OctopusEngine
{
public:
    static constexpr int kBanks = 16, kSlots = 4, kPresets = kBanks * kSlots, kFs = 4;
    enum Mode { presetMode = 0, stompMode = 1 };

    // commandes pilotables en MIDI (CC appris) ou par les paramètres du plugin
    enum Target { fsA, fsB, fsC, fsD, bankDown, bankUp, modeToggle, numTargets };
    static juce::String targetName (int t);

    struct Preset
    {
        juce::String name;
        juce::StringArray entries;          // effets au format texte de REAPER (sans Octopus, sans FXID)
        int selfPos = 0;                    // position d'Octopus parmi eux
        juce::Array<int> stomp[kFs];        // blocs (positions dans entries) commandés par chaque footswitch en mode STOMP
        bool saved = false;
    };

    explicit OctopusEngine (ChainHost& host);

    void tick();                            // 30 Hz
    void refreshNow();
    int generation() const noexcept { return gen; }

    // ---- chaîne
    bool isReady() const noexcept { return ready; }
    juce::String statusText() const { return status; }
    juce::String trackName() const { return track; }
    const juce::Array<FxInfo>& chain() const noexcept { return fx; }
    const juce::Array<int>& blocks() const noexcept { return blk; }      // indices (chaîne REAPER) des blocs affichés, Octopus exclu
    int blockOf (const juce::String& guid) const;                         // position de bloc, -1 si absent
    ChainHost& chainHost() noexcept { return host; }

    void toggle (int chainIndex);
    void setEnabled (int chainIndex, bool on);
    void remove (int chainIndex);
    void moveBlock (int chainIndex, int toBlockPos);                      // toBlockPos : position voulue parmi les blocs
    bool addFx (const InstalledFx& f, int blockPos);                       // blockPos >= nombre de blocs : à la fin
    int  addFxList (const juce::Array<InstalledFx>& list, int blockPos);   // plusieurs effets à la suite (un seul point d'annulation)
    int  chromaIndex() const;                                               // bloc CHROMA allumé (accordeur), -1 sinon
    static bool isChroma (const FxInfo& f);
    void openWindow (int chainIndex, bool show);
    void setParallel (int chainIndex, bool on);

    const juce::Array<InstalledFx>& installedList (bool reload = false);

    // ---- footswitchs
    Mode mode() const noexcept { return md; }
    void setMode (Mode m);
    void press (int fs);
    juce::String fsTitle (int fs) const;    // "1A" / "DRIVE"
    juce::String fsText (int fs) const;     // nom du preset / des blocs commandés
    bool fsLit (int fs) const;
    int fsKind (int fs) const;              // famille du premier bloc commandé (couleur), -1 = aucun

    // couleur de chaque footswitch (lettre lumineuse et case de l'écran en mode PRESET)
    juce::uint32 fsColour (int fs) const { return fsCol[(size_t) juce::jlimit (0, kFs - 1, fs)]; }
    void setFsColour (int fs, juce::uint32 c) { fsCol[(size_t) juce::jlimit (0, kFs - 1, fs)] = c; changed(); }
    static constexpr juce::uint32 kDefaultFsColour = 0xff6fc8ff;

    bool isAssigned (int fs, const juce::String& guid) const { return stompGuids[fs].contains (guid); }
    void toggleAssign (int fs, const juce::String& guid);
    void clearAssign (int fs);

    // ---- banks / presets
    int bank() const noexcept { return viewBank; }
    void bankStep (int delta);
    void setBank (int b);
    int current() const noexcept { return cur; }
    bool modified() const noexcept { return cur >= 0 && sigLoaded.isNotEmpty() && sigLoaded != signature(); }
    const Preset& preset (int idx) const { return presets[(size_t) juce::jlimit (0, kPresets - 1, idx)]; }
    static juce::String label (int idx);    // 0 -> "1A"
    juce::String displayName (int idx) const;
    bool load (int idx);
    bool save (int idx);
    void rename (int idx, const juce::String& name);
    void clear (int idx);
    void copy (int idx)  { clip = preset (idx); hasClip = clip.saved; }
    bool canPaste() const { return hasClip; }
    void paste (int idx);
    void swapPresets (int a, int b);

    juce::ValueTree banksTree() const;                  // export d'un fichier de banks
    bool importBanks (const juce::ValueTree& t);

    // ---- MIDI
    void midiCc (int channel, int cc, int value);      // depuis la file du processeur (thread de messages)
    void midiProgram (int channel, int program);
    void paramInput (int target, float value);         // paramètre du plugin (automation / MIDI learn de REAPER)
    int learnTarget = -1;                               // en attente d'un CC
    int ccOf (int t) const { return cc[(size_t) t]; }
    bool latching (int t) const { return latch[(size_t) t]; }
    void setLatching (int t, bool on) { latch[(size_t) t] = on; changed(); }
    void clearCc (int t) { cc[(size_t) t] = -1; changed(); }
    bool programChange = true;                          // Program Change 1..64 = presets 1A..16D
    void setProgramChange (bool on) { programChange = on; changed(); }
    void resetMidi();

    // ---- message de la ligne d'état
    void say (const juce::String& m, double ms = 2500.0);
    juce::String message() const;

    // ---- état (sauvé avec le projet)
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree& t);
    std::function<void()> onStateChanged;               // le processeur garde une copie de l'état à jour
    std::function<void()> onTokenClash;                 // deux Octopus se reconnaissent pareil (piste dupliquée)

private:
    ChainHost& host;
    juce::Array<FxInfo> fx;
    juce::Array<int> blk;
    bool ready = false;
    juce::String status, track;
    int gen = 1, tickCount = 0;

    std::array<Preset, kPresets> presets;
    Preset clip; bool hasClip = false;
    int cur = -1, viewBank = 0;
    Mode md = presetMode;
    juce::StringArray stompGuids[kFs];
    juce::String sigLoaded;
    bool sigPending = false;
    juce::Array<InstalledFx> installedCache;
    bool installedLoaded = false;

    std::array<juce::uint32, kFs> fsCol { kDefaultFsColour, kDefaultFsColour, kDefaultFsColour, kDefaultFsColour };
    std::array<int, numTargets> cc;
    std::array<bool, numTargets> latch;
    std::array<bool, numTargets> ccPrev, paramPrev;
    std::array<float, numTargets> paramLast;

    juce::String msg;
    double msgUntil = 0.0;

    juce::String signature() const;
    void changed();                                     // l'état sauvé a changé
    void trigger (int target);
    int chainIndexForBlock (int blockPos) const;        // indice REAPER où insérer pour être à cette position de bloc
    void input (int target, bool on, std::array<bool, numTargets>& prev, bool first);
    static juce::ValueTree presetToTree (const Preset& p, int idx);
    static Preset presetFromTree (const juce::ValueTree& t);
};
