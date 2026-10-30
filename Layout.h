#pragma once
#include <juce_graphics/juce_graphics.h>

// Repère de la pédale OCTOPUS : 1240 x 928 unités, images en 2x. Mêmes valeurs que tools/make_art.py.
namespace OctoLayout
{
    constexpr int kW = 1240, kH = 928;
    // tailles de fenêtre (largeur en pixels) : 50 %, 60 % (taille standard), 75 %, 100 % ;
    // la taille par défaut se choisit dans le menu (réglage commun à toutes les instances, 60 % au départ)
    constexpr int kNumSizes = 4;
    const int kWidths[kNumSizes] = { 828, 993, 1241, 1655 };
    const char* const kSizeNames[kNumSizes] = { "50 %", "60 %", "75 %", "100 %" };
    constexpr int kStandardSize = 1, kMinW = 828, kMaxW = 1700;

    // écran : repère local de 1064 x 624 unités, affiché dans la vitre avec le facteur kG
    constexpr float kG = 1106.0f / 1064.0f;
    const juce::Rectangle<float> glass { 67.0f, 104.0f, 1106.0f, 649.0f };
    constexpr float glassRadius = 12.0f;
    constexpr float fsY = 846.0f;                              // remonté : anneau lumineux autour de l'écrou
    const float fsX[4] = { 202.1f, 480.7f, 759.3f, 1037.9f };   // alignés sur les 4 cases du bas de l'écran
    constexpr float fsSprite = 150.0f;                          // côté du sprite footswitch (ombre comprise)
    constexpr float fsRadius = 38.0f;                           // zone cliquable (cercle)
    constexpr float ledY = 781.0f, ledDx = 0.0f;                 // lettre lumineuse A-D au-dessus du footswitch (face à un plat de l'écrou)
    constexpr float ledR = 9.0f;
    // logo OCTOPUS (dessiné par le plugin, net à toutes les tailles) : symétrique du logo Studio Wiga (bord droit à 1174),
    // 54 de haut, centré en hauteur entre le haut du boîtier et la lunette (51) ; image 1704 x 446
    constexpr float logoH = 54.0f, logoW = logoH * 1704.0f / 446.0f, logoRight = 1174.0f, logoCy = 51.0f;
    constexpr float ringR0 = 46.5f, ringR1 = 52.0f;             // anneau lumineux (comme la Phase 90), mêmes valeurs que make_art.py
}
