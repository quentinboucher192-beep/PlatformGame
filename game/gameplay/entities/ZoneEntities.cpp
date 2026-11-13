// Zones: triggers, combat arenas, camera zones, secret areas and enemy spawns.

#include <cmath>

#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"

namespace cosmic {

namespace {
std::vector<std::string> splitList(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    out.erase(std::remove_if(out.begin(), out.end(), [](const auto& x) { return x.empty(); }), out.end());
    return out;
}

void drawZone(RenderContext& ctx, const Rect& box, Color c, const char* /*label*/) {
    ctx.r.fillWorldRect(box, c.withAlpha(28));
    ctx.r.drawWorldRect(box, c.withAlpha(170), 1.0f);
}
}  // namespace

// ------------------------------------------------------------------ trigger

void TriggerEntity::onPlayerEnter(GameWorld& w) {
    if (!active) return;
    if (obj_.prop<bool>("once", true) && fired_) return;
    fired_ = true;
    w.emit(id(), "enter");
}

void TriggerEntity::onPlayerExit(GameWorld& w) {
    if (active) w.emit(id(), "exit");
}

bool TriggerEntity::handleAction(GameWorld&, const std::string& action, const Json&) {
    if (action == "enable") active = true;
    else if (action == "disable") active = false;
    else if (action == "reset") fired_ = false;
    else return false;
    return true;
}

void TriggerEntity::render(RenderContext& ctx) {
    if (ctx.editor) drawZone(ctx, box, {120, 255, 120, 255}, "Déclencheur");
}

// ------------------------------------------------------------- combat zone

void CombatZoneEntity::init(GameWorld& w) {
    for (const auto& wave : splitList(obj_.propStr("waves"), ';')) {
        auto ids = splitList(wave, ',');
        if (!ids.empty()) waves_.push_back(ids);
    }
    doors_ = splitList(obj_.propStr("doors"), ',');
    if (waves_.empty()) {
        // Automatic mode: every enemy placed inside the arena forms one wave.
        std::vector<std::string> inside;
        for (const auto& o : w.level().objects)
            if (o.type == "enemy" && box.contains(o.rect.center())) inside.push_back(o.id);
        if (!inside.empty()) waves_.push_back(inside);
    }
}

void CombatZoneEntity::setDoors(GameWorld& w, bool open) {
    for (const auto& d : doors_)
        if (LevelEntity* e = w.entity(d)) e->handleAction(w, open ? "open" : "close", Json::object());
}

void CombatZoneEntity::startWave(GameWorld& w, size_t index) {
    wave_ = index;
    if (index >= waves_.size()) return;
    for (const auto& id : waves_[index]) {
        auto* es = dynamic_cast<EnemySpawnEntity*>(w.entity(id));
        if (!es) continue;
        Enemy* e = es->enemy(w);
        if (!e || !e->alive()) es->respawn(w, false);
        else if (e->dormant()) e->wake(w);
        if (Enemy* en = es->enemy(w)) en->alert(w);
    }
    if (index > 0) w.hint("Vague " + std::to_string(index + 1) + " / " + std::to_string(waves_.size()), 2.0f);
}

void CombatZoneEntity::onPlayerEnter(GameWorld& w) {
    if (started_ || cleared_ || waves_.empty()) return;
    started_ = true;
    setDoors(w, false);
    if (obj_.prop<bool>("lockCamera", true)) {
        CameraZone z;
        z.area = box.expanded(16);
        z.clampToArea = true;
        w.lockCameraZone(&z);
    }
    std::string music = obj_.propStr("music");
    if (!music.empty()) w.setMusic(music);
    if (obj_.prop<bool>("announce", true)) {
        WorldMessage m;
        m.style = WorldMessage::Style::Title;
        m.text = "Embuscade !";
        m.subtitle = "Éliminez tous les ennemis";
        m.duration = 2.0f;
        w.message(m);
        w.playSound("sounds/enemy_alert.wav", 0.9f, 0.8f);
    }
    startWave(w, 0);
    w.emit(id(), "start");
}

void CombatZoneEntity::fixedUpdate(GameWorld& w, float dt) {
    if (!started_ || cleared_) return;
    if (waveDelay_ > 0.0f) {
        waveDelay_ -= dt;
        if (waveDelay_ <= 0.0f) startWave(w, wave_ + 1);
        return;
    }
    bool allDead = true;
    if (wave_ < waves_.size()) {
        for (const auto& id : waves_[wave_]) {
            auto* es = dynamic_cast<EnemySpawnEntity*>(w.entity(id));
            if (es && es->enemyAlive(w)) allDead = false;
        }
    }
    if (!allDead) return;
    if (wave_ + 1 < waves_.size()) {
        waveDelay_ = 0.9f;
        return;
    }
    cleared_ = true;
    setDoors(w, true);
    w.lockCameraZone(nullptr);
    if (!obj_.propStr("music").empty()) w.restoreLevelMusic();
    if (obj_.prop<bool>("announce", true)) {
        WorldMessage m;
        m.style = WorldMessage::Style::Title;
        m.text = "Zone sécurisée";
        m.duration = 1.8f;
        w.message(m);
        w.playSound("sounds/checkpoint.wav", 0.8f, 0.8f);
    }
    w.emit(id(), "cleared");
}

void CombatZoneEntity::onPlayerRespawn(GameWorld& w) {
    if (!started_ || cleared_) return;
    started_ = false;
    waveDelay_ = 0.0f;
    setDoors(w, true);
    bool explicitWaves = !obj_.propStr("waves").empty();
    for (const auto& wave : waves_)
        for (const auto& id : wave)
            if (auto* es = dynamic_cast<EnemySpawnEntity*>(w.entity(id))) es->respawn(w, explicitWaves);
}

bool CombatZoneEntity::handleAction(GameWorld& w, const std::string& action, const Json&) {
    if (action == "activate" || action == "start") {
        bool was = playerInside;
        playerInside = true;
        onPlayerEnter(w);
        playerInside = was;
        return true;
    }
    return false;
}

std::string CombatZoneEntity::debugInfo() const {
    return cleared_ ? "nettoyée" : started_ ? "vague " + std::to_string(wave_ + 1) : "en attente";
}

void CombatZoneEntity::render(RenderContext& ctx) {
    if (ctx.editor) drawZone(ctx, box, {255, 120, 60, 255}, "Arène");
}

// ------------------------------------------------------------- camera zone

void CameraZoneEntity::init(GameWorld& /*w*/) {
    zone_.area = box;
    zone_.center = box.center();
    std::string mode = obj_.propStr("mode", "clamp");
    zone_.lockCenter = mode == "lock";
    zone_.clampToArea = mode == "clamp";
    zone_.zoom = obj_.prop<float>("zoom", 0.0f);
}

bool CameraZoneEntity::handleAction(GameWorld&, const std::string& action, const Json&) {
    if (action == "enable") active = true;
    else if (action == "disable") active = false;
    else return false;
    return true;
}

void CameraZoneEntity::render(RenderContext& ctx) {
    if (ctx.editor) drawZone(ctx, box, {255, 255, 120, 255}, "Caméra");
}

// ------------------------------------------------------------- secret area

void SecretAreaEntity::onPlayerEnter(GameWorld& w) { w.addSecretFound(id(), obj_.propStr("message")); }

void SecretAreaEntity::update(GameWorld& /*w*/, float dt) {
    reveal_ = approach(reveal_, playerInside ? 1.0f : 0.0f, dt * 3.0f);
}

void SecretAreaEntity::render(RenderContext& ctx) {
    if (ctx.editor) drawZone(ctx, box, {255, 217, 138, 255}, "Secret");
}

// ------------------------------------------------------------- enemy spawn

void EnemySpawnEntity::init(GameWorld& w) {
    if (w.mode() == WorldMode::Editor) {
        if (const EnemyDefinition* d = w.content().enemy(obj_.propStr("enemy", "glowmite"))) {
            if (!d->assetRoot.empty()) w.resources().pushMount(d->assetRoot);
            previewAnim_ = w.resources().animation(d->animation);
            if (!d->assetRoot.empty()) w.resources().popMount();
        }
        return;
    }
    respawn(w, obj_.prop<bool>("dormant", false));
}

Enemy* EnemySpawnEntity::enemy(GameWorld& w) const { return w.findEnemy(enemyUid_); }

bool EnemySpawnEntity::enemyAlive(GameWorld& w) const {
    Enemy* e = enemy(w);
    return e && e->alive();
}

void EnemySpawnEntity::respawn(GameWorld& w, bool dormant) {
    if (Enemy* old = enemy(w)) old->despawn();
    Enemy* e = w.spawnEnemy(obj_.propStr("enemy", "glowmite"), obj_.rect.bottomCenter(), obj_.prop<bool>("faceLeft", false),
                            id(), obj_.prop<float>("patrol", 96.0f));
    enemyUid_ = e ? e->uid() : 0;
    if (e && dormant) e->setDormant();
}

bool EnemySpawnEntity::handleAction(GameWorld& w, const std::string& action, const Json&) {
    if (action == "spawn" || action == "activate" || action == "wake" || action == "enable") {
        Enemy* e = enemy(w);
        if (!e || !e->alive()) {
            respawn(w, true);
            e = enemy(w);
        }
        if (e && e->dormant()) e->wake(w);
        if (e && obj_.prop<bool>("boss", false)) w.startBoss(*e);
        return true;
    }
    if (action == "startBoss") {
        if (Enemy* e = enemy(w)) w.startBoss(*e);
        return true;
    }
    if (action == "kill" || action == "disable") {
        if (Enemy* e = enemy(w); e && e->alive()) e->kill(w);
        return true;
    }
    return false;
}

void EnemySpawnEntity::fixedUpdate(GameWorld& w, float /*dt*/) {
    if (!obj_.prop<bool>("boss", false)) return;
    Enemy* e = enemy(w);
    if (e && e->alive() && e->engaged() && w.activeBoss() != e) w.startBoss(*e);
}

void EnemySpawnEntity::render(RenderContext& ctx) {
    if (!ctx.editor) return;
    if (previewAnim_ && previewAnim_->texture) {
        const AnimationClip* c = previewAnim_->clip("idle");
        if (!c && !previewAnim_->clips.empty()) c = &previewAnim_->clips.begin()->second;
        if (c) {
            int f = static_cast<int>(ctx.time * 6.0f) % std::max(1, c->frameCount());
            Rect src = previewAnim_->frameRect(*c, f);
            bool flip = obj_.prop<bool>("faceLeft", false);
            Vec2 feet = obj_.rect.bottomCenter();
            float px = flip ? previewAnim_->frameWidth - previewAnim_->pivot.x : previewAnim_->pivot.x;
            Rect dst{feet.x - px, feet.y - previewAnim_->pivot.y, static_cast<float>(previewAnim_->frameWidth),
                     static_cast<float>(previewAnim_->frameHeight)};
            Color tint = obj_.prop<bool>("dormant", false) ? Color{255, 255, 255, 140} : Color::white();
            ctx.r.drawWorldTexture(*previewAnim_->texture, &src, dst, tint, BlendMode::Alpha, flip);
        }
    }
    float patrol = obj_.prop<float>("patrol", 96.0f);
    Vec2 f = obj_.rect.bottomCenter();
    ctx.r.drawWorldLine({f.x - patrol, f.y + 2}, {f.x + patrol, f.y + 2}, {255, 77, 109, 110}, 1.0f);
    if (obj_.prop<bool>("boss", false)) ctx.r.drawWorldRect(obj_.rect.expanded(4), {255, 77, 109, 200}, 2.0f);
}

}  // namespace cosmic
