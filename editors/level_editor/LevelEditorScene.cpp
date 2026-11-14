// Level editor: document management, history, preview world, test play, frame layout.

#include "editors/level_editor/LevelEditorScene.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/GameServices.h"
#include "engine/core/FileUtil.h"
#include "packaging/Sha256.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/Thumbnail.h"

namespace cosmic {

std::unique_ptr<Scene> makeLevelEditor(GameServices& g, const std::string& levelId) {
    return std::make_unique<LevelEditorScene>(g, levelId);
}

std::unique_ptr<Scene> makeLevelEditorHelp(GameServices& g, const std::string& levelId) {
    auto s = std::make_unique<LevelEditorScene>(g, levelId);
    s->openHelpOnEnter = true;
    return s;
}

LevelEditorScene::LevelEditorScene(GameServices& g, std::string levelId) : g_(g), requestedId_(std::move(levelId)) {
    props_.onPickObject = [this](const std::string& key) {
        pickTarget_ = key;
        flash("Cliquez sur un objet du niveau pour l'ajouter (Échap pour annuler)");
    };
}

LevelEditorScene::~LevelEditorScene() = default;

std::unique_ptr<Scene> makeLevelEditorLive(GameServices& g, const std::string& levelId, const std::string& projectId, const std::string& role) {
    auto s = std::make_unique<LevelEditorScene>(g, levelId);
    s->goLive(projectId, role);
    return s;
}

void LevelEditorScene::goLive(const std::string& projectId, const std::string& role) {
    liveProject_ = projectId;
    liveRole_ = role;
}

void LevelEditorScene::onEnter() {
    g_.app.audio().stopMusic(500);
    g_.app.audio().stopAmbience(500);
    if (!requestedId_.empty() && g_.content.level(requestedId_)) {
        openLevel(requestedId_);
    } else {
        loadDocument(LevelData::makeEmpty("nouveau_niveau", "Nouveau niveau", "sylvanox"), false);
        modal_ = Modal::Open;
    }
    loadFavorites();
    explorerOpen_.insert(activeLayer_);
    if (!liveProject_.empty()) {
        // Live editing: the room's document replaces the local copy once joined.
        live_ = std::make_unique<LiveSession>(g_.online, liveProject_, Paths::userDir() / "online" / ("live_" + liveProject_ + ".json"));
        liveUi_ = std::make_unique<OnlineUi>(g_);
        static const char* ranks[] = {"viewer", "tester", "editor", "admin", "owner"};
        int rank = 0;
        for (int i = 0; i < 5; ++i)
            if (liveRole_ == ranks[i]) rank = i;
        if (rank < 2) readOnly_ = true;  // readers and testers watch
        live_->join();
    }
    // Screenshots / support: --online-open check|history|objects|duplicate|select-one|
    // select-many|menu-test|favorites.
    const std::string& o = g_.onlineOpen;
    if (o == "select-one" || o == "select-many") {
        // Objects of the gameplay layer that link to others first (they show the links).
        std::vector<std::string> pick;
        for (const auto& ob : level_.objects) {
            const ObjectTypeInfo* t = ObjectTypes::find(ob.type);
            bool linked = false;
            if (t)
                for (const auto& pd : t->props) linked |= pd.type == PropType::ObjectRef && !ob.propStr(pd.key.c_str()).empty();
            if (linked) pick.push_back(ob.id);
        }
        for (const auto& ob : level_.objects)
            if (pick.size() < 6 && ob.type != "solid" && std::find(pick.begin(), pick.end(), ob.id) == pick.end()) pick.push_back(ob.id);
        if (!pick.empty()) {
            selection_.clear();
            for (size_t i = 0; i < pick.size() && i < (o == "select-one" ? 1u : 6u); ++i) selection_.insert(pick[i]);
            if (const LevelObject* first = level_.findObject(pick.front())) cam_.center = first->rect.center();
        }
        g_.onlineOpen.clear();
    } else if (o.rfind("select:", 0) == 0 || o.rfind("entity:", 0) == 0) {
        if (o.rfind("entity:", 0) == 0) libraryOpen_ = false;  // the whole column for the entity
        if (const LevelObject* ob = level_.findObject(o.substr(7))) {
            selection_ = {ob->id};
            cam_.center = ob->rect.center();
        }
        g_.onlineOpen.clear();
    } else if (o == "by-type") {
        explorerGroup_ = 1;
        g_.onlineOpen.clear();
    } else if (o == "menu-test") {
        menu_ = Menu::Test;
        g_.onlineOpen.clear();
    } else if (o == "favorites") {
        libraryCategory_ = 7;
        g_.onlineOpen.clear();
    }
    if (o == "check") leftTab_ = LeftTab::Check;
    if (o == "history") leftTab_ = LeftTab::History;
    if (o == "objects") leftTab_ = LeftTab::Explorer;
    if (o == "duplicate" && readOnly_) duplicateForEditing();
    if (o == "settings") modal_ = Modal::Settings;
    if (o == "events") modal_ = Modal::Events;
    if (o == "cinematics") modal_ = Modal::Cinematics;
    if (o == "open") modal_ = Modal::Open;
    if (o == "check" || o == "history" || o == "objects" || o == "duplicate" || o == "settings" || o == "events" ||
        o == "cinematics" || o == "open")
        g_.onlineOpen.clear();
    if (openHelpOnEnter) modal_ = Modal::Help;
}

void LevelEditorScene::onExit() {
    if (live_) {
        // The local copy keeps what was edited together, linked to the project's revision.
        live_->leave();
        if (liveLoaded_) {
            g_.content.saveUserLevel(level_);
            fs::path linksFile = Paths::userDir() / "online" / "projects.json";
            Json links = Json::object();
            if (auto t = FileUtil::readText(linksFile)) {
                Json j = Json::parse(*t, nullptr, false);
                if (j.is_object()) links = j;
            }
            links[level_.id]["project_id"] = liveProject_;
            links[level_.id]["revision"] = live_->revision();
            links[level_.id]["local_hash"] = Sha256::hex(level_.toJson().dump());
            FileUtil::writeTextAtomic(linksFile, links.dump(1));
            dirty_ = false;
        }
    }
    if (dirty_) autosave();
    if (mounted_) g_.app.resources().popMount();
    mounted_ = false;
    g_.app.input().stopTextInput(g_.app.window());
}

void LevelEditorScene::onResume() {
    // Back from a test run.
    g_.app.audio().stopMusic(400);
    g_.app.audio().stopAmbience(400);
    previewDirty_ = true;
}

void LevelEditorScene::flash(const std::string& msg, bool error) {
    message_ = msg;
    messageError_ = error;
    messageTimer_ = 4.0f;
    if (error) g_.uiSound("ui_error");
}

// ================================================================== document

void LevelEditorScene::openLevel(const std::string& id) {
    std::string err;
    auto lvl = g_.content.loadLevel(id, &err);
    if (!lvl) {
        flash("Impossible d'ouvrir le niveau : " + err, true);
        return;
    }
    const LevelEntry* e = g_.content.level(id);
    if (mounted_) g_.app.resources().popMount();
    mounted_ = false;
    assetRoot_.clear();
    if (e && !e->assetRoot.empty()) {
        assetRoot_ = Paths::toUtf8(e->assetRoot);
        g_.app.resources().pushMount(e->assetRoot);
        mounted_ = true;
    }
    bool builtin = e && e->source != ContentSource::User;
    readOnlySource_ = builtin && e ? (e->source == ContentSource::Package ? e->packageId : std::string{}) : std::string{};
    loadDocument(*lvl, builtin);
    if (builtin)
        flash(e->source == ContentSource::Package ? "Niveau importé : lecture seule. « Dupliquer pour modifier » crée votre copie."
                                                  : "Niveau de la campagne : lecture seule. « Dupliquer pour modifier » crée votre copie.");
    // Offer the autosave if it is newer than the file.
    fs::path auto_ = Paths::autosaveDir() / (FileUtil::slugify(level_.id) + ".json");
    std::error_code ec;
    autosaveAvailable_ = e && FileUtil::exists(auto_) && fs::last_write_time(auto_, ec) > fs::last_write_time(e->path, ec);
}

void LevelEditorScene::newLevel(const std::string& id, const std::string& name) {
    std::string sid = FileUtil::slugify(id.empty() ? name : id);
    if (g_.content.levelIdExists(sid)) {
        flash("Cet identifiant existe déjà", true);
        return;
    }
    if (mounted_) g_.app.resources().popMount();
    mounted_ = false;
    assetRoot_.clear();
    loadDocument(LevelData::makeEmpty(sid, name.empty() ? sid : name, "sylvanox"), false);
    dirty_ = true;
    save();
}

void LevelEditorScene::loadDocument(const LevelData& level, bool builtinSource) {
    level_ = level;
    level_.ensureDefaultLayers();
    builtinSource_ = builtinSource;
    readOnly_ = builtinSource;
    checksDirty_ = true;
    issues_.clear();
    groupCounter_ = 0;
    for (const auto& o : level_.objects) {
        std::string gid = o.propStr("group");
        if (gid.size() > 1 && gid[0] == 'g') groupCounter_ = std::max(groupCounter_, std::atoi(gid.c_str() + 1));
    }
    selection_.clear();
    history_.reset(level_.toJson());
    dirty_ = false;
    activeLayer_ = level_.findLayer("gameplay") ? "gameplay" : level_.layers.front().id;
    // Frame the player start.
    cam_.zoom = 1.0f;
    cam_.center = level_.bounds.center();
    for (const auto& o : level_.objects)
        if (o.type == "player_start") cam_.center = o.rect.center() + Vec2{120, -40};
    previewDirty_ = true;
}

bool LevelEditorScene::save() {
    if (level_.id.empty()) return false;
    if (readOnly_) {
        flash("Lecture seule : « Dupliquer pour modifier » pour enregistrer votre version", true);
        return false;
    }
    level_.metadata["editedWith"] = std::string("Cosmic Platformer ") + COSMIC_VERSION_STRING;
    level_.metadata["savedAt"] = FileUtil::nowString();
    if (!g_.content.saveUserLevel(level_)) {
        flash("Échec de l'enregistrement", true);
        return false;
    }
    dirty_ = false;
    lastSaved_ = FileUtil::nowString();
    std::error_code ec;
    fs::remove(Paths::autosaveDir() / (FileUtil::slugify(level_.id) + ".json"), ec);
    autosaveAvailable_ = false;
    flash("Niveau enregistré : " + Paths::toUtf8(Paths::userLevelsDir() / (FileUtil::slugify(level_.id) + ".json")));
    return true;
}

void LevelEditorScene::autosave() {
    if (readOnly_) return;
    fs::path p = Paths::autosaveDir() / (FileUtil::slugify(level_.id) + ".json");
    if (FileUtil::saveJson(p, level_.toJson())) Log::info("Editor autosave: {}", Paths::toUtf8(p));
}

void LevelEditorScene::markChanged(const std::string& label) {
    liveDirty_ = true;
    lastEditLabel_ = label;
    if (readOnly_) {
        // Campaign / imported level: every edit is refused (the document goes back to its
        // last state); the banner offers the copy.
        if (auto l = LevelData::fromJson(history_.current())) level_ = *l;
        previewDirty_ = true;
        flash(live_ ? "Lecture seule : votre rôle dans le projet ne permet pas de le modifier" : "Lecture seule : « Dupliquer pour modifier » d'abord", true);
        return;
    }
    checksDirty_ = true;
    if (history_.commit(level_.toJson(), label)) {
        dirty_ = true;
        previewDirty_ = true;
        previewDelay_ = 0.12f;
    }
}

void LevelEditorScene::applyJson(const Json& j) {
    liveDirty_ = true;  // undo / redo: sent like any edit
    if (auto l = LevelData::fromJson(j)) {
        level_ = *l;
        for (auto it = selection_.begin(); it != selection_.end();)
            it = level_.findObject(*it) ? std::next(it) : selection_.erase(it);
        dirty_ = true;
        previewDirty_ = true;
    }
}

void LevelEditorScene::rebuildPreview() {
    WorldServices ws{g_.app.resources(), g_.app.audio(), g_.content, nullptr, 0.0f, 1.0f};
    world_ = std::make_unique<GameWorld>(ws, WorldMode::Editor);
    world_->load(level_);
    renderer_.build(g_.app.renderer(), g_.app.resources(), *world_);
    previewDirty_ = false;
}

void LevelEditorScene::duplicateForEditing() {
    std::string base = FileUtil::slugify(level_.id + "_copie");
    std::string nid = base;
    int n = 2;
    while (g_.content.levelIdExists(nid)) nid = base + std::to_string(n++);
    std::string from = level_.id;
    level_.id = nid;
    level_.name += " (copie)";
    if (g_.online.loggedIn()) level_.author = g_.online.username();
    level_.metadata.erase("generator");
    level_.metadata["revision"] = 1;
    level_.metadata["duplicatedFrom"] = from;
    if (!readOnlySource_.empty()) level_.metadata["assetPackage"] = readOnlySource_;
    readOnly_ = false;
    builtinSource_ = false;
    readOnlySource_.clear();
    history_.reset(level_.toJson(), "Copie de " + from);
    dirty_ = true;
    if (save()) flash("Copie créée : " + nid + " — elle est à vous, modifiez-la librement.");
}

void LevelEditorScene::captureThumbnail() {
    // The view of the editor camera becomes the level's thumbnail (WORKSHOP, ONLINE, packages).
    Vec2 focus = cam_.center;
    std::vector<uint8_t> png = renderLevelThumbnail(g_, level_, assetRoot_, &focus);
    if (png.empty()) {
        flash("Capture impossible", true);
        return;
    }
    level_.metadata["thumbnailFocus"] = {focus.x, focus.y};
    fs::path file = Paths::userLevelsDir() / (FileUtil::slugify(level_.id) + ".thumb.png");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (!FileUtil::writeBinary(file, png)) {
        flash("Impossible d'écrire " + Paths::toUtf8(file), true);
        return;
    }
    markChanged("Vignette");
    flash("Vignette capturée : " + Paths::toUtf8(file));
}

void LevelEditorScene::testFrom(Vec2 feet) {
    if (level_.countObjects("player_start") == 0) {
        flash("Placez d'abord un départ du joueur", true);
        return;
    }
    autosave();
    PlayOptions opt;
    opt.level = level_;
    opt.editorTest = true;
    opt.assetRoot = assetRoot_;
    opt.spawn = feet;
    g_.app.pushScene(makeGameScene(g_, level_.id, opt));
}

void LevelEditorScene::testPlay(bool fromCamera) {
    if (level_.countObjects("player_start") == 0) {
        flash("Placez d'abord un départ du joueur", true);
        return;
    }
    autosave();
    PlayOptions opt;
    opt.level = level_;
    opt.editorTest = true;
    opt.assetRoot = assetRoot_;
    if (fromCamera) {
        // Drop the player at the camera centre, on the first solid ground below.
        Vec2 p = cam_.center;
        GameWorld* w = world_.get();
        if (w) {
            Rect probe = Rect::fromBottomCenter(p, {10, 22});
            for (int i = 0; i < 400 && !w->physics().overlapsSolid(probe.translated({0, 2})); ++i) probe.y += 2;
            p = probe.bottomCenter();
        }
        opt.spawn = p;
    }
    g_.app.pushScene(makeGameScene(g_, level_.id, opt));
}

// ================================================================ operations

std::string LevelEditorScene::createObject(const PaletteEntry& entry, const Rect& rect) {
    const ObjectTypeInfo* t = ObjectTypes::find(entry.type);
    if (!t) return {};
    if (t->unique) {
        for (auto& o : level_.objects)
            if (o.type == entry.type) {
                o.rect.x = rect.x;
                o.rect.y = rect.y;
                markChanged("Déplacer " + t->name);
                return o.id;
            }
    }
    LevelObject o;
    o.id = newObjectId();
    o.type = entry.type;
    o.props = t->defaultProps();
    for (auto& [k, v] : entry.props.items()) o.props[k] = v;
    std::string layer = t->defaultLayer;
    if (!level_.findLayer(layer) || !layerEditable(layer)) layer = activeLayer_;
    o.layer = layer;
    o.rect = rect;
    level_.objects.push_back(o);
    markChanged("Ajouter " + t->name);
    return o.id;
}

void LevelEditorScene::deleteSelection() {
    if (selection_.empty()) return;
    level_.objects.erase(std::remove_if(level_.objects.begin(), level_.objects.end(),
                                        [&](const LevelObject& o) { return selection_.count(o.id) && layerEditable(o.layer); }),
                         level_.objects.end());
    size_t n = selection_.size();
    selection_.clear();
    markChanged("Supprimer " + std::to_string(n) + " objet(s)");
}

void LevelEditorScene::copySelection() {
    clipboard_.clear();
    for (const auto& o : level_.objects)
        if (selection_.count(o.id)) clipboard_.push_back(o);
    if (!clipboard_.empty()) flash(std::to_string(clipboard_.size()) + " objet(s) copié(s)");
}

void LevelEditorScene::paste(Vec2 at) {
    if (clipboard_.empty()) return;
    Rect bounds = clipboard_.front().rect;
    for (const auto& o : clipboard_) bounds = bounds.united(o.rect);
    float grid = snapOn_ ? (gridIndex_ == 0 ? 8.0f : gridIndex_ == 1 ? 16.0f : 32.0f) : 1.0f;
    Vec2 target = snapPoint(at, grid);
    Vec2 delta = target - bounds.pos();
    selection_.clear();
    for (auto o : clipboard_) {
        const ObjectTypeInfo* t = ObjectTypes::find(o.type);
        if (t && t->unique) continue;
        o.id = newObjectId();
        o.rect = o.rect.translated(delta);
        level_.objects.push_back(o);
        selection_.insert(o.id);
    }
    markChanged("Coller");
}

void LevelEditorScene::duplicateSelection() {
    copySelection();
    if (clipboard_.empty()) return;
    Rect b = clipboard_.front().rect;
    for (const auto& o : clipboard_) b = b.united(o.rect);
    paste(b.pos() + Vec2{32, 0});
}

void LevelEditorScene::rotateSelection(float degrees) {
    bool any = false;
    for (auto& o : level_.objects) {
        if (!selection_.count(o.id)) continue;
        const ObjectTypeInfo* t = ObjectTypes::find(o.type);
        if (!t) continue;
        if (t->freeRotation) {
            o.rotation = std::fmod(o.rotation + degrees + 360.0f, 360.0f);
            any = true;
        } else if (t->rotatable || o.type == "solid" || o.type == "liquid" || o.type == "trigger") {
            // Quarter turn: rotate the rectangle around its centre.
            Vec2 c = o.rect.center();
            if (o.type == "solid" || o.type == "liquid" || o.type == "trigger") {
                o.rect = Rect::fromCenter(c, {o.rect.h, o.rect.w});
                o.rect.x = std::round(o.rect.x / 16.0f) * 16.0f;
                o.rect.y = std::round(o.rect.y / 16.0f) * 16.0f;
            } else {
                o.rotation = std::fmod(o.rotation + (degrees > 0 ? 90.0f : 270.0f), 360.0f);
            }
            any = true;
        }
    }
    if (any) markChanged("Rotation");
}

void LevelEditorScene::moveSelection(Vec2 delta) {
    for (auto& o : level_.objects)
        if (selection_.count(o.id) && layerEditable(o.layer)) o.rect = o.rect.translated(delta);
}

void LevelEditorScene::alignSelection(int mode) {
    std::vector<LevelObject*> objs;
    for (auto& o : level_.objects)
        if (selection_.count(o.id) && layerEditable(o.layer)) objs.push_back(&o);
    if (objs.size() < 2) return;
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (auto* o : objs) {
        minX = std::min(minX, o->rect.x);
        maxX = std::max(maxX, o->rect.right());
        minY = std::min(minY, o->rect.y);
        maxY = std::max(maxY, o->rect.bottom());
    }
    const float cx = (minX + maxX) * 0.5f, cy = (minY + maxY) * 0.5f;
    if (mode == 6 || mode == 7) {
        // Even gaps between the objects, the outer ones stay in place.
        bool h = mode == 6;
        std::sort(objs.begin(), objs.end(), [h](auto* a, auto* b) { return h ? a->rect.x < b->rect.x : a->rect.y < b->rect.y; });
        float total = 0.0f;
        for (auto* o : objs) total += h ? o->rect.w : o->rect.h;
        float span = h ? maxX - minX : maxY - minY;
        float gap = (span - total) / static_cast<float>(objs.size() - 1);
        float pos = h ? minX : minY;
        for (auto* o : objs) {
            if (h) o->rect.x = std::round(pos);
            else o->rect.y = std::round(pos);
            pos += (h ? o->rect.w : o->rect.h) + gap;
        }
    } else {
        for (auto* o : objs) {
            switch (mode) {
                case 0: o->rect.x = minX; break;
                case 1: o->rect.x = std::round(cx - o->rect.w * 0.5f); break;
                case 2: o->rect.x = maxX - o->rect.w; break;
                case 3: o->rect.y = minY; break;
                case 4: o->rect.y = std::round(cy - o->rect.h * 0.5f); break;
                case 5: o->rect.y = maxY - o->rect.h; break;
                default: break;
            }
        }
    }
    static const char* names[] = {"Aligner à gauche", "Centrer horizontalement", "Aligner à droite", "Aligner en haut",
                                  "Centrer verticalement", "Aligner en bas", "Répartir horizontalement", "Répartir verticalement"};
    markChanged(names[std::clamp(mode, 0, 7)]);
}

void LevelEditorScene::groupSelection(bool group) {
    if (selection_.empty()) return;
    std::string gid = group ? "g" + std::to_string(++groupCounter_) : std::string{};
    for (auto& o : level_.objects) {
        if (!selection_.count(o.id)) continue;
        if (group) o.props["group"] = gid;
        else o.props.erase("group");
    }
    markChanged(group ? "Grouper" : "Dégrouper");
    flash(group ? std::to_string(selection_.size()) + " objets groupés (" + gid + ") : un clic sélectionne tout le groupe"
                : "Groupe défait");
}

void LevelEditorScene::expandToGroups() {
    std::set<std::string> groups;
    for (const auto& o : level_.objects)
        if (selection_.count(o.id))
            if (std::string gid = o.propStr("group"); !gid.empty()) groups.insert(gid);
    if (groups.empty()) return;
    for (const auto& o : level_.objects)
        if (groups.count(o.propStr("group"))) selection_.insert(o.id);
}

LevelObject* LevelEditorScene::primary() {
    if (selection_.size() != 1) return nullptr;
    return level_.findObject(*selection_.begin());
}

bool LevelEditorScene::layerEditable(const std::string& layerId) const {
    const LevelLayer* l = level_.findLayer(layerId);
    return l && l->visible && !l->locked;
}

std::string LevelEditorScene::objectAt(Vec2 world) const {
    // Topmost first: highest layer order, then last in list.
    std::string best;
    int bestOrder = -1000000;
    float bestArea = 1e18f;
    for (const auto& o : level_.objects) {
        if (!layerEditable(o.layer)) continue;
        Rect r = o.rect.expanded(2);
        if (!r.contains(world)) continue;
        const LevelLayer* l = level_.findLayer(o.layer);
        int order = (l ? l->order : 0) * 1000 + o.z;
        float area = o.rect.w * o.rect.h;
        // Prefer smaller objects inside bigger ones at the same order (pick details in zones).
        if (order > bestOrder || (order == bestOrder && area <= bestArea)) {
            bestOrder = order;
            bestArea = area;
            best = o.id;
        }
    }
    return best;
}

float LevelEditorScene::snapFor(const LevelObject& o) const {
    if (!snapOn_) return 1.0f;
    float grid = gridIndex_ == 0 ? 8.0f : gridIndex_ == 1 ? 16.0f : 32.0f;
    if (const ObjectTypeInfo* t = ObjectTypes::find(o.type)) grid = std::min(grid, std::max(1.0f, t->snap));
    if (o.type == "solid" || o.type == "oneway") grid = std::max(grid, 16.0f);
    return grid;
}

Vec2 LevelEditorScene::snapPoint(Vec2 p, float grid) const {
    if (grid <= 1.0f) return {std::round(p.x), std::round(p.y)};
    return {std::round(p.x / grid) * grid, std::round(p.y / grid) * grid};
}

// ===================================================================== frame

void LevelEditorScene::update(float dt) {
    time_ += dt;
    if (live_) updateLive(dt);
    if (messageTimer_ > 0) messageTimer_ -= dt;
    if (previewDelay_ > 0) previewDelay_ -= dt;
    if (previewDirty_ && previewDelay_ <= 0 && drag_ == Drag::None) rebuildPreview();
    if (world_) {
        world_->fixedUpdate(PlayerInput{}, dt);
        world_->update(dt);
    }
    if (dirty_ && g_.settings.gameplay.editorAutosave) {
        autosaveTimer_ += dt;
        if (autosaveTimer_ >= static_cast<float>(g_.settings.gameplay.editorAutosaveSeconds)) {
            autosaveTimer_ = 0.0f;
            autosave();
            flash("Sauvegarde automatique effectuée");
        }
    }
}

void LevelEditorScene::render(float alpha) {
    UIContext& ui = g_.ui;
    ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
    Renderer& r = ui.r();
    float W = ui.screenW(), H = ui.screenH();
    r.fillRect({0, 0, W, H}, {8, 10, 20, 255});
    // Fixed panels: top bar, EXPLORATEUR (left), BIBLIOTHÈQUE / ENTITÉ (right), the view with
    // its header and, for a multiple selection, its table.
    float top = ui.s(62), status = ui.s(34), left = ui.s(340), right = ui.s(430), headerH = ui.s(40);
    float tableH = selection_.size() >= 2 ? std::min(ui.s(236), ui.s(76) + static_cast<float>(selection_.size()) * ui.s(28)) : 0.0f;
    Rect center{left, top, W - left - right, H - top - status};
    viewport_ = {center.x, center.y + headerH, center.w, center.h - headerH - tableH};

    bool modal = modal_ != Modal::None || !conflicts_.empty();
    bool menu = menu_ != Menu::None;
    ui.inputEnabled = !modal && !menu;
    drawViewport(viewport_, alpha);
    if (!modal && !menu) {
        handleViewport(viewport_);
        handleShortcuts();
    }
    drawCanvasHeader({center.x, center.y, center.w, headerH});
    if (tableH > 0.0f) drawSelectionTable({center.x, viewport_.bottom(), center.w, tableH});
    drawToolbar({0, 0, W, top});
    drawLeftPanel({0, top, left, H - top - status});
    drawRightPanel({W - right, top, right, H - top - status});
    drawStatus({0, H - status, W, status});
    ui.inputEnabled = !modal;
    if (menu && !modal) drawMenus();
    ui.inputEnabled = true;
    if (!conflicts_.empty()) drawConflicts();
    else if (modal) drawModal();
    if (messageTimer_ > 0.0f) {
        TextStyle st;
        st.size = ui.s(22);
        st.color = messageError_ ? ui.theme().danger : ui.theme().text;
        st.align = Align::Center;
        st.valign = VAlign::Middle;
        Vec2 sz = ui.text().measure(message_, FontFace::Body, st.size);
        Rect bg = Rect::fromCenter({viewport_.center().x, viewport_.bottom() - ui.s(40)}, {sz.x + ui.s(40), ui.s(44)});
        if (modal && bg.intersects(modalRect_)) {  // never over a dialog: in the band below it
            bg = Rect::fromCenter({W * 0.5f, (modalRect_.bottom() + H) * 0.5f}, bg.size());
        } else {
            // Nor over the map of the level or the live notifications: just above them.
            for (int pass = 0; pass < 2; ++pass) {
                if (minimapOn_ && bg.intersects(minimapRect_)) bg.y = minimapRect_.y - ui.s(10) - bg.h;
                if (liveFeedRect_.w > 0.0f && bg.intersects(liveFeedRect_)) bg.y = liveFeedRect_.y - ui.s(10) - bg.h;
            }
        }
        r.fillRoundedRect(bg, ui.s(10), {6, 10, 24, 225});
        ui.text().draw(message_, bg.center(), st);
    }
    liveFeedRect_ = {};  // computed again by the next frame's live overlay
    ui.end();
}

}  // namespace cosmic
