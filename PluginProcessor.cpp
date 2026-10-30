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
    tunerBuf.assign ((size_t) tunerFifo.getTotalSize(), 0.0f);
    tuner.prepare (sampleRate);
    engine = std::make_unique<OctopusEngine> (*host);
    engine->onStateChanged = [this] { storeEngineState(); };
    engine->onTokenClash = [this] { newToken(); };
    storeEngineState();
    startTimerHz (30);
}

OctopusProcessor::~OctopusProcessor()
{
    stopTimer();
    analysis.stopThread (2000);
}

void OctopusProcessor::prepareToPlay (double sr, int)
{
    const bool running = analysis.isThreadRunning();
    analysis.stopThread (2000);
    sampleRate = sr > 0 ? sr : 48000.0;
    tuner.prepare (sampleRate);
    tuner.setReference ((double) chromaState.ref);
    if (! chromaState.notes.empty()) tuner.setTuning (chromaState.notes);
    tunerFifo.reset();
    if (running || tunerOn.load()) analysis.startThread (juce::Thread::Priority::low);
}

void OctopusProcessor::releaseResources()
{
    analysis.stopThread (2000);
}

void OctopusProcessor::AnalysisThread::run()
{
    std::vector<float> tmp (8192);
    while (! threadShouldExit())
    {
        int ready = owner.tunerFifo.getNumReady();
        if (ready == 0) { wait (5); continue; }
        while (ready > 0 && ! threadShouldExit())
        {
            const int n = juce::jmin (ready, (int) tmp.size());
            const auto r = owner.tunerFifo.read (n);
            if (r.blockSize1 > 0) std::copy_n (owner.tunerBuf.data() + r.startIndex1, r.blockSize1, tmp.data());
            if (r.blockSize2 > 0) std::copy_n (owner.tunerBuf.data() + r.startIndex2, r.blockSize2, tmp.data() + r.blockSize1);
            owner.tuner.push (tmp.data(), n);
            ready -= n;
        }
    }
}

// ------------------------------------------------------------------ CHROMA : Octopus devient l'accordeur
namespace
{
    // texte de l'état d'un plugin dans REAPER (lignes base64 du bloc <VST ...>) -> octets décodés
    std::string decodedState (const juce::String& entry)
    {
        juce::StringArray l; l.addLines (entry);
        juce::String b64; bool in = false;
        for (auto& raw : l)
        {
            const auto t = raw.trim();
            if (! in) { if (t.startsWith ("<VST") || t.startsWith ("<CLAP")) in = true; continue; }
            if (t == ">") break;
            b64 << t;
        }
        juce::MemoryOutputStream mo;
        if (! juce::Base64::convertFromBase64 (mo, b64))
        {
            juce::MemoryOutputStream m2;                         // lignes encodées séparément
            in = false;
            for (auto& raw : l)
            {
                const auto t = raw.trim();
                if (! in) { if (t.startsWith ("<VST") || t.startsWith ("<CLAP")) in = true; continue; }
                if (t == ">") break;
                juce::Base64::convertFromBase64 (m2, t);
            }
            return std::string ((const char*) m2.getData(), m2.getDataSize());
        }
        return std::string ((const char*) mo.getData(), mo.getDataSize());
    }

    juce::String xmlAttr (const std::string& data, const char* name)
    {
        const std::string key = std::string (" ") + name + "=\"";
        const auto p = data.find (key);
        if (p == std::string::npos) return {};
        const auto e = data.find ('"', p + key.size());
        if (e == std::string::npos) return {};
        auto v = juce::String (juce::CharPointer_UTF8 (data.c_str() + p + key.size()), e - p - key.size());
        return v.replace ("&amp;", "&").replace ("&quot;", "\"").replace ("&apos;", "'").replace ("&lt;", "<").replace ("&gt;", ">");
    }
}

void OctopusProcessor::refreshChroma (bool readTuning)
{
    auto& h = *host;
    auto& c = chromaState;
    if (c.index < 0) return;
    if (c.muteParam < 0 || c.modeParam < 0 || c.refParam < 0)
    {
        const int np = juce::jmin (64, h.numParams (c.index));
        for (int p = 0; p < np; ++p)
        {
            const auto n = h.paramName (c.index, p);
            if (n == "Mute") c.muteParam = p;
            else if (n == "Mode") c.modeParam = p;
            else if (n.startsWith ("Reference")) c.refParam = p;
        }
    }
    if (c.muteParam >= 0) c.muted = h.getParam (c.index, c.muteParam) > 0.5;
    if (c.modeParam >= 0) c.mode = juce::jlimit (0, 2, (int) std::lround (h.getParam (c.index, c.modeParam) * 2.0));
    if (c.refParam >= 0)  c.ref = 430 + (int) std::lround (h.getParam (c.index, c.refParam) * 20.0);
    tuner.setReference ((double) c.ref);
    if (readTuning)
    {
        const auto data = decodedState (h.fxChunk (c.index));
        const auto notes = xmlAttr (data, "tuningNotes");
        const auto key = xmlAttr (data, "tuningKey");
        if (notes.isNotEmpty())
        {
            c.notes = tuna::parseNotes (notes.toStdString());
            c.tuningName = xmlAttr (data, "tuningName");
            c.tuningKey = key;
        }
        // accordage changé dans CHROMA lui-même : il redevient la référence
        if (key.isNotEmpty() && key != lastChromaKey)
        {
            if (lastChromaKey.isNotEmpty()) { const juce::ScopedLock sl (stateLock); overrideKey = {}; overrideNotes.clear(); overrideName = {}; }
            lastChromaKey = key;
        }
        if (overrideKey.isNotEmpty() && ! overrideNotes.empty())
        {
            c.notes = overrideNotes; c.tuningName = overrideName; c.tuningKey = overrideKey;
        }
        if (c.notes.empty()) { c.notes = { 40, 45, 50, 55, 59, 64 }; c.tuningName = "E standard"; c.tuningKey = "factory|GUITARE 6|E"; }
        tuner.setTuning (c.notes);
    }
}

void OctopusProcessor::updateChroma()
{
    auto& c = chromaState;
    const int idx = engine->chromaIndex();
    const bool on = idx >= 0;
    const auto guid = on ? engine->chain().getReference (idx).guid : juce::String();
    if (on != c.on || guid != chromaGuid)
    {
        const bool was = c.on;
        if (guid != chromaGuid && guid.isNotEmpty()) lastChromaKey = {};       // autre bloc CHROMA : on relit son accordage
        c = ChromaState();
        c.on = on; c.index = idx; chromaGuid = guid;
        if (on)
        {
            refreshChroma (true);
            if (! was) { tunerFifo.reset(); tunerOn = true; analysis.startThread (juce::Thread::Priority::low); }
        }
        else
        {
            tunerOn = false;
            analysis.stopThread (500);
        }
        chromaTick = 0;
        return;
    }
    if (! on) return;
    c.index = idx;
    ++chromaTick;
    if (chromaTick % 6 == 0) refreshChroma (chromaTick % 60 == 0);   // réglages 5 fois / s, accordage toutes les 2 s
}

void OctopusProcessor::chromaSelectTuning (int i)
{
    if (i < 0)                                        // revenir à l'accordage choisi dans CHROMA
    {
        { const juce::ScopedLock sl (stateLock); overrideKey = {}; overrideNotes.clear(); overrideName = {}; }
        if (chromaState.on) refreshChroma (true);
        return;
    }
    if (! juce::isPositiveAndBelow (i, library.size())) return;
    const auto& t = library.get (i);
    {
        const juce::ScopedLock sl (stateLock);       // lu par getStateInformation
        overrideKey = TuningLibrary::keyOf (t);
        overrideNotes = t.notes;
        overrideName = juce::String::fromUTF8 (tuna::displayName (t, false).c_str());
    }
    chromaState.notes = overrideNotes;
    chromaState.tuningName = overrideName;
    chromaState.tuningKey = overrideKey;
    tuner.setTuning (overrideNotes);
}

void OctopusProcessor::chromaSetMute (bool mute)
{
    if (chromaState.index < 0 || chromaState.muteParam < 0) return;
    host->setParam (chromaState.index, chromaState.muteParam, mute ? 1.0 : 0.0);
    chromaState.muted = mute;
}

void OctopusProcessor::chromaNextMode()
{
    if (chromaState.index < 0 || chromaState.modeParam < 0) return;
    chromaState.mode = (chromaState.mode + 1) % 3;
    host->setParam (chromaState.index, chromaState.modeParam, chromaState.mode / 2.0);
}

void OctopusProcessor::chromaExit()
{
    if (chromaState.index >= 0) engine->setEnabled (chromaState.index, false);
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

    // accordeur : copie mono dans la file (sans verrou ni allocation), seulement si un bloc CHROMA est allumé
    if (tunerOn.load (std::memory_order_relaxed) && getTotalNumInputChannels() > 0)
    {
        const int n = buffer.getNumSamples(), nch = juce::jmin (2, getTotalNumInputChannels());
        const auto w = tunerFifo.write (n);
        auto put = [&] (int start, int count, int offset)
        {
            for (int i = 0; i < count; ++i)
            {
                float v = 0.0f;
                for (int c = 0; c < nch; ++c) v += buffer.getReadPointer (c)[offset + i];
                tunerBuf[(size_t) (start + i)] = v / (float) nch;
            }
        };
        if (w.blockSize1 > 0) put (w.startIndex1, w.blockSize1, 0);
        if (w.blockSize2 > 0) put (w.startIndex2, w.blockSize2, w.blockSize1);
    }

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
    updateChroma();
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
    {
        const juce::ScopedLock sl (stateLock);
        root.setProperty ("tuningKey", overrideKey, nullptr);
        root.setProperty ("tuningName", overrideName, nullptr);
        root.setProperty ("tuningNotes", juce::String (tuna::notesToString (overrideNotes)), nullptr);
    }
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
    {
        const juce::ScopedLock sl (stateLock);
        overrideKey = root["tuningKey"].toString();
        overrideName = root["tuningName"].toString();
        overrideNotes = tuna::parseNotes (root["tuningNotes"].toString().toStdString());
        if (overrideNotes.empty()) overrideKey = {};
    }
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
