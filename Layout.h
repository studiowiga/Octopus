#pragma once
#include <juce_graphics/juce_graphics.h>

// Repère de la pédale OCTOPUS : 1267 x 981 unités (= pixels du visuel fourni après recadrage, tools/make_frame.py),
// images en 2x. Le boîtier, la lunette et les footswitchs viennent de la photo ; le plugin y dessine l'écran,
// le logo OCTOPUS, les lettres A-D et les anneaux lumineux.
namespace OctoLayout
{
    constexpr int kW = 1267, kH = 981;
    // tailles de fenêtre (largeur en pixels) : 50 %, 60 % (taille standard), 75 %, 100 % ;
    // la taille par défaut se choisit dans le menu (réglage commun à toutes les instances, 60 % au départ)
    constexpr int kNumSizes = 4;
    const int kWidths[kNumSizes] = { 828, 993, 1241, 1655 };
    const char* const kSizeNames[kNumSizes] = { "50 %", "60 %", "75 %", "100 %" };
    constexpr int kStandardSize = 1, kMinW = 828, kMaxW = 1700;

    // écran : repère local de 1064 x 624 unités, affiché dans l'ouverture de la photo (1125 x 660) avec le facteur kG
    constexpr float kG = 1125.0f / 1064.0f;
    const juce::Rectangle<float> glass { 72.0f, 109.0f, 1125.0f, 660.0f };
    constexpr float glassRadius = 14.0f;

    // footswitchs de la photo (écrou), boutons qui s'enfoncent (centre et rayon du chapeau, un peu décalé par la perspective)
    constexpr float fsY = 879.0f;
    const float fsX[4] = { 204.5f, 487.5f, 775.5f, 1060.5f };
    const float capX[4] = { 200.5f, 485.5f, 778.5f, 1060.5f };
    const float capY[4] = { 877.0f, 877.0f, 875.5f, 875.0f };
    constexpr float capR = 31.0f;
    constexpr bool drawFsSprite = false;                       // les footswitchs sont ceux de la photo
    constexpr float fsSprite = 150.0f;                          // zone redessinée autour d'un footswitch
    constexpr float fsRadius = 46.0f;                           // zone cliquable (cercle)
    // lettres A-D (à la place de celles gravées sur la photo) : centre et hauteur de police
    const float ledXs[4] = { 205.5f, 487.0f, 773.0f, 1056.5f };
    constexpr float ledY = 806.5f, ledDx = 0.0f, letterFont = 29.0f;
    constexpr float ledR = 9.0f;
    // logo OCTOPUS (dessiné par le plugin, net à toutes les tailles) : à la place de celui de la photo, bord droit à 1194
    // (symétrique du logo Studio Wiga), 61.5 de haut, centré à 53 ; dessin 1704 x 446 dans une image 1768 x 510
    constexpr float logoH = 61.5f, logoW = logoH * 1704.0f / 446.0f, logoRight = 1194.0f, logoCy = 53.0f;
    constexpr float logoPad = logoH * 32.0f / 446.0f;
    // anneau lumineux autour du bouton qui s'enfonce (comme la Phase 90)
    constexpr float ringR0 = 31.5f, ringR1 = 34.5f;
}
