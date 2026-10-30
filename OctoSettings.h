#pragma once
#include <juce_core/juce_core.h>

// Réglages communs à toutes les instances d'Octopus (fichier de l'utilisateur) : taille par défaut de la fenêtre.
namespace OctoSettings
{
    inline juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Studio Wiga").getChildFile ("Octopus").getChildFile ("settings.xml");
    }
    // indice de taille (0 = 50 %, 1 = 60 %, 2 = 75 %, 3 = 100 %) ; 60 % si rien n'a été choisi
    inline int defaultSize()
    {
        if (auto x = juce::XmlDocument::parse (file()))
            return juce::jlimit (0, 3, x->getIntAttribute ("defaultSize", 1));
        return 1;
    }
    inline void setDefaultSize (int idx)
    {
        juce::XmlElement x ("OctopusSettings");
        if (auto old = juce::XmlDocument::parse (file())) x = *old;
        x.setAttribute ("defaultSize", juce::jlimit (0, 3, idx));
        file().getParentDirectory().createDirectory();
        x.writeTo (file());
    }
}
