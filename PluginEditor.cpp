#include "PluginEditor.h"
#include "FxCategory.h"
#include "Layout.h"
#include "UserFxPresets.h"
#include "OctoSettings.h"
#include <BinaryData.h>

using namespace juce;
namespace L = OctoLayout;

namespace
{
    constexpr int kW = L::kW, kH = L::kH;
    constexpr float kAsset = 2.0f;                               // images livrées en 2x (HiDPI)

    // ---- couleurs de l'écran
    const Colour kScrBg (0xff0a0c10), kScrPanel (0xff13161c), kScrPanel2 (0xff1a1e26), kLine (0xff59616e),
                 kText (0xffeef1f5), kDim (0xff8d96a4), kFaint (0xff3a414d), kAccent (0xff4fc3f7), kAmber (0xffffb547),
                 kPresetLed (0xff6fc8ff);

    // ---- écran : repère local (unités), origine = coin haut gauche de la vitre (1064 x 624)
    constexpr float kSW = 1064.0f, kSH = 624.0f;
    constexpr float kHeaderH = 74.0f;
    // 3 rangées visibles ; même espace entre les carrés horizontalement et verticalement (pas de 140 = carré 70 + 70)
    constexpr int kRows = 3, kCols = 7;
    constexpr float kGridTop = 106.0f, kRowPitch = 140.0f, kColPitch = 140.0f, kColX0 = (kSW - kColPitch * (float) (kCols - 1)) * 0.5f;   // grille centrée
    constexpr float kTile = 70.0f, kTileDy = 40.0f;              // carré du bloc, centre à rowTop + kTileDy
    // cases des footswitchs : même écart entre elles et avec les bords de l'écran, décollées du bas
    constexpr float kFooterY = 547.0f, kFooterH = 58.0f, kFooterGap = 16.0f, kFooterW = (kSW - 5.0f * kFooterGap) / 4.0f;
    constexpr float kFooterLine = 528.0f;                                       // trait au-dessus des cases
    const Rectangle<float> kOverlay (0.0f, kHeaderH, kSW, kSH - kHeaderH);

    Image loadPng (const void* d, int n) { return ImageFileFormat::loadFrom (d, (size_t) n).convertedToFormat (Image::ARGB); }

    // redimensionne une fois (par le timer, jamais dans paint), puis garde en cache tant que la taille ne change pas
    struct ScaledCache
    {
        Image src, img; float scale = -1.0f;
        void prepare (float s)
        {
            if (std::abs (s - scale) <= 1.0e-4f && img.isValid()) return;
            scale = s;
            const float k = s / kAsset;
            const int w = jmax (1, roundToInt ((float) src.getWidth() * k)), h = jmax (1, roundToInt ((float) src.getHeight() * k));
            img = src.rescaled (w, h, Graphics::highResamplingQuality);
        }
        const Image& get() const { return img.isValid() ? img : src; }
        float pixelsPerUnit() const { return img.isValid() ? scale : kAsset; }
    };

    // réduction nette d'une grande image : divisions par 2 successives (moyenne de 4 pixels), puis dernier pas en haute qualité
    Image crispRescale (Image im, int w, int h)
    {
        if (! im.isValid() || w <= 0 || h <= 0) return {};
        im = im.convertedToFormat (Image::ARGB);
        while (im.getWidth() >= w * 2 && im.getHeight() >= h * 2)
        {
            const int nw = im.getWidth() / 2, nh = im.getHeight() / 2;
            Image half (Image::ARGB, nw, nh, true);
            const Image::BitmapData sd (im, Image::BitmapData::readOnly);
            Image::BitmapData dd (half, Image::BitmapData::writeOnly);
            for (int y = 0; y < nh; ++y)
                for (int x = 0; x < nw; ++x)
                {
                    int acc[4] = { 0, 0, 0, 0 };       // pixels prémultipliés : la moyenne garde des bords propres
                    for (int k = 0; k < 4; ++k)
                    {
                        const auto* p = sd.getPixelPointer (x * 2 + (k & 1), y * 2 + (k >> 1));
                        for (int c = 0; c < 4; ++c) acc[c] += p[c];
                    }
                    auto* q = dd.getPixelPointer (x, y);
                    for (int c = 0; c < 4; ++c) q[c] = (uint8) ((acc[c] + 2) / 4);
                }
            im = half;
        }
        return im.getWidth() == w && im.getHeight() == h ? im : im.rescaled (w, h, Graphics::highResamplingQuality);
    }

    String fsLetter (int f) { return String::charToString ((juce_wchar) ('A' + f)); }

    // ids des boutons de l'écran
    enum ButtonId
    {
        bNone = 0, bBankPrev, bBankNext, bModePreset, bModeStomp, bSave, bBanks, bClose,
        bParamOnOff, bParamWindow, bPagePrev, bPageNext, bExport, bImport, bPc, bReload,
        bAddPicked, bClearPicked, bParamMidi, bPresets, bTunerTunings, bPresetSave, bPresetRescan, bTunerMute, bTunerMode, bTunerExit,
        bChip = 100,                     // + catégorie du navigateur
        bBank = 200,                     // + numéro de bank
        bCard = 300,                     // + slot * 10 + action
        bFsColour = 450                  // + footswitch
    };
    enum CardAction { cLoad, cSave, cRename, cCopy, cPaste, cClear, numCardActions };
    const char* const kCardText[numCardActions] = { "LOAD", "SAVE", "RENAME", "COPY", "PASTE", "CLEAR" };
    const char* const kChips[] = { "All", "Studio Wiga", "VST3", "VST", "CLAP", "JS", "AU" };
    constexpr int kNumChips = 7;

    struct Btn { Rectangle<float> r; int id; String text; bool active = false; bool enabled = true; };
}

//==============================================================================
class PedalView : public Component, private Timer
{
public:
    enum class Overlay { none, browser, params, banks };

    explicit PedalView (OctopusProcessor& p) : proc (p), eng (p.getEngine())
    {
        bg.src    = loadPng (BinaryData::background_png, BinaryData::background_pngSize);
        fsOff.src = loadPng (BinaryData::fs_off_png, BinaryData::fs_off_pngSize);
        fsOn.src  = loadPng (BinaryData::fs_on_png, BinaryData::fs_on_pngSize);
        logoSrc   = loadPng (BinaryData::logo_octopus_blanc_png, BinaryData::logo_octopus_blanc_pngSize);
        tfMedium  = Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize);
        tfBold    = Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
        tfRegular = Typeface::createSystemTypefaceFor (BinaryData::PoppinsRegular_ttf, BinaryData::PoppinsRegular_ttfSize);
        tfMono    = Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoSemiBold_ttf, BinaryData::IBMPlexMonoSemiBold_ttfSize);
        for (auto& l : ledLevel) l = 0.0f;

        styleEditor (search, "Search plug-ins...");
        search.onTextChange = [this] { listScroll = 0; askRepaint (screenPx()); };
        search.onEscapeKey = [this] { closeOverlay(); };
        search.onReturnKey = [this]
        {
            if (picked.size() > 0) { addPicked(); return; }
            const auto items = filtered();
            if (items.size() > 0) { picked.add (items.getReference (0)); addPicked(); }
        };
        addChildComponent (search);

        styleEditor (renameEd, "Preset name");
        renameEd.onReturnKey = [this] { commitRename(); };
        renameEd.onEscapeKey = [this] { renameSlot = -1; renameEd.setVisible (false); askRepaint (screenPx()); };
        renameEd.onFocusLost = [this] { commitRename(); };
        addChildComponent (renameEd);

        styleEditor (headerNameEd, "Preset name");
        headerNameEd.onReturnKey = [this] { commitHeaderName(); };
        headerNameEd.onEscapeKey = [this] { headerRenaming = false; headerNameEd.setVisible (false); askRepaint (screenPx()); };
        headerNameEd.onFocusLost = [this] { commitHeaderName(); };
        addChildComponent (headerNameEd);

        styleEditor (presetNameEd, "User preset name");
        presetNameEd.onReturnKey = [this] { commitPresetName(); };
        presetNameEd.onEscapeKey = [this] { cancelPresetName(); };
        addChildComponent (presetNameEd);

        setOpaque (true);
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    ~PedalView() override { stopTimer(); }

    // ------------------------------------------------------------ public (outil de rendu, éditeur)
    void openBrowser (int blockPos)
    {
        overlay = Overlay::browser;
        insertPos = blockPos;
        listScroll = 0;
        picked.clear();
        eng.installedList();
        search.setText ({}, dontSendNotification);
        search.setVisible (true);
        layoutEditors();
        if (isShowing()) search.grabKeyboardFocus();
        askRepaint (screenPx());
    }
    void openParams (int chainIndex)
    {
        if (! isPositiveAndBelow (chainIndex, eng.chain().size())) return;
        overlay = Overlay::params;
        paramGuid = eng.chain().getReference (chainIndex).guid;
        paramPage = 0;
        presetMode = false;
        midiView = false;
        paramListKey = {};
        hideEditors();
        pollParams (true);
        askRepaint (screenPx());
    }
    void openBanks()
    {
        overlay = Overlay::banks;
        hideEditors();
        askRepaint (screenPx());
    }
    void closeOverlay()
    {
        overlay = Overlay::none;
        hideEditors();
        askRepaint (screenPx());
    }
    void setChip (int c) { chip = jlimit (0, kNumChips - 1, c); listScroll = 0; askRepaint (screenPx()); }
    void pickVisible (int v) { const auto items = filtered(); if (isPositiveAndBelow (v, items.size())) togglePick (items.getReference (v)); }
    void showPresets() { openPresets (true); }
    void setSearch (const String& t) { search.setText (t, sendNotification); }
    void simulateHover (Point<float> unitPos) { updateHover (unitPos); }

    // anti-gel v2 : jamais de calcul lourd ici, les caches seront refaits par le timer quand la taille sera stable
    void resized() override
    {
        s = (float) getWidth() / (float) kW;
        resizeSettle = 4;
        layoutEditors();
        repaint();
    }

    void initCaches()
    {
        dispScale = physScale();
        approxScale = (float) Component::getApproximateScaleFactorForComponent (this);
        resizeSettle = 0;
        rebuildCaches();
    }

    // ------------------------------------------------------------ dessin
    void paint (Graphics& g) override
    {
        lastPaintMs = Time::getMillisecondCounterHiRes();
        repaintAskedMs = 0.0;
        g.fillAll (Colours::black);
        g.drawImage (bg.get(), getLocalBounds().toFloat());
        if (logoImg.isValid())             // logo OCTOPUS à sa taille exacte, calé sur la grille des pixels : net
        {
            const auto lr = r (Rectangle<float> (L::logoRight - L::logoW - L::logoPad, L::logoCy - L::logoH * 0.5f - L::logoPad,
                                                 L::logoW + 2.0f * L::logoPad, L::logoH + 2.0f * L::logoPad));
            g.setOpacity (0.94f);
            const bool exact = std::abs ((float) logoImg.getWidth() - lr.getWidth() * dispScale) < 1.5f;   // sinon : cache pas encore refait (redimensionnement)
            g.drawImage (logoImg, exact ? Rectangle<float> ((float) roundToInt (lr.getX()), (float) roundToInt (lr.getY()),
                                                            (float) logoImg.getWidth() / dispScale, (float) logoImg.getHeight() / dispScale)
                                        : lr);
            g.setOpacity (1.0f);
        }

        for (int f = 0; f < 4; ++f)
        {
            const auto fr = r (Rectangle<float> (L::fsSprite, L::fsSprite).withCentre ({ L::fsX[f], L::fsY }));
            g.drawImage (fsOff.get(), fr);
            if (ledLevel[f] > 0.003f)       // flanc de l'écrou tourné vers la LED
            {
                g.setOpacity (jmin (1.0f, ledLevel[f]) * 0.35f);
                g.drawImage (fsOn.get(), fr);
                g.setOpacity (1.0f);
            }
            if (pressedFs == f)             // comme Echoflex : le bouton s'assombrit tant qu'on appuie
            {
                const auto c = r (Point<float> (L::fsX[f], L::fsY));
                const float pr = L::fsRadius * 0.54f * s;
                g.setColour (Colours::black.withAlpha (0.25f));
                g.fillEllipse (c.x - pr, c.y - pr, pr * 2.0f, pr * 2.0f);
            }
            drawRing (g, f, ledLevel[f], ledColour[f]);         // anneau lumineux autour du bouton qui s'enfonce
            drawLetter (g, f, ledPos (f), ledLevel[f], ledColour[f]);
        }

        drawScreen (g);
    }

private:
    OctopusProcessor& proc;
    OctopusEngine& eng;
    ScaledCache bg, fsOff, fsOn;
    Image logoSrc, logoImg;               // logo OCTOPUS : source pleine résolution, puis image à la taille exacte en pixels
    Image glow[4];
    Colour glowColour[4];
    Typeface::Ptr tfMedium, tfBold, tfRegular;
    float s = 1.0f;

    // anti-gel v2
    float dispScale = 1.0f, approxScale = 1.0f;
    int resizeSettle = 0;
    double lastPaintMs = 0.0, repaintAskedMs = 0.0, lastScaleCheckMs = 0.0;

    // état affiché
    int drawnGen = -1;
    float ledLevel[4];
    Colour ledColour[4] { kPresetLed, kPresetLed, kPresetLed, kPresetLed };
    int pressedFs = -1;

    Overlay overlay = Overlay::none;
    int firstRow = 0;                              // défilement de la grille (rangées)
    int hoverSlot = -1, hoverBtn = bNone, hoverItem = -1, hoverFooter = -1, hoverParam = -1;
    // clic / glisser sur un bloc
    int downSlot = -1;
    Point<float> downPos;
    bool dragging = false;
    Point<float> dragPos;
    int dropPos = -1;
    int pendingToggle = -1;                        // indice REAPER, basculé après le délai du double-clic
    double pendingToggleAt = 0.0;
    // navigateur
    TextEditor search, renameEd, presetNameEd, headerNameEd;
    bool headerRenaming = false;           // nom du preset chargé en cours de saisie (clic sur le nom en haut)
    int chip = 0, listScroll = 0, insertPos = 0;
    // paramètres
    String paramGuid;
    int paramPage = 0, paramDrag = -1;
    struct ParamView { int index; String name, text; double value; int steps; };
    // paramètres affichés : ceux du plugin sans le MIDI, ou seulement le MIDI (bouton MIDI) ; liste faite une fois par effet
    bool midiView = false;
    Array<int> paramList;
    String paramListKey;
    // double-clic sur un bloc : détecté par Octopus lui-même (même bloc, moins de 650 ms entre les deux appuis),
    // et la bascule ON / OFF d'un clic simple est annulée si le double-clic arrive après elle
    String lastClickGuid, toggledGuid;
    double lastClickMs = 0.0, toggledAtMs = 0.0;
    Point<float> lastClickPos;
    Array<ParamView> params;
    int paramCount = 0, pollTick = 0;
    String drawnParamSig;
    // presets du plugin (dans le panneau des paramètres)
    bool presetMode = false;
    PluginPresets plist;
    String plistGuid;
    std::vector<UserFxPreset> upresets;     // presets utilisateur enregistrés par Octopus pour ce plugin (en tête de liste)
    String upPlugin;                        // nom d'origine du plugin (clé du fichier)
    int userCur = -1;                       // preset utilisateur chargé en dernier
    int presetEdIndex = -2;                 // -2 : pas de saisie ; -1 : nouveau preset ; >= 0 : renommer ce preset
    int presetScroll = 0;
    // navigateur : effets choisis (dans l'ordre), ajoutés en une fois
    Array<InstalledFx> picked;
    // accordeur (CHROMA)
    Typeface::Ptr tfMono;
    float strobePhase = 0.0f;
    bool drawnTuner = false;
    // banks
    int renameSlot = -1;
    std::unique_ptr<FileChooser> chooser;

    // ------------------------------------------------------------ repères
    Rectangle<float> r (Rectangle<float> d) const { return d * s; }
    Point<float> r (Point<float> d) const { return d * s; }
    // écran : repère local -> pixels de la fenêtre
    // écran : repère local 1064 x 624, agrandi de kG dans la vitre
    Rectangle<float> S (Rectangle<float> local) const { return (local * L::kG + L::glass.getPosition()) * s; }
    Point<float> S (Point<float> local) const { return (local * L::kG + L::glass.getPosition()) * s; }
    float S (float v) const { return v * L::kG * s; }
    Point<float> toLocal (Point<float> px) const { return (px / s - L::glass.getPosition()) / L::kG; }
    Rectangle<int> screenPx() const { return r (L::glass).toNearestInt().expanded (2); }

    Point<float> ledPos (int f) const { return { L::fsX[f] + L::ledDx, L::ledY }; }
    Rectangle<float> glowRect (int f) const { return Rectangle<float> (128.0f, 128.0f).withCentre (ledPos (f)); }

    float physScale() const
    {
        if (auto* d = Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) return jmax (1.0f, (float) d->scale);
        return 1.0f;
    }

    void askRepaint (Rectangle<int> area)
    {
        if (repaintAskedMs <= 0.0) repaintAskedMs = Time::getMillisecondCounterHiRes();
        repaint (area);
    }

    Font font (const Typeface::Ptr& tf, float unitsHigh) const { return Font (FontOptions (tf).withHeight (unitsHigh * L::kG * s)); }   // texte de l'écran

    void styleEditor (TextEditor& ed, const String& hint)
    {
        ed.setColour (TextEditor::backgroundColourId, kScrPanel2);
        ed.setColour (TextEditor::outlineColourId, kFaint);
        ed.setColour (TextEditor::focusedOutlineColourId, kAccent);
        ed.setColour (TextEditor::textColourId, kText);
        ed.setColour (TextEditor::highlightColourId, kAccent.withAlpha (0.35f));
        ed.setColour (CaretComponent::caretColourId, kAccent);
        ed.setTextToShowWhenEmpty (hint, kDim);
        ed.setSelectAllWhenFocused (true);
        ed.setEscapeAndReturnKeysConsumed (true);
    }

    void layoutEditors()
    {
        const float h = 18.0f * s;
        search.setBounds (S (Rectangle<float> (16.0f, 130.0f, 330.0f, 38.0f)).toNearestInt());
        search.setFont (font (tfMedium, 18.0f));
        search.setIndents (roundToInt (S (10.0f)), jmax (0, roundToInt ((S (38.0f) - h) * 0.5f) - 1));
        if (renameSlot >= 0)
        {
            renameEd.setBounds (S (cardRect (renameSlot).withTrimmedLeft (80.0f).withWidth (250.0f).reduced (0.0f, 14.0f)).toNearestInt());
            renameEd.setFont (font (tfMedium, 18.0f));
            renameEd.setIndents (roundToInt (S (8.0f)), jmax (0, roundToInt ((S (36.0f) - h) * 0.5f) - 1));
        }
        headerNameEd.setBounds (S (headerNameRect()).toNearestInt());
        headerNameEd.setFont (font (tfMedium, 24.0f));
        headerNameEd.setIndents (roundToInt (S (6.0f)), jmax (0, roundToInt ((S (36.0f) - 24.0f * s) * 0.5f) - 1));
        presetNameEd.setBounds (S (Rectangle<float> (104.0f, 132.0f, 540.0f, 36.0f)).toNearestInt());
        presetNameEd.setFont (font (tfMedium, 18.0f));
        presetNameEd.setIndents (roundToInt (S (8.0f)), jmax (0, roundToInt ((S (36.0f) - h) * 0.5f) - 1));
    }

    void hideEditors()
    {
        search.setVisible (false);
        renameSlot = -1;
        renameEd.setVisible (false);
        presetEdIndex = -2;
        presetNameEd.setVisible (false);
        headerRenaming = false;
        headerNameEd.setVisible (false);
    }

    // ---- nom du preset chargé : clic sur le nom en haut de l'écran pour le changer
    static Rectangle<float> headerNameRect() { return { 132.0f, 8.0f, 376.0f, 36.0f }; }
    void startHeaderRename()
    {
        const int cur = eng.current();
        if (cur < 0) { eng.say ("Load a preset (or SAVE the chain) to name it"); return; }
        headerRenaming = true;
        layoutEditors();
        headerNameEd.setText (eng.preset (cur).name, dontSendNotification);
        headerNameEd.setVisible (true);
        headerNameEd.grabKeyboardFocus();
        headerNameEd.selectAll();
        askRepaint (screenPx());
    }
    void commitHeaderName()
    {
        if (! headerRenaming) return;
        headerRenaming = false;
        headerNameEd.setVisible (false);
        if (eng.current() >= 0) eng.rename (eng.current(), headerNameEd.getText());
        askRepaint (screenPx());
    }

    // ------------------------------------------------------------ caches (timer seulement)
    void rebuildCaches()
    {
        if (getWidth() <= 0) return;
        const float ps = s * dispScale;
        for (auto* c : { &bg, &fsOff, &fsOn }) c->prepare (ps);
        logoImg = crispRescale (logoSrc, roundToInt ((L::logoW + 2.0f * L::logoPad) * ps), roundToInt ((L::logoH + 2.0f * L::logoPad) * ps));
        layoutEditors();
        askRepaint (getLocalBounds());
    }

    // La façade reçoit la lumière de la LED : fond x éclairement x couleur (le grain s'allume, les creux restent sombres),
    // tache serrée + diffusion large et faible, très léger halo optique autour de la lentille. Calque avec alpha
    // (seule la lumière ajoutée), affiché avec l'intensité de la LED.
    void buildGlow (int f)
    {
        glowColour[f] = ledColour[f];
        const auto& src = bg.get();
        const float ppu = bg.pixelsPerUnit();
        const auto gr = glowRect (f);
        const int x0 = roundToInt (gr.getX() * ppu), y0 = roundToInt (gr.getY() * ppu);
        const int w = roundToInt (gr.getWidth() * ppu), h = roundToInt (gr.getHeight() * ppu);
        if (w <= 0 || h <= 0) return;
        Image out (Image::ARGB, w, h, true);
        const Image::BitmapData sd (src, Image::BitmapData::readOnly);
        Image::BitmapData od (out, Image::BitmapData::writeOnly);
        const auto c = ledPos (f);
        const float cr = ledColour[f].getFloatRed(), cg = ledColour[f].getFloatGreen(), cb = ledColour[f].getFloatBlue();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const int sx = x0 + x, sy = y0 + y;
                if (sx < 0 || sy < 0 || sx >= src.getWidth() || sy >= src.getHeight()) continue;
                const float ux = (float) sx / ppu, uy = (float) sy / ppu;
                const float d = std::hypot (ux - c.x, (uy - c.y) * 1.08f);
                const float tight = 1.0f / std::pow (1.0f + (d / 13.0f) * (d / 13.0f), 1.5f);
                const float wide = 0.20f * std::exp (-d / 36.0f);
                const float win = jlimit (0.0f, 1.0f, (gr.getWidth() * 0.5f - 4.0f - d) / 22.0f);   // retombe à zéro avant le bord du calque
                const float illum = (1.35f * tight + wide) * win * win;
                const auto p = sd.getPixelColour (sx, sy);
                const float lum = 0.25f + (p.getFloatRed() + p.getFloatGreen() + p.getFloatBlue()) / 3.0f;
                const float halo = 0.10f * std::exp (-(d / 13.0f) * (d / 13.0f));
                const float k = illum * lum * 1.9f + halo;
                const float ar = jmin (1.0f, k * cr), ag = jmin (1.0f, k * cg), ab = jmin (1.0f, k * cb);
                const float a = jmin (1.0f, jmax (ar, jmax (ag, ab)));
                if (a < 0.002f) continue;
                od.setPixelColour (x, y, Colour::fromFloatRGBA (ar / a, ag / a, ab / a, a));
            }
        glow[f] = out;
    }

    // ------------------------------------------------------------ lettre rétroéclairée A / B / C / D (à la place d'une LED)
    // Éteinte : lettre gravée dans la façade, verre fumé sombre ; le bord bas droit du creux accroche la lumière (en haut à gauche).
    // Allumée : seul le tracé de la lettre s'éclaire, dans la couleur du mode (aucune tache de lumière sur la façade).
    void drawLetter (Graphics& g, int f, Point<float> c0, float lvl, Colour col)
    {
        GlyphArrangement ga;
        ga.addLineOfText (Font (FontOptions (tfBold).withHeight (38.0f * s)), fsLetter (f), 0.0f, 0.0f);
        Path p;
        ga.createPath (p);
        const auto c = r (c0);
        const auto b = p.getBounds();
        p.applyTransform (AffineTransform::translation (c.x - b.getCentreX(), c.y - b.getCentreY()));
        const auto pb = p.getBounds();

        // gravure : arête claire en bas à droite, ombre en haut à gauche dans le creux
        g.setColour (Colours::white.withAlpha (0.16f));
        g.fillPath (p, AffineTransform::translation (0.9f * s, 1.0f * s));
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillPath (p, AffineTransform::translation (-0.6f * s, -0.7f * s));
        // verre fumé éteint, à peine teinté
        g.setGradientFill (ColourGradient (Colour (0xff4a4f59).interpolatedWith (col, 0.12f), 0.0f, pb.getY(),
                                           Colour (0xff2c3038).interpolatedWith (col, 0.08f), 0.0f, pb.getBottom(), false));
        g.fillPath (p);
        if (lvl <= 0.003f) return;

        // seule la lettre s'éclaire : verre teinté rétroéclairé, intensité modérée, très léger débord lumineux sur son contour
        const float k = lvl;
        // halo discret qui épouse la forme de la lettre (contours de plus en plus larges et transparents)
        for (float wdt = 28.0f; wdt > 5.0f; wdt -= 2.5f)
        {
            g.setColour (col.withAlpha (0.024f * lvl));
            g.strokePath (p, PathStrokeType (wdt * s, PathStrokeType::curved, PathStrokeType::rounded));
        }
        g.setColour (col.withAlpha (0.06f * lvl));
        g.strokePath (p, PathStrokeType (3.4f * s, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (col.withAlpha (0.16f * lvl));
        g.strokePath (p, PathStrokeType (1.8f * s, PathStrokeType::curved, PathStrokeType::rounded));
        ColourGradient body (col.interpolatedWith (Colours::white, 0.45f).withAlpha (k), 0.0f, pb.getY(),
                             col.withMultipliedBrightness (0.95f).withAlpha (k), 0.0f, pb.getBottom(), false);
        g.setGradientFill (body);
        g.fillPath (p);
    }

    // ------------------------------------------------------------ anneau lumineux autour du footswitch (comme la Phase 90)
    // Dans le footswitch, dans la gorge entre le bouton qui s'enfonce et la bague.
    // Éteint : le diffuseur gris du visuel. Allumé : verre éclairé dans la couleur du footswitch (couleur du preset en
    // mode PRESET, du bloc en mode STOMP), cœur plus clair, et halo qui déborde sur la façade.
    void drawRing (Graphics& g, int f, float lvl, Colour col)
    {
        if (lvl <= 0.003f) return;
        const auto c = r (Point<float> (L::fsX[f], L::fsY));
        const float rm = (L::ringR0 + L::ringR1) * 0.5f * s, wdt = (L::ringR1 - L::ringR0) * s;
        Path ring;
        ring.addEllipse (c.x - rm, c.y - rm, rm * 2.0f, rm * 2.0f);
        for (float hw = 13.0f; hw > 3.0f; hw -= 1.5f)                    // halo : traits de plus en plus larges et transparents
        {
            g.setColour (col.withAlpha (0.045f * lvl));
            g.strokePath (ring, PathStrokeType (hw * s));
        }
        g.setColour (col.withAlpha (0.85f * lvl));
        g.strokePath (ring, PathStrokeType (wdt));
        g.setColour (col.interpolatedWith (Colours::white, 0.55f).withAlpha (0.9f * lvl));   // cœur du diffuseur
        g.strokePath (ring, PathStrokeType (wdt * 0.42f));
    }

    // (ancienne LED, gardée pour référence : plus dessinée)
    [[maybe_unused]] void drawLed (Graphics& g, Point<float> c0, float lvl, Colour col)
    {
        const auto c = r (c0);
        const float rr = L::ledR * s;
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillEllipse (c.x - rr + 0.9f * s, c.y - rr + 1.4f * s, rr * 2.0f, rr * 2.0f);
        ColourGradient chrome (Colour (0xffd2d4d8), c.x - rr * 0.8f, c.y - rr * 0.9f, Colour (0xff2a2c31), c.x + rr * 0.8f, c.y + rr * 0.9f, false);
        chrome.addColour (0.45, Colour (0xff74777e));
        g.setGradientFill (chrome);
        g.fillEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f);
        if (lvl > 0.003f)
        {
            g.setGradientFill (ColourGradient (col.withAlpha (0.35f * lvl), c.x, c.y, col.withAlpha (0.0f), c.x + rr, c.y, true));
            g.fillEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f);
        }
        const float gr = rr * 0.77f;
        g.setGradientFill (ColourGradient (Colour (0xff020203), c.x - gr * 0.6f, c.y - gr * 0.7f, Colour (0xff16181c), c.x + gr * 0.7f, c.y + gr * 0.8f, false));
        g.fillEllipse (c.x - gr, c.y - gr, gr * 2.0f, gr * 2.0f);
        const float lr = rr * 0.62f;
        const auto dark = col.withMultipliedBrightness (0.28f).withMultipliedSaturation (0.8f);
        g.setGradientFill (ColourGradient (dark, c.x - lr * 0.3f, c.y - lr * 0.4f, dark.withMultipliedBrightness (0.25f), c.x + lr, c.y + lr, true));
        g.fillEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f);
        if (lvl > 0.003f)
        {
            const float hot = jlimit (0.0f, 1.0f, lvl * 1.05f);
            ColourGradient body (Colours::white.withAlpha (hot), c.x + lr * 0.03f, c.y + lr * 0.02f, col.withMultipliedBrightness (0.6f).withAlpha (lvl), c.x + lr, c.y, true);
            body.addColour (0.18, col.interpolatedWith (Colours::white, 0.7f).withAlpha (hot));
            body.addColour (0.45, col.withAlpha (lvl));
            g.setGradientFill (body);
            g.fillEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f);
        }
        g.setColour (Colours::black.withAlpha (0.35f));
        g.drawEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f, 0.7f * s);
        ColourGradient sp (Colours::white.withAlpha (0.30f + 0.15f * lvl), c.x - lr * 0.38f, c.y - lr * 0.50f,
                           Colours::white.withAlpha (0.0f), c.x - lr * 0.38f + lr * 0.36f, c.y - lr * 0.50f, true);
        g.setGradientFill (sp);
        g.fillEllipse (c.x - lr * 0.74f, c.y - lr * 0.80f, lr * 0.72f, lr * 0.56f);
    }

    // ------------------------------------------------------------ grille de blocs
    int numSlots() const { return eng.blocks().size() + 1; }           // blocs + case « ajouter »
    int totalRows() const { return jmax (kRows, (numSlots() + kCols - 1) / kCols); }
    int maxFirstRow() const { return jmax (0, totalRows() - kRows); }
    float rowTop (int row) const { return kGridTop + kRowPitch * (float) (row - firstRow); }
    Point<float> slotCentre (int slot) const { return { kColX0 + kColPitch * (float) (slot % kCols), rowTop (slot / kCols) + kTileDy }; }
    Rectangle<float> tileRect (int slot) const { return Rectangle<float> (kTile, kTile).withCentre (slotCentre (slot)); }
    bool slotVisible (int slot) const { const int row = slot / kCols; return row >= firstRow && row < firstRow + kRows; }

    int slotAt (Point<float> p) const
    {
        if (p.y < kGridTop - 6.0f || p.y > kFooterLine) return -1;
        for (int k = firstRow * kCols; k < jmin (numSlots(), (firstRow + kRows) * kCols); ++k)
            if (tileRect (k).expanded (12.0f, 6.0f).withHeight (kTile + 34.0f).contains (p)) return k;
        return -1;
    }

    // position d'insertion pendant un glisser (0 .. nombre de blocs) : devant la case la plus proche
    int dropAt (Point<float> p) const
    {
        const int n = eng.blocks().size();
        int best = n; float bestD = 1.0e9f;
        for (int k = 0; k <= n; ++k)
        {
            if (! slotVisible (k) && k < n) continue;
            const auto c = slotCentre (k) - Point<float> (kColPitch * 0.5f, 0.0f);
            const float d = c.getDistanceFrom (p);
            if (d < bestD) { bestD = d; best = k; }
        }
        return best;
    }

    void drawConnections (Graphics& g)
    {
        const int n = numSlots();
        Path path;
        // lignes de retour : même débord à gauche et à droite des carrés (un demi-écart)
        const float xL = kColX0 - kTile * 0.5f - (kColPitch - kTile) * 0.5f;
        const float xR = kColX0 + kColPitch * (float) (kCols - 1) + kTile * 0.5f + (kColPitch - kTile) * 0.5f;
        auto P = [this] (float x, float y) { return S (Point<float> (x, y)); };
        // entrée
        {
            const auto c0 = slotCentre (0);
            path.startNewSubPath (P (xL - 6.0f, c0.y));
            path.lineTo (S (c0));
        }
        for (int k = 1; k < n; ++k)
        {
            const auto a = slotCentre (k - 1), b = slotCentre (k);
            if (k % kCols != 0) { path.lineTo (S (b)); continue; }
            // retour sous la rangée : au milieu de l'espace libre entre le nom des blocs et le haut des carrés de la rangée suivante
            const float nameBottom = kTileDy + kTile * 0.5f + 4.0f + 22.0f;
            const float nextTop = kRowPitch + kTileDy - kTile * 0.5f;
            const float ry = rowTop ((k - 1) / kCols) + (nameBottom + nextTop) * 0.5f - 8.0f;   // un peu au-dessus du milieu
            path.lineTo (P (xR, a.y)); path.lineTo (P (xR, ry)); path.lineTo (P (xL, ry)); path.lineTo (P (xL, b.y)); path.lineTo (S (b));
        }
        const auto last = slotCentre (n - 1);
        path.lineTo (P (xR + 6.0f, last.y));
        g.setColour (kLine.withAlpha (0.75f));
        g.strokePath (path, PathStrokeType (2.0f * s, PathStrokeType::mitered, PathStrokeType::rounded));

        g.setFont (font (tfBold, 15.0f));
        g.setColour (kDim);
        if (firstRow == 0)
        {
            const auto c0 = slotCentre (0);
            g.drawText ("IN", S (Rectangle<float> (xL - 6.0f - 22.0f, c0.y - 24.0f, 44.0f, 18.0f)), Justification::centred);
            g.fillEllipse (Rectangle<float> (S (7.0f), S (7.0f)).withCentre (P (xL - 6.0f, c0.y)));
        }
        if (slotVisible (n - 1))
        {
            g.drawText ("OUT", S (Rectangle<float> (xR + 6.0f - 22.0f, last.y - 24.0f, 44.0f, 18.0f)), Justification::centred);
            g.fillEllipse (Rectangle<float> (S (7.0f), S (7.0f)).withCentre (P (xR + 6.0f, last.y)));
        }
    }

    void drawBlock (Graphics& g, int slot, const FxInfo& f, Point<float> centre, float alpha, bool ghost)
    {
        const int kind = FxCat::classify (f.original);
        const auto col = FxCat::colour (kind);
        const auto t = S (Rectangle<float> (kTile, kTile).withCentre (centre));
        const bool on = f.enabled && ! f.offline;
        const bool hov = ! ghost && hoverSlot == slot;
        const float rad = S (11.0f);
        // famille utilitaire (blanc) : carreau blanc plein et icône sombre quand il est allumé, pour trancher net avec un bloc éteint
        const bool white = kind == FxCat::utility && on;

        // carreau
        g.setColour (kScrBg.withAlpha (alpha));
        g.fillRoundedRectangle (t, rad);
        if (on)
        {
            const float a0 = white ? 0.855f : 0.42f, a1 = white ? 0.665f : 0.16f;   // blanc : 2 x 5 % de moins
            g.setGradientFill (ColourGradient (col.withAlpha (a0 * alpha), t.getX(), t.getY(), col.withAlpha (a1 * alpha), t.getX(), t.getBottom(), false));
            g.fillRoundedRectangle (t, rad);
        }
        else
        {
            g.setColour (kScrPanel2.withAlpha (alpha));
            g.fillRoundedRectangle (t, rad);
        }
        g.setColour ((on ? col : kFaint.brighter (0.25f)).withAlpha (alpha * (hov ? 1.0f : 0.9f)));
        g.drawRoundedRectangle (t.reduced (S (0.8f)), rad, S (hov ? 2.6f : 1.8f));
        if (f.guid == paramGuid && overlay == Overlay::params)
        {
            g.setColour (kText.withAlpha (alpha));
            g.drawRoundedRectangle (t.expanded (S (4.0f)), rad + S (4.0f), S (1.6f));
        }

        // icône
        FxCat::drawIcon (g, kind, t.reduced (S (13.0f)), (white ? Colour (0xff1a1d23) : on ? col.brighter (0.35f) : kDim.withAlpha (0.55f)).withAlpha (alpha), S (2.6f));

        // témoin ON (coin haut droit)
        const auto led = Rectangle<float> (S (8.0f), S (8.0f)).withCentre ({ t.getRight() - S (10.0f), t.getY() + S (10.0f) });
        g.setColour (white ? Colour (0xff1a1d23).withAlpha (alpha) : on ? col.brighter (0.6f).withAlpha (alpha) : kFaint.withAlpha (alpha));
        g.fillEllipse (led);

        // badges : parallèle, fenêtre ouverte, hors ligne
        g.setFont (font (tfBold, 12.0f));
        if (f.parallel > 0)
        {
            g.setColour (kAmber.withAlpha (alpha));
            g.drawText ("||", Rectangle<float> (t.getX() + S (4.0f), t.getBottom() - S (17.0f), S (16.0f), S (14.0f)), Justification::centred);
        }
        if (f.open)
        {
            g.setColour (kText.withAlpha (0.8f * alpha));
            g.drawRect (Rectangle<float> (t.getX() + S (6.0f), t.getY() + S (6.0f), S (9.0f), S (7.0f)), S (1.2f));
        }
        if (f.offline)
        {
            g.setColour (Colour (0xffe0533f).withAlpha (alpha));
            g.drawLine ({ t.getX() + S (8.0f), t.getBottom() - S (8.0f), t.getRight() - S (8.0f), t.getY() + S (8.0f) }, S (2.0f));
        }

        // nom
        if (! ghost)
        {
            const auto nameR = S (Rectangle<float> (centre.x - kColPitch * 0.5f + 2.0f, centre.y + kTile * 0.5f + 4.0f, kColPitch - 4.0f, 24.0f));
            g.setFont (font (tfMedium, 17.5f));
            g.setColour ((on ? kText : kDim).withAlpha (alpha));
            g.drawText (FxCat::shortName (f.name), nameR, Justification::centredTop, true);
        }
    }

    void drawGrid (Graphics& g)
    {
        const auto& blocks = eng.blocks();
        const auto& fx = eng.chain();
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (S (Rectangle<float> (0.0f, kGridTop - 8.0f, kSW, kFooterLine - kGridTop + 6.0f)).toNearestInt());

        drawConnections (g);

        // cases vides (repères discrets)
        for (int k = numSlots(); k < (firstRow + kRows) * kCols; ++k)
        {
            if (k / kCols < firstRow) continue;
            g.setColour (kFaint.withAlpha (0.5f));
            g.fillEllipse (Rectangle<float> (S (4.0f), S (4.0f)).withCentre (S (slotCentre (k))));
        }

        const int dragFrom = dragging ? downSlot : -1;
        for (int k = 0; k < blocks.size(); ++k)
        {
            if (! slotVisible (k)) continue;
            drawBlock (g, k, fx.getReference (blocks[k]), slotCentre (k), k == dragFrom ? 0.25f : 1.0f, false);
        }

        // case « ajouter »
        const int add = blocks.size();
        if (slotVisible (add))
        {
            const auto t = S (tileRect (add));
            const bool hov = hoverSlot == add;
            g.setColour (kScrBg);
            g.fillRoundedRectangle (t, S (11.0f));
            Path dash, outline;
            outline.addRoundedRectangle (t.reduced (S (1.0f)), S (11.0f));
            const float dl[2] = { S (5.0f), S (4.0f) };
            PathStrokeType (S (1.6f)).createDashedStroke (dash, outline, dl, 2);
            g.setColour (hov ? kAccent : kDim.withAlpha (0.7f));
            g.fillPath (dash);
            const auto c = t.getCentre();
            const float a = S (13.0f);
            g.drawLine (c.x - a, c.y, c.x + a, c.y, S (2.6f));
            g.drawLine (c.x, c.y - a, c.x, c.y + a, S (2.6f));
            g.setFont (font (tfMedium, 16.0f));
            g.drawText (blocks.isEmpty() ? "Add an effect" : "Add", S (Rectangle<float> (slotCentre (add).x - 70.0f, slotCentre (add).y + kTile * 0.5f + 3.0f, 140.0f, 22.0f)),
                        Justification::centredTop, true);
        }

        // glisser : repère d'insertion + bloc qui suit la souris
        if (dragging && downSlot >= 0 && downSlot < blocks.size())
        {
            if (dropPos >= 0)
            {
                const auto c = slotCentre (jmin (dropPos, numSlots() - 1)) - Point<float> (kColPitch * 0.5f, 0.0f);
                g.setColour (kAccent);
                g.fillRoundedRectangle (S (Rectangle<float> (5.0f, kTile + 14.0f).withCentre (c)), S (2.5f));
            }
            drawBlock (g, -1, fx.getReference (blocks[downSlot]), dragPos, 0.9f, true);
        }

        // défilement
        if (maxFirstRow() > 0)
        {
            g.setFont (font (tfBold, 14.0f));
            g.setColour (kDim);
            const String t = String (firstRow + 1) + "-" + String (jmin (totalRows(), firstRow + kRows)) + " / " + String (totalRows());
            g.drawText (t, S (Rectangle<float> (kSW - 120.0f, kFooterLine - 20.0f, 112.0f, 18.0f)), Justification::centredRight);
            if (firstRow > 0) g.drawText (CharPointer_UTF8 ("\xe2\x96\xb2"), S (Rectangle<float> (kSW - 30.0f, kGridTop - 6.0f, 24.0f, 18.0f)), Justification::centred);
            if (firstRow < maxFirstRow()) g.drawText (CharPointer_UTF8 ("\xe2\x96\xbc"), S (Rectangle<float> (kSW - 30.0f, kFooterLine - 38.0f, 24.0f, 18.0f)), Justification::centred);
        }
    }

    // ------------------------------------------------------------ boutons
    std::vector<Btn> headerButtons() const
    {
        std::vector<Btn> v;
        v.push_back ({ { 520.0f, 19.0f, 34.0f, 36.0f }, bBankPrev, CharPointer_UTF8 ("\xe2\x97\x80") });
        v.push_back ({ { 660.0f, 19.0f, 34.0f, 36.0f }, bBankNext, CharPointer_UTF8 ("\xe2\x96\xb6") });
        v.push_back ({ { 706.0f, 19.0f, 84.0f, 36.0f }, bModePreset, "PRESET", eng.mode() == OctopusEngine::presetMode });
        v.push_back ({ { 798.0f, 19.0f, 84.0f, 36.0f }, bModeStomp, "STOMP", eng.mode() == OctopusEngine::stompMode });
        v.push_back ({ { 892.0f, 19.0f, 78.0f, 36.0f }, bBanks, "BANKS", overlay == Overlay::banks });
        v.push_back ({ { 978.0f, 19.0f, 72.0f, 36.0f }, bSave, "SAVE", false, eng.isReady() });
        return v;
    }

    Rectangle<float> bankRect (int b) const { return { 16.0f + 82.0f * (float) (b % 4), 134.0f + 72.0f * (float) (b / 4), 74.0f, 64.0f }; }
    Rectangle<float> cardRect (int slot) const { return { 356.0f, 134.0f + 76.0f * (float) slot, 692.0f, 66.0f }; }

    std::vector<Btn> overlayButtons() const
    {
        std::vector<Btn> v;
        if (overlay == Overlay::none) return v;
        v.push_back ({ { 1008.0f, 84.0f, 40.0f, 36.0f }, bClose, CharPointer_UTF8 ("\xe2\x9c\x95") });
        if (overlay == Overlay::browser)
        {
            float x = 360.0f;
            for (int c = 0; c < kNumChips; ++c)
            {
                const float w = 26.0f + 9.4f * (float) String (kChips[c]).length();
                v.push_back ({ { x, 130.0f, w, 38.0f }, bChip + c, kChips[c], chip == c });
                x += w + 6.0f;
            }
            v.push_back ({ { 702.0f, 84.0f, 84.0f, 36.0f }, bClearPicked, "CLEAR", false, picked.size() > 0 });
            v.push_back ({ { 792.0f, 84.0f, 112.0f, 36.0f }, bAddPicked, picked.isEmpty() ? String ("ADD") : "ADD " + String (picked.size()), picked.size() > 0, picked.size() > 0 });
            v.push_back ({ { 910.0f, 84.0f, 90.0f, 36.0f }, bReload, "RESCAN" });
        }
        else if (overlay == Overlay::params)
        {
            const auto* f = paramFx();
            const bool on = f != nullptr && f->enabled;
            const int pages = jmax (1, (paramCount + 7) / 8);
            v.push_back ({ { 400.0f, 84.0f, 110.0f, 36.0f }, bPresets, "PRESETS", presetMode });
            v.push_back ({ { 516.0f, 84.0f, 104.0f, 36.0f }, bParamWindow, "WINDOW", f != nullptr && f->open });
            v.push_back ({ { 626.0f, 84.0f, 76.0f, 36.0f }, bParamMidi, "MIDI", midiView && ! presetMode });
            if (presetMode)
            {
                // flèches : preset précédent / suivant (comme les flèches du menu de presets de REAPER)
                v.push_back ({ { 760.0f, 84.0f, 34.0f, 36.0f }, bPagePrev, CharPointer_UTF8 ("\xe2\x97\x80"), false, plist.count > 1 });
                v.push_back ({ { 872.0f, 84.0f, 34.0f, 36.0f }, bPageNext, CharPointer_UTF8 ("\xe2\x96\xb6"), false, plist.count > 1 });
                v.push_back ({ { 652.0f, 132.0f, 116.0f, 36.0f }, bPresetSave, presetEdIndex >= -1 ? String ("OK") : String ("SAVE USER"), presetEdIndex >= -1 });
                v.push_back ({ { 776.0f, 132.0f, 126.0f, 36.0f }, bPresetRescan, "RELOAD LIST" });
            }
            else
            {
                v.push_back ({ { 760.0f, 84.0f, 34.0f, 36.0f }, bPagePrev, CharPointer_UTF8 ("\xe2\x97\x80"), false, paramPage > 0 });
                v.push_back ({ { 872.0f, 84.0f, 34.0f, 36.0f }, bPageNext, CharPointer_UTF8 ("\xe2\x96\xb6"), false, paramPage < pages - 1 });
            }
            // BYPASS à côté de la croix : allumé quand l'effet est contourné
            v.push_back ({ { 914.0f, 84.0f, 88.0f, 36.0f }, bParamOnOff, "BYPASS", ! on });
        }
        else if (overlay == Overlay::banks)
        {
            for (int b = 0; b < OctopusEngine::kBanks; ++b)
                v.push_back ({ bankRect (b), bBank + b, String (b + 1), eng.bank() == b });
            for (int sl = 0; sl < OctopusEngine::kSlots; ++sl)
            {
                const auto c = cardRect (sl);
                const int idx = eng.bank() * OctopusEngine::kSlots + sl;
                const bool saved = eng.preset (idx).saved;
                float x = c.getRight() - 8.0f;
                for (int a = numCardActions; --a >= 0;)
                {
                    const float w = a == cRename ? 84.0f : 64.0f;
                    x -= w;
                    bool en = true;
                    if (a == cLoad || a == cRename || a == cCopy || a == cClear) en = saved;
                    if (a == cSave) en = eng.isReady();
                    if (a == cPaste) en = eng.canPaste();
                    v.push_back ({ { x, c.getY() + 15.0f, w - 6.0f, 36.0f }, bCard + sl * 10 + a, kCardText[a], false, en });
                }
            }
            for (int f = 0; f < 4; ++f)
                v.push_back ({ { 520.0f + 132.0f * (float) f, 470.0f, 124.0f, 44.0f }, bFsColour + f, "COLOUR " + fsLetter (f) });
            v.push_back ({ { 16.0f, 560.0f, 156.0f, 44.0f }, bExport, "EXPORT..." });
            v.push_back ({ { 180.0f, 560.0f, 156.0f, 44.0f }, bImport, "IMPORT..." });
            v.push_back ({ { 356.0f, 560.0f, 400.0f, 44.0f }, bPc, String ("PROGRAM CHANGE = PRESETS : ") + (eng.programChange ? "ON" : "OFF"), eng.programChange });
        }
        return v;
    }

    int buttonAt (Point<float> p) const
    {
        if (tunerShown())
        {
            for (auto& b : tunerButtons()) if (b.r.contains (p)) return b.id;
            return bNone;
        }
        if (overlay != Overlay::none)
            for (auto& b : overlayButtons()) if (b.enabled && b.r.contains (p)) return b.id;
        for (auto& b : headerButtons()) if (b.enabled && b.r.contains (p)) return b.id;
        return bNone;
    }

    void drawButton (Graphics& g, const Btn& b, float textSize = 15.0f)
    {
        const auto rr = S (b.r);
        const bool hov = hoverBtn == b.id && b.enabled;
        g.setColour (b.active ? kAccent.withAlpha (0.22f) : (hov ? kScrPanel2.brighter (0.15f) : kScrPanel2));
        g.fillRoundedRectangle (rr, S (7.0f));
        g.setColour (b.active ? kAccent : (hov ? kDim : kFaint));
        g.drawRoundedRectangle (rr.reduced (S (0.6f)), S (7.0f), S (1.2f));
        g.setFont (font (tfBold, textSize));
        g.setColour (! b.enabled ? kFaint : (b.active ? kText : kText.withAlpha (0.85f)));
        g.drawText (b.text, rr, Justification::centred, true);
    }

    // ------------------------------------------------------------ écran complet
    void drawScreen (Graphics& g)
    {
        const auto glass = r (L::glass);
        Graphics::ScopedSaveState ss (g);
        Path clip; clip.addRoundedRectangle (glass, L::glassRadius * s);
        g.reduceClipRegion (clip);
        g.setGradientFill (ColourGradient (Colour (0xff0d1015), glass.getX(), glass.getY(), Colour (0xff07080b), glass.getX(), glass.getBottom(), false));
        g.fillRect (glass);

        if (tunerShown()) drawTuner (g);
        else
        {
            drawHeader (g);
            if (! eng.isReady())
                drawNotReady (g);
            else
            {
                drawGrid (g);
                drawFooter (g);
            }
            if (overlay != Overlay::none) drawOverlay (g);
        }

        // vitre : léger reflet en biais (lumière en haut à gauche) + assombrissement des bords
        Path gloss;
        gloss.startNewSubPath (glass.getX(), glass.getY());
        gloss.lineTo (glass.getX() + glass.getWidth() * 0.46f, glass.getY());
        gloss.lineTo (glass.getX() + glass.getWidth() * 0.30f, glass.getBottom());
        gloss.lineTo (glass.getX(), glass.getBottom());
        gloss.closeSubPath();
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.035f), glass.getX(), glass.getY(), Colours::white.withAlpha (0.0f),
                                           glass.getX() + glass.getWidth() * 0.35f, glass.getY() + glass.getHeight() * 0.6f, false));
        g.fillPath (gloss);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (glass.reduced (0.5f), L::glassRadius * s, 1.0f);
    }

    void drawHeader (Graphics& g)
    {
        g.setColour (kScrPanel);
        g.fillRect (S (Rectangle<float> (0.0f, 0.0f, kSW, kHeaderH)));
        g.setColour (kFaint.withAlpha (0.6f));
        g.fillRect (S (Rectangle<float> (0.0f, kHeaderH - 1.0f, kSW, 1.0f)));

        // preset chargé
        const int cur = eng.current();
        const auto badge = S (Rectangle<float> (14.0f, 13.0f, 104.0f, 48.0f));
        // même couleur que le footswitch de ce preset (choisie dans BANKS > FOOTSWITCH COLOURS)
        const auto badgeCol = cur >= 0 ? Colour (eng.fsColour (cur % OctopusEngine::kSlots)) : kScrPanel2;
        g.setColour (badgeCol);
        g.fillRoundedRectangle (badge, S (8.0f));
        g.setFont (font (tfBold, 32.0f));
        g.setColour (cur < 0 ? kDim : badgeCol.getPerceivedBrightness() > 0.55f ? kScrBg : Colours::white);
        g.drawText (cur >= 0 ? OctopusEngine::label (cur) : String ("--"), badge.translated (0.0f, S (1.5f)), Justification::centred);

        const String name = cur >= 0 ? eng.displayName (cur) : String (eng.isReady() ? "Live chain" : "OCTOPUS");
        const auto nameR = S (headerNameRect());
        g.setFont (font (tfMedium, 26.0f));
        g.setColour (kText);
        if (! headerRenaming) g.drawText (name, nameR, Justification::centredLeft, true);
        if (eng.modified() && ! headerRenaming)
        {
            const float tw = jmin (nameR.getWidth() - S (60.0f), GlyphArrangement::getStringWidth (font (tfMedium, 26.0f), name));
            g.setFont (font (tfBold, 14.0f));
            g.setColour (kAmber);
            g.drawText ("EDITED", Rectangle<float> (nameR.getX() + tw + S (10.0f), nameR.getY() + S (9.0f), S (60.0f), S (18.0f)), Justification::centredLeft);
        }

        const auto msg = eng.message();
        String sub = eng.trackName().isNotEmpty() ? eng.trackName() : eng.statusText();
        if (msg.isNotEmpty()) sub = msg;
        g.setFont (font (tfRegular, 17.0f));
        g.setColour (msg.isNotEmpty() ? kAccent : kDim);
        g.drawText (sub, S (Rectangle<float> (132.0f, 44.0f, 380.0f, 22.0f)), Justification::centredLeft, true);

        // bank affichée
        g.setFont (font (tfBold, 17.0f));
        g.setColour (kText);
        g.drawText ("BANK " + String (eng.bank() + 1).paddedLeft ('0', 2), S (Rectangle<float> (554.0f, 19.0f, 106.0f, 36.0f)), Justification::centred);
        for (auto& b : headerButtons()) drawButton (g, b);
    }

    void drawNotReady (Graphics& g)
    {
        auto area = S (Rectangle<float> (60.0f, 120.0f, kSW - 120.0f, 250.0f));
        g.setFont (font (tfBold, 30.0f));
        g.setColour (kText);
        g.drawText ("OCTOPUS", area.removeFromTop (S (50.0f)), Justification::centred);
        g.setFont (font (tfMedium, 21.0f));
        g.setColour (kAccent);
        g.drawText (eng.statusText(), area.removeFromTop (S (40.0f)), Justification::centred, true);
        g.setFont (font (tfRegular, 17.0f));
        g.setColour (kDim);
        g.drawFittedText ("Insert Octopus on a track in REAPER : its screen shows that track's FX chain.\n"
                          "Add, remove, move and switch effects here ; they change in the track's FX chain at the same time.",
                          area.toNearestInt(), Justification::centredTop, 3);
    }

    void drawFooter (Graphics& g)
    {
        g.setColour (kFaint.withAlpha (0.45f));
        g.fillRect (S (Rectangle<float> (0.0f, kFooterLine, kSW, 1.0f)));
        const bool inPresetMode = eng.mode() == OctopusEngine::presetMode;
        for (int f = 0; f < 4; ++f)
        {
            const auto cell = footerCell (f);
            const auto c = S (cell);
            const bool lit = eng.fsLit (f);
            const int kind = eng.fsKind (f);
            const auto col = inPresetMode ? Colour (eng.fsColour (f)) : (kind >= 0 ? FxCat::colour (kind) : kDim);
            const bool hov = hoverFooter == f;
            g.setColour (lit ? col.withAlpha (0.16f) : (hov ? kScrPanel2.brighter (0.1f) : kScrPanel));
            g.fillRoundedRectangle (c, S (9.0f));
            g.setColour (lit ? col : kFaint.withAlpha (hov ? 1.0f : 0.7f));
            g.drawRoundedRectangle (c.reduced (S (0.6f)), S (9.0f), S (lit ? 2.0f : 1.2f));
            // lettre du footswitch
            const auto letter = S (Rectangle<float> (cell.getX() + 8.0f, cell.getY() + 13.0f, 36.0f, 36.0f));
            g.setColour (lit ? col : kScrPanel2.brighter (0.2f));
            g.fillRoundedRectangle (letter, S (6.0f));
            g.setFont (font (tfBold, 20.0f));
            g.setColour (lit ? kScrBg : kText.withAlpha (0.8f));
            g.drawText (fsLetter (f), letter.translated (0.0f, S (1.0f)), Justification::centred);
            g.setFont (font (tfBold, 15.0f));
            g.setColour (lit ? col.brighter (0.3f) : kDim);
            g.drawText (eng.fsTitle (f), S (Rectangle<float> (cell.getX() + 54.0f, cell.getY() + 7.0f, cell.getWidth() - 60.0f, 20.0f)), Justification::centredLeft, true);
            g.setFont (font (tfMedium, 18.0f));
            g.setColour (lit ? kText : kText.withAlpha (0.75f));
            g.drawText (eng.fsText (f), S (Rectangle<float> (cell.getX() + 54.0f, cell.getY() + 28.0f, cell.getWidth() - 62.0f, 26.0f)), Justification::centredLeft, true);
        }
    }

    Rectangle<float> footerCell (int f) const
    {
        return { kFooterGap + (kFooterW + kFooterGap) * (float) f, kFooterY, kFooterW, kFooterH };
    }
    int footerAt (Point<float> p) const
    {
        if (overlay != Overlay::none || ! eng.isReady()) return -1;
        for (int f = 0; f < 4; ++f) if (footerCell (f).contains (p)) return f;
        return -1;
    }

    // ------------------------------------------------------------ fenêtres sur l'écran
    void drawOverlay (Graphics& g)
    {
        g.setGradientFill (ColourGradient (Colour (0xff0b0d11), 0.0f, S (kOverlay).getY(), Colour (0xff08090c), 0.0f, S (kOverlay).getBottom(), false));
        g.fillRect (S (kOverlay));
        g.setColour (kFaint.withAlpha (0.6f));
        g.fillRect (S (Rectangle<float> (0.0f, kOverlay.getY(), kSW, 1.0f)));
        g.setFont (font (tfBold, 20.0f));
        g.setColour (kText);
        if (overlay == Overlay::browser)
        {
            g.drawText ("ADD AN EFFECT", S (Rectangle<float> (16.0f, 84.0f, 220.0f, 36.0f)), Justification::centredLeft);
            g.setFont (font (tfRegular, 16.0f));
            g.setColour (kDim);
            const int n = eng.blocks().size();
            const String where = insertPos >= n ? "at the end of the chain" : "before " + FxCat::shortName (eng.chain().getReference (eng.blocks()[insertPos]).name);
            g.drawText (where + "  -  click to select (several in a row), ADD", S (Rectangle<float> (236.0f, 84.0f, 460.0f, 36.0f)), Justification::centredLeft, true);
            drawBrowserList (g);
        }
        else if (overlay == Overlay::params) drawParams (g);
        else if (overlay == Overlay::banks)  drawBanks (g);
        for (auto& b : overlayButtons())
        {
            drawButton (g, b, b.id == bClose ? 18.0f : 15.0f);
            if (b.id >= bFsColour && b.id < bFsColour + 4)          // pastille de la couleur choisie
            {
                const auto sw = S (Rectangle<float> (b.r.getX() + 10.0f, b.r.getCentreY() - 8.0f, 16.0f, 16.0f));
                g.setColour (Colour (eng.fsColour (b.id - bFsColour)));
                g.fillEllipse (sw);
                g.setColour (Colours::white.withAlpha (0.3f));
                g.drawEllipse (sw, S (1.0f));
            }
        }
        if (overlay == Overlay::banks)
        {
            g.setFont (font (tfBold, 15.0f));
            g.setColour (kDim);
            g.drawText ("FOOTSWITCH COLOURS (PRESET MODE)", S (Rectangle<float> (16.0f, 470.0f, 496.0f, 44.0f)), Justification::centredRight);
        }
    }

    // navigateur : filtre (catégorie + recherche), deux colonnes
    Array<InstalledFx> filtered()
    {
        Array<InstalledFx> out;
        const auto q = search.getText().trim().toLowerCase();
        for (auto& f : eng.installedList())
        {
            const auto type = FxCat::typeOf (f.name).toUpperCase();
            if (type.endsWith ("I")) continue;                          // instruments (VSTi, VST3i, CLAPi, AUi)
            switch (chip)
            {
                case 1: if (! FxCat::vendor (f.name).equalsIgnoreCase ("Studio Wiga")) continue; break;
                case 2: if (type != "VST3") continue; break;
                case 3: if (type != "VST" && type != "VST2") continue; break;
                case 4: if (type != "CLAP") continue; break;
                case 5: if (type != "JS") continue; break;
                case 6: if (type != "AU") continue; break;
                default: break;
            }
            if (q.isNotEmpty() && ! f.name.toLowerCase().contains (q)) continue;
            out.add (f);
        }
        return out;
    }

    static constexpr int kListRows = 12;
    Rectangle<float> itemRect (int visibleIndex) const
    {
        const int col = visibleIndex / kListRows, row = visibleIndex % kListRows;
        return { 16.0f + 520.0f * (float) col, 182.0f + 34.0f * (float) row, 512.0f, 31.0f };
    }

    void drawBrowserList (Graphics& g)
    {
        const auto items = filtered();
        const int perPage = kListRows * 2;
        if (items.isEmpty())
        {
            g.setFont (font (tfMedium, 18.0f));
            g.setColour (kDim);
            g.drawText (eng.installedList().isEmpty() ? "REAPER did not give its plug-in list (REAPER 7 needed)" : "No plug-in found",
                        S (Rectangle<float> (16.0f, 230.0f, 1032.0f, 40.0f)), Justification::centred);
            return;
        }
        for (int v = 0; v < perPage; ++v)
        {
            const int i = listScroll * kListRows + v;
            if (i >= items.size()) break;
            const auto& f = items.getReference (i);
            const auto ir = S (itemRect (v));
            const bool hov = hoverItem == v;
            const int order = pickedIndex (f);
            g.setColour (order >= 0 ? kAccent.withAlpha (0.20f) : (hov ? kScrPanel2.brighter (0.12f) : kScrPanel));
            g.fillRoundedRectangle (ir, S (6.0f));
            if (order >= 0)
            {
                g.setColour (kAccent);
                g.drawRoundedRectangle (ir.reduced (S (0.6f)), S (6.0f), S (1.4f));
                const auto badge = Rectangle<float> (S (24.0f), S (24.0f)).withCentre ({ ir.getRight() - S (186.0f), ir.getCentreY() });
                g.fillEllipse (badge);
                g.setFont (font (tfBold, 14.0f));
                g.setColour (kScrBg);
                g.drawText (String (order + 1), badge, Justification::centred);
            }
            const int kind = FxCat::classify (f.name);
            g.setColour (FxCat::colour (kind));
            g.fillRoundedRectangle (ir.withWidth (S (5.0f)), S (2.5f));
            FxCat::drawIcon (g, kind, Rectangle<float> (S (22.0f), S (22.0f)).withCentre ({ ir.getX() + S (24.0f), ir.getCentreY() }), FxCat::colour (kind).brighter (0.3f), S (1.8f));
            g.setFont (font (tfMedium, 17.0f));
            g.setColour (kText);
            g.drawText (FxCat::shortName (f.name), ir.withTrimmedLeft (S (44.0f)).withTrimmedRight (S (204.0f)), Justification::centredLeft, true);
            g.setFont (font (tfRegular, 13.5f));
            g.setColour (kDim);
            const auto vend = FxCat::vendor (f.name);
            g.drawText (FxCat::typeOf (f.name) + (vend.isNotEmpty() ? "  " + vend : String()), ir.withTrimmedRight (S (10.0f)).removeFromRight (S (166.0f)),
                        Justification::centredRight, true);
        }
        const int pages = (items.size() + kListRows - 1) / kListRows;
        if (pages > 2)
        {
            g.setFont (font (tfRegular, 14.0f));
            g.setColour (kDim);
            g.drawText (String (items.size()) + " plug-ins  -  scroll for more", S (Rectangle<float> (16.0f, 598.0f, 1032.0f, 20.0f)), Justification::centredRight);
        }
    }

    void addFromBrowser (const InstalledFx& f)
    {
        const int at = insertPos;
        closeOverlay();
        eng.addFx (f, at);
    }

    int pickedIndex (const InstalledFx& f) const
    {
        for (int k = 0; k < picked.size(); ++k) if (picked.getReference (k).name == f.name) return k;
        return -1;
    }
    void togglePick (const InstalledFx& f)
    {
        const int k = pickedIndex (f);
        if (k >= 0) picked.remove (k); else picked.add (f);
        askRepaint (screenPx());
    }
    // tous les effets choisis, dans l'ordre, à la suite, en une fois
    void addPicked()
    {
        if (picked.isEmpty()) return;
        const auto list = picked;
        const int at = insertPos;
        closeOverlay();
        eng.addFxList (list, at);
    }

    // paramètres du bloc choisi
    const FxInfo* paramFx() const
    {
        for (auto& f : eng.chain()) if (f.guid == paramGuid) return &f;
        return nullptr;
    }
    int paramFxIndex() const
    {
        for (int i = 0; i < eng.chain().size(); ++i) if (eng.chain().getReference (i).guid == paramGuid) return i;
        return -1;
    }
    Rectangle<float> paramCell (int k) const { return { 16.0f + 520.0f * (float) (k % 2), 134.0f + 112.0f * (float) (k / 2), 512.0f, 100.0f }; }
    Rectangle<float> paramBar (int k) const { auto c = paramCell (k); return { c.getX() + 14.0f, c.getY() + 62.0f, c.getWidth() - 28.0f, 22.0f }; }

    // lit les paramètres affichés (au plus 8) ; renvoie true si quelque chose a changé
    bool pollParams (bool force)
    {
        const int i = paramFxIndex();
        auto& h = eng.chainHost();
        if (i < 0) { if (overlay == Overlay::params) closeOverlay(); return false; }
        const auto key = paramGuid + (midiView ? "/midi" : "/main") + "/" + String (h.numParams (i));
        if (key != paramListKey)
        {
            // tout ce qui concerne le MIDI (canal, learn, CC émulés...) est rangé derrière le bouton MIDI
            paramList.clearQuick();
            const int n = h.numParams (i);
            for (int p = 0; p < n; ++p)
                if (h.paramName (i, p).containsIgnoreCase ("MIDI") == midiView) paramList.add (p);
            paramListKey = key;
        }
        paramCount = paramList.size();
        const int pages = jmax (1, (paramCount + 7) / 8);
        paramPage = jlimit (0, pages - 1, paramPage);
        params.clearQuick();
        String sig;
        for (int k = 0; k < 8; ++k)
        {
            const int idx = paramPage * 8 + k;
            if (idx >= paramCount) break;
            const int p = paramList[idx];
            ParamView pv { p, h.paramName (i, p), h.paramText (i, p), h.getParam (i, p), h.paramSteps (i, p) };
            sig << pv.name << pv.text << String (pv.value, 4) << "|";
            params.add (pv);
        }
        sig << paramCount << paramPage;
        if (! force && sig == drawnParamSig) return false;
        drawnParamSig = sig;
        return true;
    }

    void drawParams (Graphics& g)
    {
        const auto* f = paramFx();
        if (f == nullptr) return;
        const int kind = FxCat::classify (f->original);
        FxCat::drawIcon (g, kind, S (Rectangle<float> (16.0f, 86.0f, 32.0f, 32.0f)), FxCat::colour (kind).brighter (0.3f), S (2.0f));
        g.setFont (font (tfBold, 21.0f));
        g.setColour (kText);
        g.drawText (FxCat::shortName (f->name), S (Rectangle<float> (58.0f, 84.0f, 336.0f, 36.0f)), Justification::centredLeft, true);
        if (presetMode) { drawPresets (g, kind); return; }
        const int pages = jmax (1, (paramCount + 7) / 8);
        g.setFont (font (tfBold, 15.0f));
        g.setColour (kDim);
        g.drawText (String (paramPage + 1) + " / " + String (pages), S (Rectangle<float> (796.0f, 84.0f, 74.0f, 36.0f)), Justification::centred);
        if (params.isEmpty())
        {
            g.setFont (font (tfMedium, 18.0f));
            g.drawText (midiView ? "No MIDI parameter" : "This plug-in has no parameter", S (Rectangle<float> (16.0f, 230.0f, 1032.0f, 40.0f)), Justification::centred);
            return;
        }
        const auto col = FxCat::colour (kind);
        for (int k = 0; k < params.size(); ++k)
        {
            const auto& pv = params.getReference (k);
            const auto cell = S (paramCell (k));
            g.setColour (hoverParam == k || paramDrag == k ? kScrPanel2.brighter (0.08f) : kScrPanel);
            g.fillRoundedRectangle (cell, S (8.0f));
            // nom du paramètre : grand et lisible ; valeur telle que le plugin l'affiche
            g.setFont (font (tfMedium, 23.0f));
            g.setColour (kText);
            g.drawText (pv.name, cell.reduced (S (14.0f), 0.0f).withY (cell.getY() + S (8.0f)).withHeight (S (38.0f)).withTrimmedRight (S (170.0f)), Justification::centredLeft, true);
            g.setFont (font (tfBold, 21.0f));
            g.setColour (col.brighter (0.4f));
            g.drawText (pv.text, cell.reduced (S (14.0f), 0.0f).withY (cell.getY() + S (8.0f)).withHeight (S (38.0f)).removeFromRight (S (200.0f)), Justification::centredRight, true);
            const auto bar = S (paramBar (k));
            const float v = (float) jlimit (0.0, 1.0, pv.value);
            g.setColour (kScrBg);
            g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
            if (pv.steps >= 2)
            {
                // paramètre à crans (modes, interrupteurs) : une case par position, la position choisie allumée
                const int n = pv.steps;
                const int cur = roundToInt (v * (float) (n - 1));
                const float gap = n <= 24 ? S (4.0f) : 0.0f;
                const float w = (bar.getWidth() - gap * (float) (n - 1)) / (float) n;
                for (int st = 0; st < n; ++st)
                {
                    const auto seg = Rectangle<float> (bar.getX() + (w + gap) * (float) st, bar.getY(), w, bar.getHeight());
                    g.setColour (st == cur ? col : (st < cur && n > 4 ? col.withAlpha (0.35f) : kScrPanel2.brighter (0.15f)));
                    g.fillRoundedRectangle (seg, jmin (seg.getHeight(), seg.getWidth()) * 0.35f);
                }
            }
            else
            {
                g.setColour (col.withAlpha (0.85f));
                g.fillRoundedRectangle (bar.withWidth (jmax (bar.getHeight(), bar.getWidth() * v)), bar.getHeight() * 0.5f);
            }
        }
    }

    // ---- presets du plugin (usine / utilisateur, ceux de la liste de REAPER)
    void openPresets (bool reprobe)
    {
        const int i = paramFxIndex();
        if (i < 0) return;
        presetMode = true;
        if (reprobe || plistGuid != paramGuid)
        {
            MouseCursor::showWaitCursor();
            plist = eng.chainHost().presets (i, true);
            MouseCursor::hideWaitCursor();
            if (plistGuid != paramGuid) userCur = -1;
            plistGuid = paramGuid;
            presetScroll = 0;
        }
        else plist.current = eng.chainHost().presets (i, false).current;
        upPlugin = eng.chain().getReference (i).original;
        upresets = UserFxPresets::load (upPlugin);
        askRepaint (screenPx());
    }

    void loadPluginPreset (int k)
    {
        const int i = paramFxIndex();
        if (i < 0 || ! isPositiveAndBelow (k, plist.names.size())) return;
        auto& h = eng.chainHost();
        h.beginUndo();
        const bool ok = h.loadPreset (i, plist.byName ? -1 : k, plist.names[k]);
        h.endUndo ("preset " + plist.names[k]);
        if (ok) { userCur = -1; plist.current = plist.byName ? k : h.presets (i, false).current; eng.say (FxCat::shortName (eng.chain().getReference (i).name) + " : " + plist.names[k]); }
        else eng.say ("REAPER could not load " + plist.names[k]);
        pollParams (true);
        askRepaint (screenPx());
    }

    int presetTotal() const { return (int) upresets.size() + plist.names.size(); }
    void clickPresetItem (int k)          // k : rang dans la liste affichée (presets utilisateur d'Octopus d'abord)
    {
        const int nu = (int) upresets.size();
        if (k < nu) loadUserPreset (k);
        else loadPluginPreset (k - nu);
    }

    void loadUserPreset (int u)
    {
        const int i = paramFxIndex();
        if (i < 0 || ! isPositiveAndBelow (u, (int) upresets.size())) return;
        auto& h = eng.chainHost();
        const auto& p = upresets[(size_t) u];
        h.beginUndo();
        const bool ok = h.setFxState (i, p.key, p.data);
        h.endUndo ("preset " + p.name);
        if (ok) { userCur = u; plist.current = -1; eng.say (FxCat::shortName (eng.chain().getReference (i).name) + " : " + p.name); }
        else eng.say ("Could not load " + p.name);
        pollParams (true);
        askRepaint (screenPx());
    }

    void startPresetSave (int renameIndex = -1)
    {
        const int i = paramFxIndex();
        if (i < 0) return;
        if (presetEdIndex >= -1 && renameIndex == -1) { commitPresetName(); return; }   // le bouton devient OK pendant la saisie
        presetEdIndex = renameIndex;
        String n;
        if (renameIndex >= 0) n = upresets[(size_t) renameIndex].name;
        else
        {
            n = userCur >= 0 ? upresets[(size_t) userCur].name : eng.chainHost().presetName (i);
            if (n.isEmpty()) n = "My preset " + String ((int) upresets.size() + 1);
        }
        layoutEditors();
        presetNameEd.setText (n, dontSendNotification);
        presetNameEd.setVisible (true);
        presetNameEd.grabKeyboardFocus();
        presetNameEd.selectAll();
        askRepaint (screenPx());
    }

    void cancelPresetName()
    {
        presetEdIndex = -2;
        presetNameEd.setVisible (false);
        askRepaint (screenPx());
    }

    void commitPresetName()
    {
        if (presetEdIndex < -1) return;
        const int mode = presetEdIndex;
        const auto name = presetNameEd.getText().trim();
        cancelPresetName();
        if (name.isEmpty()) return;
        if (mode >= 0)                                             // renommer
        {
            const int other = UserFxPresets::indexOf (upresets, name);
            if (other >= 0 && other != mode) { eng.say ("A user preset is already named " + name); return; }
            upresets[(size_t) mode].name = name;
            UserFxPresets::save (upPlugin, upresets);
            return;
        }
        const int existing = UserFxPresets::indexOf (upresets, name);
        if (existing < 0) { storeUserPreset (-1, name); return; }
        PopupMenu m;                                               // jamais d'écrasement sans confirmation
        m.addSectionHeader ("\"" + name + "\" already exists");
        m.addItem (1, "Replace it with the current settings");
        m.addItem (2, "Cancel");
        SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp, existing, name] (int r) { if (sp != nullptr && r == 1) sp->storeUserPreset (existing, name); });
    }

    void storeUserPreset (int index, const String& name)      // index -1 : nouveau
    {
        const int i = paramFxIndex();
        if (i < 0) return;
        UserFxPreset p { name, {}, {} };
        if (! eng.chainHost().getFxState (i, p.key, p.data)) { eng.say ("REAPER did not give the settings of this plug-in"); return; }
        if (index >= 0) upresets[(size_t) index] = p;
        else
        {
            upresets.push_back (p);
            std::stable_sort (upresets.begin(), upresets.end(), [] (const UserFxPreset& a, const UserFxPreset& b) { return a.name.compareNatural (b.name) < 0; });
        }
        if (! UserFxPresets::save (upPlugin, upresets)) { eng.say ("Could not write " + UserFxPresets::fileFor (upPlugin).getFullPathName()); return; }
        userCur = UserFxPresets::indexOf (upresets, name);
        plist.current = -1;
        eng.say ("User preset saved : " + name);
        askRepaint (screenPx());
    }

    void userPresetMenu (int u)
    {
        if (! isPositiveAndBelow (u, (int) upresets.size())) return;
        PopupMenu m;
        m.addSectionHeader (upresets[(size_t) u].name);
        m.addItem (1, "Load");
        m.addItem (2, "Replace with the current settings");
        m.addItem (3, "Rename");
        m.addSeparator();
        m.addItem (4, "Delete");
        SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp, u] (int r)
        {
            if (sp == nullptr || ! isPositiveAndBelow (u, (int) sp->upresets.size())) return;
            if (r == 1) sp->loadUserPreset (u);
            else if (r == 2) sp->storeUserPreset (u, sp->upresets[(size_t) u].name);
            else if (r == 3) sp->startPresetSave (u);
            else if (r == 4)
            {
                sp->eng.say ("User preset deleted : " + sp->upresets[(size_t) u].name);
                sp->upresets.erase (sp->upresets.begin() + u);
                if (sp->userCur == u) sp->userCur = -1; else if (sp->userCur > u) --sp->userCur;
                UserFxPresets::save (sp->upPlugin, sp->upresets);
                sp->askRepaint (sp->screenPx());
            }
        });
    }

    void stepPluginPreset (int dir)
    {
        const int i = paramFxIndex();
        if (i < 0) return;
        auto& h = eng.chainHost();
        h.beginUndo();
        h.stepPreset (i, dir);
        h.endUndo ("preset");
        plist.current = h.presets (i, false).current;
        userCur = -1;
        eng.say (FxCat::shortName (eng.chain().getReference (i).name) + " : " + h.presetName (i));
        pollParams (true);
        askRepaint (screenPx());
    }

    void drawPresets (Graphics& g, int kind)
    {
        const auto col = FxCat::colour (kind);
        const int i = paramFxIndex();
        g.setFont (font (tfBold, 15.0f));
        g.setColour (kDim);
        g.drawText (plist.count > 0 ? String (plist.current >= 0 ? plist.current + 1 : 0) + " / " + String (plist.count) : String ("-"),
                    S (Rectangle<float> (796.0f, 84.0f, 74.0f, 36.0f)), Justification::centred);
        // preset actif
        g.setFont (font (tfRegular, 15.0f));
        g.setColour (kDim);
        g.drawText ("CURRENT", S (Rectangle<float> (16.0f, 132.0f, 90.0f, 36.0f)), Justification::centredLeft);
        g.setFont (font (tfMedium, 21.0f));
        g.setColour (col.brighter (0.4f));
        auto curName = i >= 0 ? eng.chainHost().presetName (i) : String();
        if (userCur >= 0 && isPositiveAndBelow (userCur, (int) upresets.size())) curName = upresets[(size_t) userCur].name + "  (user)";
        if (presetEdIndex < -1)
            g.drawText (curName.isNotEmpty() ? curName : String ("(modified / no preset)"), S (Rectangle<float> (104.0f, 132.0f, 540.0f, 36.0f)), Justification::centredLeft, true);
        const int nu = (int) upresets.size();
        if (presetTotal() == 0)
        {
            g.setFont (font (tfMedium, 18.0f));
            g.setColour (kDim);
            g.drawText (plist.count == 0 ? "This plug-in has no preset in REAPER" : "Preset names not available for this plug-in : use the arrows",
                        S (Rectangle<float> (16.0f, 260.0f, 1032.0f, 40.0f)), Justification::centred);
            return;
        }
        for (int v = 0; v < kListRows * 2; ++v)
        {
            const int k = presetScroll * kListRows + v;
            if (k >= presetTotal()) break;
            const auto ir = S (itemRect (v));
            const bool mine = k < nu;
            const int rk = k - nu;
            const bool cur = mine ? k == userCur : (! plist.byName && rk == plist.current && userCur < 0);
            g.setColour (cur ? col.withAlpha (0.22f) : (hoverItem == v ? kScrPanel2.brighter (0.12f) : kScrPanel));
            g.fillRoundedRectangle (ir, S (6.0f));
            if (cur) { g.setColour (col); g.drawRoundedRectangle (ir.reduced (S (0.6f)), S (6.0f), S (1.4f)); }
            g.setFont (font (tfRegular, 13.5f));
            g.setColour (kDim);
            g.drawText (mine ? String (CharPointer_UTF8 ("\xe2\x98\x85")) : String (rk + 1), ir.withWidth (S (40.0f)), Justification::centred);
            g.setFont (font (tfMedium, 17.0f));
            g.setColour (kText);
            g.drawText (mine ? upresets[(size_t) k].name : plist.names[rk], ir.withTrimmedLeft (S (44.0f)).withTrimmedRight (S (96.0f)), Justification::centredLeft, true);
            g.setFont (font (tfBold, 12.5f));
            const bool user = mine || plist.user[rk];
            g.setColour (user ? kAmber : kDim);
            g.drawText (user ? "USER" : "FACTORY", ir.withTrimmedRight (S (12.0f)).removeFromRight (S (84.0f)), Justification::centredRight);
        }
        if (plist.names.isEmpty() && plist.count > 0)
        {
            g.setFont (font (tfRegular, 14.0f));
            g.setColour (kDim);
            g.drawText ("REAPER presets of this plug-in : use the arrows", S (Rectangle<float> (16.0f, 598.0f, 600.0f, 20.0f)), Justification::centredLeft);
        }
        const int rows = (presetTotal() + kListRows - 1) / kListRows;
        if (rows > 2)
        {
            g.setFont (font (tfRegular, 14.0f));
            g.setColour (kDim);
            g.drawText (String (presetTotal()) + " presets  -  scroll for more", S (Rectangle<float> (16.0f, 598.0f, 1032.0f, 20.0f)), Justification::centredRight);
        }
    }

    void setParamFromX (int k, float x)
    {
        if (! isPositiveAndBelow (k, params.size())) return;
        const auto bar = paramBar (k);
        double v = jlimit (0.0, 1.0, (double) ((x - bar.getX()) / bar.getWidth()));
        const int i = paramFxIndex();
        if (i < 0) return;
        const auto& pv = params.getReference (k);
        if (pv.steps >= 2)
        {
            // à crans : la case sous la souris, jamais une valeur intermédiaire
            const int n = pv.steps;
            v = (double) jlimit (0, n - 1, (int) std::floor (v * n)) / (double) (n - 1);
            if (std::abs (v - pv.value) < 1.0e-6) return;
        }
        eng.chainHost().setParam (i, pv.index, v);
        if (pollParams (false)) askRepaint (screenPx());
    }

    // banks
    void drawBanks (Graphics& g)
    {
        g.drawText ("BANKS", S (Rectangle<float> (16.0f, 84.0f, 200.0f, 36.0f)), Justification::centredLeft);
        g.setFont (font (tfRegular, 16.0f));
        g.setColour (kDim);
        g.drawText ("16 banks x 4 presets  -  footswitches A-D recall the presets of the bank shown (PRESET mode)",
                    S (Rectangle<float> (130.0f, 84.0f, 860.0f, 36.0f)), Justification::centredLeft, true);
        // points : banks qui contiennent des presets
        for (int b = 0; b < OctopusEngine::kBanks; ++b)
        {
            int n = 0;
            for (int sl = 0; sl < OctopusEngine::kSlots; ++sl) n += eng.preset (b * OctopusEngine::kSlots + sl).saved ? 1 : 0;
            const auto br = bankRect (b);
            for (int d = 0; d < n; ++d)
            {
                g.setColour (kPresetLed.withAlpha (0.9f));
                g.fillEllipse (S (Rectangle<float> (5.0f, 5.0f).withCentre ({ br.getCentreX() - 10.5f + 7.0f * (float) d, br.getBottom() - 9.0f })));
            }
        }
        for (int sl = 0; sl < OctopusEngine::kSlots; ++sl)
        {
            const int idx = eng.bank() * OctopusEngine::kSlots + sl;
            const auto c = S (cardRect (sl));
            const bool isCur = eng.current() == idx;
            g.setColour (isCur ? kPresetLed.withAlpha (0.12f) : kScrPanel);
            g.fillRoundedRectangle (c, S (8.0f));
            g.setColour (isCur ? kPresetLed : kFaint.withAlpha (0.7f));
            g.drawRoundedRectangle (c.reduced (S (0.6f)), S (8.0f), S (isCur ? 1.8f : 1.0f));
            const auto lab = Rectangle<float> (c.getX() + S (10.0f), c.getY() + S (13.0f), S (58.0f), S (40.0f));
            g.setColour (isCur ? kText : kScrPanel2.brighter (0.2f));
            g.fillRoundedRectangle (lab, S (6.0f));
            g.setFont (font (tfBold, 19.0f));
            g.setColour (isCur ? kScrBg : kText);
            g.drawText (OctopusEngine::label (idx), lab.translated (0.0f, S (1.0f)), Justification::centred);
            if (renameSlot != sl)
            {
                const auto& p = eng.preset (idx);
                g.setFont (font (tfMedium, 18.0f));
                g.setColour (p.saved ? kText : kDim);
                g.drawText (eng.displayName (idx), Rectangle<float> (c.getX() + S (80.0f), c.getY(), S (250.0f), c.getHeight() * 0.62f).translated (0.0f, S (4.0f)), Justification::centredLeft, true);
                g.setFont (font (tfRegular, 13.5f));
                g.setColour (kDim);
                const String info = p.saved ? String (p.entries.size()) + (p.entries.size() == 1 ? " effect" : " effects") : String ("free slot");
                g.drawText (info, Rectangle<float> (c.getX() + S (80.0f), c.getY() + c.getHeight() * 0.55f, S (250.0f), c.getHeight() * 0.38f), Justification::centredLeft, true);
            }
        }
    }

    void startRename (int slot)
    {
        renameSlot = slot;
        layoutEditors();
        renameEd.setText (eng.preset (eng.bank() * OctopusEngine::kSlots + slot).name, dontSendNotification);
        renameEd.setVisible (true);
        renameEd.grabKeyboardFocus();
        askRepaint (screenPx());
    }

    void commitRename()
    {
        if (renameSlot < 0) return;
        const int idx = eng.bank() * OctopusEngine::kSlots + renameSlot;
        renameSlot = -1;
        renameEd.setVisible (false);
        eng.rename (idx, renameEd.getText());
        askRepaint (screenPx());
    }

    void exportBanks()
    {
        chooser = std::make_unique<FileChooser> ("Export Octopus banks", File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Octopus banks.octobank"), "*.octobank");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                              [this] (const FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == File()) return;
            f = f.withFileExtension ("octobank");
            MemoryBlock mb;
            {
                MemoryOutputStream mo (mb, false);
                GZIPCompressorOutputStream gz (mo, 6);
                eng.banksTree().writeToStream (gz);
            }
            if (f.replaceWithData (mb.getData(), mb.getSize())) eng.say ("Banks exported : " + f.getFileName());
            else eng.say ("Could not write " + f.getFileName());
        });
    }

    void importBanks()
    {
        chooser = std::make_unique<FileChooser> ("Import Octopus banks (replaces all 64 presets)", File::getSpecialLocation (File::userDocumentsDirectory), "*.octobank");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            MemoryBlock mb;
            f.loadFileAsData (mb);
            MemoryInputStream mi (mb, false);
            GZIPDecompressorInputStream gz (mi);
            if (! eng.importBanks (ValueTree::readFromStream (gz))) eng.say ("Not an Octopus banks file");
        });
    }

    // ------------------------------------------------------------ accordeur : l'écran de CHROMA dans Octopus
    // Quand un bloc CHROMA est allumé, Octopus devient l'accordeur : même moteur (TunerEngine de CHROMA) appliqué au son
    // qui traverse Octopus, même écran (dessin repris de CHROMA, police IBM Plex Mono), réglages lus dans CHROMA
    // (mode, La de référence, accordage). À droite : MUTE / MODE (paramètres de CHROMA) et EXIT (éteint le bloc CHROMA).
    static constexpr float kTW = 828.0f, kTH = 594.5f;              // repère de l'écran de CHROMA
    bool tunerShown() const { return eng.isReady() && proc.chroma().on; }
    // écran de CHROMA un peu réduit, centré dans la partie gauche ; colonne de boutons à droite
    static constexpr float kTunerColX = 800.0f;
    float tunerScale() const { return 0.84f * kSH / kTH; }
    Rectangle<float> tunerRect() const
    {
        const float w = kTW * tunerScale(), h = kTH * tunerScale();
        return { (kTunerColX - w) * 0.5f, (kSH - h) * 0.5f, w, h };
    }
    std::vector<Btn> tunerButtons() const
    {
        const float w = 196.0f, x = kTunerColX + (kSW - kTunerColX - w) * 0.5f;
        const auto& c = proc.chroma();
        std::vector<Btn> v;
        v.push_back ({ { x, 134.0f, w, 58.0f }, bTunerTunings, "TUNINGS" });
        v.push_back ({ { x, 206.0f, w, 58.0f }, bTunerMute, c.muted ? "MUTED" : "MUTE", c.muted });
        v.push_back ({ { x, 278.0f, w, 72.0f }, bTunerMode, "MODE" });
        v.push_back ({ { x, 520.0f, w, 58.0f }, bTunerExit, "EXIT" });
        return v;
    }
    Rectangle<float> tuningBox() const { return { 414.0f - 165.0f, 16.0f, 330.0f, 66.0f }; }   // repère de CHROMA

    // presets d'accordage : ceux de CHROMA (favoris, usine par catégorie, perso)
    void tuningMenu()
    {
        auto& lib = proc.tuningLibrary();
        lib.reload();
        const auto cur = proc.chroma().tuningKey;
        PopupMenu m;
        m.addSectionHeader ("TUNINGS (CHROMA)");
        m.addItem (1, "Use the tuning set in CHROMA", proc.tuningOverridden(), ! proc.tuningOverridden());
        m.addSeparator();
        auto label = [&lib] (int i)
        {
            const auto& t = lib.get (i);
            return String::fromUTF8 (tuna::displayName (t, false).c_str()) + "    " + String (tuna::notesToString (t.notes));
        };
        const auto fav = lib.favouriteIndices();
        if (! fav.empty())
        {
            PopupMenu f;
            for (int i : fav) f.addItem (1000 + i, label (i), true, lib.keyAt (i) == cur);
            m.addSubMenu (CharPointer_UTF8 ("\xe2\x98\x85 FAVORITES"), f);
        }
        StringArray cats;
        std::vector<PopupMenu> menus;
        for (int i = 0; i < lib.size(); ++i)
        {
            const auto& t = lib.get (i);
            const auto cat = String::fromUTF8 (tuna::displayCategory (t.category, false).c_str());
            int k = cats.indexOf (cat);
            if (k < 0) { cats.add (cat); menus.emplace_back(); k = cats.size() - 1; }
            menus[(size_t) k].addItem (1000 + i, label (i), true, lib.keyAt (i) == cur);
        }
        for (int k = 0; k < cats.size(); ++k) m.addSubMenu (cats[k], menus[(size_t) k], true, nullptr, false);
        SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            sp->proc.chromaSelectTuning (r == 1 ? -1 : r - 1000);
            sp->eng.say ("Tuning : " + sp->proc.chroma().tuningName);
        });
    }
    Font mono (float h) const { return Font (FontOptions (tfMono).withHeight (h)); }

    struct TC   // couleurs de l'écran de CHROMA
    {
        static Colour orange()   { return Colour (0xffff7a3a); }
        static Colour amber()    { return Colour (0xffffb23a); }
        static Colour amberDim() { return Colour (0xff3a2808); }
        static Colour green()    { return Colour (0xff5dff8a); }
        static Colour greenDim() { return Colour (0xff12351f); }
        static Colour grey()     { return Colour (0xffc9ced3); }
    };

    static String sign1 (float v)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return (r > 0 ? "+" : (r < 0 ? "-" : "")) + String (std::abs (r), 1);
    }

    static void segment (Graphics& g, Rectangle<float> r, Colour c, bool lit)
    {
        if (lit)
        {
            g.setColour (c.withAlpha (0.10f)); g.fillRoundedRectangle (r.expanded (7, 6), 6.0f);
            g.setColour (c.withAlpha (0.22f)); g.fillRoundedRectangle (r.expanded (3.5f, 3.0f), 4.0f);
            g.setColour (c); g.fillRoundedRectangle (r, 2.5f);
            g.setColour (Colours::white.withAlpha (0.35f)); g.fillRoundedRectangle (r.reduced (r.getWidth() * 0.3f, r.getHeight() * 0.2f), 1.5f);
        }
        else { g.setColour (c); g.fillRoundedRectangle (r, 2.5f); }
    }

    void drawTuner (Graphics& g)
    {
        const auto& c = proc.chroma();
        const auto sn = proc.tunerSnapshot();
        g.setColour (Colour (0xff050706));
        g.fillRect (S (Rectangle<float> (0.0f, 0.0f, kSW, kSH)));
        {
            Graphics::ScopedSaveState ss (g);
            const auto o = S (tunerRect().getPosition());
            g.addTransform (AffineTransform::scale (tunerScale() * L::kG * s).translated (o.x, o.y));
            g.setColour (Colour (0xff050706));
            g.fillRect (0.0f, 0.0f, kTW, kTH);
            const bool autoMono = c.mode == 1 && sn.monoOffTuning && ! sn.anyStringActive;
            tunerHeader (g, sn, c, autoMono);
            if (c.mode == 0 || autoMono) tunerMono (g, sn);
            else if (c.mode == 1)        tunerPoly (g, sn);
            else                         tunerStrobe (g, sn);
        }
        // colonne de droite
        const auto colR = Rectangle<float> (kTunerColX, 0.0f, kSW - kTunerColX, kSH);
        g.setColour (Colour (0xff07090a));
        g.fillRect (S (colR));
        g.setColour (TC::amber().withAlpha (0.25f));
        g.fillRect (S (Rectangle<float> (colR.getX(), 24.0f, 1.5f, kSH - 48.0f)));
        g.setFont (Font (FontOptions (tfMono).withHeight (S (26.0f))));
        g.setColour (TC::orange());
        g.drawText ("TUNER", S (Rectangle<float> (colR.getX(), 40.0f, colR.getWidth(), 34.0f)), Justification::centred);
        g.setFont (Font (FontOptions (tfMono).withHeight (S (13.0f))));
        g.setColour (TC::grey().withAlpha (0.7f));
        g.drawText ("CHROMA", S (Rectangle<float> (colR.getX(), 74.0f, colR.getWidth(), 20.0f)), Justification::centred);
        for (auto& b : tunerButtons())
        {
            const auto rr = S (b.r);
            const bool hov = hoverBtn == b.id;
            const bool red = b.id == bTunerMute && b.active;
            const auto ac = red ? Colour (0xffff2a18) : TC::amber();
            g.setColour (ac.withAlpha (red ? 0.85f : (hov ? 0.16f : 0.06f)));
            g.fillRoundedRectangle (rr, S (8.0f));
            g.setColour (ac.withAlpha (0.65f));
            g.drawRoundedRectangle (rr.reduced (S (1.0f)), S (8.0f), S (1.4f));
            g.setFont (Font (FontOptions (tfMono).withHeight (S (21.0f))));
            g.setColour (red ? Colour (0xff140806) : ac);
            if (b.id == bTunerMode)
            {
                static const char* names[3] = { "MONO", "POLY", "STROBE" };
                g.drawText (b.text, rr.withTrimmedBottom (rr.getHeight() * 0.38f).translated (0.0f, S (4.0f)), Justification::centred);
                g.setFont (Font (FontOptions (tfMono).withHeight (S (13.0f))));
                g.setColour (TC::grey().withAlpha (0.7f));
                g.drawText (names[jlimit (0, 2, c.mode)], rr.withTrimmedTop (rr.getHeight() * 0.58f), Justification::centredTop);
            }
            else g.drawText (b.text, rr, Justification::centred);
        }
    }

    void tunerHeader (Graphics& g, const tuna::Snapshot& sn, const ChromaState& c, bool autoMono)
    {
        static const char* names[3] = { "MONO", "POLY", "STROBE" };
        g.setFont (mono (42.0f));
        g.setColour (TC::orange());
        g.drawText (autoMono ? String ("POLY>MONO") : String (names[jlimit (0, 2, c.mode)]), Rectangle<float> (28, 18, 230, 62), Justification::centredLeft);
        g.drawText ("A=" + String (c.ref), Rectangle<float> (kTW - 28 - 200, 18, 200, 62), Justification::centredRight);
        // nom de l'accordage choisi dans CHROMA (+ étiquette d'instrument : G7, B5...)
        const bool green = c.mode == 1 && ! autoMono && sn.allInTune;
        const auto col = green ? TC::green() : TC::amber();
        const Rectangle<float> r (414.0f - 165.0f, 16.0f, 330.0f, 66.0f);
        g.setColour (col.withAlpha (0.07f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (col.withAlpha (green ? 0.85f : 0.55f));
        g.drawRoundedRectangle (r.reduced (1), 7.0f, 2.0f);
        const int n = (int) c.notes.size();
        const String tag = (c.tuningKey.startsWith ("user|") ? "P" : (c.tuningKey.contains ("|BASSE") ? "B" : "G")) + String (n);
        g.setFont (mono (20.0f));
        g.setColour (col.withAlpha (0.6f));
        g.drawText (tag, Rectangle<float> (r.getX() + 12, r.getY(), 40, r.getHeight()), Justification::centredLeft);
        const String nm = c.tuningName.toUpperCase();
        float fh = 38.0f;
        while (fh > 18.0f && GlyphArrangement::getStringWidth (mono (fh), nm) > r.getWidth() - 70) fh -= 1.0f;
        g.setFont (mono (fh));
        g.setColour (col);
        g.drawText (nm, Rectangle<float> (r.getX() + 48, r.getY(), r.getWidth() - 60, r.getHeight()), Justification::centred, true);
        g.setColour (TC::amber().withAlpha (0.25f));
        g.fillRect (28.0f, 96.0f, kTW - 56.0f, 2.0f);
    }

    void tunerPoly (Graphics& g, const tuna::Snapshot& sn)
    {
        const int n = jmax (1, sn.numStrings);
        const float top = 112.0f, area = 420.0f;
        const float rowH = jmin (68.0f, area / n);
        const float y0 = top + (area - rowH * n) * 0.5f;
        const float x0 = 166.0f, x1 = 800.0f;
        const int segs = 25, mid = 12;
        const float pitch = (x1 - x0) / segs, segW = pitch * 0.62f;
        g.setColour (TC::green().withAlpha (0.13f));
        for (float y = y0; y < y0 + rowH * n; y += 9.0f) g.fillRect (x0 + pitch * (mid + 0.5f) - 0.75f, y, 1.5f, 4.0f);
        for (int row = 0; row < n; ++row)
        {
            const int idx = n - 1 - row;   // corde la plus aiguë en haut
            const auto& st = sn.strings[idx];
            const float cy = y0 + rowH * (row + 0.5f);
            float alpha = 0.32f;
            if (st.measured) alpha = st.active ? 1.0f : jmap (jlimit (0.4f, 6.0f, st.age), 0.4f, 6.0f, 1.0f, 0.45f);
            const bool inTune = st.measured && std::abs (st.cents) <= (float) tuna::TunerEngine::kPolyInTune;
            g.setFont (mono (jmin (46.0f, rowH * 0.72f)));
            g.setColour ((inTune ? TC::green() : TC::amber()).withAlpha (alpha));
            g.drawText (String (tuna::noteName (st.midi)), Rectangle<float> (28, cy - rowH * 0.5f, 132, rowH), Justification::centredLeft);
            const int lit = st.measured ? jlimit (0, segs - 1, mid + (int) std::lround (st.cents / 4.0f)) : -1;
            const float h = rowH * 0.50f;
            for (int k = 0; k < segs; ++k)
            {
                const float hh = (k == mid ? rowH * 0.66f : h);
                Rectangle<float> r (x0 + pitch * k + (pitch - segW) * 0.5f, cy - hh * 0.5f, segW, hh);
                if (k == lit) segment (g, r, (inTune ? TC::green() : TC::amber()).withAlpha (alpha), true);
                else if (lit >= 0 && ((k > mid && k < lit) || (k < mid && k > lit))) segment (g, r, TC::amber().withAlpha (0.30f * alpha), false);
                else segment (g, r, k == mid ? TC::greenDim() : TC::amberDim(), false);
            }
        }
        g.setFont (mono (27.0f));
        g.setColour (TC::grey().withAlpha (0.75f));
        g.drawText ("-50", Rectangle<float> (x0 - 4, 544, 100, 36), Justification::centredLeft);
        g.drawText ("0", Rectangle<float> (x0 + pitch * (mid + 0.5f) - 50, 544, 100, 36), Justification::centred);
        g.drawText ("+50", Rectangle<float> (x1 - 96, 544, 100, 36), Justification::centredRight);
    }

    void tunerMono (Graphics& g, const tuna::Snapshot& sn)
    {
        const bool has = sn.monoMidi >= 0 && (sn.monoActive || sn.monoAge < 2.0f);
        const float alpha = ! has ? 0.3f : (sn.monoActive ? 1.0f : jmap (jlimit (0.25f, 2.0f, sn.monoAge), 0.25f, 2.0f, 1.0f, 0.4f));
        const float c = has ? sn.monoCents : 0.0f;
        const bool inTune = has && sn.monoPrecise && std::abs (c) <= (float) tuna::TunerEngine::kMonoInTune;
        const auto col = (inTune ? TC::green() : TC::amber()).withAlpha (alpha);
        const String pc = has ? String (tuna::pitchClassName (sn.monoMidi)) : String ("--");
        const String letter = pc.substring (0, 1), acc = pc.substring (1);
        const String oct = has ? String (tuna::octaveOf (sn.monoMidi)) : String();
        const auto big = mono (230.0f), small = mono (82.0f);
        auto tw = [] (const Font& f, const String& t) { return GlyphArrangement::getStringWidth (f, t); };
        const float wL = tw (big, has ? letter : pc), wA = tw (small, acc), wO = tw (small, oct);
        const float total = wL + jmax (wA, wO) + 6.0f;
        const float x = 414.0f - total * 0.5f;
        g.setColour (col.withAlpha (alpha * 0.18f));
        g.setFont (big);
        g.drawText (has ? letter : pc, Rectangle<float> (x - 3, 98, wL + 6, 250), Justification::centredLeft);
        g.setColour (col);
        g.drawText (has ? letter : pc, Rectangle<float> (x, 100, wL + 4, 250), Justification::centredLeft);
        g.setFont (small);
        if (acc.isNotEmpty()) g.drawText (acc, Rectangle<float> (x + wL + 6, 128, wA + 6, 90), Justification::centredLeft);
        g.drawText (oct, Rectangle<float> (x + wL + 6, 240, wO + 6, 90), Justification::centredLeft);
        const int segs = 41, mid = 20;
        const float x0 = 64.0f, x1 = kTW - 64.0f, pitch = (x1 - x0) / segs, segW = pitch * 0.6f;
        const float cy = 392.0f;
        for (int k = 0; k < segs; ++k)
        {
            if (k == mid) continue;
            const float hh = (k - mid) % 4 == 0 ? 52.0f : 36.0f;
            segment (g, Rectangle<float> (x0 + pitch * k + (pitch - segW) * 0.5f, cy - hh * 0.5f, segW, hh), TC::amberDim(), false);
        }
        const float markW = segW * 1.1f, markH = 118.0f, curW = segW * 0.8f, curH = 88.0f;
        const float centreX = x0 + pitch * (mid + 0.5f);
        const Rectangle<float> centre (centreX - markW * 0.5f, cy - markH * 0.5f, markW, markH);
        if (inTune) segment (g, centre, TC::green().withAlpha (alpha), true);
        else
        {
            g.setColour (Colours::white.withAlpha (0.10f)); g.fillRoundedRectangle (centre.expanded (4, 3), 4.0f);
            g.setColour (Colours::white.withAlpha (has ? 0.95f : 0.55f)); g.fillRoundedRectangle (centre, 2.5f);
            if (has)
            {
                const float cx = centreX + jlimit (-50.0f, 50.0f, c) / 2.5f * pitch;
                segment (g, Rectangle<float> (cx - curW * 0.5f, cy - curH * 0.5f, curW, curH), TC::amber().withAlpha (alpha), true);
            }
        }
        g.setFont (mono (26.0f));
        g.setColour (TC::grey().withAlpha (0.7f));
        g.drawText ("-50", Rectangle<float> (x0 - 4, 448, 100, 32), Justification::centredLeft);
        g.drawText ("+50", Rectangle<float> (x1 - 96, 448, 100, 32), Justification::centredRight);
        g.setFont (mono (56.0f));
        g.setColour (col);
        g.drawText (has ? ((sn.monoPrecise ? String() : String ("~")) + sign1 (c) + " c") : String ("-- c"), Rectangle<float> (234, 466, 360, 80), Justification::centred);
        g.setFont (mono (28.0f));
        g.setColour (TC::grey().withAlpha (0.75f * jmax (0.4f, alpha)));
        if (has) g.drawText (String (sn.monoHz, sn.monoHz < 100 ? 2 : 1) + " Hz", Rectangle<float> (28, 476, 220, 66), Justification::centredLeft);
        if (has)
            for (int i = 0; i < sn.numStrings; ++i)
                if (sn.strings[i].midi == sn.monoMidi)
                {
                    g.drawText ("STRING " + String (sn.numStrings - i), Rectangle<float> (kTW - 248, 476, 220, 66), Justification::centredRight);
                    break;
                }
    }

    void tunerStrobe (Graphics& g, const tuna::Snapshot& sn)
    {
        const bool has = sn.monoMidi >= 0 && (sn.monoActive || sn.monoAge < 2.0f);
        const float alpha = ! has ? 0.3f : (sn.monoActive ? 1.0f : 0.45f);
        const float c = has ? sn.monoCents : 0.0f;
        const bool inTune = has && sn.monoPrecise && std::abs (c) <= (float) tuna::TunerEngine::kMonoInTune;
        const auto col = (inTune ? TC::green() : TC::amber()).withAlpha (alpha);
        g.setFont (mono (120.0f));
        g.setColour (col);
        g.drawText (has ? String (tuna::noteName (sn.monoMidi)) : String ("--"), Rectangle<float> (40, 104, 420, 150), Justification::centredLeft);
        g.setFont (mono (58.0f));
        g.drawText (has ? sign1 (c) + " c" : String ("-- c"), Rectangle<float> (kTW - 400, 120, 372, 96), Justification::centredRight);
        g.setFont (mono (28.0f));
        g.setColour (TC::grey().withAlpha (0.75f * jmax (0.4f, alpha)));
        if (has) g.drawText (String (sn.monoHz, sn.monoHz < 100 ? 3 : 2) + " Hz", Rectangle<float> (kTW - 400, 206, 372, 50), Justification::centredRight);
        const Rectangle<float> area (44, 284, kTW - 88, 230);
        g.setColour (Colour (0xff0c0f0d));
        g.fillRoundedRectangle (area, 6.0f);
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (area.toNearestInt());
            const float bandH[3] = { 86.0f, 66.0f, 50.0f }, period[3] = { 96.0f, 48.0f, 24.0f };
            float y = area.getY() + 10.0f;
            for (int b = 0; b < 3; ++b)
            {
                const float P = period[b];
                float off = std::fmod (strobePhase * period[0] * (float) (1 << b), P);
                if (off < 0) off += P;
                for (float x = area.getX() - P + off; x < area.getRight(); x += P)
                {
                    g.setColour (col.withAlpha (alpha * (0.78f - 0.14f * b)));
                    g.fillRect (Rectangle<float> (x, y, P * 0.5f, bandH[b]));
                }
                y += bandH[b] + 9.0f;
            }
            g.setColour (Colours::black.withAlpha (0.55f));
            g.fillRect (area.getCentreX() - 2.0f, area.getY(), 4.0f, area.getHeight());
            g.setColour (Colours::white.withAlpha (0.35f));
            g.fillRect (area.getCentreX() - 0.75f, area.getY(), 1.5f, area.getHeight());
        }
        g.setFont (mono (26.0f));
        g.setColour (TC::grey().withAlpha (0.7f));
        g.drawText ("< FLAT", Rectangle<float> (44, 530, 200, 40), Justification::centredLeft);
        g.drawText ("SHARP >", Rectangle<float> (kTW - 244, 530, 200, 40), Justification::centredRight);
    }

    // ------------------------------------------------------------ souris
    void updateHover (Point<float> p)
    {
        const int hb = buttonAt (p);
        int hs = -1, hi = -1, hf = -1, hp = -1;
        if (overlay == Overlay::none) { hs = eng.isReady() ? slotAt (p) : -1; hf = footerAt (p); }
        else if (overlay == Overlay::browser)
        {
            for (int v = 0; v < kListRows * 2; ++v) if (itemRect (v).contains (p)) { hi = v; break; }
            if (hi >= 0 && listScroll * kListRows + hi >= filtered().size()) hi = -1;
        }
        else if (overlay == Overlay::params && presetMode)
        {
            for (int v = 0; v < kListRows * 2; ++v) if (itemRect (v).contains (p)) { hi = v; break; }
            if (hi >= 0 && presetScroll * kListRows + hi >= presetTotal()) hi = -1;
        }
        else if (overlay == Overlay::params)
            for (int k = 0; k < params.size(); ++k) if (paramCell (k).contains (p)) { hp = k; break; }
        if (hb != hoverBtn || hs != hoverSlot || hi != hoverItem || hf != hoverFooter || hp != hoverParam)
        {
            hoverBtn = hb; hoverSlot = hs; hoverItem = hi; hoverFooter = hf; hoverParam = hp;
            askRepaint (screenPx());
        }
    }

    int fsAt (Point<float> unitPos) const
    {
        for (int f = 0; f < 4; ++f)
            if (unitPos.getDistanceFrom ({ L::fsX[f], L::fsY }) <= L::fsRadius) return f;
        return -1;
    }

    void mouseMove (const MouseEvent& e) override { updateHover (toLocal (e.position)); }
    void mouseExit (const MouseEvent&) override { updateHover ({ -100.0f, -100.0f }); }

    void mouseDown (const MouseEvent& e) override
    {
        const auto unit = e.position / s;
        const auto p = toLocal (e.position);
        downSlot = -1; dragging = false; paramDrag = -1;
        if (renameSlot >= 0) commitRename();

        // footswitchs
        const int f = fsAt (unit);
        if (f >= 0)
        {
            if (e.mods.isPopupMenu()) { footswitchMenu (f); return; }
            pressedFs = f;
            eng.press (f);                                 // comme Echoflex : action dès l'appui
            askRepaint (r (Rectangle<float> (L::fsSprite, L::fsSprite).withCentre ({ L::fsX[f], L::fsY })).toNearestInt());
            askRepaint (screenPx());
            return;
        }

        if (! L::glass.contains (unit)) { if (e.mods.isPopupMenu()) pedalMenu(); return; }

        const int b = buttonAt (p);
        if (b != bNone && ! e.mods.isPopupMenu()) { clickButton (b); return; }
        if (tunerShown())
        {
            if (e.mods.isPopupMenu()) { pedalMenu(); return; }
            if (tunerRect().contains (p))
            {
                // comme sur CHROMA : clic sur le nom du mode = mode suivant
                const auto t = (p - tunerRect().getPosition()) / tunerScale();
                if (tuningBox().expanded (6.0f).contains (t)) tuningMenu();          // comme sur CHROMA : clic sur le nom de l'accordage
                else if (t.y < 90.0f && t.x < 260.0f) proc.chromaNextMode();
            }
            return;
        }
        if (headerNameRect().contains (p) && ! e.mods.isPopupMenu() && eng.isReady()) { startHeaderRename(); return; }

        if (overlay == Overlay::browser)
        {
            // clic : choisir / retirer (numérotés dans l'ordre) ; double-clic : ajouter tout de suite
            for (int v = 0; v < kListRows * 2; ++v)
                if (itemRect (v).contains (p))
                {
                    const auto items = filtered();
                    const int i = listScroll * kListRows + v;
                    if (i >= items.size()) return;
                    const auto& fx = items.getReference (i);
                    if (e.getNumberOfClicks() >= 2) { if (pickedIndex (fx) < 0) picked.add (fx); addPicked(); }
                    else togglePick (fx);
                    return;
                }
            return;
        }
        if (overlay == Overlay::params && presetMode)
        {
            for (int v = 0; v < kListRows * 2; ++v)
                if (itemRect (v).contains (p))
                {
                    const int k = presetScroll * kListRows + v;
                    if (k >= presetTotal()) return;
                    if (e.mods.isPopupMenu()) { if (k < (int) upresets.size()) userPresetMenu (k); return; }
                    clickPresetItem (k);
                    return;
                }
            return;
        }
        if (overlay == Overlay::params)
        {
            for (int k = 0; k < params.size(); ++k)
                if (paramCell (k).contains (p)) { paramDrag = k; setParamFromX (k, p.x); return; }
            return;
        }
        if (overlay == Overlay::banks)
        {
            for (int sl = 0; sl < OctopusEngine::kSlots; ++sl)
                if (cardRect (sl).contains (p) && e.getNumberOfClicks() >= 2 && eng.preset (eng.bank() * 4 + sl).saved)
                    eng.load (eng.bank() * 4 + sl);
            return;
        }

        if (! eng.isReady()) return;
        const int ft = footerAt (p);
        if (ft >= 0)
        {
            if (e.mods.isPopupMenu()) footswitchMenu (ft);
            else eng.press (ft);
            return;
        }
        const int slot = slotAt (p);
        const int n = eng.blocks().size();
        if (e.mods.isPopupMenu())
        {
            if (slot >= 0 && slot < n) blockMenu (slot);
            else gridMenu (slot >= 0 ? slot : n);
            return;
        }
        if (slot == n) { openBrowser (n); return; }
        if (slot >= 0 && slot < n) blockDown (slot, p, Time::getMillisecondCounterHiRes());
    }

public:
    // ---- clic / double-clic sur un bloc (fonctions séparées des événements souris pour être testées avec des temps donnés)
    void blockDown (int slot, Point<float> p, double now)
    {
        if (! isPositiveAndBelow (slot, eng.blocks().size())) return;
        {
            const auto guid = eng.chain().getReference (eng.blocks()[slot]).guid;
            // double-clic détecté par Octopus (pas par le système) : même bloc, moins de 650 ms, souris presque immobile
            if (guid == lastClickGuid && now - lastClickMs < 650.0 && p.getDistanceFrom (lastClickPos) < 16.0f)
            {
                pendingToggle = -1;                         // double-clic : paramètres, jamais de bascule
                if (toggledGuid == guid && now - toggledAtMs < 900.0)
                {
                    // double-clic lent : la bascule du premier clic est déjà passée -> on la défait
                    const int b = eng.blockOf (guid);
                    if (b >= 0) eng.toggle (eng.blocks()[b]);
                }
                toggledGuid = {}; lastClickGuid = {};
                downSlot = -1;
                const int b = eng.blockOf (guid);
                if (b >= 0) openParams (eng.blocks()[b]);
                return;
            }
            lastClickGuid = guid; lastClickMs = now; lastClickPos = p;
            downSlot = slot;
            downPos = p;
        }
    }

    void blockUp (int slot, double now)
    {
        if (isPositiveAndBelow (slot, eng.blocks().size()) && eng.chain().getReference (eng.blocks()[slot]).guid == lastClickGuid)
        {
            // clic simple : ON / OFF, appliqué 400 ms après le relâchement si aucun second clic n'est venu
            pendingToggle = eng.blocks()[slot];
            pendingToggleAt = now + 400.0;
        }
    }

    void runPendingToggle (double now)
    {
        if (pendingToggle >= 0 && now >= pendingToggleAt)
        {
            const int i = pendingToggle;
            pendingToggle = -1;
            if (isPositiveAndBelow (i, eng.chain().size()))
            {
                toggledGuid = eng.chain().getReference (i).guid;
                toggledAtMs = now;
                eng.toggle (i);
            }
        }
    }
    bool paramsOpen() const { return overlay == Overlay::params; }
    void toggleMidiView() { clickButton (bParamMidi); }
    void closeAll() { closeOverlay(); }
    Point<float> blockCentre (int slot) const { return slotCentre (slot); }

private:

    void mouseDrag (const MouseEvent& e) override
    {
        const auto p = toLocal (e.position);
        if (overlay == Overlay::params && paramDrag >= 0) { setParamFromX (paramDrag, p.x); return; }
        if (downSlot < 0) return;
        if (! dragging && p.getDistanceFrom (downPos) > 7.0f) dragging = true;
        if (dragging)
        {
            dragPos = p;
            dropPos = dropAt (p);
            // défilement automatique près des bords de la grille
            if (p.y < kGridTop && firstRow > 0) --firstRow;
            else if (p.y > kFooterLine - 10.0f && firstRow < maxFirstRow()) ++firstRow;
            askRepaint (screenPx());
        }
    }

    void mouseUp (const MouseEvent&) override
    {
        if (pressedFs >= 0)
        {
            const int f = pressedFs;
            pressedFs = -1;
            askRepaint (r (Rectangle<float> (L::fsSprite, L::fsSprite).withCentre ({ L::fsX[f], L::fsY })).toNearestInt());
        }
        paramDrag = -1;
        if (downSlot >= 0)
        {
            const int slot = downSlot;
            downSlot = -1;
            if (dragging)
            {
                dragging = false;
                const int from = slot, to = dropPos;
                dropPos = -1;
                if (to >= 0 && to != from && to != from + 1)
                    eng.moveBlock (eng.blocks()[from], to > from ? to - 1 : to);
                askRepaint (screenPx());
                return;
            }
            blockUp (slot, Time::getMillisecondCounterHiRes());
        }
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        const auto p = toLocal (e.position);
        const int dir = w.deltaY < 0 ? 1 : (w.deltaY > 0 ? -1 : 0);
        if (dir == 0 || ! Rectangle<float> (0, 0, kSW, kSH).contains (p)) return;
        if (overlay == Overlay::browser)
        {
            const int rows = (filtered().size() + kListRows - 1) / kListRows;
            listScroll = jlimit (0, jmax (0, rows - 2), listScroll + dir);
        }
        else if (overlay == Overlay::params && presetMode)
        {
            const int rows = (presetTotal() + kListRows - 1) / kListRows;
            presetScroll = jlimit (0, jmax (0, rows - 2), presetScroll + dir);
        }
        else if (overlay == Overlay::params)
        {
            const int pages = jmax (1, (paramCount + 7) / 8);
            paramPage = jlimit (0, pages - 1, paramPage + dir);
            pollParams (true);
        }
        else if (overlay == Overlay::none) firstRow = jlimit (0, maxFirstRow(), firstRow + dir);
        updateHover (p);
        askRepaint (screenPx());
    }

    bool keyPressed (const KeyPress& k) override
    {
        if (k == KeyPress::escapeKey && overlay != Overlay::none) { closeOverlay(); return true; }
        if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && overlay == Overlay::none
            && isPositiveAndBelow (hoverSlot, eng.blocks().size()))
        {
            eng.remove (eng.blocks()[hoverSlot]);
            return true;
        }
        return false;
    }

    void clickButton (int b)
    {
        switch (b)
        {
            case bBankPrev:   eng.bankStep (-1); break;
            case bBankNext:   eng.bankStep (1); break;
            case bModePreset: eng.setMode (OctopusEngine::presetMode); break;
            case bModeStomp:  eng.setMode (OctopusEngine::stompMode); break;
            case bSave:       saveMenu(); break;
            case bBanks:      if (overlay == Overlay::banks) closeOverlay(); else openBanks(); break;
            case bClose:      closeOverlay(); break;
            case bReload:     eng.installedList (true); listScroll = 0; break;
            case bParamOnOff: { const int i = paramFxIndex(); if (i >= 0) eng.toggle (i); break; }
            case bParamWindow:{ const int i = paramFxIndex(); if (const auto* f = paramFx()) eng.openWindow (i, ! f->open); break; }
            case bPagePrev:   if (presetMode) stepPluginPreset (-1); else { paramPage = jmax (0, paramPage - 1); pollParams (true); } break;
            case bPageNext:   if (presetMode) stepPluginPreset (1); else { ++paramPage; pollParams (true); } break;
            case bPresets:    if (presetMode) { presetMode = false; cancelPresetName(); } else openPresets (false); break;
            case bParamMidi:  presetMode = false; cancelPresetName(); midiView = ! midiView; paramPage = 0; pollParams (true); break;
            case bPresetRescan: openPresets (true); break;
            case bAddPicked:  addPicked(); break;
            case bClearPicked: picked.clear(); break;
            case bTunerTunings: tuningMenu(); break;
            case bPresetSave: startPresetSave (-1); break;
            case bTunerMute:  proc.chromaSetMute (! proc.chroma().muted); break;
            case bTunerMode:  proc.chromaNextMode(); break;
            case bTunerExit:  proc.chromaExit(); break;
            case bExport:     exportBanks(); break;
            case bImport:     importBanks(); break;
            case bPc:         eng.setProgramChange (! eng.programChange); break;
            default:
                if (b >= bChip && b < bChip + kNumChips) setChip (b - bChip);
                else if (b >= bFsColour && b < bFsColour + 4) colourMenu (b - bFsColour);
                else if (b >= bBank && b < bBank + OctopusEngine::kBanks) eng.setBank (b - bBank);
                else if (b >= bCard && b < bCard + 100)
                {
                    const int sl = (b - bCard) / 10, a = (b - bCard) % 10;
                    const int idx = eng.bank() * OctopusEngine::kSlots + sl;
                    if (a == cLoad) eng.load (idx);
                    else if (a == cSave) confirmSave (idx);
                    else if (a == cRename) startRename (sl);
                    else if (a == cCopy) { eng.copy (idx); eng.say ("Copied " + OctopusEngine::label (idx)); }
                    else if (a == cPaste) eng.paste (idx);
                    else if (a == cClear) eng.clear (idx);
                }
                break;
        }
        askRepaint (screenPx());
    }

    // enregistrement : jamais d'écrasement silencieux d'un preset existant
    // couleur d'un footswitch (lettre lumineuse et case de l'écran, en mode PRESET)
    void colourMenu (int f)
    {
        static const std::pair<const char*, juce::uint32> pal[] = {
            { "Blue (default)", OctopusEngine::kDefaultFsColour }, { "Cyan", 0xff3ce0e8 }, { "Green", 0xff45e070 }, { "Lime", 0xffb6e83a },
            { "Yellow", 0xffffe23a }, { "Amber", 0xffffb547 }, { "Orange", 0xffff7a2e }, { "Red", 0xffff4040 },
            { "Pink", 0xffff5fb4 }, { "Purple", 0xffb07aff }, { "White", 0xfff2f4f8 } };
        PopupMenu m;
        m.addSectionHeader ("Footswitch " + fsLetter (f) + " colour");
        int id = 1;
        for (auto& c : pal) m.addColouredItem (id++, c.first, Colour (c.second), true, eng.fsColour (f) == c.second);
        SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp, f] (int r)
        {
            if (sp == nullptr || r < 1 || r > (int) std::size (pal)) return;
            sp->eng.setFsColour (f, pal[r - 1].second);
        });
    }

    void confirmSave (int idx)
    {
        if (! eng.preset (idx).saved) { eng.save (idx); return; }
        PopupMenu m;
        m.addSectionHeader ("Replace " + OctopusEngine::label (idx) + " (" + eng.displayName (idx) + ") ?");
        m.addItem (1, "Yes, save the current chain in " + OctopusEngine::label (idx));
        m.addItem (2, "Cancel");
        SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp, idx] (int r) { if (sp != nullptr && r == 1) sp->eng.save (idx); });
    }

    void saveMenu()
    {
        PopupMenu m;
        const int cur = eng.current();
        m.addSectionHeader ("Save the current chain");
        if (cur >= 0) m.addItem (1, "Save " + OctopusEngine::label (cur) + "  " + eng.displayName (cur));
        PopupMenu to;
        for (int b = 0; b < OctopusEngine::kBanks; ++b)
        {
            PopupMenu bm;
            for (int sl = 0; sl < OctopusEngine::kSlots; ++sl)
            {
                const int idx = b * OctopusEngine::kSlots + sl;
                bm.addItem (100 + idx, OctopusEngine::label (idx) + "  " + eng.displayName (idx), true, idx == cur);
            }
            to.addSubMenu ("Bank " + String (b + 1), bm, true, nullptr, b == eng.bank());
        }
        m.addSubMenu ("Save as...", to);
        SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp, cur] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            if (r == 1) sp->eng.save (cur);
            else if (r >= 100) sp->confirmSave (r - 100);
        });
    }

    // ------------------------------------------------------------ menus (clic droit)
    void blockMenu (int slot)
    {
        const int i = eng.blocks()[slot];
        const auto f = eng.chain().getReference (i);
        PopupMenu m, assign, moveM;
        m.addSectionHeader (FxCat::shortName (f.name));
        m.addItem (1, f.enabled ? "Bypass (OFF)" : "Enable (ON)");
        m.addItem (2, "Parameters on screen");
        m.addItem (3, f.open ? "Close plug-in window" : "Open plug-in window");
        m.addSeparator();
        for (int k = 0; k < 4; ++k) assign.addItem (10 + k, "Footswitch " + fsLetter (k), true, eng.isAssigned (k, f.guid));
        m.addSubMenu ("Assign to footswitch (STOMP)", assign);
        m.addItem (4, "Process in parallel with previous", slot > 0, f.parallel > 0);
        m.addSeparator();
        moveM.addItem (20, "Move left", slot > 0);
        moveM.addItem (21, "Move right", slot < eng.blocks().size() - 1);
        moveM.addItem (22, "Move to start", slot > 0);
        moveM.addItem (23, "Move to end", slot < eng.blocks().size() - 1);
        m.addSubMenu ("Move", moveM);
        m.addItem (5, "Insert an effect before");
        m.addItem (6, "Insert an effect after");
        m.addSeparator();
        m.addItem (7, "Remove " + FxCat::shortName (f.name));
        SafePointer<PedalView> sp (this);
        const String guid = f.guid;
        m.showMenuAsync (PopupMenu::Options(), [sp, slot, guid] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            auto& e = sp->eng;
            const int b = e.blockOf (guid);
            if (b < 0) return;
            const int idx = e.blocks()[b];
            const int n = e.blocks().size();
            switch (r)
            {
                case 1: e.toggle (idx); break;
                case 2: sp->openParams (idx); break;
                case 3: e.openWindow (idx, ! e.chain().getReference (idx).open); break;
                case 4: e.setParallel (idx, e.chain().getReference (idx).parallel == 0); break;
                case 5: sp->openBrowser (b); break;
                case 6: sp->openBrowser (b + 1); break;
                case 7: e.remove (idx); break;
                case 20: e.moveBlock (idx, b - 1); break;
                case 21: e.moveBlock (idx, b + 1); break;
                case 22: e.moveBlock (idx, 0); break;
                case 23: e.moveBlock (idx, n - 1); break;
                default: if (r >= 10 && r < 14) e.toggleAssign (r - 10, guid); break;
            }
            juce::ignoreUnused (slot);
        });
    }

    void gridMenu (int blockPos)
    {
        PopupMenu m;
        m.addItem (1, "Add an effect");
        m.addItem (2, "Save the chain...");
        m.addItem (3, "Banks...");
        addSizeMenu (m);
        SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp, blockPos] (int r)
        {
            if (sp == nullptr) return;
            if (r == 1) sp->openBrowser (blockPos);
            else if (r == 2) sp->saveMenu();
            else if (r == 3) sp->openBanks();
            else sp->handleSize (r);
        });
    }

    void addLearnItems (PopupMenu& m, int target)
    {
        const int c = eng.ccOf (target);
        m.addItem (500 + target, "MIDI Learn : " + OctopusEngine::targetName (target) + " (" + (c >= 0 ? "CC " + String (c) : String ("none")) + ")",
                   true, eng.learnTarget == target);
        m.addItem (520 + target, "Clear MIDI", c >= 0);
        m.addItem (540 + target, "MIDI footswitch latching (127 / 0 alternately)", true, eng.latching (target));
    }

    void footswitchMenu (int f)
    {
        PopupMenu m, assign;
        m.addSectionHeader ("Footswitch " + fsLetter (f));
        const int idx = eng.bank() * OctopusEngine::kSlots + f;
        m.addItem (1, "Save the current chain in " + OctopusEngine::label (idx), eng.isReady());
        m.addItem (2, "Load " + OctopusEngine::label (idx), eng.preset (idx).saved && eng.isReady());
        m.addSeparator();
        for (int k = 0; k < eng.blocks().size(); ++k)
        {
            const auto& fx = eng.chain().getReference (eng.blocks()[k]);
            assign.addItem (100 + k, FxCat::shortName (fx.name), true, eng.isAssigned (f, fx.guid));
        }
        if (eng.blocks().isEmpty()) assign.addItem (99, "(no block in the chain)", false);
        m.addSubMenu ("STOMP : blocks switched by " + fsLetter (f), assign);
        m.addItem (3, "STOMP : clear assignment", true);
        m.addSeparator();
        addLearnItems (m, f);
        midiSubmenu (m);
        addSizeMenu (m);
        SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp, f, idx] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            auto& e = sp->eng;
            if (r == 1) sp->confirmSave (idx);
            else if (r == 2) e.load (idx);
            else if (r == 3) e.clearAssign (f);
            else if (r >= 100 && r < 100 + e.blocks().size()) e.toggleAssign (f, e.chain().getReference (e.blocks()[r - 100]).guid);
            else if (! sp->handleMidi (r)) sp->handleSize (r);
        });
    }

    void midiSubmenu (PopupMenu& m)
    {
        PopupMenu midi;
        for (int t = 0; t < OctopusEngine::numTargets; ++t)
        {
            const int c = eng.ccOf (t);
            midi.addItem (500 + t, "Learn : " + OctopusEngine::targetName (t) + " (" + (c >= 0 ? "CC " + String (c) : String ("none")) + ")", true, eng.learnTarget == t);
        }
        midi.addSeparator();
        if (eng.learnTarget >= 0) midi.addItem (600, "Cancel MIDI learn");
        midi.addItem (601, "Program Change 1-64 = presets 1A-16D", true, eng.programChange);
        midi.addItem (602, "Reset MIDI (all unassigned)");
        midi.addSeparator();
        midi.addItem (603, "Parameters fsA-fsD, bankDown, bankUp, mode can also use REAPER's MIDI learn", false);
        m.addSeparator();
        m.addSubMenu ("MIDI", midi);
    }

    bool handleMidi (int r)
    {
        if (r >= 500 && r < 500 + OctopusEngine::numTargets)
        {
            eng.learnTarget = eng.learnTarget == r - 500 ? -1 : r - 500;
            if (eng.learnTarget >= 0) eng.say ("MIDI learn : press the MIDI footswitch for " + OctopusEngine::targetName (eng.learnTarget), 8000.0);
            return true;
        }
        if (r >= 520 && r < 520 + OctopusEngine::numTargets) { eng.clearCc (r - 520); return true; }
        if (r >= 540 && r < 540 + OctopusEngine::numTargets) { eng.setLatching (r - 540, ! eng.latching (r - 540)); return true; }
        if (r == 600) { eng.learnTarget = -1; return true; }
        if (r == 601) { eng.setProgramChange (! eng.programChange); return true; }
        if (r == 602) { eng.resetMidi(); return true; }
        return false;
    }

    void pedalMenu()
    {
        PopupMenu m;
        midiSubmenu (m);
        addSizeMenu (m);
        SafePointer<PedalView> sp (this);
        m.showMenuAsync (PopupMenu::Options(), [sp] (int r) { if (sp != nullptr && ! sp->handleMidi (r)) sp->handleSize (r); });
    }

    void addSizeMenu (PopupMenu& m)
    {
        auto* ed = findParentComponentOfClass<OctopusEditor>();
        const int cur = ed != nullptr ? ed->getScaleIndex() : -1;
        const int def = OctoSettings::defaultSize();
        PopupMenu sub, defM;
        for (int i = 0; i < L::kNumSizes; ++i)
        {
            sub.addItem (910 + i, String (L::kSizeNames[i]) + (i == def ? "  (default)" : ""), true, cur == i);
            defM.addItem (920 + i, L::kSizeNames[i], true, i == def);
        }
        sub.addSeparator();
        sub.addSubMenu ("Default size (new windows)", defM);
        m.addSeparator();
        m.addSubMenu ("Plugin size", sub);
    }

    void handleSize (int r)
    {
        auto* ed = findParentComponentOfClass<OctopusEditor>();
        if (ed == nullptr) return;
        if (r >= 910 && r < 910 + L::kNumSizes) ed->setScaleIndex (r - 910);
        else if (r >= 920 && r < 920 + L::kNumSizes)
        {
            OctoSettings::setDefaultSize (r - 920);       // taille par défaut de toutes les fenêtres d'Octopus
            ed->setScaleIndex (-1);                       // cette fenêtre suit la nouvelle taille par défaut
            eng.say ("Default size : " + String (L::kSizeNames[r - 920]));
        }
    }

    // ------------------------------------------------------------ timer (30 Hz)
    void timerCallback() override
    {
        const double now = Time::getMillisecondCounterHiRes();
        auto* ed = findParentComponentOfClass<OctopusEditor>();
        if (ed != nullptr) ed->checkRenderer();

        if (resizeSettle > 0 && --resizeSettle == 0) rebuildCaches();
        else if (resizeSettle == 0 && now - lastScaleCheckMs > 250.0)
        {
            lastScaleCheckMs = now;
            const float sc = physScale(), ap = (float) Component::getApproximateScaleFactorForComponent (this);
            if (std::abs (sc - dispScale) > 0.01f || std::abs (ap - approxScale) > 0.01f) { dispScale = sc; approxScale = ap; rebuildCaches(); }
        }

        if (repaintAskedMs > 0.0 && isShowing() && now - repaintAskedMs > 1500.0 && lastPaintMs < repaintAskedMs)
        {
            repaintAskedMs = 0.0;
            if (ed != nullptr) ed->resetRenderer();
            repaint();
        }

        // clic simple sur un bloc : bascule ON / OFF une fois le délai du double-clic passé
        runPendingToggle (now);

        // accordeur : l'écran de CHROMA s'anime en continu tant que le bloc CHROMA est allumé
        if (tunerShown())
        {
            if (! drawnTuner) { drawnTuner = true; if (overlay != Overlay::none) closeOverlay(); }
            const auto sn = proc.tunerSnapshot();
            strobePhase += (1.0f / 30.0f) * jlimit (-6.0f, 6.0f, sn.monoCents * 0.25f);
            if (std::abs (strobePhase) > 1.0e4f) strobePhase = 0.0f;
            askRepaint (screenPx());
        }
        else if (drawnTuner) { drawnTuner = false; askRepaint (screenPx()); }

        // écran : redessiné seulement quand l'état affiché change
        if (eng.generation() != drawnGen)
        {
            drawnGen = eng.generation();
            if (overlay == Overlay::params) pollParams (true);
            firstRow = jlimit (0, maxFirstRow(), firstRow);
            askRepaint (screenPx());
        }
        else if (overlay == Overlay::params && ++pollTick % 3 == 0 && paramDrag < 0 && pollParams (false))
            askRepaint (screenPx());

        // LED des footswitchs : fondu (0.45 à l'allumage, 0.28 à l'extinction, comme Echoflex), couleur selon le mode
        for (int f = 0; f < 4; ++f)
        {
            const bool lit = eng.isReady() && eng.fsLit (f);
            Colour col = Colour (eng.fsColour (f));
            if (eng.mode() == OctopusEngine::stompMode) { const int k = eng.fsKind (f); col = k >= 0 ? FxCat::colour (k) : col; }
            if (lit || ledLevel[f] < 0.01f) ledColour[f] = col;                     // pendant l'extinction, la couleur reste
            const float target = lit ? 1.0f : 0.0f;
            const float before = ledLevel[f];
            ledLevel[f] += (target - ledLevel[f]) * (target > ledLevel[f] ? 0.45f : 0.28f);
            if (std::abs (ledLevel[f] - target) < 0.004f) ledLevel[f] = target;
            if (std::abs (ledLevel[f] - before) > 0.002f || ledColour[f] != drawnLedColour[f])
            {
                drawnLedColour[f] = ledColour[f];
                askRepaint (r (glowRect (f)).toNearestInt().expanded (2));
                askRepaint (r (Rectangle<float> (L::fsSprite, L::fsSprite).withCentre ({ L::fsX[f], L::fsY })).toNearestInt());
            }
        }
    }
    Colour drawnLedColour[4];

    friend class OctopusEditor;
};

//==============================================================================
OctopusEditor::OctopusEditor (OctopusProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    view = std::make_unique<PedalView> (proc);
    addAndMakeVisible (*view);
    setResizable (true, true);
    const int minW = L::kMinW;                                                    // jamais plus petit que 50 %
    const int maxW = L::kMaxW;
    setResizeLimits (minW, (int) std::lround ((double) minW * kH / kW), maxW, (int) std::lround ((double) maxW * kH / kW));
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setOpaque (true);
    setScaleIndex (jlimit (-1, L::kNumSizes - 1, proc.uiScaleIdx.load()));
    view->initCaches();
}

OctopusEditor::~OctopusEditor() = default;

//==============================================================================
// Anti-gel v2 (Studio Wiga) :
//  1. moteur de rendu logiciel forcé sur Windows (Direct2D peut figer la fenêtre dans certains hôtes),
//     appliqué à chaque nouvelle fenêtre hôte (peer) et revérifié toutes les secondes, jamais en boucle ;
//  2. aucun calcul lourd dans paint() : les caches d'images (fond, footswitchs, éclairage des LED) sont refaits
//     par le timer seulement, après stabilisation de la taille ou quand l'échelle d'affichage change (au plus toutes les 250 ms) ;
//  3. chien de garde : si un redessin demandé n'est pas arrivé au bout de 1,5 s alors que la fenêtre est
//     visible, le moteur de rendu est réappliqué et toute la fenêtre est redessinée ;
//  4. côté audio : aucun appel à l'hôte ni allocation depuis le thread audio (voir PluginProcessor).
void OctopusEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if ((void*) peer != lastPeer) { lastPeer = (void*) peer; repaint(); }
    lastRendererCheckMs = Time::getMillisecondCounterHiRes();
   #if JUCE_WINDOWS
    if (peer != nullptr)
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw)
            peer->setCurrentRenderingEngine (sw);
    }
   #endif
}

void OctopusEditor::checkRenderer()
{
    if ((void*) getPeer() != lastPeer || Time::getMillisecondCounterHiRes() - lastRendererCheckMs > 1000.0)
        useSoftwareRenderer();
}

void OctopusEditor::resetRenderer()
{
    lastPeer = nullptr;
    useSoftwareRenderer();
    repaint();
}

void OctopusEditor::paint (Graphics& g) { g.fillAll (Colours::black); }
void OctopusEditor::parentHierarchyChanged() { useSoftwareRenderer(); }
void OctopusEditor::visibilityChanged()      { useSoftwareRenderer(); if (isShowing()) repaint(); }

void OctopusEditor::resized()
{
    view->setBounds (getLocalBounds());   // jamais de setSize ici (boucle avec l'hôte à 125 / 150 %)
}

int OctopusEditor::getScaleIndex() const
{
    for (int i = 0; i < L::kNumSizes; ++i) if (std::abs (getWidth() - L::kWidths[i]) <= 3) return i;
    return -1;
}

void OctopusEditor::setScaleIndex (int idx)
{
    idx = jlimit (-1, L::kNumSizes - 1, idx);
    proc.uiScaleIdx = idx;                                  // -1 : suit la taille par défaut choisie dans le menu
    const int w = L::kWidths[idx < 0 ? OctoSettings::defaultSize() : idx];
    setSize (w, (int) std::lround ((double) w * kH / kW));
}

void OctopusEditor::showScreen (int which, int arg)
{
    switch (which)
    {
        case 1: view->openBrowser (arg); break;
        case 2: view->openParams (arg); break;
        case 3: view->openBanks(); break;
        case 4: view->openParams (arg); view->showPresets(); break;
        default: view->closeOverlay(); break;
    }
}

void OctopusEditor::screenSearch (const String& t, int chip) { view->setChip (chip); view->setSearch (t); }
void OctopusEditor::screenPick (int visibleIndex) { view->pickVisible (visibleIndex); }

void OctopusEditor::screenParamsMidi() { view->toggleMidiView(); }

// test du clic / double-clic : temps en ms (appui, relâchement) pour chaque clic, puis attente ; renvoie true si les paramètres sont ouverts
bool OctopusEditor::testBlockClicks (int slot, const Array<double>& downUp, double waitMs)
{
    view->closeAll();
    const auto c = view->blockCentre (slot);
    static double t0 = 1.0e9;
    t0 += 1.0e5;                       // chaque série de clics bien après la précédente
    for (int k = 0; k + 1 < downUp.size(); k += 2)
    {
        view->runPendingToggle (t0 + downUp[k]);
        view->blockDown (slot, c, t0 + downUp[k]);
        if (view->paramsOpen()) break;
        view->blockUp (slot, t0 + downUp[k + 1]);
    }
    view->runPendingToggle (t0 + downUp.getLast() + waitMs);
    return view->paramsOpen();
}
void OctopusEditor::screenHover (Point<float> unitPos) { view->simulateHover (unitPos); }

int OctopusEditor::screenUserPreset (int op, const String& arg)
{
    auto& v = *view;
    switch (op)
    {
        case 0: v.startPresetSave (-1); v.presetNameEd.setText (arg, dontSendNotification); break;
        case 1: if (v.presetEdIndex < -1) v.startPresetSave (-1); v.presetNameEd.setText (arg, dontSendNotification); v.commitPresetName(); break;
        case 2: v.loadUserPreset (arg.getIntValue()); break;
        case 3: v.cancelPresetName(); v.upresets.clear(); v.userCur = -1; UserFxPresets::save (v.upPlugin, v.upresets); break;
        default: break;
    }
    v.askRepaint (v.screenPx());
    return (int) v.upresets.size();
}
void OctopusEditor::screenTuningMenu() { view->tuningMenu(); }
