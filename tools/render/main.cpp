#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace juce;

// Captures de l'interface + tests du moteur (presets, rappel différentiel, état) sur une chaîne simulée.
static void pump (int n = 30) { for (int i = 0; i < n; ++i) MessageManager::getInstance()->runDispatchLoopUntil (34); }

static String chainText (OctopusEngine& e)
{
    StringArray s;
    for (auto& f : e.chain()) s.add ((f.isSelf ? "[OCTO]" : "") + f.original.fromFirstOccurrenceOf (": ", false, false).upToFirstOccurrenceOf (" (", false, false) + (f.enabled ? "" : "(off)"));
    return s.joinIntoString (" > ");
}

static int failures = 0;
static void check (bool ok, const String& what) { std::printf ("%s %s\n", ok ? "  OK  " : "  FAIL", what.toRawUTF8()); if (! ok) ++failures; }

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const String out = argc > 1 ? argv[1] : ".";
    OctopusProcessor proc;
    auto& e = proc.getEngine();
    e.refreshNow();

    // ---------------------------------------------------------------- tests du moteur
    std::printf ("Tests moteur\n");
    const auto initial = chainText (e);
    check (e.blocks().size() == 10, "10 blocs affichés (Octopus exclu) : " + initial);
    e.save (0);
    check (e.preset (0).saved && e.preset (0).entries.size() == 10 && e.preset (0).selfPos == 0, "1A enregistré (10 effets, Octopus en premier)");
    check (! e.preset (0).entries[0].contains ("FXID"), "FXID retiré des presets");

    e.toggle (e.blocks()[2]);                                   // Crycat ON
    check (e.modified(), "chaîne modifiée -> EDITED");
    e.save (1);
    const auto chainB = chainText (e);
    e.load (0);
    check (chainText (e) == initial, "rappel 1A (même chaîne, ON/OFF seulement) : " + chainText (e));
    e.load (1);
    check (chainText (e) == chainB, "rappel 1B");

    e.remove (e.blocks()[9]);                                   // retire Holyverb
    e.addFx ({ "VST3: Pipeline (Studio Wiga)", "" }, 0);         // ajoute Pipeline en tête
    e.moveBlock (e.blocks()[0], 3);
    const auto chainC = chainText (e);
    e.toggleAssign (0, e.chain().getReference (e.blocks()[1]).guid);
    e.toggleAssign (0, e.chain().getReference (e.blocks()[2]).guid);
    e.save (2);
    e.load (0);
    check (chainText (e) == initial, "rappel 1A après changement de structure (chaîne remplacée)");
    e.load (2);
    check (chainText (e) == chainC, "rappel 1C : " + chainC);
    e.setMode (OctopusEngine::stompMode);
    check (e.fsText (0).contains ("+"), "footswitch A (STOMP) commande 2 blocs après rappel : " + e.fsText (0));
    e.press (0);
    check (e.fsText (0).isNotEmpty() && ! e.fsLit (0), "STOMP A : les 2 blocs passent OFF");
    e.press (0);
    check (e.fsLit (0), "STOMP A : les 2 blocs repassent ON");
    e.setMode (OctopusEngine::presetMode);

    // état sauvé / relu
    MemoryBlock mb;
    proc.getStateInformation (mb);
    OctopusProcessor proc2;
    proc2.setStateInformation (mb.getData(), (int) mb.getSize());
    auto& e2 = proc2.getEngine();
    check (e2.preset (2).saved && e2.preset (2).entries.size() == e.preset (2).entries.size() && e2.current() == 2,
           "état relu (" + String ((int) mb.getSize()) + " octets)");

    // MIDI : learn + momentané + Program Change
    e.setMode (OctopusEngine::presetMode);
    e.learnTarget = OctopusEngine::fsB;
    e.midiCc (1, 80, 127);
    check (e.ccOf (OctopusEngine::fsB) == 80, "MIDI learn footswitch B = CC 80");
    e.midiCc (1, 80, 0);
    e.midiCc (1, 80, 127);
    check (e.current() == 1, "CC 80 (momentané) -> preset 1B");
    e.midiProgram (1, 0);
    check (e.current() == 0, "Program Change 1 -> preset 1A");
    e.paramInput (OctopusEngine::fsC, 0.0f);
    e.paramInput (OctopusEngine::fsC, 1.0f);
    check (e.current() == 2, "paramètre fsC -> preset 1C");

    // ---------------------------------------------------------------- captures
    std::unique_ptr<AudioProcessorEditor> edp (proc.createEditor());
    auto* ed = static_cast<OctopusEditor*> (edp.get());
    std::printf ("taille par défaut : %d x %d\n", ed->getWidth(), ed->getHeight());
    auto shot = [&] (const String& name)
    {
        pump();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        File f (out + "/" + name + ".png"); f.deleteFile();
        FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
        std::printf ("%s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };

    e.load (0);
    e.rename (0, "Clean Ambient");
    e.rename (1, "Crunch Wah");
    e.rename (2, "Lead Pipeline");
    e.save (3); e.rename (3, "Doom Drop G");
    e.load (0);
    shot ("octopus_preset");
    e.toggle (e.blocks()[4]);
    ed->screenHover ({ 108.0f + 150.0f * 2.0f, 80.0f + 36.0f });
    shot ("octopus_edited_hover");

    e.load (2);
    e.setMode (OctopusEngine::stompMode);
    e.toggleAssign (1, e.chain().getReference (e.blocks()[5]).guid);
    e.toggleAssign (2, e.chain().getReference (e.blocks()[8]).guid);
    e.toggleAssign (3, e.chain().getReference (e.blocks()[9]).guid);
    shot ("octopus_stomp");

    e.setMode (OctopusEngine::presetMode);
    ed->showScreen (1, 3);
    ed->screenSearch ({}, 1);
    ed->screenPick (5); ed->screenPick (1); ed->screenPick (9);
    shot ("octopus_add_effect");
    ed->screenSearch ("rea", 0);
    shot ("octopus_add_search");

    // clic / double-clic sur un bloc (temps simulés, en ms : appui, relâchement, ...)
    {
        const int slot = 1;
        auto on = [&] { return e.chain().getReference (e.blocks()[slot]).enabled; };
        bool before = on();
        bool opened = ed->testBlockClicks (slot, { 0.0, 80.0, 250.0, 320.0 }, 800);
        check (opened && on() == before, "double-clic rapide : paramètres ouverts, effet inchangé");
        before = on();
        opened = ed->testBlockClicks (slot, { 0.0, 120.0, 560.0, 640.0 }, 800);
        check (opened && on() == before, "double-clic lent (après la bascule du 1er clic) : paramètres ouverts, effet rétabli");
        before = on();
        opened = ed->testBlockClicks (slot, { 0.0, 100.0 }, 600);
        check (! opened && on() != before, "clic simple : ON / OFF, pas de paramètres");
        before = on();
        opened = ed->testBlockClicks (slot, { 0.0, 100.0, 1000.0, 1100.0 }, 600);
        check (! opened && on() == before, "deux clics espacés d'une seconde : deux bascules");
        ed->showScreen (0);
    }

    ed->showScreen (2, e.blocks()[6]);
    shot ("octopus_params");
    ed->screenParamsMidi();
    shot ("octopus_params_midi");
    ed->showScreen (4, e.blocks()[6]);
    shot ("octopus_presets");
    // presets utilisateur enregistrés par Octopus
    {
        auto& h = e.chainHost();
        const int fi = e.blocks()[6];
        ed->screenUserPreset (3, {});
        const double before = h.getParam (fi, 3);
        h.setParam (fi, 3, 0.8);
        check (ed->screenUserPreset (1, "My Lead") == 1, "preset utilisateur enregistré");
        h.setParam (fi, 3, 0.1);
        ed->screenUserPreset (1, "Ambient Wash");
        ed->screenUserPreset (2, "1");                            // triés par nom : 0 Ambient Wash, 1 My Lead
        check (std::abs (h.getParam (fi, 3) - 0.8) < 1.0e-6, "preset utilisateur rechargé (paramètre retrouvé)");
        ed->screenUserPreset (0, "Crunch Rhythm");
        shot ("octopus_presets_user");
        ed->screenUserPreset (3, {});
        h.setParam (fi, 3, before);
    }
    e.setFsColour (0, 0xff6fc8ff); e.setFsColour (1, 0xff45e070); e.setFsColour (2, 0xffffb547); e.setFsColour (3, 0xffff4040);
    ed->showScreen (3);
    shot ("octopus_banks");
    ed->showScreen (0);
    shot ("octopus_fs_colours");
    ed->showScreen (0);

    // accordeur : bloc CHROMA allumé, guitare 7 cordes (G1 joué 8 cents bas)
    {
        proc.prepareToPlay (48000.0, 256);
        int ci = -1;
        for (int i = 0; i < e.chain().size(); ++i) if (OctopusEngine::isChroma (e.chain().getReference (i))) ci = i;
        auto& h = e.chainHost();
        h.setParam (ci, 0, 0.0); h.setParam (ci, 1, 0.0); h.setParam (ci, 2, 0.5);
        e.setEnabled (ci, true);
        auto play = [&] (double hz, double secs)
        {
            AudioBuffer<float> b (2, 256); MidiBuffer m;
            static double ph = 0;
            for (int blk = 0; blk < (int) (secs * 48000 / 256); ++blk)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float v = (float) (0.3 * std::sin (ph) + 0.15 * std::sin (2 * ph) + 0.08 * std::sin (3 * ph));
                    b.setSample (0, i, v); b.setSample (1, i, v);
                    ph += 2.0 * MathConstants<double>::pi * hz / 48000.0;
                }
                proc.processBlock (b, m);
                if (blk % 8 == 0) MessageManager::getInstance()->runDispatchLoopUntil (5);
            }
        };
        pump (10);
        play (49.0 * std::pow (2.0, -8.0 / 1200.0), 2.5);
        shot ("octopus_tuner_mono");
        h.setParam (ci, 1, 1.0);
        play (110.0 * std::pow (2.0, 3.0 / 1200.0), 2.5);
        shot ("octopus_tuner_strobe");
        h.setParam (ci, 1, 0.5);
        pump (10);
        play (73.42, 2.0);
        shot ("octopus_tuner_poly");
        check (proc.chroma().on && proc.chroma().notes.size() == 7, "accordeur : CHROMA allumé, accordage lu dans CHROMA (" + proc.chroma().tuningName + ")");
        {
            auto& lib = proc.tuningLibrary();
            int dropD = -1;
            for (int i = 0; i < lib.size(); ++i) if (lib.get (i).notes.size() == 6 && String (tuna::displayName (lib.get (i), false)).containsIgnoreCase ("drop d")) { dropD = i; break; }
            check (dropD >= 0, "bibliothèque de CHROMA : accordage Drop D trouvé");
            proc.chromaSelectTuning (dropD);
            pump (5);
            check (proc.chroma().notes.size() == 6 && proc.chroma().tuningName.containsIgnoreCase ("drop"), "accordage choisi dans Octopus : " + proc.chroma().tuningName);
            play (73.42, 1.5);
            shot ("octopus_tuner_tuning");
            proc.chromaSelectTuning (-1);
            pump (5);
        }
        const auto sn = proc.tunerSnapshot();
        std::printf ("  accordeur : note %d, %.1f cents\n", sn.monoMidi, sn.monoCents);
        e.setEnabled (ci, false);
        pump (5);
        check (! proc.chroma().on, "CHROMA éteint : retour à la chaîne");
    }

    ed->setScaleIndex (0);
    check (ed->getWidth() == 828 && ed->getHeight() == 620, "taille 50 % : " + String (ed->getWidth()) + " x " + String (ed->getHeight()));
    shot ("octopus_50pc");
    ed->setScaleIndex (-1);
    check (ed->getWidth() == 993, "taille par défaut (60 % sans réglage) : " + String (ed->getWidth()));
    ed->setScaleIndex (3);
    shot ("octopus_100pc");

    // longue chaîne : défilement
    for (int i = 0; i < 9; ++i) e.addFx ({ "VST3: ReaEQ (Cockos)", "" }, 99);
    shot ("octopus_long_chain");

    edp.reset();
    std::printf ("%s\n", failures == 0 ? "TOUS LES TESTS OK" : (String (failures) + " ECHEC(S)").toRawUTF8());
    return failures == 0 ? 0 : 1;
}
