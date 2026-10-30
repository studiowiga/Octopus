#pragma once
#include <juce_core/juce_core.h>

// Presets utilisateur des plugins, enregistrés par Octopus (REAPER n'offre pas de fonction pour créer un preset
// utilisateur de sa liste) : un fichier par plugin, retrouvé avec le nom d'origine du plugin (fx_name).
// Chaque preset garde l'état complet du plugin (vst_chunk / clap_chunk de REAPER, ou ses paramètres).
struct UserFxPreset
{
    juce::String name, key, data;
};

class UserFxPresets
{
public:
    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Studio Wiga").getChildFile ("Octopus").getChildFile ("Plugin presets");
    }
    static juce::File fileFor (const juce::String& plugin)
    {
        auto n = juce::File::createLegalFileName (plugin.trim()).replaceCharacters (": ", "__");
        if (n.isEmpty()) n = "plugin";
        return folder().getChildFile (n.substring (0, 120) + ".xml");
    }

    static std::vector<UserFxPreset> load (const juce::String& plugin)
    {
        std::vector<UserFxPreset> v;
        if (auto x = juce::XmlDocument::parse (fileFor (plugin)))
            for (auto* p : x->getChildWithTagNameIterator ("Preset"))
                v.push_back ({ p->getStringAttribute ("name"), p->getStringAttribute ("key"), p->getAllSubText().trim() });
        return v;
    }

    static bool save (const juce::String& plugin, const std::vector<UserFxPreset>& v)
    {
        juce::XmlElement root ("OctopusPluginPresets");
        root.setAttribute ("plugin", plugin);
        for (auto& p : v)
        {
            auto* e = root.createNewChildElement ("Preset");
            e->setAttribute ("name", p.name);
            e->setAttribute ("key", p.key);
            e->addTextElement (p.data);
        }
        const auto f = fileFor (plugin);
        if (v.empty()) return ! f.existsAsFile() || f.deleteFile();
        f.getParentDirectory().createDirectory();
        return root.writeTo (f);
    }

    static int indexOf (const std::vector<UserFxPreset>& v, const juce::String& name)
    {
        for (size_t i = 0; i < v.size(); ++i) if (v[i].name.equalsIgnoreCase (name.trim())) return (int) i;
        return -1;
    }
};
