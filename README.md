# OCTOPUS (Studio Wiga)

Plugin VST3 (Windows / macOS) **pour REAPER** : un écran façon multi-effet (inspiré du Quad Cortex mini) qui pilote
la chaîne d'effets de la piste où il est inséré. Pédale sans jacks qui occupe toute la fenêtre (tailles 50 %, 60 %, 75 %,
100 % ; taille par défaut au choix dans le menu, 60 % au départ), écran sur presque toute la façade, 4 footswitchs inox surmontés de leur lettre A-D rétroéclairée.

## Ce que fait Octopus
- **L'écran montre la chaîne de la piste** (tous les effets sauf Octopus), en blocs colorés par famille
  (accordeur, ampli, baffle, drive, dynamique, EQ, wah / filtre, delay, reverb, modulation, pitch, spécial, utilitaire en blanc),
  reliés de IN à OUT. Ce que l'on fait dans REAPER apparaît aussi à l'écran (relecture ~7 fois par seconde).
- **Clic** sur un bloc : ON / OFF. **Double-clic** : paramètres à l'écran (8 par page, glisser pour régler, bouton WINDOW
  pour la fenêtre du plugin). **Glisser** : déplacer. **Clic droit** : ouvrir la fenêtre, assigner à un footswitch,
  traitement parallèle, déplacer, insérer avant / après, supprimer. Touche Suppr : supprime le bloc survolé.
- **Ajouter** (case « + ») : liste des plugins installés fournie par REAPER, recherche et filtres
  (Studio Wiga, VST3, VST, CLAP, JS, AU ; instruments masqués). Plusieurs plugins se choisissent à la suite (numérotés dans
  l'ordre des clics) puis ADD les insère tous dans la piste, dans cet ordre, en un seul point d'annulation.
- **Presets de chaque plugin** (bouton PRESETS du panneau des paramètres) : presets d'usine et utilisateur de REAPER, numérotés,
  chargés d'un clic, flèches précédent / suivant. Pour lire les noms, chaque preset est chargé un instant puis l'état exact
  du plugin est rétabli (vst_chunk / clap_chunk). **SAVE USER** : presets utilisateur gardés par Octopus pour chaque plugin
  (`Studio Wiga/Octopus/Plugin presets/<plugin>.xml` dans le dossier de données utilisateur, état exact du plugin), en tête de
  liste avec une étoile ; clic droit : charger, remplacer, renommer, supprimer.
- **Accordeur** : quand un bloc CHROMA est allumé, Octopus affiche l'écran de CHROMA (même dessin) et fait tourner le même
  moteur (TunerEngine de CHROMA) sur le son qui le traverse ; mode, La de référence et accordage lus dans CHROMA
  (paramètres + état du plugin) ; écran un peu réduit, boutons TUNINGS, MUTE, MODE, EXIT. TUNINGS (ou clic sur le nom de
  l'accordage) : presets d'accordage de CHROMA (favoris, usine par instrument, accordages perso de `library.xml`) ; le choix est
  gardé par Octopus avec le projet et s'efface si l'accordage est changé dans CHROMA lui-même.
- **Banks / presets** : 16 banks x 4 presets (1A à 16D). Un preset garde la chaîne complète (plugins, ordre, réglages,
  ON / OFF, assignations STOMP), au format de REAPER. SAVE (jamais d'écrasement sans confirmation), BANKS (charger,
  enregistrer, renommer, copier / coller, effacer, exporter / importer un fichier `.octobank`, couleur de chaque footswitch A-D en mode PRESET). En-tête : PRESET, STOMP,
  BANKS, SAVE.
- **Rappel sans coupure inutile** : si le preset a la même suite de plugins que la chaîne actuelle, seuls les blocs dont les
  réglages diffèrent sont rechargés, les autres ne font que passer ON / OFF (changement quasi instantané). Sinon la chaîne est
  remplacée. Octopus n'est jamais recréé ni déplacé. Un plugin désinstallé est signalé, les autres sont placés correctement.
- **Footswitchs A-D** : mode **PRESET** (rappellent les presets A-D de la bank affichée, lettre dans la couleur choisie dans BANKS, bleu par défaut) ou **STOMP**
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
`python3 tools/make_art.py` génère `background.png` (boîtier graphite brossé sans jacks, lunette et vitre, logos Studio Wiga et OCTOPUS), `fs_off.png` / `fs_on.png` (footswitch inox, photo réelle détourée `design/footswitch_inox_source.png`,
ombres selon la lumière en haut à gauche). Positions communes : `Layout.h`.

## Build et vérifications
- GitHub Actions (`.github/workflows/build.yml`) : artefacts `Octopus-VST3-Windows` et `Octopus-macOS-VST3`, et job `tests`.
- `tools/reaper_sim` : le pont REAPER testé contre une API de REAPER simulée (états de piste au format RPP) : 36 tests
  (presets, rappel partiel, Octopus au milieu de la chaîne, effets d'entrée, plugin manquant, deux Octopus...).
- `tools/render` : fenêtre compilée sous Linux sur une chaîne simulée, tests du moteur et captures (`design/renders/`).
- `tools/host_check` : charge le VST3 compilé comme un hôte (paramètres, son inchangé, état, fenêtre).

v1.0.0 : première version.
v1.0.1 : footswitchs tournés de 30° (un plat de l'écrou en haut), LED au-dessus de chaque footswitch, lettre à gauche ; éclairage des LED qui retombe à zéro avant le bord du calque.
v1.0.2 : les LED sont remplacées par les lettres A, B, C, D rétroéclairées au-dessus des footswitchs (gravées quand elles sont éteintes, lumineuses dans la couleur du mode quand elles sont allumées).
v1.0.3 : lettres moins lumineuses ; seul le tracé de la lettre s'allume (plus de tache de lumière sur la façade).
v1.0.4 : lettres A-D un peu plus lumineuses.
v1.0.5 : très léger halo autour des lettres allumées.
v1.0.6 : halo des lettres allumées accentué (dégradé progressif qui épouse la lettre).
v1.1.0 : taille par défaut = ancienne taille 100 % (affichée 60 %), 75 % et 100 % plus grands ; jacks retirés et pédale agrandie dans la fenêtre ;
  accordeur CHROMA dans Octopus ; ajout de plusieurs effets d'un coup ; presets usine / utilisateur de chaque plugin ; famille utilitaire en blanc.
v1.1.1 : double-clic fiable (détecté par Octopus, la bascule d'un double-clic lent est défaite) ; noms des paramètres plus grands ;
  paramètres à crans réglés cran par cran ; paramètres MIDI derrière le bouton MIDI ; BYPASS à côté de la croix.
v1.1.2 : presets utilisateur des plugins (SAVE USER) ; en-tête PRESET / STOMP séparés, SAVE après BANKS ; logo Studio Wiga plus petit
  et centré entre le haut de la pédale et l'écran ; couleur des footswitchs A-D au choix (BANKS) ; accordeur plus compact et menu
  TUNINGS avec les presets d'accordage de CHROMA.
v1.1.3 : trois rangées de blocs, même espace entre les blocs en largeur et en hauteur ; cases des footswitchs décollées du bas de
  l'écran ; logo Studio Wiga encore réduit ; taille 50 % et choix de la taille par défaut (réglage commun, `settings.xml`).
v1.1.4 : cases des footswitchs à égale distance entre elles et des bords de l'écran.
v1.1.5 : ligne de liaison entre deux rangées centrée entre le nom des blocs et la rangée suivante ; grille centrée, lignes de retour
  de même longueur à gauche et à droite.
v1.1.6 : bloc utilitaire allumé en blanc plein (icône sombre), bien distinct d'un bloc éteint ; ligne de liaison entre les rangées un peu remontée.
v1.1.7 : blanc du bloc utilitaire allumé 5 % moins lumineux.
v1.1.8 : nouveau logo OCTOPUS (pieuvre, « fx chain controller ») en haut à droite, en symétrie du logo Studio Wiga.
v1.1.9 : logo OCTOPUS agrandi (54 unités de haut).
v1.2.0 : nouveaux footswitchs inox (photo fournie, écrou plat en haut) ; blanc du bloc utilitaire allumé encore 5 % plus transparent.
