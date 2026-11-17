// 2.61 Studio de Créations (see Studio.h): the creations open, their drafts, the server, the
// explorer, the tabs, the top bar and the small widgets shared by the panels.

#include "ui/studio/Studio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/OnlineSession.h"
#include "packaging/Sha256.h"
#include "ui/screens/ChallengeUi.h"
#include "ui/studio/StudioStyle.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::studio {

using challenge_ui::fitLabel;
using challenge_ui::fz;
using namespace style;

namespace {

fs::path draftsDir() { return Paths::userDir() / "studio" / "brouillons"; }
fs::path archivesDir() { return Paths::userDir() / "studio" / "archives"; }

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

/// "14:32" of an ISO time (local time of this computer).
std::string clockOf(const std::string& iso) {
    auto t = parseIsoUtc(iso);
    if (!t) return "";
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
    std::tm m{};
#ifdef _WIN32
    localtime_s(&m, &tt);
#else
    localtime_r(&tt, &m);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d", m.tm_hour, m.tm_min);
    return b;
}

const char* kNewNames[kDocKinds] = {"Nouvelle combinaison", "Nouvelle traînée", "Nouveau cadre", "Nouvel effet de victoire"};
const char* kGroupNames[kDocKinds] = {"Combinaisons", "Traînées", "Cadres", "Effets de victoire"};

Px pxOf(Rgb8 c) { return Px::rgb(c.r, c.g, c.b); }

}  // namespace

/// The first `n` characters (code points) of a UTF-8 text.
std::string firstChars(const std::string& s, size_t n) {
    size_t i = 0, count = 0;
    while (i < s.size()) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (count == n) break;
            ++count;
        }
        ++i;
    }
    return s.substr(0, i);
}

namespace {

}  // namespace

Studio::Studio(GameServices& g, Hooks hooks) : g_(g), ui_(g.ui), hooks_(std::move(hooks)), online_(g) {
    if (auto p = g_.app.resources().resolve("animations/nova.json"))
        if (auto j = FileUtil::loadJson(*p)) novaAnims_ = suitAnims(*j);
}

Studio::~Studio() {
    // Nothing is lost: what was drawn and not saved becomes (or updates) its draft.
    for (auto& d : docs_)
        if (d->dirty) saveDraft(*d, true);
    saveSession();
}

// ------------------------------------------------------------------ documents

Studio::OpenDoc* Studio::active() { return active_ >= 0 && active_ < static_cast<int>(docs_.size()) ? docs_[static_cast<size_t>(active_)].get() : nullptr; }
const Studio::OpenDoc* Studio::active() const {
    return active_ >= 0 && active_ < static_cast<int>(docs_.size()) ? docs_[static_cast<size_t>(active_)].get() : nullptr;
}

std::vector<DocAnim> Studio::animsOf(const OpenDoc& d) const { return d.doc.anims(novaAnims_); }

const DocAnim* Studio::currentAnim(const OpenDoc& d) const {
    // Made again only when a strip's frames or rhythm changed (a suit's never do); assigned in
    // place, so that a pointer given earlier in the frame stays valid.
    const bool stale = d.animCache.empty() ||
                       (d.doc.kind != DocKind::Suit && (d.animCache[0].count != std::max(1, d.doc.stripFrames()) || d.animFps != d.doc.fps));
    if (stale) {
        std::vector<DocAnim> fresh = animsOf(d);
        if (fresh.size() != d.animCache.size()) d.animCache = std::move(fresh);
        else
            for (size_t i = 0; i < fresh.size(); ++i) d.animCache[i] = fresh[i];
        d.animFps = d.doc.fps;
    }
    if (d.animCache.empty()) return nullptr;
    return &d.animCache[static_cast<size_t>(std::clamp(d.anim, 0, static_cast<int>(d.animCache.size()) - 1))];
}

IRect Studio::currentFrame(const OpenDoc& d) const {
    const DocAnim* a = currentAnim(d);
    if (!a) return d.doc.image.bounds();
    return d.doc.frameRect(*a, std::clamp(d.frame, 0, a->count - 1));
}

const PixelImage& Studio::novaSheet() {
    if (!novaTried_) {
        novaTried_ = true;
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        if (auto p = g_.app.resources().resolve("sprites/player/nova.png"); p && loadRgbaImage(*p, px, w, h))
            novaSheet_ = PixelImage::fromRgba(w, h, std::move(px));
    }
    return novaSheet_;
}

PixelImage Studio::suitFromColors(const SuitColors& c) {
    PixelImage out = novaSheet();
    if (!out.empty()) recolorSuitPixels(out.data().data(), out.data().data(), out.data().size() / 4, c);
    return out;
}

void Studio::openDoc(StudioDoc doc, bool dirty) {
    auto d = std::make_unique<OpenDoc>();
    d->doc = std::move(doc);
    d->dirty = dirty;
    d->unsent = d->doc.serverId == 0;
    if (d->doc.kind == DocKind::Suit) {
        // On the running animation, its first frame (the most drawn one).
        const auto anims = animsOf(*d);
        for (size_t i = 0; i < anims.size(); ++i)
            if (anims[i].id == "run") d->anim = static_cast<int>(i);
    }
    docs_.push_back(std::move(d));
    active_ = static_cast<int>(docs_.size()) - 1;
    selKind_ = static_cast<int>(docs_.back()->doc.kind);
    selKey_ = "o:" + docs_.back()->doc.uid;
    cursorX_ = cursorY_ = 0;
    saveSession();
}

void Studio::newDoc(DocKind kind) {
    PixelImage start;
    if (kind == DocKind::Suit) start = novaSheet();
    if (kind == DocKind::Frame) start = renderRing(defaultCreationLook("frame"), 64);
    StudioDoc d = StudioDoc::make(kind, newDraftUid(), kNewNames[static_cast<int>(kind)], start, 4);
    if (kind != DocKind::Suit) d.look = defaultCreationLook(kindId(kind));
    openDoc(std::move(d), true);
    hooks_.say(std::string(kindName(kind)) + " créée sur ce PC : dessinez-la, « Enregistrer » garde le brouillon, « Envoyer au serveur » la publie.", false);
}

void Studio::openDraft(const std::string& uid) {
    for (size_t i = 0; i < docs_.size(); ++i)
        if (docs_[i]->doc.uid == uid) {
            active_ = static_cast<int>(i);
            return;
        }
    std::string why;
    auto d = StudioDoc::loadDraft(draftsDir() / uid, &why);
    if (!d) d = StudioDoc::loadDraft(archivesDir() / uid, &why);
    if (!d) {
        hooks_.say("Brouillon illisible : " + why, true);
        return;
    }
    openDoc(std::move(*d), false);
}

const CreationImageInfo* Studio::imageInfoOf(int serverId) const {
    auto it = adminImages_.find(serverId);
    return it == adminImages_.end() ? nullptr : &it->second;
}

void Studio::openServer(const Json& c) {
    const int id = jget<int>(c, "id", 0);
    for (size_t i = 0; i < docs_.size(); ++i)
        if (docs_[i]->doc.serverId == id) {
            active_ = static_cast<int>(i);
            return;
        }
    for (const StudioDoc& dr : drafts_)
        if (dr.serverId == id) return openDraft(dr.uid);
    auto kind = kindFromId(jstr(c, "kind", "suit"));
    if (!kind) return;
    StudioDoc d;
    d.kind = *kind;
    d.uid = newDraftUid();
    d.name = jstr(c, "name");
    d.serverId = id;
    d.serverUpdatedAt = jstr(c, "updated_at");
    d.archived = jget<bool>(c, "archived", false);
    if (*kind == DocKind::Suit) {
        d.colors = suitColorsFromJson(jfield(c, "colors", Json())).value_or(d.colors);
        d.frameW = d.frameH = kSuitFrame;
        // Its image as every game draws it now: Nova's sheet in its three colours.
        d.image = suitFromColors(d.colors);
    } else {
        d.look = creationLookFromJson(kindId(*kind), c).value_or(defaultCreationLook(kindId(*kind)));
        if (*kind == DocKind::Frame) {
            d.frameW = d.frameH = 64;
            d.image = renderRing(d.look, 64);
            d.fps = 8;
        } else {
            d.frameW = d.frameH = 16;
            d.image = PixelImage(64, 16);
        }
    }
    d.paletteFromImage();
    openDoc(std::move(d), false);
    OpenDoc& od = *docs_.back();
    od.unsent = false;
    // Its image on the server, when it has one: downloaded (the administrators may read them all).
    if (const CreationImageInfo* ii = imageInfoOf(id)) {
        od.loadingImage = true;
        const std::string uid = od.doc.uid;
        const CreationImageInfo copy = *ii;
        g_.online.getBinary(ii->path(), [this, alive = std::weak_ptr<int>(life_), uid, copy](const HttpResponse& r) {
            if (alive.expired()) return;
            for (auto& dd : docs_) {
                if (dd->doc.uid != uid) continue;
                dd->loadingImage = false;
                std::vector<uint8_t> bytes(r.body.begin(), r.body.end());
                const std::string why = r.status == 200 ? CreationImageBook::checkBytes(copy, bytes) : std::string("le serveur a répondu ") + std::to_string(r.status);
                int w = 0, h = 0;
                std::vector<uint8_t> rgba;
                if (!why.empty() || !decodeImage(bytes.data(), bytes.size(), w, h, rgba)) {
                    hooks_.say("Image de « " + dd->doc.name + " » non téléchargée (" + (why.empty() ? std::string("illisible") : why) +
                                   ") : ses paramètres sont dessinés à la place.",
                               true);
                    return;
                }
                dd->doc.image = PixelImage::fromRgba(w, h, std::move(rgba));
                dd->doc.serverImageHash = copy.hash;
                if (dd->doc.kind != DocKind::Suit) {
                    dd->doc.frameW = copy.frameW;
                    dd->doc.frameH = copy.frameH;
                    dd->doc.fps = copy.fps;
                }
                if (copy.emitter) dd->doc.emitter = *copy.emitter;
                dd->doc.paletteFromImage();
                dd->history.clear();
                dd->texDirty = true;  // its size may have changed: all of it
            }
        });
    }
}

void Studio::duplicateSelected() {
    StudioDoc copy;
    bool have = false;
    if (OpenDoc* d = active(); d && (selKey_.empty() || selKey_ == "o:" + d->doc.uid)) {
        copy = d->doc;
        have = true;
    } else if (selKey_.rfind("d:", 0) == 0) {
        for (const StudioDoc& dr : drafts_)
            if (dr.uid == selKey_.substr(2)) {
                copy = dr;
                have = true;
            }
    } else if (selKey_.rfind("s:", 0) == 0) {
        const int id = std::atoi(selKey_.c_str() + 2);
        for (const Json& c : serverList())
            if (jget<int>(c, "id", 0) == id) {
                openServer(c);
                if (OpenDoc* d = active()) {
                    copy = d->doc;
                    have = true;
                }
            }
    }
    if (!have) return;
    copy.uid = newDraftUid();
    copy.serverId = 0;
    copy.serverUpdatedAt.clear();
    copy.serverImageHash.clear();
    copy.archived = false;
    copy.savedAt.clear();
    copy.name = firstChars(copy.name, 24) + " (copie)";
    const std::string n = copy.name;
    openDoc(std::move(copy), true);
    hooks_.say("« " + n + " » : copie ouverte, pas encore enregistrée.", false);
}

void Studio::archiveSelected() {
    if (selKey_.rfind("s:", 0) == 0 || (active() && selKey_ == "o:" + active()->doc.uid && active()->doc.serverId > 0)) {
        // A creation of the server: archived there (it stays won by those who have it).
        int id = selKey_.rfind("s:", 0) == 0 ? std::atoi(selKey_.c_str() + 2) : active()->doc.serverId;
        bool archived = false;
        for (const Json& c : serverList())
            if (jget<int>(c, "id", 0) == id) archived = jget<bool>(c, "archived", false);
        Json body{{"archived", !archived}};
        sending_ = true;
        g_.online.send("POST", "/api/v1/admin/creations/" + std::to_string(id) + "/archive", body,
                       OnlineService::guard(life_, [this, id, archived](const ApiResult& r) {
                           sending_ = false;
                           if (hooks_.lostRole(r)) return;
                           if (!r.ok) return hooks_.say(r.message.empty() ? "Refusé par le serveur." : r.message, true);
                           for (auto& d : docs_)
                               if (d->doc.serverId == id) d->doc.archived = !archived;
                           hooks_.say(archived ? "Restaurée sur le serveur : de nouveau proposée en récompense."
                                               : "Archivée sur le serveur : plus proposée en récompense ; ceux qui l'ont gagnée la gardent.",
                                      false);
                           hooks_.reloadList();
                       }));
        return;
    }
    // A draft of this computer: put away in studio/archives (or brought back).
    std::string uid;
    if (selKey_.rfind("d:", 0) == 0) uid = selKey_.substr(2);
    if (selKey_.rfind("o:", 0) == 0) uid = selKey_.substr(2);
    if (uid.empty()) return;
    for (size_t i = 0; i < docs_.size(); ++i)
        if (docs_[i]->doc.uid == uid) {
            if (docs_[i]->dirty) saveDraft(*docs_[i], true);
            closeTab(static_cast<int>(i), true);
            break;
        }
    std::error_code ec;
    const bool back = fs::exists(archivesDir() / uid, ec);
    const fs::path from = back ? archivesDir() / uid : draftsDir() / uid, to = back ? draftsDir() / uid : archivesDir() / uid;
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    if (ec) return hooks_.say("Brouillon impossible à déplacer : " + ec.message(), true);
    hooks_.say(back ? "Brouillon restauré." : "Brouillon archivé sur ce PC (« Archives » pour le revoir).", false);
    selKey_.clear();
    loadDrafts();
}

void Studio::startRename() {
    std::string name;
    if (OpenDoc* d = active(); d && selKey_ == "o:" + d->doc.uid) name = d->doc.name;
    else if (selKey_.rfind("d:", 0) == 0 || selKey_.rfind("s:", 0) == 0) {
        // Opened first: the new name goes with the creation (its draft, then the server).
        if (selKey_[0] == 'd') openDraft(selKey_.substr(2));
        else
            for (const Json& c : serverList())
                if (jget<int>(c, "id", 0) == std::atoi(selKey_.c_str() + 2)) openServer(c);
        if (OpenDoc* d = active()) {
            name = d->doc.name;
            selKey_ = "o:" + d->doc.uid;
        }
    }
    if (!active()) return;
    renaming_ = true;
    renameText_ = name;
    focusRename_ = true;
}

bool Studio::saveDraft(OpenDoc& d, bool quiet) {
    dropSelection(d);
    std::string why;
    if (!d.doc.saveDraft(draftsDir() / d.doc.uid, &why)) {
        if (!quiet) hooks_.say("Brouillon non enregistré : " + why, true);
        return false;
    }
    d.dirty = false;
    if (!quiet) hooks_.say("Brouillon « " + d.doc.name + " » enregistré sur ce PC à " + clockOf(d.doc.savedAt) + ".", false);
    loadDrafts();
    saveSession();
    return true;
}

void Studio::closeTab(int index, bool force) {
    if (index < 0 || index >= static_cast<int>(docs_.size())) return;
    if (!force && docs_[static_cast<size_t>(index)]->dirty) {
        confirmClose_ = index;
        return;
    }
    docs_.erase(docs_.begin() + index);
    if (active_ >= static_cast<int>(docs_.size())) active_ = static_cast<int>(docs_.size()) - 1;
    else if (active_ > index) --active_;
    stroking_ = false;
    saveSession();
}

void Studio::loadDrafts() {
    draftsLoaded_ = true;
    drafts_.clear();
    archived_.clear();
    std::error_code ec;
    for (const auto& [dir, list] : {std::pair{draftsDir(), &drafts_}, std::pair{archivesDir(), &archived_}})
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_directory(ec)) continue;
            if (auto d = StudioDoc::loadDraft(e.path())) list->push_back(std::move(*d));
        }
    auto byTime = [](const StudioDoc& a, const StudioDoc& b) { return a.savedAt > b.savedAt; };
    std::sort(drafts_.begin(), drafts_.end(), byTime);
    std::sort(archived_.begin(), archived_.end(), byTime);
}

void Studio::saveSession() const {
    Json open = Json::array();
    for (const auto& d : docs_)
        if (!d->doc.savedAt.empty()) open.push_back(d->doc.uid);
    std::error_code ec;
    fs::create_directories(Paths::userDir() / "studio", ec);
    const OpenDoc* a = active();
    FileUtil::writeTextAtomic(Paths::userDir() / "studio" / "session.json",
                              Json{{"open", open}, {"active", a ? a->doc.uid : std::string()}, {"seen", seenFiles_}}.dump(1));
}

void Studio::loadSession() {
    sessionLoaded_ = true;
    auto j = FileUtil::loadJson(Paths::userDir() / "studio" / "session.json");
    if (!j) return;
    for (const Json& s : jfield(*j, "seen", Json::array()))
        if (s.is_string()) seenFiles_.push_back(s.get<std::string>());
    const std::string want = jstr(*j, "active");
    for (const Json& u : jfield(*j, "open", Json::array()))
        if (u.is_string() && Paths::isSafeRelative(u.get<std::string>())) {
            std::error_code ec;
            if (fs::exists(draftsDir() / u.get<std::string>() / "doc.json", ec)) openDraft(u.get<std::string>());
        }
    for (size_t i = 0; i < docs_.size(); ++i)
        if (docs_[i]->doc.uid == want) active_ = static_cast<int>(i);
}

// ------------------------------------------------------------------ editing

void Studio::touch(OpenDoc& d) {
    d.dirty = true;
    d.unsent = true;
}

void Studio::markDirty(OpenDoc& d, const IRect& r) { d.texRect = d.texRect.unite(r); }

void Studio::beginEdit(OpenDoc& d, IRect region) {
    // Only the region the change may touch is kept (a frame for the tools): never the whole
    // sheet for a stroke.
    editRegion_ = region.empty() ? d.doc.image.bounds() : region.intersect(d.doc.image.bounds());
    editWhole_ = editRegion_ == d.doc.image.bounds();
    strokeBefore_ = editWhole_ ? d.doc.image : d.doc.image.crop(editRegion_);
    metaBefore_ = d.doc.metaJson();
    editOpen_ = true;
}

void Studio::restoreEdit(OpenDoc& d, const IRect& r) {
    if (!editOpen_) return;
    const IRect part = r.intersect(editRegion_);
    if (editWhole_ && strokeBefore_.w() == d.doc.image.w() && strokeBefore_.h() == d.doc.image.h())
        d.doc.image.paste(strokeBefore_.crop(part), part.x, part.y);
    else if (!editWhole_)
        d.doc.image.paste(strokeBefore_.crop({part.x - editRegion_.x, part.y - editRegion_.y, part.w, part.h}), part.x, part.y);
    markDirty(d, part);
}

void Studio::cancelEdit(OpenDoc& d) {
    if (!editOpen_) return;
    if (editWhole_) {
        d.doc.image = strokeBefore_;
        d.texDirty = true;
    } else {
        d.doc.image.paste(strokeBefore_, editRegion_.x, editRegion_.y);
        markDirty(d, editRegion_);
    }
    editOpen_ = false;
    strokeBefore_ = PixelImage();
}

void Studio::commit(OpenDoc& d, const std::string& label) {
    if (!editOpen_) return;
    editOpen_ = false;
    const Json after = d.doc.metaJson();
    const Json mb = metaBefore_ == after ? Json() : metaBefore_, ma = metaBefore_ == after ? Json() : after;
    const bool whole = editWhole_ || strokeBefore_.empty();
    bool kept = false;
    if (whole) kept = d.history.record(label, strokeBefore_, d.doc.image, mb, ma);
    else kept = d.history.recordRegion(label, editRegion_, strokeBefore_, d.doc.image, mb, ma);
    if (kept) touch(d);
    // A whole change (an import, a frame added...): the texture follows at once.
    if (whole) d.texDirty = true;
    strokeBefore_ = PixelImage();
}

void Studio::commitMeta(OpenDoc& d, const std::string& label, const Json& before) {
    const Json after = d.doc.metaJson();
    if (before == after) return;
    if (d.history.recordRegion(label, {}, PixelImage(), d.doc.image, before, after)) touch(d);
}

void Studio::undo(OpenDoc& d) {
    dropSelection(d);
    stroking_ = false;
    if (const ImageEdit* e = d.history.undo(d.doc.image)) {
        if (!e->metaBefore.is_null()) d.doc.applyMeta(e->metaBefore);
        touch(d);
        if (e->resized) d.texDirty = true;
        else markDirty(d, e->region);
        hooks_.say("Annulé : " + e->label + ".", false);
    }
    const DocAnim* a = currentAnim(d);
    if (a) d.frame = std::clamp(d.frame, 0, a->count - 1);
}

void Studio::redo(OpenDoc& d) {
    dropSelection(d);
    stroking_ = false;
    if (const ImageEdit* e = d.history.redo(d.doc.image)) {
        if (!e->metaAfter.is_null()) d.doc.applyMeta(e->metaAfter);
        touch(d);
        if (e->resized) d.texDirty = true;
        else markDirty(d, e->region);
        hooks_.say("Rétabli : " + e->label + ".", false);
    }
    const DocAnim* a = currentAnim(d);
    if (a) d.frame = std::clamp(d.frame, 0, a->count - 1);
}

void Studio::uploadTexture(OpenDoc& d) {
    const PixelImage& img = d.doc.image;
    if (img.empty()) return;
    if (!d.tex || d.tex->width() != img.w() || d.tex->height() != img.h()) {
        SDL_Texture* t = SDL_CreateTexture(g_.app.renderer().sdl(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, img.w(), img.h());
        if (!t) return;
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
        d.tex = std::make_shared<Texture>(t, img.w(), img.h(), "<studio " + d.doc.uid + ">");
        d.texDirty = true;
    }
    // Only the pixels changed since the last upload (the whole image after an import, an undo
    // of a frame added...), with the selection lifted where it is being moved.
    const IRect r = d.texDirty ? img.bounds() : d.texRect.intersect(img.bounds());
    d.texDirty = false;
    d.texRect = {};
    if (r.empty()) return;
    const SDL_Rect sr{r.x, r.y, r.w, r.h};
    if (d.lifted) {
        PixelImage part = img.crop(r);
        const IRect f = currentFrame(d);
        part.stamp(d.liftedPx, d.liftX - r.x, d.liftY - r.y, {f.x - r.x, f.y - r.y, f.w, f.h});
        SDL_UpdateTexture(d.tex->sdl(), &sr, part.data().data(), r.w * 4);
    } else {
        SDL_UpdateTexture(d.tex->sdl(), &sr, img.data().data() + (static_cast<size_t>(r.y) * static_cast<size_t>(img.w()) + static_cast<size_t>(r.x)) * 4,
                          img.w() * 4);
    }
}

TexturePtr Studio::textureOf(const PixelImage& img, const std::string& name) {
    if (img.empty()) return nullptr;
    SDL_Surface* s = SDL_CreateSurfaceFrom(img.w(), img.h(), SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(img.data().data()), img.w() * 4);
    if (!s) return nullptr;
    TexturePtr t = g_.app.renderer().textureFromSurface(s, name, true);
    SDL_DestroySurface(s);
    if (t) SDL_SetTextureScaleMode(t->sdl(), SDL_SCALEMODE_NEAREST);
    return t;
}

int Studio::frameAt(const DocAnim& a, float t, bool loop) const {
    float total = 0.0f;
    for (float d : a.durations) total += d;
    if (total <= 0.0f || a.durations.empty()) return 0;
    float local = loop ? std::fmod(t, total) : std::min(t, total - 0.0001f);
    for (size_t i = 0; i < a.durations.size(); ++i) {
        if (local < a.durations[i]) return static_cast<int>(i);
        local -= a.durations[i];
    }
    return static_cast<int>(a.durations.size()) - 1;
}

void Studio::drawNovaFrame(const Texture& sheet, const DocAnim& anim, int frame, const Rect& dst, Color tint, bool flip) {
    const Rect src{static_cast<float>(frame * kSuitFrame), static_cast<float>(anim.row * kSuitFrame), static_cast<float>(kSuitFrame),
                   static_cast<float>(kSuitFrame)};
    g_.app.renderer().drawTexture(sheet, &src, dst, tint, BlendMode::Alpha, flip);
}

// ------------------------------------------------------------------ the server

void Studio::setServerList(const Json& creations, bool loaded) {
    // Read where ADMINISTRATION › Créations keeps it (no copy each frame).
    serverList_ = creations.is_array() ? &creations : nullptr;
    const bool first = loaded && !serverLoaded_;
    serverLoaded_ = loaded;
    // The archived state follows the server (an archive done elsewhere).
    for (auto& d : docs_)
        for (const Json& c : serverList())
            if (d->doc.serverId > 0 && jget<int>(c, "id", 0) == d->doc.serverId) d->doc.archived = jget<bool>(c, "archived", false);
    if (first) loadAdminImages();
}

void Studio::loadAdminImages() {
    if (!g_.online.loggedIn()) return;
    g_.online.get("/api/v1/admin/creations/images", OnlineService::guard(life_, [this](const ApiResult& r) {
                      if (hooks_.lostRole(r)) return;
                      // A server before 2.61 (404 of an unknown route is lostRole's) or offline: no images.
                      if (!r.ok) return;
                      adminImages_.clear();
                      for (const Json& i : jfield(r.data, "images", Json::array()))
                          if (auto info = CreationImageInfo::fromJson(i)) adminImages_[info->creationId()] = std::move(*info);
                  }),
                  false);
}

void Studio::send() {
    OpenDoc* dp = active();
    if (!dp || sending_) return;
    OpenDoc& d = *dp;
    dropSelection(d);
    if (const std::string p = d.doc.sendProblem(novaAnims_); !p.empty()) return hooks_.say(p, true);
    if (!g_.online.loggedIn()) return hooks_.say("Connectez-vous avec un compte administrateur pour envoyer au serveur.", true);
    const std::vector<uint8_t> png = encodePng(d.doc.image.w(), d.doc.image.h(), d.doc.image.data());
    if (png.empty()) return hooks_.say("Image impossible à écrire en PNG.", true);
    if (png.size() > kCreationImageMaxBytes)
        return hooks_.say("Image trop lourde : " + std::to_string(png.size() / 1024) + " Ko en PNG (" + std::to_string(kCreationImageMaxBytes / 1024) +
                              " Ko au plus). Moins de couleurs différentes la rendent plus légère.",
                          true);
    // First the creation (its name and parameters, as ADMINISTRATION › Créations sends them), then its image.
    Json body = Json::object();
    const std::string kind = kindId(d.doc.kind);
    if (d.doc.kind == DocKind::Suit) body["colors"] = suitColorsToJson(d.doc.colors);
    else {
        CreationLook l = d.doc.look;
        l.kind = kind;
        body = creationLookToJson(l);
    }
    std::string name = d.doc.name;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    body["name"] = name;
    std::string path = "/api/v1/admin/creations";
    const bool isNew = d.doc.serverId == 0;
    if (isNew) body["kind"] = kind;
    else {
        path += "/" + std::to_string(d.doc.serverId);
        if (!d.doc.serverUpdatedAt.empty()) body["updated_at"] = d.doc.serverUpdatedAt;
    }
    sending_ = true;
    const std::string uid = d.doc.uid;
    g_.online.send(isNew ? "POST" : "PUT", path, body, OnlineService::guard(life_, [this, uid, png](const ApiResult& r) {
                       if (hooks_.lostRole(r)) {
                           sending_ = false;
                           return;
                       }
                       OpenDoc* dd = nullptr;
                       for (auto& x : docs_)
                           if (x->doc.uid == uid) dd = x.get();
                       if (!r.ok || !dd) {
                           sending_ = false;
                           std::string m = r.networkFailure() ? std::string("Connexion impossible : rien n'a changé sur le serveur.")
                                                              : r.message.empty() ? std::string("Refusé par le serveur.") : r.message;
                           if (r.code == "VALIDATION_ERROR" && r.details.is_array() && !r.details.empty())
                               m += " (" + jstr(r.details[0], "field") + " : " + jstr(r.details[0], "message") + ")";
                           return hooks_.say(m, true);
                       }
                       const Json& c = jfield(r.data, "creation", Json());
                       dd->doc.serverId = jget<int>(c, "id", dd->doc.serverId);
                       dd->doc.serverUpdatedAt = jstr(c, "updated_at", dd->doc.serverUpdatedAt);
                       sendImage(uid, png);
                   }));
}

void Studio::sendImage(const std::string& uid, const std::vector<uint8_t>& png) {
    OpenDoc* dd = nullptr;
    for (auto& x : docs_)
        if (x->doc.uid == uid) dd = x.get();
    if (!dd) {
        sending_ = false;
        return;
    }
    StudioDoc& doc = dd->doc;
    std::string q = "?";
    if (doc.kind != DocKind::Suit) {
        q += "frame_w=" + std::to_string(doc.frameW) + "&frame_h=" + std::to_string(doc.frameH) + "&frames=" + std::to_string(doc.stripFrames()) +
             "&fps=" + std::to_string(doc.fps);
        if (particleKind(doc.kind)) {
            const ImageEmitter& e = doc.emitter;
            q += "&rate=" + std::to_string(e.rate) + "&life_ms=" + std::to_string(e.lifeMs) + "&size=" + std::to_string(e.size) +
                 "&speed=" + std::to_string(e.speed) + "&spread=" + std::to_string(e.spread) + "&gravity=" + std::to_string(e.gravity) +
                 "&spin=" + std::to_string(e.spin) + "&fade=" + std::to_string(e.fade) + "&sparkles=" + (e.sparkles ? "1" : "0");
        }
    }
    const std::string path = "/api/v1/admin/creations/" + std::to_string(doc.serverId) + "/image" + (q.size() > 1 ? q : std::string());
    const std::string hash = Sha256::hex(png);
    g_.online.sendBytes("PUT", path, std::string(png.begin(), png.end()), OnlineService::guard(life_, [this, uid, hash](const ApiResult& r) {
                            sending_ = false;
                            if (hooks_.lostRole(r)) return;
                            OpenDoc* d = nullptr;
                            for (auto& x : docs_)
                                if (x->doc.uid == uid) d = x.get();
                            if (!r.ok) {
                                hooks_.say("Création enregistrée sur le serveur, mais son image est refusée : " +
                                               (r.message.empty() ? std::string("erreur inconnue.") : r.message),
                                           true);
                                hooks_.reloadList();
                                return;
                            }
                            if (d) {
                                d->doc.serverImageHash = jstr(jfield(r.data, "image", Json()), "hash", hash);
                                d->unsent = false;
                                d->sentAt = nowUtcIso();
                                saveDraft(*d, true);
                                hooks_.say("« " + d->doc.name + " » envoyée au serveur avec son image : tous les jeux la dessinent à leur prochaine lecture.", false);
                            }
                            hooks_.reloadList();
                            loadAdminImages();
                            g_.refreshServerSuits();
                        }),
                        30000);
}

// ------------------------------------------------------------------ small widgets

bool Studio::chip(const std::string& name, const Rect& r, const std::string& text, bool selected, bool enabled, Color accent) {
    Renderer& rr = ui_.r();
    bool hovered = false;
    const bool clicked = ui_.hotspot(name, r, &hovered) && enabled;
    const bool focused = ui_.focused(name);
    const Color acc = accent.a ? accent : kAccent;
    rr.fillRoundedRect(r, r.h * 0.5f, selected ? kSel : Color{10, 17, 36, 160});
    rr.drawRoundedRect(r, r.h * 0.5f, selected ? acc : (hovered || focused) ? acc.withAlpha(200) : (accent.a ? accent : kBorder),
                       std::max(1.0f, ui_.s(selected || focused ? 2.0f : 1.0f)));
    const float fs = ui_.fitFont(text, selected ? FontFace::Bold : FontFace::Body, fz(ui_, 13.0f), r.w - ui_.s(12), 0.6f);
    ui_.label(text, r.center(), fs, !enabled ? kDim : selected ? Color::white() : (accent.a ? accent : kSub), Align::Center,
              selected ? FontFace::Bold : FontFace::Body, VAlign::Middle);
    if (clicked) g_.uiSound("ui_move", 0.4f);
    return clicked;
}

bool Studio::smallButton(const std::string& name, const Rect& r, const std::string& text, bool enabled, bool primary, bool danger) {
    UIContext::ButtonStyle st;
    st.small = true;
    st.enabled = enabled;
    st.primary = primary;
    st.danger = danger;
    st.fontSize = ui_.fitFont(text, FontFace::Bold, fz(ui_, 14.0f), r.w - ui_.s(16), 0.55f);
    return ui_.button(name, r, text, st);
}

bool Studio::check(const std::string& name, const Rect& r, const std::string& text, bool& value) {
    Renderer& rr = ui_.r();
    bool hovered = false;
    const bool clicked = ui_.hotspot(name, r, &hovered);
    if (clicked) {
        value = !value;
        g_.uiSound("ui_move", 0.4f);
    }
    const bool focused = ui_.focused(name);
    if (focused || hovered) rr.fillRoundedRect(r, ui_.s(6), kSel.withAlpha(140));
    const float box = std::min(r.h - ui_.s(8), ui_.s(14));
    const Rect b{r.x + ui_.s(4), r.center().y - box * 0.5f, box, box};
    rr.drawRoundedRect(b, ui_.s(3), value ? kAccent : kDim, std::max(1.0f, ui_.s(1.5f)));
    if (value) {
        rr.drawLine({b.x + box * 0.2f, b.y + box * 0.55f}, {b.x + box * 0.42f, b.y + box * 0.78f}, kAccent, std::max(1.5f, ui_.s(2)));
        rr.drawLine({b.x + box * 0.42f, b.y + box * 0.78f}, {b.x + box * 0.82f, b.y + box * 0.25f}, kAccent, std::max(1.5f, ui_.s(2)));
    }
    fitLabel(ui_, text, {b.right() + ui_.s(6), r.center().y}, 13, r.right() - b.right() - ui_.s(8), value ? kAccent : kSub);
    return clicked;
}

bool Studio::slider(const std::string& name, const Rect& r, const std::string& label, int& value, int minV, int maxV, int step,
                    const std::string& valueText) {
    Renderer& rr = ui_.r();
    InputManager& in = ui_.input();
    bool hovered = false;
    ui_.hotspot(name, r, &hovered);
    const bool focused = ui_.focused(name);
    const float fs = fz(ui_, 13.5f);
    const float lineH = ui_.s(18);
    const float vw = ui_.text().measure(valueText, FontFace::Bold, ui_.s(fs)).x;
    fitLabel(ui_, label, {r.x, r.y + lineH * 0.5f}, 13.5f, r.w - vw - ui_.s(10), focused ? kText : kSub);
    ui_.label(valueText, {r.right(), r.y + lineH * 0.5f}, fs, kAccent, Align::Right, FontFace::Bold, VAlign::Middle);
    const Rect track{r.x, r.y + lineH + ui_.s(4), r.w, ui_.s(8)};
    int v = value;
    if (ui_.acceptsInput() && focused && !ui_.editingText()) {
        const int d = in.shift() ? std::max(1, step / 5) : step;
        if (in.menuPressed(Action::MenuLeft)) v -= d;
        if (in.menuPressed(Action::MenuRight)) v += d;
        ui_.consumeNavigation(true, false);
    }
    const int id = static_cast<int>(ui_.id(name) & 0x7FFFFFFF);
    if (ui_.acceptsInput() && hovered && in.mousePressed(SDL_BUTTON_LEFT)) dragSlider_ = id;
    if (dragSlider_ == id) {
        if (!in.mouseDown(SDL_BUTTON_LEFT)) dragSlider_ = -1;
        else {
            const float k = std::clamp((in.mousePos().x - track.x) / std::max(1.0f, track.w), 0.0f, 1.0f);
            v = minV + static_cast<int>(std::lround(k * static_cast<float>(maxV - minV)));
        }
    }
    v = std::clamp(v, minV, maxV);
    const bool changed = v != value;
    value = v;
    const float k = static_cast<float>(v - minV) / static_cast<float>(std::max(1, maxV - minV));
    rr.fillRoundedRect(track, track.h * 0.5f, kField);
    rr.fillRoundedRect({track.x, track.y, std::max(track.h, track.w * k), track.h}, track.h * 0.5f, kAccent);
    if (focused || hovered || dragSlider_ == id) rr.drawRoundedRect(track.expanded(ui_.s(3)), track.h, kAccent.withAlpha(150), std::max(1.0f, ui_.s(1)));
    return changed;
}

void Studio::tip(const std::string& name, const Rect& r, const std::string& title, const std::string& detail) {
    const bool hovered = ui_.usingPointer() && ui_.acceptsInput() && ui_.mouseOver(r);
    const bool focused = !ui_.usingPointer() && ui_.focused(name);
    if (!hovered && !focused) return;
    const std::string key = name + "|" + std::to_string(static_cast<int>(r.x)) + "|" + std::to_string(static_cast<int>(r.y));
    tipSeen_ = true;
    if (tipHover_ != key) {
        tipHover_ = key;
        tipSince_ = clock_;
    }
    if (clock_ - tipSince_ < (focused ? 0.2f : 0.45f)) return;
    tipKey_ = key;
    tipTitle_ = title;
    tipDetail_ = detail;
    tipRect_ = r;
}

void Studio::drawTip() {
    if (!tipSeen_) tipHover_.clear();
    if (tipKey_.empty()) return;
    Renderer& rr = ui_.r();
    const float pad = ui_.s(10), maxW = ui_.s(300);
    const float tfs = fz(ui_, 13.5f), dfs = fz(ui_, 13.0f);
    const float titleW = std::min(maxW, ui_.text().measure(tipTitle_, FontFace::Bold, ui_.s(tfs)).x);
    const float detailW = tipDetail_.empty() ? 0.0f : std::min(maxW, ui_.text().measure(tipDetail_, FontFace::Body, ui_.s(dfs)).x + ui_.s(2));
    const float w = std::max(titleW, detailW) + 2 * pad;
    const float th = ui_.s(tfs) * 1.3f;
    const float dh = tipDetail_.empty() ? 0.0f : ui_.text().measureWrapped(tipDetail_, FontFace::Body, ui_.s(dfs), w - 2 * pad).y + ui_.s(4);
    const float h = th + dh + 2 * pad - ui_.s(4);
    // At the right of the widget, else at its left, else under it; always on the screen.
    float x = tipRect_.right() + ui_.s(6), y = tipRect_.y;
    if (x + w > ui_.screenW() - ui_.s(8)) x = tipRect_.x - w - ui_.s(6);
    if (x < ui_.s(8)) {
        x = std::clamp(tipRect_.x, ui_.s(8), ui_.screenW() - w - ui_.s(8));
        y = tipRect_.bottom() + ui_.s(6);
    }
    y = std::clamp(y, ui_.s(8), ui_.screenH() - h - ui_.s(8));
    const Rect box{x, y, w, h};
    rr.fillRoundedRect(box, ui_.s(8), kTip);
    rr.drawRoundedRect(box, ui_.s(8), kAccent, std::max(1.0f, ui_.s(1)));
    fitLabel(ui_, tipTitle_, {x + pad, y + pad + th * 0.4f}, 13.5f, w - 2 * pad, kText, Align::Left, FontFace::Bold);
    if (!tipDetail_.empty()) ui_.labelWrapped(tipDetail_, {x + pad, y + pad + th - ui_.s(2)}, w - 2 * pad, dfs, kDim);
}

void Studio::drawSection(const Rect& r) {
    ui_.r().fillRoundedRect(r, ui_.s(12), kPanel);
    ui_.r().drawRoundedRect(r, ui_.s(12), kBorder, std::max(1.0f, ui_.s(1)));
}

float Studio::sectionTitle(const std::string& t, float x, float y, float w) {
    fitLabel(ui_, t, {x, y + ui_.s(9)}, 14, w, kDim, Align::Left, FontFace::Bold);
    return ui_.s(22);
}

void Studio::drawChecker(const Rect& r, float cell) {
    Renderer& rr = ui_.r();
    rr.fillRect(r, kCheckB);
    cell = std::max(4.0f, cell);
    rr.pushClip(r);
    int row = 0;
    for (float y = r.y; y < r.bottom(); y += cell, ++row) {
        int col = row & 1;
        for (float x = r.x; x < r.right(); x += cell, ++col)
            if (col & 1) rr.fillRect({x, y, cell, cell}, kCheckA);
    }
    rr.popClip();
}

void Studio::drawSwatch(const Rect& r, Px c, bool selected) {
    Renderer& rr = ui_.r();
    if (c.a < 255) drawChecker(r, r.h * 0.5f);
    rr.fillRoundedRect(r, ui_.s(5), Color{c.r, c.g, c.b, c.a});
    if (selected) rr.drawRoundedRect(r.expanded(ui_.s(2)), ui_.s(6), Color::white(), std::max(1.5f, ui_.s(2)));
}

// ------------------------------------------------------------------ drawing

bool Studio::modal() const { return importOpen_ || recolorOpen_ || confirmClose_ >= 0 || libraryOpen_; }

bool Studio::capturesBack() const {
    if (modal() || renaming_ || mirrorMenu_ || stroking_) return true;
    const OpenDoc* d = active();
    return d && (d->hasSel || d->lifted);
}

bool Studio::hasUnsaved() const {
    for (const auto& d : docs_)
        if (d->dirty) return true;
    return false;
}

void Studio::open(const std::string& arg) { wantOpen_ = arg; }

void Studio::draw(const Rect& bar, const Rect& body) {
    const float now = ui_.time();
    dt_ = clock_ < 0.0f ? 0.0f : std::clamp(now - clock_, 0.0f, 0.1f);
    clock_ = now;
    if (!draftsLoaded_) loadDrafts();
    if (!sessionLoaded_) loadSession();
    if (!libraryLoaded_) loadLibrary();
    if (!wantOpen_.empty()) {
        const std::string w = wantOpen_;
        wantOpen_.clear();
        // Automated screenshots: "new:<kind>", "import[:<file>]", "draw", a draft's uid.
        if (w.rfind("new:", 0) == 0) {
            if (auto k = kindFromId(w.substr(4))) newDoc(*k);
        } else if (w.rfind("import", 0) == 0) {
            if (docs_.empty()) newDoc(DocKind::Suit);
            openImport(w.size() > 7 ? Paths::fromUtf8(w.substr(7)) : fs::path());
        } else {
            openDraft(w);
        }
    }
    tipKey_.clear();
    tipSeen_ = false;
    if (clock_ - importScanT_ > 3.0f) scanImportFolder(false);
    if (!modal()) handleShortcuts();
    ui_.pushId("studio");
    drawTopBar(bar);
    const float gap = ui_.s(12);
    const float exW = std::clamp(ui_.s(250), 190.0f, body.w * 0.2f);
    const float rightW = std::clamp(ui_.s(300), 230.0f, body.w * 0.24f);
    drawExplorer({body.x, body.y, exW, body.h});
    const Rect center{body.x + exW + gap, body.y, body.w - exW - rightW - 2 * gap, body.h};
    const Rect right{center.right() + gap, body.y, rightW, body.h};
    const float tabsH = ui_.s(36);
    drawTabs({center.x, center.y, center.w, tabsH});
    const Rect work{center.x, center.y + tabsH + ui_.s(8), center.w, center.h - tabsH - ui_.s(8)};
    if (!active()) {
        drawEmpty(work);
    } else {
        const float friseH = std::clamp(ui_.s(150), 118.0f, work.h * 0.3f);
        const Rect top{work.x, work.y, work.w, work.h - friseH - ui_.s(8)};
        const float railW = ui_.s(52);
        drawRail({top.x, top.y, railW, top.h});
        drawCanvasPanel({top.x + railW + ui_.s(8), top.y, top.w - railW - ui_.s(8), top.h});
        drawFrise({work.x, work.bottom() - friseH, work.w, friseH});
    }
    drawRight(right);
    ui_.popId();
    if (!modal()) drawTip();
}

void Studio::drawEmpty(const Rect& r) {
    drawSection(r);
    const float w = std::min(r.w - ui_.s(48), ui_.s(620));
    const std::string t =
        "Aucune création ouverte. Choisissez-en une dans l'explorateur (celles du serveur et vos brouillons), « + Nouvelle » pour en "
        "commencer une, ou « Importer… » pour découper une planche d'images : glissez un PNG dans la fenêtre, ou déposez-le dans le dossier "
        "« Images à importer » du jeu.";
    const float h = ui_.text().measureWrapped(t, FontFace::Body, ui_.s(fz(ui_, 16)), w).y;
    ui_.labelWrapped(t, {r.center().x - w * 0.5f, r.center().y - h * 0.5f - ui_.s(30)}, w, fz(ui_, 16), kSub);
    const std::vector<std::string> labels = {"+ Nouvelle combinaison", "Importer…"};
    float bw = 0.0f;
    for (const auto& l : labels) bw = std::max(bw, ui_.text().measure(l, FontFace::Bold, ui_.s(fz(ui_, 15))).x + ui_.s(36));
    bw = std::min(bw, (w - ui_.s(12)) * 0.5f);
    const float by = r.center().y + h * 0.5f - ui_.s(10);
    if (smallButton("empty_new", {r.center().x - bw - ui_.s(6), by, bw, ui_.s(40)}, labels[0], true, true)) newDoc(DocKind::Suit);
    if (smallButton("empty_import", {r.center().x + ui_.s(6), by, bw, ui_.s(40)}, labels[1])) openImport({});
}

void Studio::drawTopBar(const Rect& r) {
    OpenDoc* d = active();
    ui_.pushId("bar");
    const std::vector<std::string> labels = {"Enregistrer · Ctrl+S", "Envoyer au serveur", "Donner en récompense…"};
    const float gap = ui_.s(8);
    std::vector<float> widths;
    float total = 0.0f;
    for (const auto& l : labels) {
        widths.push_back(ui_.text().measure(l, FontFace::Bold, ui_.s(fz(ui_, 14))).x + ui_.s(32));
        total += widths.back() + gap;
    }
    const float room = r.w * 0.68f;
    if (total > room)
        for (float& w : widths) w *= room / total;
    float x = r.right();
    std::vector<Rect> rects(3);
    for (int i = 2; i >= 0; --i) {
        x -= widths[static_cast<size_t>(i)];
        rects[static_cast<size_t>(i)] = {x, r.y + (r.h - ui_.s(36)) * 0.5f, widths[static_cast<size_t>(i)], ui_.s(36)};
        x -= gap;
    }
    // The state of the open creation, in the room left.
    std::string state;
    if (sending_) state = "Envoi au serveur…";
    else if (!d) state = "Studio : dessinez les images des créations ; elles partent au serveur avec « Envoyer au serveur ».";
    else {
        state = d->dirty ? std::string("Modifications non enregistrées")
                         : d->doc.savedAt.empty() ? std::string("Pas encore enregistré sur ce PC")
                                                  : "Brouillon enregistré sur ce PC à " + clockOf(d->doc.savedAt);
        state += d->doc.serverId == 0 ? " · pas encore envoyé au serveur" : d->unsent ? " · modifié depuis l'envoi au serveur" : " · à jour sur le serveur";
        if (d->loadingImage) state += " · téléchargement de son image…";
    }
    fitLabel(ui_, state, {r.x, r.center().y}, 14, x - r.x - ui_.s(12), kDim);
    const bool canSave = d != nullptr;
    if (smallButton("save", rects[0], labels[0], canSave) && d) saveDraft(*d);
    tip("save", rects[0], "Enregistrer le brouillon · Ctrl+S", "Gardé sur ce PC (dossier utilisateur du jeu, studio/brouillons) : rien ne part au serveur.");
    std::string why = !d ? "Ouvrez une création d'abord." : !g_.online.loggedIn() ? "Connectez-vous avec un compte administrateur." : d->doc.sendProblem(novaAnims_);
    if (sending_) why = "Envoi en cours…";
    if (smallButton("send", rects[1], labels[1], why.empty(), true)) send();
    tip("send", rects[1], "Envoyer au serveur",
        why.empty() ? "Crée ou met à jour la création sur Cosmic Online, puis envoie son image ; le serveur vérifie tout et le note au Journal."
                    : why);
    std::string giveWhy = !d ? "Ouvrez une création d'abord." : d->doc.serverId == 0 ? "Envoyez-la d'abord au serveur." : d->doc.archived ? "Archivée : restaurez-la d'abord." : "";
    if (smallButton("give", rects[2], labels[2], giveWhy.empty() && !sending_) && d && hooks_.giveReward)
        hooks_.giveReward(std::string("srv") + kindId(d->doc.kind) + ":" + std::to_string(d->doc.serverId), d->doc.name);
    tip("give", rects[2], "Donner en récompense…",
        giveWhy.empty() ? "Ouvre le Calendrier des défis : choisissez un défi à venir, puis « Récompenses »." : giveWhy);
    ui_.popId();
}

// ------------------------------------------------------------------ explorer

void Studio::drawExplorer(const Rect& r) {
    Renderer& rr = ui_.r();
    drawSection(r);
    ui_.pushId("explorer");
    const float pad = ui_.s(12);
    float y = r.y + pad;
    const float w = r.w - 2 * pad, x = r.x + pad;
    y += sectionTitle("EXPLORATEUR", x, y, w);
    fitLabel(ui_, "Rechercher", {x, y + ui_.s(7)}, 12, w, kDim);
    y += ui_.s(16);
    ui_.textField("search", {x, y, w, ui_.s(32)}, search_, "Nom, couleur, type…", 40);
    y += ui_.s(32) + ui_.s(8);
    // The buttons at the bottom, measured first (two rows), then the tree between.
    const float bh = ui_.s(34), bgap = ui_.s(6);
    const float buttonsY = r.bottom() - pad - bh * 2 - bgap;
    const Rect tree{x - ui_.s(4), y, w + ui_.s(8), buttonsY - ui_.s(8) - y};

    struct Entry {
        std::string key;
        DocKind kind;
        std::string name;
        Px a, b;
        bool dirty = false, archived = false, draft = false, image = false, reserve = false;
    };
    std::vector<Entry> entries;
    const std::string q = lower(search_);
    auto matches = [&](const std::string& name, DocKind k, Px a, Px b) {
        if (q.empty()) return true;
        return lower(name).find(q) != std::string::npos || lower(kindName(k)).find(q) != std::string::npos || hexOf(a).find(q) != std::string::npos ||
               hexOf(b).find(q) != std::string::npos;
    };
    auto swatches = [](const StudioDoc& d, Px& a, Px& b) {
        if (d.kind == DocKind::Suit) {
            a = pxOf(d.colors.suit);
            b = pxOf(d.colors.accent);
        } else {
            a = pxOf(d.look.a);
            b = pxOf(d.look.b);
        }
    };
    std::vector<int> represented;
    for (const auto& od : docs_) {
        Entry e{"o:" + od->doc.uid, od->doc.kind, od->doc.name};
        swatches(od->doc, e.a, e.b);
        e.dirty = od->dirty;
        e.archived = od->doc.archived;
        e.draft = od->doc.serverId == 0;
        if (od->doc.serverId > 0) represented.push_back(od->doc.serverId);
        if (matches(e.name, e.kind, e.a, e.b) && (showArchives_ || !e.archived)) entries.push_back(e);
    }
    auto isOpen = [this](const std::string& uid) {
        for (const auto& od : docs_)
            if (od->doc.uid == uid) return true;
        return false;
    };
    for (const StudioDoc& dr : drafts_) {
        if (isOpen(dr.uid)) continue;
        Entry e{"d:" + dr.uid, dr.kind, dr.name};
        swatches(dr, e.a, e.b);
        e.draft = dr.serverId == 0;
        e.archived = dr.archived;
        if (dr.serverId > 0) represented.push_back(dr.serverId);
        if (matches(e.name, e.kind, e.a, e.b) && (showArchives_ || !e.archived)) entries.push_back(e);
    }
    if (showArchives_)
        for (const StudioDoc& dr : archived_) {
            Entry e{"d:" + dr.uid, dr.kind, dr.name + " (brouillon archivé)"};
            swatches(dr, e.a, e.b);
            e.archived = true;
            e.draft = true;
            if (matches(dr.name, e.kind, e.a, e.b)) entries.push_back(e);
        }
    for (const Json& c : serverList()) {
        const int id = jget<int>(c, "id", 0);
        if (std::find(represented.begin(), represented.end(), id) != represented.end()) continue;
        auto k = kindFromId(jstr(c, "kind", "suit"));
        if (!k) continue;
        Entry e{"s:" + std::to_string(id), *k, jstr(c, "name")};
        if (*k == DocKind::Suit) {
            const SuitColors s = suitColorsFromJson(jfield(c, "colors", Json())).value_or(SuitColors{});
            e.a = pxOf(s.suit);
            e.b = pxOf(s.accent);
        } else if (auto l = creationLookFromJson(kindId(*k), c)) {
            e.a = pxOf(l->a);
            e.b = pxOf(l->b);
        }
        e.archived = jget<bool>(c, "archived", false);
        // 2.60: a creation brought by the game (the reserve), listed apart.
        e.reserve = jget<bool>(c, "game_reserve", false);
        e.image = imageInfoOf(id) != nullptr;
        if (matches(e.name, e.kind, e.a, e.b) && (showArchives_ || !e.archived)) entries.push_back(e);
    }

    // The tree: the four kinds, then the library.
    const float rowH = ui_.s(26);
    int newFiles = 0;
    for (const ImportFile& f : importFiles_) newFiles += f.fresh && f.problem.empty() ? 1 : 0;
    std::vector<int> counts(kDocKinds, 0), reserves(kDocKinds, 0);
    for (const Entry& e : entries) (e.reserve ? reserves : counts)[static_cast<size_t>(e.kind)]++;
    float content = 0.0f;
    for (int k = 0; k < kDocKinds; ++k) {
        content += rowH;
        if (!collapsed_[k] || !q.empty()) content += rowH * static_cast<float>(counts[static_cast<size_t>(k)] + (reserves[static_cast<size_t>(k)] > 0 ? 1 : 0) +
                                                                                 (showReserve_[k] ? reserves[static_cast<size_t>(k)] : 0));
    }
    content += rowH * (collapsed_[4] ? 1 : 4) + rowH;  // the library, « Archives »
    ui_.beginScroll("tree", tree, content);
    float ty = tree.y - ui_.scrollOffset();
    const bool scrolls = content > tree.h;
    const float tw = tree.w - (scrolls ? ui_.s(14) : 0.0f);
    auto groupRow = [&](int k, const std::string& title, int count) {
        const Rect row{tree.x, ty, tw, rowH};
        bool hovered = false;
        if (ui_.hotspot("g" + std::to_string(k), row, &hovered)) {
            collapsed_[k] = !collapsed_[k];
            if (k < kDocKinds) selKind_ = k;
        }
        if (hovered || ui_.focused("g" + std::to_string(k))) rr.fillRoundedRect(row, ui_.s(6), kSel.withAlpha(120));
        const bool open = !collapsed_[k] || (!q.empty() && k < kDocKinds);
        const Vec2 c{row.x + ui_.s(10), row.center().y};
        const float s = ui_.s(4.5f);
        if (open) rr.fillTriangle({c.x - s, c.y - s * 0.6f}, {c.x + s, c.y - s * 0.6f}, {c.x, c.y + s * 0.8f}, kText);
        else rr.fillTriangle({c.x - s * 0.6f, c.y - s}, {c.x - s * 0.6f, c.y + s}, {c.x + s * 0.8f, c.y}, kText);
        const std::string n = count >= 0 ? std::to_string(count) : std::string();
        const float nw = n.empty() ? 0.0f : ui_.text().measure(n, FontFace::Body, ui_.s(fz(ui_, 14))).x + ui_.s(8);
        fitLabel(ui_, title, {row.x + ui_.s(22), row.center().y}, 14.5f, row.w - ui_.s(26) - nw, k == selKind_ ? kAccent : kText, Align::Left, FontFace::Bold);
        if (!n.empty()) ui_.label(n, {row.right() - ui_.s(4), row.center().y}, fz(ui_, 14), kDim, Align::Right, FontFace::Body, VAlign::Middle);
        ty += rowH;
        return open;
    };
    auto entryRow = [&](const Entry& e) {
        const Rect row{tree.x + ui_.s(10), ty, tw - ui_.s(10), rowH};
        ty += rowH;
        bool hovered = false;
        const bool selected = selKey_ == e.key;
        if (ui_.hotspot(e.key, row, &hovered)) {
            selKey_ = e.key;
            selKind_ = static_cast<int>(e.kind);
            renaming_ = false;
            if (e.key[0] == 'o') {
                for (size_t i = 0; i < docs_.size(); ++i)
                    if ("o:" + docs_[i]->doc.uid == e.key) active_ = static_cast<int>(i);
            } else if (e.key[0] == 'd') {
                openDraft(e.key.substr(2));
            } else {
                for (const Json& c : serverList())
                    if ("s:" + std::to_string(jget<int>(c, "id", 0)) == e.key) openServer(c);
            }
            if (active()) selKey_ = "o:" + active()->doc.uid;
        }
        const bool isActive = active() && e.key == "o:" + active()->doc.uid;
        if (isActive || selected) rr.fillRoundedRect(row, ui_.s(6), kSelRow);
        else if (hovered || ui_.focused(e.key)) rr.fillRoundedRect(row, ui_.s(6), kSel.withAlpha(120));
        const float sw = ui_.s(10);
        drawSwatch({row.x + ui_.s(8), row.center().y - sw * 0.5f, sw, sw}, e.a);
        drawSwatch({row.x + ui_.s(8) + sw + ui_.s(4), row.center().y - sw * 0.5f, sw, sw}, e.b);
        float nx = row.x + ui_.s(8) + 2 * sw + ui_.s(10);
        float right = row.right() - ui_.s(6);
        if (e.dirty) {
            rr.fillCircle({right - ui_.s(4), row.center().y}, ui_.s(3.5f), kWarm);
            right -= ui_.s(14);
        }
        const std::string tag = e.archived ? "archivée" : e.draft ? "brouillon" : e.image ? "image" : "";
        if (!tag.empty()) {
            const float tw2 = ui_.text().measure(tag, FontFace::Body, ui_.s(fz(ui_, 11.5f))).x;
            if (right - tw2 - nx > ui_.s(60)) {
                ui_.label(tag, {right, row.center().y}, fz(ui_, 11.5f), e.archived ? kWarm : kDim, Align::Right, FontFace::Body, VAlign::Middle);
                right -= tw2 + ui_.s(8);
            }
        }
        {
            i18n::Verbatim names;  // the names the administrators gave
            fitLabel(ui_, e.name.empty() ? std::string("Sans nom") : e.name, {nx, row.center().y}, 14, right - nx,
                     isActive || selected ? Color::white() : kSub);
        }
    };
    for (int k = 0; k < kDocKinds; ++k) {
        const int count = counts[static_cast<size_t>(k)] + reserves[static_cast<size_t>(k)];
        if (!groupRow(k, kGroupNames[k], count)) continue;
        for (const Entry& e : entries)
            if (static_cast<int>(e.kind) == k && !e.reserve) entryRow(e);
        const int nres = reserves[static_cast<size_t>(k)];
        if (nres > 0) {
            const Rect row{tree.x + ui_.s(10), ty, tw - ui_.s(10), rowH};
            bool hovered = false;
            if (ui_.hotspot("res" + std::to_string(k), row, &hovered)) showReserve_[k] = !showReserve_[k];
            if (hovered || ui_.focused("res" + std::to_string(k))) rr.fillRoundedRect(row, ui_.s(6), kSel.withAlpha(120));
            fitLabel(ui_, showReserve_[k] ? "Réserve du jeu (masquer)" : "+ " + std::to_string(nres) + " autres · réserve du jeu", {row.x + ui_.s(8), row.center().y}, 13,
                     row.w - ui_.s(12), kDim);
            ty += rowH;
            if (showReserve_[k])
                for (const Entry& e : entries)
                    if (static_cast<int>(e.kind) == k && e.reserve) entryRow(e);
        }
    }
    if (groupRow(4, "Bibliothèque", -1)) {
        auto libRow = [&](const std::string& id, const std::string& text, int badge) {
            const Rect row{tree.x + ui_.s(10), ty, tw - ui_.s(10), rowH};
            ty += rowH;
            bool hovered = false;
            const bool clicked = ui_.hotspot(id, row, &hovered);
            if (hovered || ui_.focused(id)) rr.fillRoundedRect(row, ui_.s(6), kSel.withAlpha(120));
            float right = row.right() - ui_.s(4);
            if (badge > 0) {
                const std::string b = std::to_string(badge) + (badge > 1 ? " nouvelles" : " nouvelle");
                const float bw = ui_.text().measure(b, FontFace::Bold, ui_.s(fz(ui_, 11.5f))).x + ui_.s(12);
                const Rect pill{right - bw, row.center().y - ui_.s(9), bw, ui_.s(18)};
                rr.fillRoundedRect(pill, pill.h * 0.5f, kWarm);
                ui_.label(b, pill.center(), fz(ui_, 11.5f), Color{26, 18, 0, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
                right = pill.x - ui_.s(6);
            }
            fitLabel(ui_, text, {row.x + ui_.s(8), row.center().y}, 14, right - row.x - ui_.s(8), kSub);
            return clicked;
        };
        if (libRow("lib_models", "Sprites du jeu (modèles)", 0)) {
            libraryOpen_ = true;
            libraryTab_ = 0;
        }
        if (libRow("lib_palettes", "Palettes", 0)) {
            libraryOpen_ = true;
            libraryTab_ = 1;
        }
        if (libRow("lib_import", "Images à importer", newFiles)) openImport({});
        ui_.newBadge("lib_import", {tree.x + ui_.s(10), ty - rowH, tw - ui_.s(10), rowH}, "admin.creations.studio.import", newFiles == 0, true);
    }
    {
        const Rect row{tree.x, ty, tw, rowH};
        check("archives", row, "Archives (serveur et brouillons)", showArchives_);
        ty += rowH;
    }
    ui_.endScroll();

    // « + Nouvelle », « Importer… », then the selected entry's actions (or its new name).
    const float half = (w - bgap) * 0.5f;
    const std::string kindNew = std::string("+ ") + (selKind_ == 0 ? "Nouvelle" : selKind_ == 1 ? "Nouvelle" : selKind_ == 2 ? "Nouveau" : "Nouvel");
    if (smallButton("new", {x, buttonsY, half, bh}, kindNew, true, true)) newDoc(static_cast<DocKind>(std::clamp(selKind_, 0, kDocKinds - 1)));
    tip("new", {x, buttonsY, half, bh}, std::string(kNewNames[std::clamp(selKind_, 0, kDocKinds - 1)]) + " · Ctrl+N",
        "Du type du groupe choisi dans l'explorateur. Une combinaison part de la planche de Nova.");
    if (smallButton("import", {x + half + bgap, buttonsY, half, bh}, "Importer…")) openImport({});
    tip("import", {x + half + bgap, buttonsY, half, bh}, "Importer une image · Ctrl+I",
        "Un PNG glissé dans la fenêtre, choisi, ou déposé dans le dossier « Images à importer » ; découpé en images avant d'être ajouté.");
    const float y2 = buttonsY + bh + bgap;
    if (renaming_) {
        const float ok = ui_.s(44);
        if (focusRename_) {
            focusRename_ = false;
            ui_.editText("rename");
        }
        ui_.textField("rename", {x, y2, w - ok - bgap, bh}, renameText_, "Nouveau nom", 64);
        const bool done = smallButton("rename_ok", {x + w - ok, y2, ok, bh}, "OK", true, true) ||
                          (ui_.focused("rename") && !ui_.editingText() && ui_.input().keyPressed(SDL_SCANCODE_RETURN));
        if (done) {
            if (OpenDoc* d = active()) {
                std::string n = renameText_;
                while (!n.empty() && n.back() == ' ') n.pop_back();
                if (!n.empty() && n != d->doc.name) {
                    const Json before = d->doc.metaJson();
                    d->doc.name = n;
                    touch(*d);
                    (void)before;
                    hooks_.say(d->doc.serverId > 0 ? "Renommée : le nouveau nom part au serveur avec « Envoyer au serveur »." : "Renommée.", false);
                }
            }
            renaming_ = false;
        }
    } else {
        const float third = (w - 2 * bgap) / 3.0f;
        const bool any = !selKey_.empty() || active();
        if (smallButton("dup", {x, y2, third, bh}, "Dupliquer", any)) duplicateSelected();
        tip("dup", {x, y2, third, bh}, "Dupliquer", "Une copie, ouverte comme nouveau brouillon (jamais envoyée tant que vous ne l'envoyez pas).");
        if (smallButton("ren", {x + third + bgap, y2, third, bh}, "Renommer", any)) startRename();
        tip("ren", {x + third + bgap, y2, third, bh}, "Renommer · F2", "Le nouveau nom part au serveur avec « Envoyer au serveur ».");
        bool archivedSel = false;
        for (const StudioDoc& a : archived_)
            if (selKey_ == "d:" + a.uid) archivedSel = true;
        if (OpenDoc* d = active(); d && selKey_ == "o:" + d->doc.uid) archivedSel = archivedSel || d->doc.archived;
        for (const Json& c : serverList())
            if (selKey_ == "s:" + std::to_string(jget<int>(c, "id", 0))) archivedSel = jget<bool>(c, "archived", false);
        if (smallButton("arch", {x + 2 * (third + bgap), y2, third, bh}, archivedSel ? "Restaurer" : "Archiver", any && !sending_)) archiveSelected();
        tip("arch", {x + 2 * (third + bgap), y2, third, bh}, archivedSel ? "Restaurer" : "Archiver",
            "Une création du serveur n'est plus proposée en récompense (ceux qui l'ont la gardent) ; un brouillon va dans les archives de ce PC.");
    }
    ui_.popId();
}

// ------------------------------------------------------------------ tabs

void Studio::drawTabs(const Rect& r) {
    Renderer& rr = ui_.r();
    ui_.pushId("tabs");
    const float plusW = ui_.s(32), gap = ui_.s(4);
    const float room = r.w - plusW - gap;
    const int n = static_cast<int>(docs_.size());
    std::vector<float> widths;
    float total = 0.0f;
    for (const auto& d : docs_) {
        const float nw = ui_.text().measure(d->doc.name.empty() ? std::string("Sans nom") : d->doc.name, FontFace::Bold, ui_.s(fz(ui_, 14))).x;
        widths.push_back(nw + ui_.s(24) + ui_.s(14) + ui_.s(22));  // name, the dot, the cross
        total += widths.back() + gap;
    }
    if (total > room && n > 0)
        for (float& w : widths) w = std::max(ui_.s(84), w * room / total);
    float x = r.x;
    rr.pushClip({r.x, r.y - ui_.s(2), room, r.h + ui_.s(4)});
    for (int i = 0; i < n; ++i) {
        OpenDoc& d = *docs_[static_cast<size_t>(i)];
        const bool act = i == active_;
        const float h = act ? r.h : r.h - ui_.s(4);
        const Rect tab{x, r.bottom() - h, widths[static_cast<size_t>(i)], h};
        x += tab.w + gap;
        const Rect cross{tab.right() - ui_.s(24), tab.y, ui_.s(22), tab.h};
        bool hovered = false;
        const std::string id = "t" + d.doc.uid;
        const bool closeHit = ui_.mouseClick("x" + d.doc.uid, cross);
        if (ui_.hotspot(id, tab, &hovered) && !closeHit) {
            active_ = i;
            selKind_ = static_cast<int>(d.doc.kind);
            selKey_ = "o:" + d.doc.uid;
        }
        if (closeHit) {
            closeTab(i, false);
            break;
        }
        rr.fillRoundedRect(tab, ui_.s(8), act ? Color{19, 29, 58, 255} : Color{12, 19, 40, 255});
        rr.drawRoundedRect(tab, ui_.s(8), act ? kAccent : (hovered || ui_.focused(id)) ? kAccent.withAlpha(150) : kBorder, std::max(1.0f, ui_.s(act ? 1.5f : 1.0f)));
        float right = cross.x;
        if (d.dirty) {
            rr.fillCircle({right - ui_.s(5), tab.center().y}, ui_.s(3.5f), kWarm);
            right -= ui_.s(14);
        }
        {
            i18n::Verbatim names;
            fitLabel(ui_, d.doc.name.empty() ? std::string("Sans nom") : d.doc.name, {tab.x + ui_.s(12), tab.center().y}, 14, right - tab.x - ui_.s(16),
                     act ? Color::white() : kSub, Align::Left, act ? FontFace::Bold : FontFace::Body);
        }
        const Vec2 c = cross.center();
        const float s = ui_.s(4);
        const Color cc = ui_.mouseOver(cross) ? kText : kDim;
        rr.drawLine({c.x - s, c.y - s}, {c.x + s, c.y + s}, cc, std::max(1.2f, ui_.s(1.6f)));
        rr.drawLine({c.x - s, c.y + s}, {c.x + s, c.y - s}, cc, std::max(1.2f, ui_.s(1.6f)));
        tip(id, tab, d.doc.name, std::string(kindName(d.doc.kind)) + (d.dirty ? " · non enregistrée (●)" : "") + " · Ctrl+W ferme l'onglet");
    }
    rr.popClip();
    const Rect plus{r.x + room + gap, r.bottom() - r.h + ui_.s(4), plusW, r.h - ui_.s(4)};
    bool hovered = false;
    if (ui_.hotspot("plus", plus, &hovered)) newDoc(static_cast<DocKind>(std::clamp(selKind_, 0, kDocKinds - 1)));
    const Vec2 c = plus.center();
    const float s = ui_.s(6);
    const Color pc = hovered || ui_.focused("plus") ? kAccent : kDim;
    rr.drawLine({c.x - s, c.y}, {c.x + s, c.y}, pc, std::max(1.5f, ui_.s(2)));
    rr.drawLine({c.x, c.y - s}, {c.x, c.y + s}, pc, std::max(1.5f, ui_.s(2)));
    tip("plus", plus, std::string(kNewNames[std::clamp(selKind_, 0, kDocKinds - 1)]) + " · Ctrl+N");
    ui_.popId();
}

// ------------------------------------------------------------------ keyboard

void Studio::handleShortcuts() {
    if (!ui_.acceptsInput() || ui_.editingText()) return;
    InputManager& in = ui_.input();
    OpenDoc* d = active();
    const bool ctrl = in.ctrl();
    if (ctrl) {
        if (in.shortcutPressed(SDLK_S) && d) saveDraft(*d);
        if (in.shortcutPressed(SDLK_Z) && d) {
            if (in.shift()) redo(*d);
            else undo(*d);
        }
        if (in.shortcutPressed(SDLK_Y) && d) redo(*d);
        if (in.shortcutPressed(SDLK_N)) newDoc(static_cast<DocKind>(std::clamp(selKind_, 0, kDocKinds - 1)));
        if (in.shortcutPressed(SDLK_I)) openImport({});
        if (in.shortcutPressed(SDLK_W) && d) closeTab(active_, false);
        if (in.shortcutPressed(SDLK_G) && d) d->grid = !d->grid;
        if (in.shortcutPressed(SDLK_P) && d) d->onion = !d->onion;
        if (in.shortcutPressed(SDLK_TAB) && !docs_.empty()) active_ = (active_ + (in.shift() ? -1 : 1) + static_cast<int>(docs_.size())) % static_cast<int>(docs_.size());
        return;
    }
    if (in.keyPressed(SDL_SCANCODE_F2)) startRename();
    if (!d) return;
    struct Key {
        SDL_Keycode k;
        Tool t;
    };
    static const Key keys[] = {{SDLK_B, Tool::Pencil}, {SDLK_E, Tool::Eraser}, {SDLK_G, Tool::Fill},    {SDLK_I, Tool::Picker}, {SDLK_L, Tool::Line},
                               {SDLK_R, Tool::Rect},   {SDLK_O, Tool::Ellipse}, {SDLK_M, Tool::Select}, {SDLK_V, Tool::Move}};
    for (const Key& k : keys)
        if (in.shortcutPressed(k.k)) {
            if (tool_ != k.t) dropSelection(*d);
            tool_ = k.t;
        }
    if (in.shortcutPressed(SDLK_H)) mirror(*d, in.shift());
    if (in.shortcutPressed(SDLK_S)) d->sym = static_cast<Symmetry>((static_cast<int>(d->sym) + 1) % 4);
    if (in.shortcutPressed(SDLK_X)) std::swap(color1_, color2_);
    if (in.shortcutPressed(SDLK_P)) {
        d->playing = !d->playing;
        d->playClock = 0.0f;
    }
    if (in.shortcutPressed(SDLK_COMMA) || in.shortcutPressed(SDLK_PERIOD)) {
        if (const DocAnim* a = currentAnim(*d)) {
            dropSelection(*d);
            d->frame = (d->frame + (in.shortcutPressed(SDLK_COMMA) ? -1 : 1) + a->count) % a->count;
        }
    }
    if (in.keyPressed(SDL_SCANCODE_DELETE) && d->hasSel) {
        dropSelection(*d);
        const IRect cut = d->sel.intersect(currentFrame(*d));
        beginEdit(*d, cut);
        d->doc.image.fill(cut, Px{});
        markDirty(*d, cut);
        commit(*d, "Effacer la sélection");
        d->hasSel = false;
    }
    if (in.keyPressed(SDL_SCANCODE_ESCAPE)) {
        if (stroking_) {
            stroking_ = false;
            cancelEdit(*d);
        } else if (mirrorMenu_) {
            mirrorMenu_ = false;
        } else if (d->lifted || d->hasSel) {
            dropSelection(*d);
            d->hasSel = false;
        } else if (renaming_) {
            renaming_ = false;
        }
    }
}

void Studio::dropFile(const std::string& path) { openImport(Paths::fromUtf8(path)); }

}  // namespace cosmic::studio
