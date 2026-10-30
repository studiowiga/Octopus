#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "OctopusEngine.h"

class ReaperBridge;

// OCTOPUS (Studio Wiga) : contrôleur de la chaîne d'effets de la piste REAPER qui l'héberge.
// Le son traverse Octopus sans modification ; tout le travail se fait sur le thread de messages (timer à 30 Hz),
// via l'API de REAPER, même fenêtre fermée.
class OctopusProcessor : public juce::AudioProcessor,
                         public juce::VST3ClientExtensions,
                         private juce::Timer
{
public:
    explicit OctopusProcessor (bool demoChain = false);
    ~OctopusProcessor() override;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Octopus"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }      // le MIDI continue vers les effets suivants
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // VST3 : REAPER se fait reconnaître ici (IReaperHostApplication)
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    void setIHostApplication (Steinberg::FUnknown*) override;

    OctopusEngine& getEngine() noexcept { return *engine; }
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> uiScaleIdx { -1 };     // -1 = taille par défaut (60 %)

    static constexpr const char* kTokenId = "octoid";
    static const char* const kTargetIds[OctopusEngine::numTargets];

private:
    std::unique_ptr<ChainHost> host;
    ReaperBridge* bridge = nullptr;         // = host quand on tourne pour de vrai
    std::unique_ptr<OctopusEngine> engine;
    std::atomic<float>* tokenParam = nullptr;
    std::atomic<float>* targetParam[OctopusEngine::numTargets] {};

    // MIDI reçu par le thread audio -> file sans verrou -> thread de messages (jamais d'appel à l'hôte en audio)
    struct MidiEv { juce::uint8 type, channel, number, value; };     // type 0 = CC, 1 = Program Change
    juce::AbstractFifo midiFifo { 512 };
    std::array<MidiEv, 512> midiBuf {};

    // état sauvé avec le projet : copie tenue à jour sur le thread de messages, lue par getStateInformation
    juce::CriticalSection stateLock;        // jamais pris par le thread audio
    juce::ValueTree engineState;
    juce::ValueTree pendingEngineState;     // reçu par setStateInformation hors du thread de messages

    void timerCallback() override;
    void newToken();
    void storeEngineState();

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctopusProcessor)
};
