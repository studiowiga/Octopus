# OCTOPUS (Studio Wiga)

Plugin VST3 (Windows / macOS) **pour REAPER** : un écran façon multi-effet (inspiré du Quad Cortex mini) qui pilote
la chaîne d'effets de la piste où il est inséré. Format **pédale double** (fenêtre de Ring Think : 661 x 495 par défaut et
au minimum, puis 75 % et 100 %), écran sur presque toute la façade, 4 footswitchs (celui d'Echoflex) surmontés de leur lettre A-D rétroéclairée.

## Ce que fait Octopus
- **L'écran montre la chaîne de la piste** (tous les effets sauf Octopus), en blocs colorés par famille
  (accordeur, ampli, baffle, drive, dynamique, EQ, wah / filtre, delay, reverb, modulation, pitch, spécial, utilitaire),
  reliés de IN à OUT. Ce que l'on fait dans REAPER apparaît aussi à l'écran (relecture ~7 fois par seconde).
- **Clic** sur un bloc : ON / OFF. **Double-clic** : paramètres à l'écran (8 par page, glisser pour régler, bouton WINDOW
  pour la fenêtre du plugin). **Glisser** : déplacer. **Clic droit** : ouvrir la fenêtre, assigner à un footswitch,
  traitement parallèle, déplacer, insérer avant / après, supprimer. Touche Suppr : supprime le bloc survolé.
- **Ajouter** (case « + ») : liste des plugins installés fournie par REAPER, recherche et filtres
  (Studio Wiga, VST3, VST, CLAP, JS, AU ; instruments masqués). L'effet est inséré dans la piste au même moment.
- **Banks / presets** : 16 banks x 4 presets (1A à 16D). Un preset garde la chaîne complète (plugins, ordre, réglages,
  ON / OFF, assignations STOMP), au format de REAPER. SAVE (jamais d'écrasement sans confirmation), BANKS (charger,
  enregistrer, renommer, copier / coller, effacer, exporter / importer un fichier `.octobank`).
- **Rappel sans coupure inutile** : si le preset a la même suite de plugins que la chaîne actuelle, seuls les blocs dont les
  réglages diffèrent sont rechargés, les autres ne font que passer ON / OFF (changement quasi instantané). Sinon la chaîne est
  remplacée. Octopus n'est jamais recréé ni déplacé. Un plugin désinstallé est signalé, les autres sont placés correctement.
- **Footswitchs A-D** : mode **PRESET** (rappellent les presets A-D de la bank affichée, lettre bleue) ou **STOMP**
  (chacun bascule un ou plusieurs blocs, lettre de la couleur du bloc). Fonctionnement d'Echoflex (action à l'appui, bouton
  assombri), seule la lettre s'allume, en fondu.
- **MIDI** (M-Vave Chocolate Plus) : MIDI learn au clic droit (footswitchs A-D, bank -, bank +, mode), momentané ou à
  accrochage (127 / 0), **Program Change 1-64 = presets 1A-16D**. Les paramètres `fsA`..`fsD`, `bankDown`, `bankUp`, `mode`
  acceptent aussi le MIDI learn de REAPER. Le MIDI continue vers les effets suivants.
- Annulation : chaque action est un point d'annulation de REAPER (« Octopus: ... »).
- Le son traverse Octopus sans modification (latence nulle).

## Comment ça marche
REAPER donne aux plugins VST3 une interface propre (`IReaperHostApplication`, SDK officiel de REAPER,
`reaper_vst3_interfaces.h`) : la piste qui héberge le plugin et toute l'API de REAPER (les fonctions de ReaScript :
`TrackFX_AddByName`, `TrackFX_Delete`, `TrackFX_SetEnabled`, `TrackFX_CopyToTrack`, `GetTrackStateChunk`...).
Octopus se retrouve dans la chaîne grâce à un jeton propre à chaque instance (paramètre « Octopus ID »), même s'il est
renommé ou s'il y a plusieurs Octopus. Toutes les actions se font sur le thread de messages (timer à 30 Hz du processeur,
fenêtre ouverte ou fermée) ; le thread audio ne fait que lire le MIDI dans une file sans verrou.
Les presets sont recréés par REAPER lui-même : une piste temporaire reçoit les effets (SetTrackStateChunk), qui sont
ensuite déplacés à leur place (TrackFX_CopyToTrack), puis la piste temporaire est supprimée (sans rafraîchissement
intermédiaire, un seul point d'annulation).

Dans un autre DAW, Octopus reste inactif et l'écran l'indique. Il pilote la chaîne normale de la piste ou ses effets
d'entrée (selon l'endroit où il est inséré), pas les effets de prise. REAPER 6.37 ou plus récent ; liste des plugins
installés : REAPER 7.

## Anti-gel v2
JUCE 8.0.15, fenêtre opaque, moteur de rendu logiciel sous Windows (réappliqué si la fenêtre hôte change), aucun calcul
lourd dans `paint()` (caches du fond, des footswitchs et de l'éclairage des LED refaits par le timer), chien de garde
(redessin non servi en 1,5 s : moteur réappliqué), aucun appel à l'hôte ni allocation dans le thread audio.

## Visuel
`python3 tools/make_art.py` génère `background.png` (boîtier graphite brossé, lunette et vitre, logo Studio Wiga,
jacks du kit, IN / OUT le long des tranches), `fs_off.png` / `fs_on.png` (footswitch d'Echoflex, photo réelle détourée,
ombres selon la lumière en haut à gauche). Positions communes : `Layout.h`.

## Build et vérifications
- GitHub Actions (`.github/workflows/build.yml`) : artefacts `Octopus-VST3-Windows` et `Octopus-macOS-VST3`, et job `tests`.
- `tools/reaper_sim` : le pont REAPER testé contre une API de REAPER simulée (états de piste au format RPP) : 30 tests
  (presets, rappel partiel, Octopus au milieu de la chaîne, effets d'entrée, plugin manquant, deux Octopus...).
- `tools/render` : fenêtre compilée sous Linux sur une chaîne simulée, tests du moteur et captures (`design/renders/`).
- `tools/host_check` : charge le VST3 compilé comme un hôte (paramètres, son inchangé, état, fenêtre).

v1.0.0 : première version.
v1.0.1 : footswitchs tournés de 30° (un plat de l'écrou en haut), LED au-dessus de chaque footswitch, lettre à gauche ; éclairage des LED qui retombe à zéro avant le bord du calque.
v1.0.2 : les LED sont remplacées par les lettres A, B, C, D rétroéclairées au-dessus des footswitchs (gravées quand elles sont éteintes, lumineuses dans la couleur du mode quand elles sont allumées).
v1.0.3 : lettres moins lumineuses ; seul le tracé de la lettre s'allume (plus de tache de lumière sur la façade).
v1.0.4 : lettres A-D un peu plus lumineuses.
