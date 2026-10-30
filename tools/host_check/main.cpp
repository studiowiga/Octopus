#include <juce_audio_utils/juce_audio_utils.h>
using namespace juce;
static int failures = 0;
static void check (bool ok, const String& w) { std::printf ("%s %s\n", ok ? "  OK  " : "  FAIL", w.toRawUTF8()); if (! ok) ++failures; }
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<VST3PluginFormat>());
    OwnedArray<PluginDescription> found;
    VST3PluginFormat vst3;
    vst3.findAllTypesForFile (found, argv[1]);
    check (found.size() == 1, "VST3 trouvé : " + (found.isEmpty() ? String() : found[0]->name + " / " + found[0]->manufacturerName + " / " + found[0]->category));
    if (found.isEmpty()) return 1;
    check (found[0]->name == "Octopus" && found[0]->manufacturerName == "Studio Wiga", "nom dans le DAW : Octopus (Studio Wiga)");
    String err;
    auto inst = fm.createPluginInstance (*found[0], 48000.0, 256, err);
    check (inst != nullptr, "instance créée " + err);
    if (! inst) return 1;
    auto& params = inst->getParameters();
    check (params.size() >= 8 && params[0]->getName (64) == "Octopus ID", "paramètre 0 = Octopus ID (" + String (params.size()) + " paramètres)");
    StringArray pn; for (int i = 0; i < jmin (10, params.size()); ++i) pn.add (params[i]->getName (32));
    std::printf ("  paramètres : %s\n", pn.joinIntoString (", ").toRawUTF8());
    check (inst->acceptsMidi() && inst->producesMidi(), "entrée et sortie MIDI");
    inst->prepareToPlay (48000.0, 256);
    AudioBuffer<float> b (2, 256); MidiBuffer m;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) b.setSample (c, i, std::sin ((float) i * 0.05f + (float) c));
    AudioBuffer<float> ref (b);
    m.addEvent (MidiMessage::controllerEvent (1, 80, 127), 10);
    inst->processBlock (b, m);
    float diff = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) diff = jmax (diff, std::abs (b.getSample (c, i) - ref.getSample (c, i)));
    check (diff < 1.0e-7f, "son inchangé (écart max " + String (diff) + ")");
    MemoryBlock st; inst->getStateInformation (st);
    check (st.getSize() > 16, "état sauvé (" + String ((int) st.getSize()) + " octets)");
    inst->setStateInformation (st.getData(), (int) st.getSize());
    std::unique_ptr<AudioProcessorEditor> ed (inst->createEditorIfNeeded());
    check (ed != nullptr && ed->getWidth() == 661 && ed->getHeight() == 495, "fenêtre " + String (ed ? ed->getWidth() : 0) + " x " + String (ed ? ed->getHeight() : 0));
    if (ed)
    {
        ed->setVisible (true);
        for (int i = 0; i < 20; ++i) MessageManager::getInstance()->runDispatchLoopUntil (30);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        File f (String (argv[2]) + "/host_view.png"); f.deleteFile();
        FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
        ed.reset();
    }
    inst->releaseResources();
    inst.reset();
    std::printf ("%s\n", failures == 0 ? "TOUS LES TESTS OK" : "ECHEC(S)");
    return failures;
}
