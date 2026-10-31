#pragma once
#include <juce_graphics/juce_graphics.h>

// Repère de la pédale OCTOPUS : 1267 x 944 unités (= pixels du visuel fourni, redressé puis recadré, tools/make_frame.py),
// images en 2x. Le boîtier et la lunette viennent de la photo ; le plugin y dessine l'écran, les footswitchs inox,
// le logo OCTOPUS, les lettres A-D et les anneaux lumineux.
namespace OctoLayout
{
    constexpr int kW = 1267, kH = 944;
    // tailles de fenêtre (largeur en pixels) : 50 %, 60 % (taille standard), 75 %, 100 % ;
    // la taille par défaut se choisit dans le menu (réglage commun à toutes les instances, 60 % au départ)
    constexpr int kNumSizes = 4;
    const int kWidths[kNumSizes] = { 828, 993, 1241, 1655 };
    const char* const kSizeNames[kNumSizes] = { "50 %", "60 %", "75 %", "100 %" };
    constexpr int kStandardSize = 1, kMinW = 828, kMaxW = 1700;

    // écran : repère local de 1064 x 624 unités, affiché dans l'ouverture de la photo redressée (1126 x 660) avec le facteur kG
    constexpr float kG = 1126.0f / 1064.0f;
    const juce::Rectangle<float> glass { 71.0f, 108.5f, 1126.0f, 1126.0f * 624.0f / 1064.0f };
    constexpr float glassRadius = 14.0f;

    // footswitchs inox vus de dessus (sprite fs_off.png de make_art.py, écrou de 80 entre plats, un plat en haut),
    // posés à la place de ceux de la photo (effacés par make_frame.py)
    constexpr float fsY = 861.0f;
    const float fsX[4] = { 204.5f, 485.5f, 770.5f, 1052.5f };
    const float capX[4] = { 204.5f, 485.5f, 770.5f, 1052.5f };  // bouton qui s'enfonce (centré sur l'écrou)
    const float capY[4] = { 861.0f, 861.0f, 861.0f, 861.0f };
    constexpr float capR = 25.0f;
    constexpr bool drawFsSprite = true;
    constexpr float fsSprite = 150.0f;                          // côté du sprite footswitch (ombre comprise)
    constexpr float fsRadius = 40.0f;                           // zone cliquable (cercle)
    // lettres A-D (à la place de celles gravées sur la photo) : centre et hauteur de police
    const float ledXs[4] = { 208.5f, 488.0f, 771.5f, 1053.0f };
    constexpr float ledY = 800.5f, ledDx = 0.0f, letterFont = 29.0f;
    constexpr float ledR = 9.0f;
    // logo OCTOPUS (dessiné par le plugin, net à toutes les tailles) : à la place de celui de la photo, bord droit à 1200
    // (comme sur le visuel), 62 de haut, centré à 53 ; dessin 1704 x 446 dans une image 1768 x 510
    constexpr float logoH = 62.0f, logoW = logoH * 1704.0f / 446.0f, logoRight = 1200.0f, logoCy = 53.0f;
    constexpr float logoPad = logoH * 32.0f / 446.0f;
    // anneau lumineux dans le footswitch, dans la gorge autour du bouton qui s'enfonce (comme la Phase 90),
    // même rayon que le diffuseur gris du sprite (make_art.py : RING_R0, RING_R1 = 27.4, 29.5)
    const float ringCx[4] = { 204.5f, 485.5f, 770.5f, 1052.5f };
    const float ringCy[4] = { 861.0f, 861.0f, 861.0f, 861.0f };
    const float ringRx[4] = { 28.45f, 28.45f, 28.45f, 28.45f };
    const float ringRy[4] = { 28.45f, 28.45f, 28.45f, 28.45f };
    constexpr float ringW = 2.1f;
}
