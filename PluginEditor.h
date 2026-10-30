#pragma once
#include "PluginProcessor.h"

class PedalView;

class OctopusEditor : public juce::AudioProcessorEditor
{
public:
    explicit OctopusEditor (OctopusProcessor&);
    ~OctopusEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;
    int  getScaleIndex() const;          // 0..3 = 50 / 60 / 75 / 100 % (-1 = taille libre)
    void setScaleIndex (int idx);        // -1 = taille par défaut (réglage commun, 60 % au départ)
    OctopusProcessor& getProc() noexcept { return proc; }

    // outil de rendu / tests : ouvre une fenêtre de l'écran (0 = aucune, 1 = ajout d'effet, 2 = paramètres, 3 = banks, 4 = presets du plugin)
    void showScreen (int which, int arg = 0);
    void screenSearch (const juce::String& text, int chip);
    void screenPick (int visibleIndex);
    void screenParamsMidi();
    bool testBlockClicks (int slot, const juce::Array<double>& downUpMs, double waitMs);
    void screenHover (juce::Point<float> screenUnits);
    // presets utilisateur du plugin affiché (0 = saisie du nom, 1 = enregistrer sous ce nom, 2 = charger le n-ième, 3 = tout effacer) ; renvoie leur nombre
    int  screenUserPreset (int op, const juce::String& arg);
    void screenTuningMenu();             // ouvre le menu des accordages de CHROMA

private:
    friend class PedalView;              // son timer surveille le moteur de rendu (anti-gel v2)
    void useSoftwareRenderer();
    void checkRenderer();
    void resetRenderer();
    void* lastPeer = nullptr;
    double lastRendererCheckMs = 0.0;
    OctopusProcessor& proc;
    std::unique_ptr<PedalView> view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctopusEditor)
};
