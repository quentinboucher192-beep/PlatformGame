// Enemy editor: list, animated preview, test arena, saving.

#include "editors/enemy_editor/EnemyEditorScene.h"

#include <cmath>
#include <format>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/enemies/Enemy.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

std::unique_ptr<Scene> makeEnemyEditor(GameServices& g, const std::string& enemyId, int tab) {
    return std::make_unique<EnemyEditorScene>(g, enemyId, tab);
}

void EnemyEditorScene::onEnter() {
    g_.app.audio().stopMusic(500);
    auto list = g_.content.enemies();
    if (!startId_.empty() && g_.content.enemy(startId_)) select(startId_);
    else if (!list.empty()) select(list.front()->id);
}

void EnemyEditorScene::onExit() {
    discardAnimationEdits();
    g_.app.input().stopTextInput(g_.app.window());
}

bool EnemyEditorScene::editingNative() const { return g_.content.isNativeEnemy(selectedId_); }

void EnemyEditorScene::discardAnimationEdits() {
    if (!animDirty_) return;
    if (!animMount_.empty()) g_.app.resources().pushMount(animMount_);
    g_.app.resources().invalidateAnimation(animPath_);
    if (!animMount_.empty()) g_.app.resources().popMount();
    animDirty_ = false;
    arenaDirty_ = true;
}

void EnemyEditorScene::flash(const std::string& m, bool error) {
    message_ = m;
    messageError_ = error;
    messageTimer_ = 4.0f;
    if (error) g_.uiSound("ui_error");
}

void EnemyEditorScene::select(const std::string& id) {
    const EnemyDefinition* d = g_.content.enemy(id);
    if (!d) return;
    discardAnimationEdits();
    work_ = *d;
    selectedId_ = id;
    dirty_ = false;
    selectedAttack_ = 0;
    selectedPhase_ = 0;
    confirmDelete_ = false;
    history_.reset(work_.toJson());
    loadAnimation();
    arenaDirty_ = true;
}

void EnemyEditorScene::loadAnimation() {
    animPath_ = work_.animation;
    animMount_ = work_.assetRoot;
    if (!work_.assetRoot.empty()) g_.app.resources().pushMount(work_.assetRoot);
    anim_ = g_.app.resources().animation(work_.animation);
    if (!work_.assetRoot.empty()) g_.app.resources().popMount();
    previewAnim_.setSet(anim_);
    auto clips = clipNames();
    previewClip_ = 0;
    for (size_t i = 0; i < clips.size(); ++i)
        if (clips[i] == "idle") previewClip_ = static_cast<int>(i);
    if (!clips.empty()) previewAnim_.play(clips[previewClip_], true);
}

std::vector<std::string> EnemyEditorScene::clipNames() const {
    std::vector<std::string> v;
    if (!anim_) return v;
    // Keep the sheet row order (idle first...).
    std::vector<const AnimationClip*> clips;
    for (const auto& [k, c] : anim_->clips) clips.push_back(&c);
    std::sort(clips.begin(), clips.end(), [](const auto* a, const auto* b) { return a->row < b->row; });
    for (const auto* c : clips) v.push_back(c->name);
    return v;
}

void EnemyEditorScene::changed() {
    if (history_.commit(work_.toJson(), "Modification")) {
        dirty_ = true;
        arenaDirty_ = true;
    }
}

void EnemyEditorScene::newEnemy(bool duplicate) {
    EnemyDefinition d = work_;
    std::string base = duplicate ? work_.id + "_copie" : "ennemi";
    std::string id = base;
    int n = 2;
    while (g_.content.enemy(id)) id = base + "_" + std::to_string(n++);
    if (!duplicate) {
        if (const EnemyDefinition* t = g_.content.enemy("glowmite")) d = *t;
        d.name = "Nouvel ennemi";
        d.description = "";
    } else {
        d.name += " (copie)";
    }
    d.id = id;
    d.builtin = false;
    d.isBoss = d.isBoss && duplicate;
    work_ = d;
    selectedId_ = id;
    g_.content.saveUserEnemy(work_);
    g_.ratings.contentChanged();
    history_.reset(work_.toJson());
    dirty_ = false;
    loadAnimation();
    arenaDirty_ = true;
    flash("Ennemi créé : " + id);
}

void EnemyEditorScene::saveEnemy() {
    if (work_.id.empty()) {
        flash("Identifiant vide", true);
        return;
    }
    work_.id = FileUtil::slugify(work_.id);
    // Native entities (the ones the campaign uses) are read-only: the changes go to a copy.
    bool copied = false;
    if (g_.content.isNativeEnemy(work_.id)) {
        if (const EnemyDefinition* native = g_.content.enemy(work_.id); native && work_.name == native->name)
            work_.name += " (perso)";
        work_.id = g_.content.freeEnemyId(work_.id + "_perso");
        copied = true;
    }
    work_.builtin = false;
    if (animDirty_ && !saveAnimation()) {
        flash("Échec de l'enregistrement de l'animation", true);
        return;
    }
    if (g_.content.saveUserEnemy(work_)) {
        g_.ratings.contentChanged();
        selectedId_ = work_.id;
        dirty_ = false;
        history_.reset(work_.toJson());
        if (copied)
            flash("Entité native non modifiable : votre version est enregistrée sous « " + work_.id + " »");
        else
            flash("Enregistré dans " + Paths::toUtf8(Paths::userEnemiesDir() / (work_.id + ".json")));
        g_.uiSound("ui_confirm");
    } else {
        flash("Échec de l'enregistrement", true);
    }
}

bool EnemyEditorScene::saveAnimation() {
    if (!anim_) return true;
    std::string rel = work_.animation;
    std::error_code ec;
    if (rel.empty() || fs::is_regular_file(Paths::assetsDir() / Paths::fromUtf8(rel), ec))
        rel = "animations/user/" + FileUtil::slugify(work_.id) + ".json";
    fs::path p = Paths::userDir() / "assets" / Paths::fromUtf8(rel);
    if (!FileUtil::saveJson(p, anim_->toJson())) return false;
    // Restore the original (possibly native) animation in the cache, then use the copy.
    discardAnimationEdits();
    work_.animation = rel;
    g_.app.resources().invalidateAnimation(rel);
    loadAnimation();
    arenaDirty_ = true;
    return true;
}

void EnemyEditorScene::deleteEnemy() {
    const EnemyDefinition* d = g_.content.enemy(selectedId_);
    if (!d || d->builtin) {
        flash("Les ennemis officiels ne peuvent pas être supprimés", true);
        return;
    }
    std::string id = selectedId_;
    g_.content.deleteUserEnemy(id);
    g_.ratings.contentChanged();
    flash("Supprimé : " + id);
    auto list = g_.content.enemies();
    if (const EnemyDefinition* again = g_.content.enemy(id)) select(again->id);  // built-in restored
    else if (!list.empty()) select(list.front()->id);
}

void EnemyEditorScene::resetArena() {
    LevelData l = LevelData::makeEmpty("enemy_arena", "Arène de test", "sylvanox");
    l.objects.clear();
    l.bounds = {0, 0, 960, 400};
    l.timerMode = TimerMode::Unlimited;
    auto add = [&](const char* type, Rect r, Json props) {
        LevelObject o;
        o.id = l.nextObjectId();
        o.type = type;
        o.layer = std::string(type) == "solid" || std::string(type) == "oneway" ? "terrain" : "gameplay";
        o.rect = r;
        o.props = std::move(props);
        l.objects.push_back(o);
    };
    add("solid", {0, 320, 960, 80}, {{"material", "ruin_stone"}});
    add("solid", {0, 0, 32, 320}, {{"material", "ancient_metal"}});
    add("solid", {928, 0, 32, 320}, {{"material", "ancient_metal"}});
    add("oneway", {240, 256, 96, 16}, {{"style", "tech_ledge"}});
    add("oneway", {620, 256, 96, 16}, {{"style", "tech_ledge"}});
    add("player_start", {155, 298, 10, 22}, Json::object());
    add("light", {472, 200, 16, 16}, {{"color", "#39f0d8"}, {"radius", 260}});
    l.ambientLight = {215, 220, 235, 255};
    WorldServices ws{g_.app.resources(), g_.app.audio(), g_.content, &g_.app.input(), g_.settings.graphics.screenShake, 1.0f};
    arena_ = std::make_unique<GameWorld>(ws, WorldMode::Arena);
    arena_->load(l);
    for (const auto* p : g_.content.powers()) arena_->player().grantPower(*p);
    float y = work_.flying ? 250.0f : 320.0f;
    arena_->spawnEnemy(work_, {640, y}, true, "test", 120);
    arenaRenderer_.build(g_.app.renderer(), g_.app.resources(), *arena_);
    arena_->camera().setZoom(0.9f);
    arenaDirty_ = false;
}

void EnemyEditorScene::fixedUpdate(float dt) {
    if (!arena_) return;
    PlayerInput in = g_.app.input().consumePlayerInput();
    if (!controlPlayer_) in = PlayerInput{};
    if (godMode_) {
        arena_->player().setInvulnerable(0.1f);
        arena_->player().health = arena_->player().maxHealth();
    }
    arena_->fixedUpdate(in, dt);
    // Respawn the enemy a moment after it dies so tuning can continue.
    bool alive = false;
    for (const auto& e : arena_->enemies()) alive |= e->alive();
    if (!alive) {
        respawnTimer_ += dt;
        if (respawnTimer_ > 1.5f) {
            respawnTimer_ = 0.0f;
            arena_->spawnEnemy(work_, {640, work_.flying ? 250.0f : 320.0f}, true, "test", 120);
        }
    }
}

void EnemyEditorScene::update(float dt) {
    time_ += dt;
    if (messageTimer_ > 0) messageTimer_ -= dt;
    if (arenaDirty_) resetArena();
    if (arena_) arena_->update(dt);
    if (previewPlaying_) previewAnim_.update(dt * previewSpeed_);
    InputManager& in = g_.app.input();
    if (controlPlayer_ && (in.keyPressed(SDL_SCANCODE_ESCAPE) || in.rawPadButton(SDL_GAMEPAD_BUTTON_BACK))) controlPlayer_ = false;
    if (!g_.ui.editingText() && in.ctrl()) {
        if (in.shortcutPressed(SDLK_S)) saveEnemy();
        if (in.shortcutPressed(SDLK_Z)) {
            work_ = EnemyDefinition::fromJson(history_.undo());
            dirty_ = arenaDirty_ = true;
        }
        if (in.shortcutPressed(SDLK_Y)) {
            work_ = EnemyDefinition::fromJson(history_.redo());
            dirty_ = arenaDirty_ = true;
        }
    }
}

void EnemyEditorScene::render(float alpha) {
    UIContext& ui = g_.ui;
    ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
    float W = ui.screenW(), H = ui.screenH();
    ui.r().fillRect({0, 0, W, H}, {8, 10, 20, 255});
    float listW = ui.s(340), propW = ui.s(520), top = ui.s(70);
    // Title bar
    ui.r().fillGradientV({0, 0, W, top}, {18, 26, 50, 255}, {12, 16, 32, 255});
    ui.label("ÉDITEUR D'ENNEMIS", {ui.s(20), top * 0.5f}, 30, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
    ui.pushId("ee");
    UIContext::ButtonStyle p;
    p.primary = dirty_;
    p.small = true;
    std::string saveLabel = editingNative() ? "Enregistrer une copie" : "Enregistrer";
    if (dirty_) saveLabel += "*";
    if (ui.button("save", {W - ui.s(620), ui.s(12), ui.s(260), top - ui.s(24)}, saveLabel, p)) saveEnemy();
    UIContext::ButtonStyle s;
    s.small = true;
    if (ui.button("level", {W - ui.s(350), ui.s(12), ui.s(200), top - ui.s(24)}, "Éditeur de niveaux", s))
        g_.app.switchScene(makeLevelEditor(g_, ""));
    UIContext::ButtonStyle q;
    q.small = true;
    q.danger = true;
    if (ui.button("quit", {W - ui.s(140), ui.s(12), ui.s(124), top - ui.s(24)}, "Quitter", q) ||
        (ui.backPressed() && !controlPlayer_))
        g_.app.switchScene(makeContentManager(g_, {}, 4));
    ui.popId();

    ui.inputEnabled = !controlPlayer_;
    Rect center{listW, top, W - listW - propW, H - top};
    drawList({0, top, listW, H - top});
    drawPreview({center.x + ui.s(16), center.y + ui.s(16), center.w - ui.s(32), center.h * 0.36f});
    arenaRect_ = {center.x + ui.s(16), center.y + center.h * 0.36f + ui.s(70), center.w - ui.s(32), center.h * 0.64f - ui.s(86)};
    drawArena(arenaRect_, alpha);
    drawProperties({W - propW, top, propW, H - top});
    ui.inputEnabled = true;
    if (messageTimer_ > 0) {
        TextStyle st;
        st.size = ui.s(22);
        st.color = messageError_ ? ui.theme().danger : ui.theme().text;
        st.align = Align::Center;
        st.valign = VAlign::Middle;
        st.shadow = true;
        ui.text().draw(message_, {center.center().x, H - ui.s(30)}, st);
    }
    ui.end();
}

void EnemyEditorScene::drawList(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRect(r, {12, 16, 32, 245});
    ui.pushId("list");
    auto list = g_.content.enemies();
    Rect l{r.x + ui.s(10), r.y + ui.s(10), r.w - ui.s(20), r.h - ui.s(150)};
    float rh = ui.s(66);
    ui.beginScroll("scroll", l, list.size() * (rh + ui.s(6)));
    float y = l.y - ui.scrollOffset();
    for (size_t i = 0; i < list.size(); ++i) {
        const EnemyDefinition* d = list[i];
        std::string sub = std::string(familyLabel(d->family)) + (d->builtin ? " · natif" : " · personnalisé");
        Color accent = d->isBoss ? ui.theme().danger : (d->builtin ? ui.theme().accent : ui.theme().warm);
        if (ui.listItem("e" + std::to_string(i), {l.x, y, l.w - ui.s(12), rh}, d->name, d->id == selectedId_, sub, accent)) {
            if (d->id != selectedId_) select(d->id);
        }
        y += rh + ui.s(6);
    }
    ui.endScroll();
    float by = r.bottom() - ui.s(130);
    UIContext::ButtonStyle st;
    st.small = true;
    if (ui.button("new", {r.x + ui.s(10), by, r.w * 0.5f - ui.s(15), ui.s(52)}, "Nouveau", st)) newEnemy(false);
    if (ui.button("dup", {r.x + r.w * 0.5f + ui.s(5), by, r.w * 0.5f - ui.s(15), ui.s(52)}, "Dupliquer", st)) newEnemy(true);
    UIContext::ButtonStyle d;
    d.small = true;
    d.danger = true;
    const EnemyDefinition* cur = g_.content.enemy(selectedId_);
    d.enabled = cur && !cur->builtin;
    if (ui.button("del", {r.x + ui.s(10), by + ui.s(62), r.w - ui.s(20), ui.s(52)}, confirmDelete_ ? "Confirmer la suppression" : "Supprimer", d)) {
        if (confirmDelete_) deleteEnemy();
        else confirmDelete_ = true;
    }
    ui.popId();
}

void EnemyEditorScene::drawPreview(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.panel(r, false, 12);
    ui.pushId("preview");
    auto clips = clipNames();
    Rect stage{r.x + ui.s(10), r.y + ui.s(10), r.w * 0.55f, r.h - ui.s(20)};
    ui.r().fillGradientV(stage, {20, 30, 56, 255}, {10, 14, 28, 255});
    if (anim_ && anim_->texture && !clips.empty()) {
        float scale = std::min((stage.h - ui.s(40)) / anim_->frameHeight, (stage.w - ui.s(40)) / anim_->frameWidth);
        scale = std::max(1.0f, std::floor(scale));
        Vec2 feet{stage.center().x, stage.bottom() - ui.s(20)};
        Rect src = previewAnim_.sourceRect();
        Rect dst{feet.x - anim_->pivot.x * scale, feet.y - anim_->pivot.y * scale, anim_->frameWidth * scale, anim_->frameHeight * scale};
        ui.r().fillRect({stage.x, feet.y, stage.w, ui.s(2)}, {57, 240, 216, 80});
        ui.r().drawTexture(*anim_->texture, &src, dst);
        if (showRanges_) {
            Rect hb = Rect::fromBottomCenter(feet, work_.size * scale * work_.scale);
            ui.r().drawRect(hb, {255, 77, 109, 220}, 2.0f);
            ui.r().drawRect(dst, {255, 255, 255, 40}, 1.0f);
        }
        ui.label(std::format("{}  ·  image {}/{}", previewAnim_.current(), previewAnim_.frame() + 1,
                             anim_->clip(previewAnim_.current()) ? anim_->clip(previewAnim_.current())->frameCount() : 0),
                 {stage.x + ui.s(10), stage.y + ui.s(8)}, 17, ui.theme().textDim);
    }
    float x = stage.right() + ui.s(16), w = r.right() - x - ui.s(10);
    float y = r.y + ui.s(10);
    if (!clips.empty()) {
        previewClip_ = std::clamp(previewClip_, 0, static_cast<int>(clips.size()) - 1);
        if (ui.cycle("clip", {x, y, w, ui.s(44)}, "Animation", previewClip_, clips)) previewAnim_.play(clips[previewClip_], true);
        y += ui.s(52);
        UIContext::ButtonStyle st;
        st.small = true;
        if (ui.button("play", {x, y, w * 0.48f, ui.s(44)}, previewPlaying_ ? "Pause" : "Lecture", st)) previewPlaying_ = !previewPlaying_;
        if (ui.button("replay", {x + w * 0.52f, y, w * 0.48f, ui.s(44)}, "Rejouer", st)) previewAnim_.play(clips[previewClip_], true);
        y += ui.s(52);
        ui.slider("speed", {x, y, w, ui.s(44)}, "Vitesse", previewSpeed_, 0.1f, 2.0f, 0.1f, "%.1fx");
        y += ui.s(52);
        // Clip timing (saved as an animation override in the user folder).
        if (anim_) {
            auto it = anim_->clips.find(clips[previewClip_]);
            if (it != anim_->clips.end()) {
                AnimationClip& c = it->second;
                float fps = c.durations.empty() ? 10.0f : 1.0f / c.durations[0];
                if (ui.slider("fps", {x, y, w, ui.s(44)}, "Images / s", fps, 1.0f, 30.0f, 1.0f, "%.0f")) {
                    for (auto& d : c.durations) d = 1.0f / fps;
                    animDirty_ = dirty_ = true;
                }
                y += ui.s(52);
                if (ui.toggle("loop", {x, y, w, ui.s(44)}, "En boucle", c.loop)) {
                    animDirty_ = dirty_ = true;
                    previewAnim_.play(c.name, true);
                }
                y += ui.s(52);
            }
        }
        ui.toggle("ranges", {x, y, w, ui.s(44)}, "Afficher hitbox et portées", showRanges_);
    }
    ui.popId();
}

void EnemyEditorScene::drawArena(const Rect& r, float alpha) {
    UIContext& ui = g_.ui;
    ui.pushId("arena");
    float by = r.y - ui.s(56);
    UIContext::ButtonStyle st;
    st.small = true;
    st.selected = controlPlayer_;
    bool en = ui.inputEnabled;
    ui.inputEnabled = true;
    if (ui.button("control", {r.x, by, ui.s(300), ui.s(48)}, controlPlayer_ ? "Contrôle : joueur (Échap)" : "Jouer dans l'arène", st))
        controlPlayer_ = !controlPlayer_;
    ui.inputEnabled = en;
    if (ui.button("reset", {r.x + ui.s(310), by, ui.s(200), ui.s(48)}, "Réinitialiser", UIContext::ButtonStyle{false, false, true}))
        arenaDirty_ = true;
    ui.toggle("god", {r.x + ui.s(520), by, ui.s(300), ui.s(48)}, "Joueur invulnérable", godMode_);
    if (!work_.attacks.empty()) {
        selectedAttack_ = std::clamp(selectedAttack_, 0, static_cast<int>(work_.attacks.size()) - 1);
        UIContext::ButtonStyle a;
        a.small = true;
        a.primary = true;
        if (ui.button("attack", {r.x + ui.s(830), by, std::max(ui.s(200), r.right() - r.x - ui.s(830)), ui.s(48)},
                      "Tester : " + work_.attacks[selectedAttack_].name, a) && arena_) {
            for (auto& e : arena_->enemies())
                if (e->alive()) e->forceAttack(*arena_, work_.attacks[selectedAttack_].id);
        }
    }
    ui.popId();
    Renderer& rr = ui.r();
    rr.pushClip(r);
    if (arena_) {
        // Frame the whole arena (960 x 400 world pixels).
        EditorCamera cam;
        cam.center = {480.0f, 214.0f};
        cam.zoom = std::min((r.w / 960.0f) / (r.h / 270.0f), (r.h / 400.0f) / (r.h / 270.0f));
        WorldView v = cam.view(r);
        RenderOptions opt;
        opt.lighting = true;
        arenaRenderer_.render(rr, g_.app.resources(), *arena_, v, alpha, opt);
        if (showRanges_) {
            rr.setView(v);
            for (const auto& e : arena_->enemies()) {
                if (!e->alive()) continue;
                rr.drawWorldCircle(e->center(), work_.detectionRange, {255, 217, 138, 90}, 1.5f);
                rr.drawWorldCircle(e->center(), work_.attackRange, {255, 77, 109, 120}, 1.5f);
                rr.drawWorldRect(e->box, {255, 77, 109, 200}, 1.5f);
                TextStyle t;
                t.size = ui.s(16);
                t.color = {255, 230, 200, 255};
                t.shadow = true;
                Vec2 sp = v.toScreen({e->box.x, e->box.y - 14});
                ui.text().draw(e->debugInfo(), sp, t);
            }
        }
    }
    rr.popClip();
    rr.drawRect(r, controlPlayer_ ? ui.theme().accent : ui.theme().border, controlPlayer_ ? 3.0f : 1.5f);
    if (!controlPlayer_)
        ui.label("Cliquez sur « Jouer dans l'arène » pour piloter Nova et tester l'ennemi", {r.center().x, r.bottom() - ui.s(26)}, 17,
                 ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
}

}  // namespace cosmic
