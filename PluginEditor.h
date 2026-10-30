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
    int  getScaleIndex() const;          // 1, 2 = 75 %, 100 %  (-1 = taille par défaut / libre)
    void setScaleIndex (int idx);        // -1 = taille par défaut (60 %, aussi la taille minimale)
    OctopusProcessor& getProc() noexcept { return proc; }

    // outil de rendu / tests : ouvre une fenêtre de l'écran (0 = aucune, 1 = ajout d'effet, 2 = paramètres, 3 = banks)
    void showScreen (int which, int arg = 0);
    void screenSearch (const juce::String& text, int chip);
    void screenHover (juce::Point<float> screenUnits);

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
