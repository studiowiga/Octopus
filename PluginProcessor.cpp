#include "PluginProcessor.h"
#include "PluginEditor.h"
#if ! OCTOPUS_DEMO_ONLY
 #include "ReaperBridge.h"
#endif

const char* const OctopusProcessor::kTargetIds[OctopusEngine::numTargets] = { "fsA", "fsB", "fsC", "fsD", "bankDown", "bankUp", "mode" };

juce::AudioProcessorValueTreeState::ParameterLayout OctopusProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    // paramètre 0 : jeton de CETTE instance (sert à se retrouver dans la chaîne de REAPER), non automatisable
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kTokenId, 1 }, "Octopus ID",
                                                        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                        juce::AudioParameterFloatAttributes().withAutomatable (false)));
    // commandes : à assigner avec le MIDI learn de REAPER ou à automatiser (comme les footswitchs MIDI d'Octopus)
    for (int t = 0; t < OctopusEngine::numTargets; ++t)
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kTargetIds[t], 1 }, OctopusEngine::targetName (t), false));
    return l;
}

OctopusProcessor::OctopusProcessor (bool demoChain)
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    tokenParam = apvts.getRawParameterValue (kTokenId);
    for (int t = 0; t < OctopusEngine::numTargets; ++t) targetParam[t] = apvts.getRawParameterValue (kTargetIds[t]);
    newToken();

   #if OCTOPUS_DEMO_ONLY
    juce::ignoreUnused (demoChain);
    host = std::make_unique<DemoChainHost>();
   #else
    if (demoChain) host = std::make_unique<DemoChainHost>();
    else
    {
        auto b = std::make_unique<ReaperBridge> ([this] { return (double) tokenParam->load(); });
        bridge = b.get();
        host = std::move (b);
    }
   #endif
    engine = std::make_unique<OctopusEngine> (*host);
    engine->onStateChanged = [this] { storeEngineState(); };
    engine->onTokenClash = [this] { newToken(); };
    storeEngineState();
    startTimerHz (30);
}

OctopusProcessor::~OctopusProcessor()
{
    stopTimer();
}

void OctopusProcessor::newToken()
{
    // valeur exacte en virgule flottante (multiple de 1/65536), loin des extrémités
    auto& r = juce::Random::getSystemRandom();
    const float v = 0.05f + (float) (r.nextInt (58982)) / 65536.0f;
    if (auto* p = apvts.getParameter (kTokenId))
    {
        if (juce::MessageManager::existsAndIsCurrentThread()) p->setValueNotifyingHost (v);
        else p->setValue (v);
    }
}

void OctopusProcessor::setIHostApplication (Steinberg::FUnknown* h)
{
   #if ! OCTOPUS_DEMO_ONLY
    if (bridge != nullptr) bridge->attach (h);
   #else
    juce::ignoreUnused (h);
   #endif
}

bool OctopusProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out;
}

// Le son passe tel quel. Le MIDI est lu (CC appris, Program Change) et laissé dans le tampon pour les effets suivants.
void OctopusProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());

    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        MidiEv ev {};
        if (msg.isController())         ev = { 0, (juce::uint8) msg.getChannel(), (juce::uint8) msg.getControllerNumber(), (juce::uint8) msg.getControllerValue() };
        else if (msg.isProgramChange()) ev = { 1, (juce::uint8) msg.getChannel(), (juce::uint8) msg.getProgramChangeNumber(), 0 };
        else continue;
        const auto w = midiFifo.write (1);                 // sans verrou ni allocation
        if (w.blockSize1 > 0) midiBuf[(size_t) w.startIndex1] = ev;
        else if (w.blockSize2 > 0) midiBuf[(size_t) w.startIndex2] = ev;
    }
}

// thread de messages : MIDI en file, paramètres de commande, image de la chaîne
void OctopusProcessor::timerCallback()
{
    {
        juce::ValueTree pending;
        {
            const juce::ScopedLock sl (stateLock);
            std::swap (pending, pendingEngineState);
        }
        if (pending.isValid()) { engine->fromTree (pending); storeEngineState(); }
    }

    int n = midiFifo.getNumReady();
    while (n-- > 0)
    {
        MidiEv ev {};
        {
            const auto r = midiFifo.read (1);
            if (r.blockSize1 > 0) ev = midiBuf[(size_t) r.startIndex1];
            else if (r.blockSize2 > 0) ev = midiBuf[(size_t) r.startIndex2];
            else break;
        }
        if (ev.type == 0) engine->midiCc (ev.channel, ev.number, ev.value);
        else engine->midiProgram (ev.channel, ev.number);
    }

    for (int t = 0; t < OctopusEngine::numTargets; ++t)
        engine->paramInput (t, targetParam[t]->load (std::memory_order_relaxed));

    engine->tick();
}

void OctopusProcessor::storeEngineState()
{
    auto t = engine->toTree();
    const juce::ScopedLock sl (stateLock);
    engineState = t;
}

juce::AudioProcessorEditor* OctopusProcessor::createEditor()
{
    return new OctopusEditor (*this);
}

// état : paramètres (dont le jeton) + banks / presets / footswitchs / MIDI, compressé
void OctopusProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("OctopusState");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("uiScale", uiScaleIdx.load(), nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    {
        const juce::ScopedLock sl (stateLock);
        if (engineState.isValid()) root.appendChild (engineState.createCopy(), nullptr);
    }
    juce::MemoryOutputStream out (dest, false);
    out.writeInt (0x3154434f);                                   // "OCT1"
    juce::GZIPCompressorOutputStream gz (out, 6);
    root.writeToStream (gz);
}

void OctopusProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size < 8) return;
    juce::MemoryInputStream in (data, (size_t) size, false);
    if (in.readInt() != 0x3154434f) return;
    juce::GZIPDecompressorInputStream gz (in);
    const auto root = juce::ValueTree::readFromStream (gz);
    if (! root.hasType ("OctopusState")) return;
    uiScaleIdx = (int) root.getProperty ("uiScale", -1);
    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid()) apvts.replaceState (params);
    const auto eng = root.getChildWithName ("OCTOPUS");
    if (! eng.isValid()) return;
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        engine->fromTree (eng);
        storeEngineState();
    }
    else
    {
        const juce::ScopedLock sl (stateLock);
        pendingEngineState = eng.createCopy();
        engineState = eng.createCopy();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OctopusProcessor();
}
