#include "ReaperBridge.h"

#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <pluginterfaces/base/funknown.h>       // SDK VST3 fourni avec JUCE
#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

// ------------------------------------------------------------------ interface REAPER (SDK officiel : reaper_vst3_interfaces.h)
// Déclarée ici à l'identique (même ordre des fonctions virtuelles, même identifiant) pour ne dépendre que de funknown.h.
// IMPORTANT : jamais dans un espace de noms anonyme. Aucune classe de ce fichier n'en dérive (l'objet vient de REAPER) :
// déclarée « interne », le compilateur pourrait en conclure que ses fonctions ne sont jamais appelées et remplacer les
// appels par un arrêt (« pure virtual method called »), ce que l'API simulée de tools/reaper_sim a montré avec GCC.
namespace OctopusReaperSdk
{
    class IReaperHostApplication : public Steinberg::FUnknown    // disponible depuis IHostApplication (REAPER 5.02+)
    {
    public:
        virtual void* PLUGIN_API getReaperApi (Steinberg::CStringA funcname) = 0;
        virtual void* PLUGIN_API getReaperParent (Steinberg::uint32 w) = 0;   // piste(=1), prise(=2), projet(=3), fxdsp(=4), trackchan(=5)
        virtual void* PLUGIN_API reaperExtended (Steinberg::uint32 call, void* p1, void* p2, void* p3) = 0;
    };
}

namespace
{
    using OctopusReaperSdk::IReaperHostApplication;

    // DECLARE_CLASS_IID (IReaperHostApplication, 0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C)
    const Steinberg::TUID kReaperHostIID = INLINE_UID (0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);

    IReaperHostApplication* rh (void* p) { return static_cast<IReaperHostApplication*> (p); }

    constexpr int kRecFlag = 0x1000000;
    constexpr int kUndoStateFx = 2;            // UNDO_STATE_FX (reaper_plugin.h)
    constexpr int kUndoStateAll = -1;          // UNDO_STATE_ALL

    juce::String utf8 (const char* s) { return s != nullptr ? juce::String (juce::CharPointer_UTF8 (s)) : juce::String(); }
}

// signatures copiées de reaper_plugin_functions.h ; MediaTrack*, ReaProject* et GUID* sont passés comme void*
// (pointeurs opaques : même représentation)
struct ReaperBridge::Api
{
    int   (*TrackFX_GetCount) (void* track) = nullptr;
    int   (*TrackFX_GetRecCount) (void* track) = nullptr;
    bool  (*TrackFX_GetFXName) (void* track, int fx, char* buf, int sz) = nullptr;
    bool  (*TrackFX_GetNamedConfigParm) (void* track, int fx, const char* parm, char* buf, int sz) = nullptr;
    bool  (*TrackFX_SetNamedConfigParm) (void* track, int fx, const char* parm, const char* value) = nullptr;
    bool  (*TrackFX_GetEnabled) (void* track, int fx) = nullptr;
    void  (*TrackFX_SetEnabled) (void* track, int fx, bool enabled) = nullptr;
    bool  (*TrackFX_GetOffline) (void* track, int fx) = nullptr;
    void  (*TrackFX_SetOffline) (void* track, int fx, bool offline) = nullptr;
    bool  (*TrackFX_GetOpen) (void* track, int fx) = nullptr;
    void* (*TrackFX_GetFXGUID) (void* track, int fx) = nullptr;
    int   (*TrackFX_GetNumParams) (void* track, int fx) = nullptr;
    bool  (*TrackFX_GetParamName) (void* track, int fx, int param, char* buf, int sz) = nullptr;
    bool  (*TrackFX_GetFormattedParamValue) (void* track, int fx, int param, char* buf, int sz) = nullptr;
    double (*TrackFX_GetParamNormalized) (void* track, int fx, int param) = nullptr;
    bool  (*TrackFX_SetParamNormalized) (void* track, int fx, int param, double value) = nullptr;
    int   (*TrackFX_AddByName) (void* track, const char* name, bool recFX, int instantiate) = nullptr;
    bool  (*TrackFX_Delete) (void* track, int fx) = nullptr;
    void  (*TrackFX_CopyToTrack) (void* src, int srcFx, void* dest, int destFx, bool isMove) = nullptr;
    void  (*TrackFX_Show) (void* track, int index, int showFlag) = nullptr;
    void  (*guidToString) (const void* guid, char* destNeed64) = nullptr;
    bool  (*GetTrackName) (void* track, char* buf, int sz) = nullptr;
    bool  (*ValidatePtr2) (void* proj, void* ptr, const char* type) = nullptr;
    void  (*Undo_BeginBlock2) (void* proj) = nullptr;
    void  (*Undo_EndBlock2) (void* proj, const char* desc, int flags) = nullptr;
    void  (*PreventUIRefresh) (int count) = nullptr;
    bool  (*GetTrackStateChunk) (void* track, char* buf, int sz, bool isUndo) = nullptr;
    bool  (*SetTrackStateChunk) (void* track, const char* str, bool isUndo) = nullptr;
    void  (*InsertTrackInProject) (void* proj, int idx, int flags) = nullptr;     // REAPER 7
    void  (*InsertTrackAtIndex) (int idx, bool wantDefaults) = nullptr;            // plus ancien (projet actif)
    void  (*DeleteTrack) (void* track) = nullptr;
    void* (*GetTrack) (void* proj, int idx) = nullptr;
    int   (*CountTracks) (void* proj) = nullptr;
    int   (*TrackFX_GetPresetIndex) (void* track, int fx, int* numberOfPresetsOut) = nullptr;
    bool  (*TrackFX_GetPreset) (void* track, int fx, char* buf, int sz) = nullptr;
    bool  (*TrackFX_SetPresetByIndex) (void* track, int fx, int idx) = nullptr;
    bool  (*TrackFX_SetPreset) (void* track, int fx, const char* name) = nullptr;
    bool  (*TrackFX_NavigatePresets) (void* track, int fx, int move) = nullptr;
    void  (*TrackFX_GetUserPresetFilename) (void* track, int fx, char* buf, int sz) = nullptr;
    bool  (*EnumInstalledFX) (int index, const char** nameOut, const char** identOut) = nullptr;
    const char* (*GetAppVersion)() = nullptr;

    bool core() const
    {
        return TrackFX_GetCount != nullptr && TrackFX_GetFXName != nullptr && TrackFX_GetEnabled != nullptr
            && TrackFX_SetEnabled != nullptr && TrackFX_AddByName != nullptr && TrackFX_Delete != nullptr
            && TrackFX_CopyToTrack != nullptr && TrackFX_Show != nullptr && TrackFX_GetParamNormalized != nullptr
            && TrackFX_GetFXGUID != nullptr && guidToString != nullptr;
    }
    bool presets() const
    {
        return GetTrackStateChunk != nullptr && SetTrackStateChunk != nullptr
            && (InsertTrackInProject != nullptr || InsertTrackAtIndex != nullptr)
            && DeleteTrack != nullptr && GetTrack != nullptr && CountTracks != nullptr;
    }
};

ReaperBridge::ReaperBridge (std::function<double()> token) : api (std::make_unique<Api>()), tokenFn (std::move (token)) {}

ReaperBridge::~ReaperBridge()
{
    if (reaper != nullptr) rh (reaper)->release();
}

void ReaperBridge::attach (Steinberg::FUnknown* host)
{
    if (host == nullptr) return;
    void* obj = nullptr;
    if (host->queryInterface (kReaperHostIID, &obj) != Steinberg::kResultOk || obj == nullptr)
        return;                                // pas REAPER : Octopus reste inactif
    if (reaper != nullptr) rh (reaper)->release();
    reaper = obj;                              // queryInterface a déjà ajouté une référence
    resolve();
}

template <typename Fn>
static void getApi (void* host, Fn& fn, const char* name)
{
    void* p = rh (host)->getReaperApi (name);
    fn = reinterpret_cast<Fn> (p);
}

void ReaperBridge::resolve()
{
    auto& a = *api;
    getApi (reaper, a.TrackFX_GetCount, "TrackFX_GetCount");
    getApi (reaper, a.TrackFX_GetRecCount, "TrackFX_GetRecCount");
    getApi (reaper, a.TrackFX_GetFXName, "TrackFX_GetFXName");
    getApi (reaper, a.TrackFX_GetNamedConfigParm, "TrackFX_GetNamedConfigParm");
    getApi (reaper, a.TrackFX_SetNamedConfigParm, "TrackFX_SetNamedConfigParm");
    getApi (reaper, a.TrackFX_GetEnabled, "TrackFX_GetEnabled");
    getApi (reaper, a.TrackFX_SetEnabled, "TrackFX_SetEnabled");
    getApi (reaper, a.TrackFX_GetOffline, "TrackFX_GetOffline");
    getApi (reaper, a.TrackFX_SetOffline, "TrackFX_SetOffline");
    getApi (reaper, a.TrackFX_GetOpen, "TrackFX_GetOpen");
    getApi (reaper, a.TrackFX_GetFXGUID, "TrackFX_GetFXGUID");
    getApi (reaper, a.TrackFX_GetNumParams, "TrackFX_GetNumParams");
    getApi (reaper, a.TrackFX_GetParamName, "TrackFX_GetParamName");
    getApi (reaper, a.TrackFX_GetFormattedParamValue, "TrackFX_GetFormattedParamValue");
    getApi (reaper, a.TrackFX_GetParamNormalized, "TrackFX_GetParamNormalized");
    getApi (reaper, a.TrackFX_SetParamNormalized, "TrackFX_SetParamNormalized");
    getApi (reaper, a.TrackFX_AddByName, "TrackFX_AddByName");
    getApi (reaper, a.TrackFX_Delete, "TrackFX_Delete");
    getApi (reaper, a.TrackFX_CopyToTrack, "TrackFX_CopyToTrack");
    getApi (reaper, a.TrackFX_Show, "TrackFX_Show");
    getApi (reaper, a.guidToString, "guidToString");
    getApi (reaper, a.GetTrackName, "GetTrackName");
    getApi (reaper, a.ValidatePtr2, "ValidatePtr2");
    getApi (reaper, a.Undo_BeginBlock2, "Undo_BeginBlock2");
    getApi (reaper, a.Undo_EndBlock2, "Undo_EndBlock2");
    getApi (reaper, a.PreventUIRefresh, "PreventUIRefresh");
    getApi (reaper, a.GetTrackStateChunk, "GetTrackStateChunk");
    getApi (reaper, a.SetTrackStateChunk, "SetTrackStateChunk");
    getApi (reaper, a.InsertTrackInProject, "InsertTrackInProject");
    getApi (reaper, a.InsertTrackAtIndex, "InsertTrackAtIndex");
    getApi (reaper, a.DeleteTrack, "DeleteTrack");
    getApi (reaper, a.GetTrack, "GetTrack");
    getApi (reaper, a.CountTracks, "CountTracks");
    getApi (reaper, a.TrackFX_GetPresetIndex, "TrackFX_GetPresetIndex");
    getApi (reaper, a.TrackFX_GetPreset, "TrackFX_GetPreset");
    getApi (reaper, a.TrackFX_SetPresetByIndex, "TrackFX_SetPresetByIndex");
    getApi (reaper, a.TrackFX_SetPreset, "TrackFX_SetPreset");
    getApi (reaper, a.TrackFX_NavigatePresets, "TrackFX_NavigatePresets");
    getApi (reaper, a.TrackFX_GetUserPresetFilename, "TrackFX_GetUserPresetFilename");
    getApi (reaper, a.EnumInstalledFX, "EnumInstalledFX");
    getApi (reaper, a.GetAppVersion, "GetAppVersion");
}

juce::String ReaperBridge::reaperVersion()
{
    return api->GetAppVersion != nullptr ? utf8 (api->GetAppVersion()) : juce::String();
}

int ReaperBridge::rawCount (bool rec)
{
    if (track == nullptr) return 0;
    if (rec) return api->TrackFX_GetRecCount != nullptr ? api->TrackFX_GetRecCount (track) : 0;
    return api->TrackFX_GetCount (track);
}

bool ReaperBridge::validIndex (int i)
{
    return track != nullptr && i >= 0 && i < rawCount (chainFlag != 0);
}

bool ReaperBridge::looksLikeOctopus (int raw)
{
    char buf[1024] = {};
    auto has = [&] { return utf8 (buf).containsIgnoreCase ("Octopus"); };
    if (api->TrackFX_GetNamedConfigParm != nullptr && api->TrackFX_GetNamedConfigParm (track, raw, "fx_name", buf, (int) sizeof (buf)) && has())
        return true;
    buf[0] = 0;
    if (api->TrackFX_GetNamedConfigParm != nullptr && api->TrackFX_GetNamedConfigParm (track, raw, "fx_ident", buf, (int) sizeof (buf)) && has())
        return true;
    buf[0] = 0;
    return api->TrackFX_GetFXName (track, raw, buf, (int) sizeof (buf)) && has();
}

// Retrouve CETTE instance dans la chaîne : son paramètre 0 ("instance") vaut un jeton aléatoire propre à l'instance.
bool ReaperBridge::locateSelf()
{
    selfRaw = -1; chainFlag = 0; clash = false;
    const double mine = tokenFn ? tokenFn() : -1.0;
    for (int pass = 0; pass < 2 && selfRaw < 0; ++pass)
    {
        const int flag = pass == 0 ? 0 : kRecFlag;
        const int n = rawCount (pass == 1);
        int matches = 0, best = -1;
        double bestErr = 1.0;
        for (int i = 0; i < n; ++i)
        {
            if (! looksLikeOctopus (i | flag)) continue;
            // paramètre "Octopus ID" : le premier du plugin (vérifié par son nom si REAPER donne les noms)
            int idParam = 0;
            if (api->TrackFX_GetParamName != nullptr && api->TrackFX_GetNumParams != nullptr)
            {
                const int np = juce::jmin (16, api->TrackFX_GetNumParams (track, i | flag));
                for (int p = 0; p < np; ++p)
                {
                    char pn[128] = {};
                    if (api->TrackFX_GetParamName (track, i | flag, p, pn, (int) sizeof (pn)) && juce::String (pn) == "Octopus ID") { idParam = p; break; }
                }
            }
            const double err = std::abs (api->TrackFX_GetParamNormalized (track, i | flag, idParam) - mine);
            if (err < 2.0e-6) { ++matches; if (err < bestErr) { bestErr = err; best = i; } }
        }
        if (best >= 0) { selfRaw = best; chainFlag = flag; clash = matches > 1; }
    }
    return selfRaw >= 0;
}

bool ReaperBridge::ready()
{
    if (reaper == nullptr) { status = "Octopus works inside REAPER only"; track = nullptr; return false; }
    if (! api->core())
    {
        status = "REAPER 6.37 or newer is needed";
        track = nullptr; return false;
    }
    auto* r = rh (reaper);
    if (r->getReaperParent (2) != nullptr)
    {
        status = "Octopus drives track FX (not take FX)";
        track = nullptr; return false;
    }
    project = r->getReaperParent (3);
    track = r->getReaperParent (1);
    if (track != nullptr && api->ValidatePtr2 != nullptr && ! api->ValidatePtr2 (project, track, "MediaTrack*"))
        track = nullptr;
    if (track == nullptr) { status = "Waiting for the track..."; return false; }

    if (! locateSelf())
    {
        // pas encore visible dans la chaîne (insertion en cours) : on attend plutôt que de risquer une erreur d'indice
        status = "Looking for Octopus in the FX chain...";
        return false;
    }
    status = chainFlag != 0 ? "REAPER - input FX" : "REAPER";
    return true;
}

juce::String ReaperBridge::trackName()
{
    if (track == nullptr || api->GetTrackName == nullptr) return {};
    char buf[512] = {};
    api->GetTrackName (track, buf, (int) sizeof (buf));
    return utf8 (buf);
}

int ReaperBridge::count()
{
    return rawCount (chainFlag != 0);
}

FxInfo ReaperBridge::info (int i)
{
    FxInfo f;
    if (! validIndex (i)) return f;
    auto& a = *api;
    const int raw = i | chainFlag;
    char buf[1024] = {};
    if (a.TrackFX_GetFXName (track, raw, buf, (int) sizeof (buf))) f.name = utf8 (buf);
    buf[0] = 0;
    if (a.TrackFX_GetNamedConfigParm != nullptr && a.TrackFX_GetNamedConfigParm (track, raw, "fx_name", buf, (int) sizeof (buf)))
        f.original = utf8 (buf);
    if (f.original.isEmpty()) f.original = f.name;
    buf[0] = 0;
    if (a.TrackFX_GetNamedConfigParm != nullptr && a.TrackFX_GetNamedConfigParm (track, raw, "parallel", buf, (int) sizeof (buf)))
        f.parallel = juce::String (buf).getIntValue();
    f.enabled = a.TrackFX_GetEnabled (track, raw);
    f.offline = a.TrackFX_GetOffline != nullptr && a.TrackFX_GetOffline (track, raw);
    f.open = a.TrackFX_GetOpen != nullptr && a.TrackFX_GetOpen (track, raw);
    if (auto* g = a.TrackFX_GetFXGUID (track, raw))
    {
        char gs[64] = {};
        a.guidToString (g, gs);
        f.guid = utf8 (gs);
    }
    f.isSelf = (i == selfRaw);
    return f;
}

void ReaperBridge::setEnabled (int i, bool on)
{
    if (validIndex (i) && i != selfRaw) api->TrackFX_SetEnabled (track, i | chainFlag, on);
}

void ReaperBridge::setOffline (int i, bool off)
{
    if (validIndex (i) && i != selfRaw && api->TrackFX_SetOffline != nullptr) api->TrackFX_SetOffline (track, i | chainFlag, off);
}

void ReaperBridge::setParallel (int i, int mode)
{
    if (validIndex (i) && api->TrackFX_SetNamedConfigParm != nullptr)
        api->TrackFX_SetNamedConfigParm (track, i | chainFlag, "parallel", juce::String (mode).toRawUTF8());
}

int ReaperBridge::add (const juce::String& fxName, int insertAt)
{
    if (track == nullptr) return -1;
    // instantiate <= -1000 : position d'insertion (-1000 = premier de la chaîne, -1001 = deuxième...)
    const int at = juce::jlimit (0, count(), insertAt);
    const int pos = api->TrackFX_AddByName (track, fxName.toRawUTF8(), chainFlag != 0, -1000 - at);
    return pos < 0 ? -1 : (pos & 0xffffff);
}

bool ReaperBridge::remove (int i)
{
    if (! validIndex (i) || selfRaw < 0 || i == selfRaw) return false;      // jamais Octopus lui-même
    const bool ok = api->TrackFX_Delete (track, i | chainFlag);
    if (ok && i < selfRaw) --selfRaw;
    return ok;
}

void ReaperBridge::move (int from, int to)
{
    if (! validIndex (from) || to < 0 || from == to) return;
    to = juce::jmin (to, count() - 1);
    api->TrackFX_CopyToTrack (track, from | chainFlag, track, to | chainFlag, true);
    locateSelf();
}

void ReaperBridge::showWindow (int i, bool show)
{
    if (validIndex (i)) api->TrackFX_Show (track, i | chainFlag, show ? 3 : 2);
}

void ReaperBridge::beginUndo()
{
    if (undoDepth++ == 0)
    {
        undoTouchedTracks = false;
        if (api->Undo_BeginBlock2 != nullptr) api->Undo_BeginBlock2 (project);
    }
}

void ReaperBridge::endUndo (const juce::String& what)
{
    if (undoDepth <= 0) return;
    if (--undoDepth == 0 && api->Undo_EndBlock2 != nullptr)
        api->Undo_EndBlock2 (project, ("Octopus: " + what).toRawUTF8(), undoTouchedTracks ? kUndoStateAll : kUndoStateFx);
}

// ------------------------------------------------------------------ paramètres
int ReaperBridge::numParams (int i)
{
    return validIndex (i) && api->TrackFX_GetNumParams != nullptr ? api->TrackFX_GetNumParams (track, i | chainFlag) : 0;
}

juce::String ReaperBridge::paramName (int i, int p)
{
    char buf[256] = {};
    if (validIndex (i) && api->TrackFX_GetParamName != nullptr) api->TrackFX_GetParamName (track, i | chainFlag, p, buf, (int) sizeof (buf));
    return utf8 (buf);
}

juce::String ReaperBridge::paramText (int i, int p)
{
    char buf[256] = {};
    if (validIndex (i) && api->TrackFX_GetFormattedParamValue != nullptr
        && api->TrackFX_GetFormattedParamValue (track, i | chainFlag, p, buf, (int) sizeof (buf)) && buf[0] != 0)
        return utf8 (buf);
    return juce::String (juce::roundToInt (getParam (i, p) * 100.0)) + " %";
}

double ReaperBridge::getParam (int i, int p)
{
    return validIndex (i) ? api->TrackFX_GetParamNormalized (track, i | chainFlag, p) : 0.0;
}

void ReaperBridge::setParam (int i, int p, double v)
{
    if (validIndex (i) && i != selfRaw && api->TrackFX_SetParamNormalized != nullptr)
        api->TrackFX_SetParamNormalized (track, i | chainFlag, p, juce::jlimit (0.0, 1.0, v));
}

// ------------------------------------------------------------------ presets (état complet de la chaîne)
bool ReaperBridge::trackChunk (void* tr, juce::String& out)
{
    // l'état peut être gros (plugins avec échantillons / IR) : tampon agrandi jusqu'à ce qu'il ne soit pas plein
    for (int size = 1 << 20; size <= (1 << 28); size <<= 2)
    {
        juce::HeapBlock<char> buf ((size_t) size, true);
        if (! api->GetTrackStateChunk (tr, buf.get(), size, false)) return false;
        const auto len = strnlen (buf.get(), (size_t) size);
        if ((int) len < size - 1)
        {
            out = juce::String (juce::CharPointer_UTF8 (buf.get()), len);
            return true;
        }
    }
    return false;
}

bool ReaperBridge::capture (juce::StringArray& entries, int& selfPos, juce::String& error)
{
    entries.clear(); selfPos = 0;
    if (! ready()) { error = status; return false; }
    if (! api->presets()) { error = "REAPER 6.37 or newer is needed for presets"; return false; }

    juce::String chunk;
    if (! trackChunk (track, chunk)) { error = "Could not read the track state"; return false; }
    juce::StringArray all;
    ChainChunk::extract (chunk, chainFlag != 0, all);
    if (all.size() != count())
    {
        error = "FX chain not readable (" + juce::String (all.size()) + " / " + juce::String (count()) + ")";
        return false;
    }
    for (int i = 0; i < all.size(); ++i)
    {
        if (i == selfRaw) { selfPos = entries.size(); continue; }
        entries.add (all[i]);
    }
    return true;
}

// Les effets d'un preset sont recréés avec REAPER lui-même (aucune conversion maison des réglages) :
//   1. une piste temporaire reçoit ces effets dans son état (SetTrackStateChunk) ;
//   2. chaque effet est déplacé vers sa position dans la chaîne d'Octopus (TrackFX_CopyToTrack) ;
//   3. la piste temporaire est supprimée.
// Octopus n'est jamais recréé ni déplacé (son état et ses presets restent en place).
bool ReaperBridge::insertEntries (const juce::Array<EntryInsert>& items, juce::String& error)
{
    if (items.isEmpty()) return true;
    if (! ready()) { error = status; return false; }
    if (! api->presets()) { error = "REAPER 6.37 or newer is needed for presets"; return false; }

    juce::StringArray entries;
    for (auto& it : items) entries.add (it.entry);

    if (api->PreventUIRefresh != nullptr) api->PreventUIRefresh (1);
    undoTouchedTracks = true;
    const int idx = api->CountTracks (project);
    if (api->InsertTrackInProject != nullptr) api->InsertTrackInProject (project, idx, 0);   // sans effets ni enveloppes par défaut
    else api->InsertTrackAtIndex (idx, false);
    void* temp = api->GetTrack (project, idx);
    bool ok = false;
    if (temp == nullptr || temp == track) error = "Could not create the temporary track";
    else
    {
        juce::String tc;
        if (! trackChunk (temp, tc)) error = "Could not read the temporary track";
        else if (ChainChunk::hasChain (tc, false)) error = "Temporary track is not empty";
        else if (! api->SetTrackStateChunk (temp, ChainChunk::withChain (tc, entries).toRawUTF8(), false))
            error = "REAPER refused the preset";
        else
        {
            const int n = api->TrackFX_GetCount (temp);
            // Plugin absent (désinstallé) : REAPER ne charge pas cet effet. On retrouve alors quel effet du preset
            // correspond à chaque effet chargé (par son nom) pour placer les autres au bon endroit.
            std::vector<int> entryOf ((size_t) n);
            for (int j = 0; j < n; ++j) entryOf[(size_t) j] = j;
            juce::StringArray missing;
            if (n != entries.size())
            {
                auto core = [] (juce::String x)
                {
                    x = x.trim();
                    const int colon = x.indexOfChar (':');
                    if (colon > 0 && colon < 7) x = x.substring (colon + 1).trim();
                    if (x.endsWithChar (')') && x.lastIndexOfChar ('(') > 0) x = x.substring (0, x.lastIndexOfChar ('(')).trim();
                    return x.toLowerCase();
                };
                bool mapped = true;
                int k = 0;
                for (int j = 0; j < n && mapped; ++j)
                {
                    char buf[1024] = {};
                    if (! (api->TrackFX_GetNamedConfigParm != nullptr && api->TrackFX_GetNamedConfigParm (temp, j, "fx_name", buf, (int) sizeof (buf))))
                        api->TrackFX_GetFXName (temp, j, buf, (int) sizeof (buf));
                    const auto name = core (utf8 (buf));
                    while (k < entries.size() && core (ChainChunk::displayName (entries[k])) != name)
                        missing.add (ChainChunk::displayName (entries[k++]));
                    if (k >= entries.size()) mapped = false;
                    else entryOf[(size_t) j] = k++;
                }
                while (mapped && k < entries.size()) missing.add (ChainChunk::displayName (entries[k++]));
                if (! mapped)                                   // noms non comparables : ordre conservé
                {
                    missing.clear();
                    for (int j = 0; j < n; ++j) entryOf[(size_t) j] = j;
                }
                error = missing.isEmpty() ? "Some plug-ins of the preset are missing (" + juce::String (n) + " / " + juce::String (entries.size()) + ")"
                                          : "Missing plug-in : " + missing.joinIntoString (", ");
            }
            // positions finales : un effet absent ne prend pas sa place, les suivants remontent d'autant
            int skipped = 0, prev = -1;
            for (int j = 0; j < n; ++j)
            {
                const int e = entryOf[(size_t) j];
                skipped += e - prev - 1;
                prev = e;
                const int at = juce::jlimit (0, count(), items[e].at - skipped);
                api->TrackFX_CopyToTrack (temp, 0, track, at | chainFlag, true);
                if (at <= selfRaw) ++selfRaw;
            }
            ok = n == entries.size();
        }
        api->DeleteTrack (temp);
    }
    if (api->PreventUIRefresh != nullptr) api->PreventUIRefresh (-1);
    locateSelf();
    return ok;
}

// ------------------------------------------------------------------ presets d'un plugin
bool ReaperBridge::namedBig (int raw, const char* key, juce::String& out)
{
    if (api->TrackFX_GetNamedConfigParm == nullptr) return false;
    for (int size = 1 << 16; size <= (1 << 27); size <<= 2)
    {
        juce::HeapBlock<char> buf ((size_t) size, true);
        if (! api->TrackFX_GetNamedConfigParm (track, raw, key, buf.get(), size)) return false;
        const auto len = strnlen (buf.get(), (size_t) size);
        if ((int) len < size - 1) { out = juce::String (juce::CharPointer_UTF8 (buf.get()), len); return out.isNotEmpty(); }
    }
    return false;
}

static juce::String cleanPresetName (juce::String n)
{
    n = n.trim();
    if (n.containsChar ('/') || n.containsChar ('\\'))                      // preset d'usine VST3 : chemin du fichier .vstpreset
        n = n.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false).upToLastOccurrenceOf (".", false, false);
    return n;
}

// REAPER donne le nombre de presets et charge un preset par son index, mais pas la liste des noms.
// Pour l'afficher, chaque preset est chargé un instant pour lire son nom, puis l'état exact du plugin
// (vst_chunk / clap_chunk) et le preset affiché sont rétablis. Fait seulement à l'ouverture de la liste.
PluginPresets ReaperBridge::presets (int i, bool probe)
{
    PluginPresets out;
    if (! validIndex (i) || i == selfRaw || api->TrackFX_GetPresetIndex == nullptr) return out;
    const int raw = i | chainFlag;
    int n = 0;
    out.current = api->TrackFX_GetPresetIndex (track, raw, &n);
    out.count = juce::jmax (0, n);
    if (! probe || out.count == 0) return out;

    juce::StringArray userNames;                                            // presets utilisateur : fichier .ini de REAPER
    if (api->TrackFX_GetUserPresetFilename != nullptr)
    {
        char fn[4096] = {};
        api->TrackFX_GetUserPresetFilename (track, raw, fn, (int) sizeof (fn));
        const juce::File f (utf8 (fn));
        if (fn[0] != 0 && f.existsAsFile())
            for (auto& l : juce::StringArray::fromLines (f.loadFileAsString()))
                if (l.trim().startsWithIgnoreCase ("Name="))
                    userNames.add (l.fromFirstOccurrenceOf ("=", false, false).trim());
    }

    juce::String saved, key;
    for (auto* k : { "vst_chunk", "clap_chunk" })
        if (namedBig (raw, k, saved)) { key = k; break; }
    const bool canRestore = key.isNotEmpty() && api->TrackFX_SetNamedConfigParm != nullptr;
    if (! canRestore || api->TrackFX_SetPresetByIndex == nullptr || api->TrackFX_GetPreset == nullptr)
    {
        // sans moyen de rétablir l'état exact (JS...) : seulement les presets utilisateur, chargés par leur nom
        out.byName = true;
        for (auto& u : userNames) { out.names.add (u); out.user.add (true); }
        return out;
    }

    if (api->PreventUIRefresh != nullptr) api->PreventUIRefresh (1);
    const int maxN = juce::jmin (out.count, 512);
    for (int k = 0; k < maxN; ++k)
    {
        api->TrackFX_SetPresetByIndex (track, raw, k);
        char nm[2048] = {};
        api->TrackFX_GetPreset (track, raw, nm, (int) sizeof (nm));
        auto name = cleanPresetName (utf8 (nm));
        if (name.isEmpty()) name = "Preset " + juce::String (k + 1);
        out.names.add (name);
        out.user.add (userNames.contains (name));
    }
    if (out.current >= 0) api->TrackFX_SetPresetByIndex (track, raw, out.current);
    api->TrackFX_SetNamedConfigParm (track, raw, key.toRawUTF8(), saved.toRawUTF8());
    if (api->PreventUIRefresh != nullptr) api->PreventUIRefresh (-1);
    return out;
}

juce::String ReaperBridge::presetName (int i)
{
    if (! validIndex (i) || api->TrackFX_GetPreset == nullptr) return {};
    char nm[2048] = {};
    api->TrackFX_GetPreset (track, i | chainFlag, nm, (int) sizeof (nm));
    return cleanPresetName (utf8 (nm));
}

bool ReaperBridge::loadPreset (int i, int idx, const juce::String& name)
{
    if (! validIndex (i) || i == selfRaw) return false;
    if (idx >= 0 && api->TrackFX_SetPresetByIndex != nullptr) return api->TrackFX_SetPresetByIndex (track, i | chainFlag, idx);
    if (name.isNotEmpty() && api->TrackFX_SetPreset != nullptr) return api->TrackFX_SetPreset (track, i | chainFlag, name.toRawUTF8());
    return false;
}

bool ReaperBridge::stepPreset (int i, int dir)
{
    if (! validIndex (i) || i == selfRaw || api->TrackFX_NavigatePresets == nullptr) return false;
    return api->TrackFX_NavigatePresets (track, i | chainFlag, dir);
}

juce::String ReaperBridge::fxChunk (int i)
{
    juce::StringArray entries; int sp = 0; juce::String err;
    if (! validIndex (i) || ! capture (entries, sp, err)) return {};
    const int e = i < sp ? i : i - 1;                     // capture() saute Octopus
    if (i == selfRaw || ! juce::isPositiveAndBelow (e, entries.size())) return {};
    return entries[e];
}

// ------------------------------------------------------------------ effets installés
juce::Array<InstalledFx> ReaperBridge::installed()
{
    juce::Array<InstalledFx> list;
    if (reaper == nullptr || api->EnumInstalledFX == nullptr) return list;
    for (int i = 0; i < 100000; ++i)
    {
        const char* name = nullptr; const char* ident = nullptr;
        if (! api->EnumInstalledFX (i, &name, &ident)) break;
        if (name == nullptr || *name == 0) continue;
        InstalledFx f;
        f.name = utf8 (name);
        f.ident = utf8 (ident);
        if (f.name.containsIgnoreCase ("Octopus") && f.name.containsIgnoreCase ("Studio Wiga")) continue;   // pas d'Octopus dans Octopus
        list.add (f);
    }
    return list;
}
