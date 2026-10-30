#pragma once
#include <juce_graphics/juce_graphics.h>

// Famille d'un effet, devinée d'après son nom (icône et couleur du bloc à l'écran, rubriques du navigateur d'effets).
namespace FxCat
{
    enum Kind { tuner, amp, cab, drive, dynamics, eq, filter, delay, reverb, mod, pitch, special, utility, other, numKinds };

    inline const char* label (int k)
    {
        static const char* n[numKinds] = { "Tuner", "Amp", "Cab / IR", "Drive", "Dynamics", "EQ", "Wah / Filter",
                                            "Delay", "Reverb", "Modulation", "Pitch", "Special", "Utility", "Other" };
        return n[juce::jlimit (0, numKinds - 1, k)];
    }

    inline juce::Colour colour (int k)
    {
        static const juce::uint32 c[numKinds] = {
            0xff33c8e8, // tuner
            0xffd9443b, // amp
            0xffb0703f, // cab
            0xffe8742c, // drive
            0xff4cb85c, // dynamics
            0xff9cc843, // eq
            0xffe6c531, // filter / wah
            0xff1fb5a6, // delay
            0xff3f7fe0, // reverb
            0xff9a5be0, // modulation
            0xffd9479a, // pitch
            0xffc77ef0, // special
            0xff8c96a3, // utility
            0xff6f7c8c  // other
        };
        return juce::Colour (c[juce::jlimit (0, numKinds - 1, k)]);
    }

    // "VST3: Holyverb (Studio Wiga)" -> "Holyverb" ; "JS: Utility/volume" -> "volume"
    inline juce::String shortName (const juce::String& full)
    {
        auto s = full.trim();
        const int colon = s.indexOfChar (':');
        if (colon > 0 && colon < 7 && ! s.substring (0, colon).containsChar (' '))
            s = s.substring (colon + 1).trim();
        if (s.endsWithChar (')'))
        {
            const int open = s.lastIndexOfChar ('(');
            if (open > 0) s = s.substring (0, open).trim();
        }
        return s.isEmpty() ? full : s;
    }

    // "VST3: Holyverb (Studio Wiga)" -> "Studio Wiga"
    inline juce::String vendor (const juce::String& full)
    {
        auto s = full.trim();
        if (! s.endsWithChar (')')) return {};
        const int open = s.lastIndexOfChar ('(');
        return open > 0 ? s.substring (open + 1, s.length() - 1).trim() : juce::String();
    }

    // "VST3: x" -> "VST3"
    inline juce::String typeOf (const juce::String& full)
    {
        const int colon = full.indexOfChar (':');
        return colon > 0 && colon < 7 ? full.substring (0, colon).trim() : juce::String();
    }

    inline int classify (const juce::String& fullName)
    {
        const auto name = shortName (fullName).toLowerCase();
        juce::StringArray words;
        words.addTokens (name, " -_/.,()[]+:&", "");
        words.removeEmptyStrings();

        auto has = [&] (std::initializer_list<const char*> keys)          // sous-chaîne (mots-clés longs)
        {
            for (auto* k : keys) if (name.contains (k)) return true;
            return false;
        };
        auto word = [&] (std::initializer_list<const char*> keys)         // début de mot (mots-clés courts)
        {
            for (auto& w : words)
                for (auto* k : keys) if (w.startsWith (k)) return true;
            return false;
        };

        // l'ordre compte : du plus spécifique au plus général
        if (has ({ "tuner", "chroma", "polytune" }) || word ({ "tune" }))                                     return tuner;
        if (has ({ "cabinet", "impulse", "reacab", "cab sim" }) || word ({ "cab", "ir", "irs" }))            return cab;
        if (has ({ "genome", "neural", "archetype", "ampli", "preamp", "guitar rig", "s-gear", "tonex",
                  "bias fx", "amplitube", "mesa", "marshall", "fortin", "nameless", "helix" })
            || word ({ "amp", "nam", "head", "stack" }))                                                     return amp;
        if (has ({ "ring think", "textur", "freeze", "ebow", "fantomine", "theremin", "granular", "synth",
                  "sitar", "jawari", "freakout", "freqout", "feedback", "infinite" }) || word ({ "arc", "ring" })) return special;
        if (has ({ "whammy", "wharry", "octav", "pitch", "semitone", "harmoni", "capo", "transpos", "fog z", "pog" })
            || word ({ "fog" }))                                                                              return pitch;
        if (has ({ "crycat", "wah", "envelope", "filter", "autowah", "talk" }) || word ({ "cry" }))         return filter;
        if (has ({ "delay", "echo", "tape" }))                                                               return delay;
        if (has ({ "verb", "hall", "plate", "shimmer", "room", "space", "ambien" }))                         return reverb;
        if (has ({ "chorus", "flang", "phase", "phaser", "vibrato", "tremolo", "rotary", "leslie", "small tone",
                  "wack", "pipeline", "vibe", "ensemble", "modul", "doubler" }) || word ({ "trem", "uni" }))  return mod;
        if (has ({ "drive", "distort", "fuzz", "screamer", "doorn", "peachy", "thorn", "saturat", "crush",
                  "klon", "muff", "boost", "tube", "overdrive", "exciter" }) || word ({ "od", "rat", "dist", "sat" })) return drive;
        if (has ({ "compress", "gate", "limit", "expander", "transient", "dynamic", "reacomp", "squeez", "sustain" })
            || word ({ "comp", "ngate" }))                                                                   return dynamics;
        if (has ({ "equaliz", "reaeq", "graphic", "parametric", "tone stack" }) || word ({ "eq" }))          return eq;
        if (has ({ "volume", "utility", "meter", "analy", "scope", "stereo", "width", "routing", "spectr",
                  "gain", "pan", "polarity", "loudness", "mixer", "channel" }))                               return utility;
        return other;
    }

    // icône vectorielle du bloc (dans le carré r)
    inline void drawIcon (juce::Graphics& g, int kind, juce::Rectangle<float> r, juce::Colour c, float stroke)
    {
        using namespace juce;
        g.setColour (c);
        const auto cx = r.getCentreX(), cy = r.getCentreY(), w = r.getWidth(), h = r.getHeight();
        PathStrokeType st (stroke, PathStrokeType::curved, PathStrokeType::rounded);
        Path p;
        switch (kind)
        {
            case tuner:     // aiguille + graduations
            {
                p.addCentredArc (cx, cy + h * 0.25f, w * 0.38f, w * 0.38f, 0.0f, -1.1f, 1.1f, true);
                g.strokePath (p, st);
                Path n; n.startNewSubPath (cx, cy + h * 0.25f); n.lineTo (cx + w * 0.12f, cy - h * 0.12f);
                g.strokePath (n, st);
                g.fillEllipse (cx - stroke * 1.4f, cy + h * 0.25f - stroke * 1.4f, stroke * 2.8f, stroke * 2.8f);
                return;
            }
            case amp:       // tête d'ampli : boîtier + grille + potards
            {
                auto b = r.reduced (w * 0.08f, h * 0.2f);
                g.drawRoundedRectangle (b, stroke * 1.5f, stroke);
                auto grille = b.removeFromBottom (b.getHeight() * 0.55f).reduced (w * 0.06f, h * 0.05f);
                for (int i = 1; i < 4; ++i)
                {
                    const float y = grille.getY() + grille.getHeight() * (float) i / 4.0f;
                    g.drawLine (grille.getX(), y, grille.getRight(), y, stroke * 0.7f);
                }
                for (int i = 0; i < 4; ++i)
                    g.fillEllipse (b.getX() + w * (0.1f + 0.15f * (float) i), b.getCentreY() - stroke, stroke * 2.0f, stroke * 2.0f);
                return;
            }
            case cab:       // haut-parleur
            {
                auto b = r.reduced (w * 0.12f);
                g.drawRoundedRectangle (b, stroke * 1.5f, stroke);
                g.drawEllipse (b.reduced (w * 0.12f), stroke);
                g.fillEllipse (b.withSizeKeepingCentre (w * 0.14f, w * 0.14f));
                return;
            }
            case drive:     // onde écrêtée
            {
                p.startNewSubPath (r.getX() + w * 0.08f, cy);
                p.lineTo (r.getX() + w * 0.2f, cy - h * 0.26f); p.lineTo (r.getX() + w * 0.42f, cy - h * 0.26f);
                p.lineTo (r.getX() + w * 0.58f, cy + h * 0.26f); p.lineTo (r.getX() + w * 0.8f, cy + h * 0.26f);
                p.lineTo (r.getRight() - w * 0.08f, cy);
                g.strokePath (p, st);
                return;
            }
            case dynamics:  // courbe de compression (coude)
            {
                auto b = r.reduced (w * 0.16f);
                g.drawLine (b.getX(), b.getBottom(), b.getRight(), b.getBottom(), stroke * 0.6f);
                g.drawLine (b.getX(), b.getBottom(), b.getX(), b.getY(), stroke * 0.6f);
                p.startNewSubPath (b.getX(), b.getBottom());
                p.lineTo (b.getX() + b.getWidth() * 0.55f, b.getY() + b.getHeight() * 0.45f);
                p.quadraticTo (b.getX() + b.getWidth() * 0.68f, b.getY() + b.getHeight() * 0.32f, b.getRight(), b.getY() + b.getHeight() * 0.22f);
                g.strokePath (p, st);
                return;
            }
            case eq:        // trois curseurs
            {
                const float ys[3] = { 0.35f, 0.62f, 0.45f };
                for (int i = 0; i < 3; ++i)
                {
                    const float x = r.getX() + w * (0.27f + 0.23f * (float) i);
                    g.drawLine (x, r.getY() + h * 0.18f, x, r.getBottom() - h * 0.18f, stroke * 0.7f);
                    g.fillRoundedRectangle (x - w * 0.07f, r.getY() + h * ys[i] - h * 0.05f, w * 0.14f, h * 0.1f, stroke);
                }
                return;
            }
            case filter:    // courbe en cloche (wah)
            {
                p.startNewSubPath (r.getX() + w * 0.08f, cy + h * 0.25f);
                p.cubicTo (cx - w * 0.14f, cy + h * 0.25f, cx - w * 0.1f, cy - h * 0.32f, cx, cy - h * 0.32f);
                p.cubicTo (cx + w * 0.1f, cy - h * 0.32f, cx + w * 0.14f, cy + h * 0.25f, r.getRight() - w * 0.08f, cy + h * 0.25f);
                g.strokePath (p, st);
                return;
            }
            case delay:     // répétitions décroissantes
            {
                for (int i = 0; i < 4; ++i)
                {
                    const float d = w * (0.2f - 0.035f * (float) i);
                    g.setColour (c.withMultipliedAlpha (1.0f - 0.2f * (float) i));
                    g.fillEllipse (r.getX() + w * (0.16f + 0.21f * (float) i) - d * 0.5f + d * 0.5f, cy - d * 0.5f, d, d);
                }
                return;
            }
            case reverb:    // arcs concentriques
            {
                for (int i = 0; i < 3; ++i)
                {
                    Path a; const float rr = w * (0.14f + 0.13f * (float) i);
                    a.addCentredArc (r.getX() + w * 0.24f, cy, rr, rr, 0.0f, 0.55f, 2.6f, true);
                    g.setColour (c.withMultipliedAlpha (1.0f - 0.22f * (float) i));
                    g.strokePath (a, st);
                }
                g.setColour (c);
                g.fillEllipse (r.getX() + w * 0.24f - stroke * 1.3f, cy - stroke * 1.3f, stroke * 2.6f, stroke * 2.6f);
                return;
            }
            case mod:       // sinusoïde
            {
                p.startNewSubPath (r.getX() + w * 0.08f, cy);
                for (int i = 1; i <= 40; ++i)
                {
                    const float t = (float) i / 40.0f;
                    p.lineTo (r.getX() + w * (0.08f + 0.84f * t), cy - h * 0.24f * std::sin (t * MathConstants<float>::twoPi * 1.5f));
                }
                g.strokePath (p, st);
                return;
            }
            case pitch:     // flèche vers le haut + octave
            {
                p.startNewSubPath (cx - w * 0.12f, cy + h * 0.3f); p.lineTo (cx - w * 0.12f, cy - h * 0.28f);
                p.startNewSubPath (cx - w * 0.28f, cy - h * 0.12f); p.lineTo (cx - w * 0.12f, cy - h * 0.3f); p.lineTo (cx + w * 0.04f, cy - h * 0.12f);
                g.strokePath (p, st);
                g.drawEllipse (cx + w * 0.08f, cy + h * 0.06f, w * 0.2f, h * 0.17f, stroke);
                g.drawLine (cx + w * 0.28f, cy + h * 0.14f, cx + w * 0.28f, cy - h * 0.26f, stroke);
                return;
            }
            case special:   // étoile scintillante
            {
                for (int i = 0; i < 4; ++i)
                {
                    const float a = MathConstants<float>::halfPi * (float) i;
                    const float s = (i % 2 == 0) ? 0.34f : 0.34f;
                    p.startNewSubPath (cx, cy);
                    p.lineTo (cx + std::cos (a) * w * s, cy + std::sin (a) * h * s);
                }
                g.strokePath (p, st);
                g.fillEllipse (cx - w * 0.09f, cy - h * 0.09f, w * 0.18f, h * 0.18f);
                g.fillEllipse (cx + w * 0.2f, cy - h * 0.3f, w * 0.08f, h * 0.08f);
                return;
            }
            case utility:   // vumètre
            {
                const float hs[5] = { 0.3f, 0.55f, 0.75f, 0.5f, 0.35f };
                for (int i = 0; i < 5; ++i)
                {
                    const float bh = h * 0.6f * hs[i];
                    g.fillRoundedRectangle (r.getX() + w * (0.17f + 0.14f * (float) i), r.getBottom() - h * 0.2f - bh, w * 0.09f, bh, stroke * 0.5f);
                }
                return;
            }
            default:        // prise (effet inconnu)
            {
                auto b = r.withSizeKeepingCentre (w * 0.42f, h * 0.34f).translated (0, h * 0.04f);
                g.drawRoundedRectangle (b, stroke * 1.5f, stroke);
                g.drawLine (b.getX() + b.getWidth() * 0.3f, b.getY(), b.getX() + b.getWidth() * 0.3f, b.getY() - h * 0.16f, stroke);
                g.drawLine (b.getRight() - b.getWidth() * 0.3f, b.getY(), b.getRight() - b.getWidth() * 0.3f, b.getY() - h * 0.16f, stroke);
                g.drawLine (cx, b.getBottom(), cx, r.getBottom() - h * 0.1f, stroke);
                return;
            }
        }
    }
}
