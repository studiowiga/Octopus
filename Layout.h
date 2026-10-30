#pragma once
#include <juce_graphics/juce_graphics.h>

// Repère de la pédale OCTOPUS : 1240 x 928 unités (même rapport que Ring Think), images en 2x.
// Mêmes valeurs que tools/make_art.py.
namespace OctoLayout
{
    constexpr int kW = 1240, kH = 928;
    constexpr int kRefW = 993;                                 // largeur de référence des tailles 60 / 75 / 100 % (comme Ring Think)
    constexpr float kDefaultScale = 661.0f / 993.0f;           // fenêtre par défaut = 661 x 495 px (taille de Ring Think)
    const float kScales[3] = { 661.0f / 993.0f, 0.75f, 1.0f };

    const juce::Rectangle<float> glass { 88.0f, 112.0f, 1064.0f, 624.0f };   // vitre de l'écran (presque toute la façade)
    constexpr float glassRadius = 12.0f;
    constexpr float fsY = 838.0f;
    const float fsX[4] = { 218.0f, 486.0f, 754.0f, 1022.0f };   // alignés sur les 4 cases du bas de l'écran
    constexpr float fsSprite = 150.0f;                          // côté du sprite footswitch (ombre comprise)
    constexpr float fsRadius = 38.0f;                           // zone cliquable (cercle)
    constexpr float ledY = 774.0f, ledDx = 0.0f;                 // LED au-dessus du footswitch (face à un plat de l'écrou, tourné de 30°)
    constexpr float ledR = 9.0f;
}
