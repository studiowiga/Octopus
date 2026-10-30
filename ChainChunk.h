#pragma once
#include <juce_core/juce_core.h>

// Lecture / écriture des chaînes d'effets au format texte de REAPER (RPPXML, comme dans un fichier .RPP ou .RfxChain).
//
// Dans l'état d'une piste (GetTrackStateChunk), la chaîne est un bloc :
//     <FXCHAIN                 (ou <FXCHAIN_REC pour les effets d'entrée)
//     WNDRECT ... / SHOW 0 / LASTSEL 0 / DOCKED 0
//     BYPASS 0 0 0             <- début d'un effet : contourné, hors ligne, ...
//     <VST "VST3: ReaEQ (Cockos)" reaeq.vst3 0 "" 1919247729{...} ""
//     ...état du plugin (base64)...
//     >
//     PRESETNAME "..."  / FLOATPOS 0 0 0 0 / FXID {GUID} / WAK 0 0
//     BYPASS 0 0 0             <- effet suivant
//     ...
//     >
// Un « effet » (entry) va de sa ligne BYPASS jusqu'à la ligne BYPASS suivante de même niveau (ou la fin de la chaîne).
// Les conteneurs (REAPER 7) ont leurs propres lignes BYPASS plus profondes : elles restent dans l'effet conteneur.
namespace ChainChunk
{
    inline bool opens (const juce::String& t)  { return t.startsWithChar ('<'); }
    inline bool closes (const juce::String& t) { return t == ">"; }

    inline juce::StringArray lines (const juce::String& s)
    {
        juce::StringArray l;
        l.addLines (s);
        return l;
    }

    // découpe le contenu d'une chaîne (lignes entre "<FXCHAIN" et son ">") en effets
    inline juce::StringArray splitEntries (const juce::StringArray& body)
    {
        juce::StringArray out;
        juce::String cur;
        bool in = false;
        int depth = 0;
        for (auto& raw : body)
        {
            const auto t = raw.trim();
            if (depth == 0 && t.startsWith ("BYPASS"))
            {
                if (in) out.add (cur);
                cur = {}; in = true;
            }
            if (in) cur << raw.trimEnd() << "\n";
            if (opens (t)) ++depth;
            else if (closes (t)) depth = juce::jmax (0, depth - 1);
        }
        if (in) out.add (cur);
        return out;
    }

    // Extrait les effets de la chaîne d'une piste. false si le bloc n'existe pas (chaîne vide) : entries reste vide.
    inline bool extract (const juce::String& trackChunk, bool inputChain, juce::StringArray& entries)
    {
        entries.clear();
        const auto l = lines (trackChunk);
        const juce::String tag = inputChain ? "<FXCHAIN_REC" : "<FXCHAIN";
        int depth = 0, start = -1;
        for (int i = 0; i < l.size(); ++i)
        {
            const auto t = l[i].trim();
            if (start < 0)
            {
                // le bloc de la chaîne est directement dans <TRACK (profondeur 1)
                if (depth == 1 && (t == tag || t.startsWith (tag + " ")))
                {
                    start = i + 1;
                    depth = 0;          // on compte maintenant à l'intérieur du bloc
                    continue;
                }
                if (opens (t)) ++depth;
                else if (closes (t)) --depth;
            }
            else
            {
                if (opens (t)) ++depth;
                else if (closes (t))
                {
                    if (depth == 0)
                    {
                        juce::StringArray body;
                        for (int k = start; k < i; ++k) body.add (l[k]);
                        entries = splitEntries (body);
                        return true;
                    }
                    --depth;
                }
            }
        }
        return false;
    }

    // Ajoute une chaîne <FXCHAIN> contenant ces effets à l'état d'une piste qui n'en a pas (piste temporaire neuve).
    inline juce::String withChain (const juce::String& trackChunk, const juce::StringArray& entries)
    {
        auto l = lines (trackChunk);
        while (l.size() > 0 && l[l.size() - 1].trim().isEmpty()) l.remove (l.size() - 1);
        int last = l.size() - 1;                         // ">" final du bloc <TRACK
        while (last > 0 && ! closes (l[last].trim())) --last;
        juce::String block = "<FXCHAIN\nSHOW 0\nLASTSEL 0\nDOCKED 0\n";
        for (auto& e : entries) block << e.trimEnd() << "\n";
        block << ">";
        l.insert (last, block);
        return l.joinIntoString ("\n") + "\n";
    }

    inline bool hasChain (const juce::String& trackChunk, bool inputChain)
    {
        juce::StringArray e;
        return extract (trackChunk, inputChain, e);
    }

    // ligne de type : <VST "VST3: ReaEQ (Cockos)" reaeq.vst3 0 "" 1919247729{...} ""
    inline juce::String typeLine (const juce::String& entry)
    {
        for (auto& raw : lines (entry))
        {
            const auto t = raw.trim();
            if (opens (t)) return t;
        }
        return {};
    }

    // nom tel que REAPER l'affiche : premier texte entre guillemets de la ligne de type ("VST3: ReaEQ (Cockos)")
    inline juce::String displayName (const juce::String& entry)
    {
        const auto t = typeLine (entry);
        const int q0 = t.indexOfChar ('"');
        if (q0 >= 0)
        {
            const int q1 = t.indexOfChar (q0 + 1, '"');
            if (q1 > q0) return t.substring (q0 + 1, q1);
        }
        // JS sans guillemets : <JS Utility/volume ""
        auto parts = juce::StringArray::fromTokens (t.substring (1), " ", "\"");
        if (parts.size() >= 2) return parts[0] + ": " + parts[1].unquoted();
        return t.substring (1);
    }

    // drapeaux de la ligne BYPASS : 0 = contourné, 1 = hors ligne
    inline bool flag (const juce::String& entry, int which)
    {
        for (auto& raw : lines (entry))
        {
            const auto t = raw.trim();
            if (t.startsWith ("BYPASS"))
            {
                auto p = juce::StringArray::fromTokens (t, " ", "");
                return p.size() > which + 1 && p[which + 1].getIntValue() != 0;
            }
        }
        return false;
    }
    inline bool bypassed (const juce::String& entry) { return flag (entry, 0); }
    inline bool offline (const juce::String& entry)  { return flag (entry, 1); }

    // supprime les lignes de premier niveau qui commencent par l'un de ces mots
    inline juce::String without (const juce::String& entry, std::initializer_list<const char*> keys)
    {
        juce::String out;
        int depth = 0;
        for (auto& raw : lines (entry))
        {
            const auto t = raw.trim();
            bool drop = false;
            if (depth == 0)
                for (auto* k : keys)
                    if (t == k || t.startsWith (juce::String (k) + " ")) { drop = true; break; }
            if (! drop && t.isNotEmpty()) out << raw.trimEnd() << "\n";
            if (opens (t)) ++depth;
            else if (closes (t)) depth = juce::jmax (0, depth - 1);
        }
        return out;
    }

    // Pour un preset : on retire l'identifiant d'instance (FXID) ; REAPER en crée un neuf à chaque chargement
    // (jamais deux effets avec le même identifiant dans le projet).
    inline juce::String forPreset (const juce::String& entry) { return without (entry, { "FXID" }); }

    // contenu « réglages » d'un effet : sans l'état ON/OFF, l'identifiant, la position de fenêtre
    inline juce::String settings (const juce::String& entry) { return without (entry, { "BYPASS", "FXID", "FLOATPOS", "WAK" }); }
}
