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
    constexpr float fsY = 858.0f;
    const float fsX[4] = { 202.1f, 480.7f, 759.3f, 1037.9f };   // alignés sur les 4 cases du bas de l'écran
    constexpr float fsSprite = 150.0f;                          // côté du sprite footswitch (ombre comprise)
    constexpr float fsRadius = 38.0f;                           // zone cliquable (cercle)
    constexpr float ledY = 792.0f, ledDx = 0.0f;                 // lettre lumineuse A-D au-dessus du footswitch (face à un plat de l'écrou)
    constexpr float ledR = 9.0f;
}
