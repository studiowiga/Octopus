#include "PluginEditor.h"
#include "FxCategory.h"
#include "Layout.h"
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
    constexpr int kRows = 4, kCols = 7;
    constexpr float kGridTop = 88.0f, kRowPitch = 112.0f, kColX0 = 104.0f, kColPitch = 140.0f;
    constexpr float kTile = 70.0f, kTileDy = 40.0f;              // carré du bloc, centre à rowTop + kTileDy
    constexpr float kFooterY = 548.0f, kFooterH = 64.0f, kFooterW = 252.0f;
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

    String fsLetter (int f) { return String::charToString ((juce_wchar) ('A' + f)); }

    // ids des boutons de l'écran
    enum ButtonId
    {
        bNone = 0, bBankPrev, bBankNext, bModePreset, bModeStomp, bSave, bBanks, bClose,
        bParamOnOff, bParamWindow, bPagePrev, bPageNext, bExport, bImport, bPc, bReload,
        bChip = 100,                     // + catégorie du navigateur
        bBank = 200,                     // + numéro de bank
        bCard = 300                      // + slot * 10 + action
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
        tfMedium  = Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize);
        tfBold    = Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
        tfRegular = Typeface::createSystemTypefaceFor (BinaryData::PoppinsRegular_ttf, BinaryData::PoppinsRegular_ttfSize);
        for (auto& l : ledLevel) l = 0.0f;

        styleEditor (search, "Search plug-ins...");
        search.onTextChange = [this] { listScroll = 0; askRepaint (screenPx()); };
        search.onEscapeKey = [this] { closeOverlay(); };
        search.onReturnKey = [this]
        {
            const auto items = filtered();
            if (items.size() > 0) addFromBrowser (items.getReference (0));
        };
        addChildComponent (search);

        styleEditor (renameEd, "Preset name");
        renameEd.onReturnKey = [this] { commitRename(); };
        renameEd.onEscapeKey = [this] { renameSlot = -1; renameEd.setVisible (false); askRepaint (screenPx()); };
        renameEd.onFocusLost = [this] { commitRename(); };
        addChildComponent (renameEd);

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

        // façade éclairée par les LED (calques précalculés, avec alpha, sous les footswitchs)
        for (int f = 0; f < 4; ++f)
            if (ledLevel[f] > 0.003f && glow[f].isValid())
            {
                g.setOpacity (jmin (1.0f, ledLevel[f]));
                g.drawImage (glow[f], r (glowRect (f)));
                g.setOpacity (1.0f);
            }

        for (int f = 0; f < 4; ++f)
        {
            const auto fr = r (Rectangle<float> (L::fsSprite, L::fsSprite).withCentre ({ L::fsX[f], L::fsY }));
            g.drawImage (fsOff.get(), fr);
            if (ledLevel[f] > 0.003f)       // flanc de l'écrou tourné vers la LED
            {
                g.setOpacity (jmin (1.0f, ledLevel[f]) * 0.85f);
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
            drawLetter (g, f, ledPos (f), ledLevel[f], ledColour[f]);
        }

        drawScreen (g);
    }

private:
    OctopusProcessor& proc;
    OctopusEngine& eng;
    ScaledCache bg, fsOff, fsOn;
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
    TextEditor search, renameEd;
    int chip = 0, listScroll = 0, insertPos = 0;
    // paramètres
    String paramGuid;
    int paramPage = 0, paramDrag = -1;
    struct ParamView { int index; String name, text; double value; };
    Array<ParamView> params;
    int paramCount = 0, pollTick = 0;
    String drawnParamSig;
    // banks
    int renameSlot = -1;
    std::unique_ptr<FileChooser> chooser;

    // ------------------------------------------------------------ repères
    Rectangle<float> r (Rectangle<float> d) const { return d * s; }
    Point<float> r (Point<float> d) const { return d * s; }
    // écran : repère local -> pixels de la fenêtre
    Rectangle<float> S (Rectangle<float> local) const { return (local + L::glass.getPosition()) * s; }
    Point<float> S (Point<float> local) const { return (local + L::glass.getPosition()) * s; }
    float S (float v) const { return v * s; }
    Point<float> toLocal (Point<float> px) const { return px / s - L::glass.getPosition(); }
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

    Font font (const Typeface::Ptr& tf, float unitsHigh) const { return Font (FontOptions (tf).withHeight (unitsHigh * s)); }

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
        search.setIndents (roundToInt (10.0f * s), jmax (0, roundToInt ((38.0f * s - h) * 0.5f) - 1));
        if (renameSlot >= 0)
        {
            renameEd.setBounds (S (cardRect (renameSlot).withTrimmedLeft (80.0f).withWidth (250.0f).reduced (0.0f, 14.0f)).toNearestInt());
            renameEd.setFont (font (tfMedium, 18.0f));
            renameEd.setIndents (roundToInt (8.0f * s), jmax (0, roundToInt ((36.0f * s - h) * 0.5f) - 1));
        }
    }

    void hideEditors()
    {
        search.setVisible (false);
        renameSlot = -1;
        renameEd.setVisible (false);
    }

    // ------------------------------------------------------------ caches (timer seulement)
    void rebuildCaches()
    {
        if (getWidth() <= 0) return;
        const float ps = s * dispScale;
        for (auto* c : { &bg, &fsOff, &fsOn }) c->prepare (ps);
        for (int f = 0; f < 4; ++f) buildGlow (f);
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
    // Allumée : la lettre s'illumine dans la couleur du mode (cœur presque blanc, bords saturés, léger halo optique) ;
    // la façade autour reçoit la lumière (calque glow, comme pour les LED).
    void drawLetter (Graphics& g, int f, Point<float> c0, float lvl, Colour col)
    {
        GlyphArrangement ga;
        ga.addLineOfText (font (tfBold, 38.0f), fsLetter (f), 0.0f, 0.0f);
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

        // halo optique autour de la lettre (deux contours larges et transparents)
        for (int k = 3; k >= 1; --k)
        {
            g.setColour (col.withAlpha (0.07f * lvl));
            g.strokePath (p, PathStrokeType ((float) k * 2.6f * s, PathStrokeType::curved, PathStrokeType::rounded));
        }
        // lettre allumée : cœur clair en haut, couleur saturée en bas
        ColourGradient body (col.interpolatedWith (Colours::white, 0.72f).withAlpha (lvl), 0.0f, pb.getY(),
                             col.withMultipliedSaturation (1.1f).withAlpha (lvl), 0.0f, pb.getBottom(), false);
        body.addColour (0.45, col.interpolatedWith (Colours::white, 0.38f).withAlpha (lvl));
        g.setGradientFill (body);
        g.fillPath (p);
        g.setColour (col.withMultipliedBrightness (0.7f).withAlpha (0.5f * lvl));
        g.strokePath (p, PathStrokeType (0.6f * s));
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
        if (p.y < kGridTop - 6.0f || p.y > kFooterY - 4.0f) return -1;
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
        const float xL = 34.0f, xR = kSW - 34.0f;
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
            const float ry = rowTop ((k - 1) / kCols) + kRowPitch - 7.0f;        // retour sous la rangée
            path.lineTo (P (xR, a.y)); path.lineTo (P (xR, ry)); path.lineTo (P (xL, ry)); path.lineTo (P (xL, b.y)); path.lineTo (S (b));
        }
        const auto last = slotCentre (n - 1);
        path.lineTo (P (xR + 4.0f, last.y));
        g.setColour (kLine.withAlpha (0.75f));
        g.strokePath (path, PathStrokeType (2.0f * s, PathStrokeType::mitered, PathStrokeType::rounded));

        g.setFont (font (tfBold, 15.0f));
        g.setColour (kDim);
        if (firstRow == 0)
        {
            const auto c0 = slotCentre (0);
            g.drawText ("IN", S (Rectangle<float> (0.0f, c0.y - 24.0f, 30.0f, 18.0f)), Justification::centred);
            g.fillEllipse (Rectangle<float> (S (7.0f), S (7.0f)).withCentre (P (xL - 6.0f, c0.y)));
        }
        if (slotVisible (n - 1))
        {
            g.drawText ("OUT", S (Rectangle<float> (kSW - 44.0f, last.y - 24.0f, 44.0f, 18.0f)), Justification::centred);
            g.fillEllipse (Rectangle<float> (S (7.0f), S (7.0f)).withCentre (P (xR + 4.0f, last.y)));
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

        // carreau
        g.setColour (kScrBg.withAlpha (alpha));
        g.fillRoundedRectangle (t, rad);
        if (on)
        {
            g.setGradientFill (ColourGradient (col.withAlpha (0.42f * alpha), t.getX(), t.getY(), col.withAlpha (0.16f * alpha), t.getX(), t.getBottom(), false));
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
        FxCat::drawIcon (g, kind, t.reduced (S (13.0f)), (on ? col.brighter (0.35f) : kDim.withAlpha (0.55f)).withAlpha (alpha), S (2.6f));

        // témoin ON (coin haut droit)
        const auto led = Rectangle<float> (S (8.0f), S (8.0f)).withCentre ({ t.getRight() - S (10.0f), t.getY() + S (10.0f) });
        g.setColour (on ? col.brighter (0.6f).withAlpha (alpha) : kFaint.withAlpha (alpha));
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
        g.reduceClipRegion (S (Rectangle<float> (0.0f, kGridTop - 8.0f, kSW, kFooterY - kGridTop + 4.0f)).toNearestInt());

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
            g.drawText (t, S (Rectangle<float> (kSW - 120.0f, kFooterY - 22.0f, 112.0f, 18.0f)), Justification::centredRight);
            if (firstRow > 0) g.drawText (CharPointer_UTF8 ("\xe2\x96\xb2"), S (Rectangle<float> (kSW - 30.0f, kGridTop - 6.0f, 24.0f, 18.0f)), Justification::centred);
            if (firstRow < maxFirstRow()) g.drawText (CharPointer_UTF8 ("\xe2\x96\xbc"), S (Rectangle<float> (kSW - 30.0f, kFooterY - 40.0f, 24.0f, 18.0f)), Justification::centred);
        }
    }

    // ------------------------------------------------------------ boutons
    std::vector<Btn> headerButtons() const
    {
        std::vector<Btn> v;
        v.push_back ({ { 520.0f, 19.0f, 34.0f, 36.0f }, bBankPrev, CharPointer_UTF8 ("\xe2\x97\x80") });
        v.push_back ({ { 660.0f, 19.0f, 34.0f, 36.0f }, bBankNext, CharPointer_UTF8 ("\xe2\x96\xb6") });
        v.push_back ({ { 706.0f, 19.0f, 88.0f, 36.0f }, bModePreset, "PRESET", eng.mode() == OctopusEngine::presetMode });
        v.push_back ({ { 794.0f, 19.0f, 88.0f, 36.0f }, bModeStomp, "STOMP", eng.mode() == OctopusEngine::stompMode });
        v.push_back ({ { 892.0f, 19.0f, 76.0f, 36.0f }, bSave, "SAVE", false, eng.isReady() });
        v.push_back ({ { 976.0f, 19.0f, 74.0f, 36.0f }, bBanks, "BANKS", overlay == Overlay::banks });
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
            v.push_back ({ { 910.0f, 84.0f, 90.0f, 36.0f }, bReload, "RESCAN" });
        }
        else if (overlay == Overlay::params)
        {
            const auto* f = paramFx();
            const bool on = f != nullptr && f->enabled;
            const int pages = jmax (1, (paramCount + 7) / 8);
            v.push_back ({ { 640.0f, 84.0f, 72.0f, 36.0f }, bParamOnOff, on ? "ON" : "OFF", on });
            v.push_back ({ { 720.0f, 84.0f, 108.0f, 36.0f }, bParamWindow, "WINDOW", f != nullptr && f->open });
            v.push_back ({ { 838.0f, 84.0f, 34.0f, 36.0f }, bPagePrev, CharPointer_UTF8 ("\xe2\x97\x80"), false, paramPage > 0 });
            v.push_back ({ { 964.0f, 84.0f, 34.0f, 36.0f }, bPageNext, CharPointer_UTF8 ("\xe2\x96\xb6"), false, paramPage < pages - 1 });
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
            v.push_back ({ { 16.0f, 560.0f, 156.0f, 44.0f }, bExport, "EXPORT..." });
            v.push_back ({ { 180.0f, 560.0f, 156.0f, 44.0f }, bImport, "IMPORT..." });
            v.push_back ({ { 356.0f, 560.0f, 400.0f, 44.0f }, bPc, String ("PROGRAM CHANGE = PRESETS : ") + (eng.programChange ? "ON" : "OFF"), eng.programChange });
        }
        return v;
    }

    int buttonAt (Point<float> p) const
    {
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

        drawHeader (g);
        if (! eng.isReady())
            drawNotReady (g);
        else
        {
            drawGrid (g);
            drawFooter (g);
        }
        if (overlay != Overlay::none) drawOverlay (g);

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
        g.setColour (cur >= 0 ? kText : kScrPanel2);
        g.fillRoundedRectangle (badge, S (8.0f));
        g.setFont (font (tfBold, 32.0f));
        g.setColour (cur >= 0 ? kScrBg : kDim);
        g.drawText (cur >= 0 ? OctopusEngine::label (cur) : String ("--"), badge.translated (0.0f, S (1.5f)), Justification::centred);

        const String name = cur >= 0 ? eng.displayName (cur) : String (eng.isReady() ? "Live chain" : "OCTOPUS");
        const auto nameR = S (Rectangle<float> (132.0f, 8.0f, 376.0f, 36.0f));
        g.setFont (font (tfMedium, 26.0f));
        g.setColour (kText);
        g.drawText (name, nameR, Justification::centredLeft, true);
        if (eng.modified())
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
        g.fillRect (S (Rectangle<float> (0.0f, kFooterY - 6.0f, kSW, 1.0f)));
        const bool presetMode = eng.mode() == OctopusEngine::presetMode;
        for (int f = 0; f < 4; ++f)
        {
            const auto cell = footerCell (f);
            const auto c = S (cell);
            const bool lit = eng.fsLit (f);
            const int kind = eng.fsKind (f);
            const auto col = presetMode ? kPresetLed : (kind >= 0 ? FxCat::colour (kind) : kDim);
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
        return Rectangle<float> (kFooterW, kFooterH).withCentre ({ L::fsX[f] - L::glass.getX(), kFooterY + kFooterH * 0.5f });
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
            g.drawText (where, S (Rectangle<float> (236.0f, 84.0f, 660.0f, 36.0f)), Justification::centredLeft, true);
            drawBrowserList (g);
        }
        else if (overlay == Overlay::params) drawParams (g);
        else if (overlay == Overlay::banks)  drawBanks (g);
        for (auto& b : overlayButtons()) drawButton (g, b, b.id == bClose ? 18.0f : 15.0f);
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
            g.setColour (hov ? kScrPanel2.brighter (0.12f) : kScrPanel);
            g.fillRoundedRectangle (ir, S (6.0f));
            const int kind = FxCat::classify (f.name);
            g.setColour (FxCat::colour (kind));
            g.fillRoundedRectangle (ir.withWidth (S (5.0f)), S (2.5f));
            FxCat::drawIcon (g, kind, Rectangle<float> (S (22.0f), S (22.0f)).withCentre ({ ir.getX() + S (24.0f), ir.getCentreY() }), FxCat::colour (kind).brighter (0.3f), S (1.8f));
            g.setFont (font (tfMedium, 17.0f));
            g.setColour (kText);
            g.drawText (FxCat::shortName (f.name), ir.withTrimmedLeft (S (44.0f)).withTrimmedRight (S (170.0f)), Justification::centredLeft, true);
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
    Rectangle<float> paramBar (int k) const { auto c = paramCell (k); return { c.getX() + 14.0f, c.getY() + 60.0f, c.getWidth() - 28.0f, 18.0f }; }

    // lit les paramètres affichés (au plus 8) ; renvoie true si quelque chose a changé
    bool pollParams (bool force)
    {
        const int i = paramFxIndex();
        auto& h = eng.chainHost();
        if (i < 0) { if (overlay == Overlay::params) closeOverlay(); return false; }
        paramCount = h.numParams (i);
        const int pages = jmax (1, (paramCount + 7) / 8);
        paramPage = jlimit (0, pages - 1, paramPage);
        params.clearQuick();
        String sig;
        for (int k = 0; k < 8; ++k)
        {
            const int p = paramPage * 8 + k;
            if (p >= paramCount) break;
            ParamView pv { p, h.paramName (i, p), h.paramText (i, p), h.getParam (i, p) };
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
        g.drawText (FxCat::shortName (f->name), S (Rectangle<float> (58.0f, 84.0f, 570.0f, 36.0f)), Justification::centredLeft, true);
        const int pages = jmax (1, (paramCount + 7) / 8);
        g.setFont (font (tfBold, 15.0f));
        g.setColour (kDim);
        g.drawText (String (paramPage + 1) + " / " + String (pages), S (Rectangle<float> (872.0f, 84.0f, 92.0f, 36.0f)), Justification::centred);
        if (params.isEmpty())
        {
            g.setFont (font (tfMedium, 18.0f));
            g.drawText ("This plug-in has no parameter", S (Rectangle<float> (16.0f, 230.0f, 1032.0f, 40.0f)), Justification::centred);
            return;
        }
        const auto col = FxCat::colour (kind);
        for (int k = 0; k < params.size(); ++k)
        {
            const auto& pv = params.getReference (k);
            const auto cell = S (paramCell (k));
            g.setColour (hoverParam == k || paramDrag == k ? kScrPanel2.brighter (0.08f) : kScrPanel);
            g.fillRoundedRectangle (cell, S (8.0f));
            g.setFont (font (tfMedium, 17.0f));
            g.setColour (kText);
            g.drawText (pv.name, cell.reduced (S (12.0f), 0.0f).withY (cell.getY() + S (8.0f)).withHeight (S (30.0f)).withTrimmedRight (S (150.0f)), Justification::centredLeft, true);
            g.setFont (font (tfBold, 17.0f));
            g.setColour (col.brighter (0.4f));
            g.drawText (pv.text, cell.reduced (S (12.0f), 0.0f).withY (cell.getY() + S (8.0f)).withHeight (S (30.0f)).removeFromRight (S (180.0f)), Justification::centredRight, true);
            const auto bar = S (paramBar (k));
            g.setColour (kScrBg);
            g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
            g.setColour (col.withAlpha (0.85f));
            g.fillRoundedRectangle (bar.withWidth (jmax (bar.getHeight(), bar.getWidth() * (float) jlimit (0.0, 1.0, pv.value))), bar.getHeight() * 0.5f);
        }
    }

    void setParamFromX (int k, float x)
    {
        if (! isPositiveAndBelow (k, params.size())) return;
        const auto bar = paramBar (k);
        const double v = jlimit (0.0, 1.0, (double) ((x - bar.getX()) / bar.getWidth()));
        const int i = paramFxIndex();
        if (i < 0) return;
        eng.chainHost().setParam (i, params.getReference (k).index, v);
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

        if (overlay == Overlay::browser)
        {
            for (int v = 0; v < kListRows * 2; ++v)
                if (itemRect (v).contains (p))
                {
                    const auto items = filtered();
                    const int i = listScroll * kListRows + v;
                    if (i < items.size()) addFromBrowser (items.getReference (i));
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
        if (slot >= 0 && slot < n)
        {
            downSlot = slot;
            downPos = p;
            if (e.getNumberOfClicks() >= 2)
            {
                pendingToggle = -1;                         // double-clic : paramètres, pas de bascule
                openParams (eng.blocks()[slot]);
                downSlot = -1;
            }
        }
    }

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
            else if (p.y > kFooterY - 10.0f && firstRow < maxFirstRow()) ++firstRow;
            askRepaint (screenPx());
        }
    }

    void mouseUp (const MouseEvent& e) override
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
            if (e.getNumberOfClicks() < 2 && isPositiveAndBelow (slot, eng.blocks().size()))
            {
                // clic simple : ON / OFF, appliqué après le délai du double-clic (un double-clic ouvre les paramètres)
                pendingToggle = eng.blocks()[slot];
                pendingToggleAt = Time::getMillisecondCounterHiRes() + (double) MouseEvent::getDoubleClickTimeout() + 10.0;
            }
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
            case bPagePrev:   paramPage = jmax (0, paramPage - 1); pollParams (true); break;
            case bPageNext:   ++paramPage; pollParams (true); break;
            case bExport:     exportBanks(); break;
            case bImport:     importBanks(); break;
            case bPc:         eng.setProgramChange (! eng.programChange); break;
            default:
                if (b >= bChip && b < bChip + kNumChips) setChip (b - bChip);
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
        PopupMenu sub;
        sub.addItem (904, "60 % (default)", true, ed != nullptr && cur < 0 && std::abs ((float) ed->getWidth() / (float) L::kRefW - L::kDefaultScale) < 0.02f);
        sub.addItem (902, "75 %", true, cur == 1);
        sub.addItem (903, "100 %", true, cur == 2);
        m.addSeparator();
        m.addSubMenu ("Plugin size", sub);
    }

    void handleSize (int r)
    {
        if (r < 902 || r > 904) return;
        if (auto* ed = findParentComponentOfClass<OctopusEditor>()) ed->setScaleIndex (r == 904 ? -1 : r - 901);
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
        if (pendingToggle >= 0 && now >= pendingToggleAt)
        {
            const int i = pendingToggle;
            pendingToggle = -1;
            eng.toggle (i);
        }

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
            Colour col = kPresetLed;
            if (eng.mode() == OctopusEngine::stompMode) { const int k = eng.fsKind (f); col = k >= 0 ? FxCat::colour (k) : kPresetLed; }
            if (lit || ledLevel[f] < 0.01f) ledColour[f] = col;                     // pendant l'extinction, la couleur reste
            if (ledColour[f] != glowColour[f] && resizeSettle == 0) buildGlow (f);
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
    const int minW = (int) std::lround (L::kRefW * L::kDefaultScale);           // jamais plus petit que la taille par défaut
    const int maxW = 1600;
    setResizeLimits (minW, (int) std::lround ((double) minW * kH / kW), maxW, (int) std::lround ((double) maxW * kH / kW));
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setOpaque (true);
    setScaleIndex (jlimit (-1, 2, proc.uiScaleIdx.load()));
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
    const float sc = (float) getWidth() / (float) L::kRefW;
    for (int i = 1; i < 3; ++i) if (std::abs (sc - L::kScales[i]) < 0.02f) return i;
    return -1;
}

void OctopusEditor::setScaleIndex (int idx)
{
    idx = jlimit (-1, 2, idx);
    if (idx == 0) idx = -1;
    proc.uiScaleIdx = idx;
    const float sc = idx < 0 ? L::kDefaultScale : L::kScales[idx];
    const int w = (int) std::lround (L::kRefW * sc);
    setSize (w, (int) std::lround ((double) w * kH / kW));
}

void OctopusEditor::showScreen (int which, int arg)
{
    switch (which)
    {
        case 1: view->openBrowser (arg); break;
        case 2: view->openParams (arg); break;
        case 3: view->openBanks(); break;
        default: view->closeOverlay(); break;
    }
}

void OctopusEditor::screenSearch (const String& t, int chip) { view->setChip (chip); view->setSearch (t); }
void OctopusEditor::screenHover (Point<float> unitPos) { view->simulateHover (unitPos); }
