// Zones: triggers, combat arenas, camera zones, secret areas and enemy spawns.

#include <cmath>

#include "engine/physics/PhysicsWorld.h"
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

Json TriggerEntity::keepState(GameWorld&) const { return fired_ ? Json{{"fired", true}} : Json(nullptr); }

void TriggerEntity::restoreKept(GameWorld&, const Json& state) {
    if (jget<bool>(state, "fired", false)) fired_ = true;
}

void TriggerEntity::render(RenderContext& ctx) {
    if (ctx.editor) drawZone(ctx, box, {120, 255, 120, 255}, "Déclencheur");
}

// ------------------------------------------------------------- combat zone

size_t CombatZoneEntity::countWaves(const LevelObject& zone, const LevelData& level) {
    size_t n = 0;
    for (const auto& wave : splitList(zone.propStr("waves"), ';'))
        if (!splitList(wave, ',').empty()) ++n;
    if (n == 0)
        for (const auto& o : level.objects)
            if (o.type == "enemy" && zone.rect.contains(o.rect.center())) return 1;
    return n;
}

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
    // 2.62.2: an ambush when none of its enemies is a boss.
    ambush_ = !waves_.empty();
    for (const auto& wave : waves_)
        for (const auto& id : wave)
            for (const auto& o : w.level().objects)
                if (o.id == id && o.type == "enemy") {
                    const EnemyDefinition* d = w.enemyDefinition(o.propStr("enemy", "glowmite"));
                    if (o.prop<bool>("boss", false) || (d && d->isBoss)) ambush_ = false;
                }
}

void CombatZoneEntity::setDoors(GameWorld& w, bool open) {
    // 2.80 (rules of cooperation 4): closed by the arena (« from »), its doors (or gates, lasers) have the
    // arena's side as their inner side: in a team, each one waits open until every player is in.
    const Json params = open ? Json::object() : Json{{"from", id()}};
    for (const auto& d : doors_)
        if (LevelEntity* e = w.entity(d)) e->handleAction(w, open ? "open" : "close", params);
}

void CombatZoneEntity::startWave(GameWorld& w, size_t index) {
    wave_ = index;
    if (index >= waves_.size()) return;
    for (const auto& id : waves_[index]) {
        auto* es = dynamic_cast<EnemySpawnEntity*>(w.entity(id));
        if (!es || es->defeated()) continue;
        Enemy* e = es->enemy(w);
        if (!e || !e->alive()) es->respawn(w, false);
        else if (e->dormant()) e->wake(w);
        if (Enemy* en = es->enemy(w)) en->alert(w);
    }
    if (index > 0) w.hint("Vague " + std::to_string(index + 1) + " / " + std::to_string(waves_.size()), 2.0f);
}

bool CombatZoneEntity::lists(const std::string& objectId) const {
    if (objectId.empty()) return false;
    for (const auto& wave : waves_)
        for (const auto& id : wave)
            if (id == objectId) return true;
    return false;
}

void CombatZoneEntity::onPlayerEnter(GameWorld& w) {
    if (suspended_ && started_ && !cleared_) {
        // 2.62.2: Nova is back in the ambush she fell in: it goes on where it was.
        suspended_ = false;
        setDoors(w, false);
        if (obj_.prop<bool>("lockCamera", true)) {
            CameraZone z;
            z.area = box.expanded(16);
            z.clampToArea = true;
            w.lockCameraZone(&z);
        }
        w.hint("L'embuscade reprend", 1.6f);
        return;
    }
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
    if (ambush_) bringBack(w);
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
    suspended_ = false;
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
    if (ambush_) {  // 2.62.2 (simulation 14): kept as it is, waiting for Nova
        suspend(w);
        return;
    }
    started_ = false;
    waveDelay_ = 0.0f;
    setDoors(w, true);
    bool explicitWaves = !obj_.propStr("waves").empty();
    bool keepWounds = obj_.prop<bool>("bossKeepsDamage", true);
    for (const auto& wave : waves_)
        for (const auto& id : wave)
            if (auto* es = dynamic_cast<EnemySpawnEntity*>(w.entity(id))) {
                if (keepWounds) es->respawnKeepingWounds(w, explicitWaves);
                else es->respawn(w, explicitWaves);
            }
}

void CombatZoneEntity::suspend(GameWorld& w) {
    if (!ambush_ || !started_ || cleared_ || suspended_) return;
    suspended_ = true;
    setDoors(w, true);  // she can walk back in; they close behind her (onPlayerEnter)
    if (obj_.prop<bool>("lockCamera", true)) w.lockCameraZone(nullptr);
}

size_t CombatZoneEntity::enemyCount() const {
    size_t n = 0;
    for (const auto& wave : waves_) n += wave.size();
    return n;
}

int CombatZoneEntity::enemiesLeft(GameWorld& w) const {
    if (!started_) return static_cast<int>(enemyCount());
    if (cleared_) return 0;
    int n = 0;
    for (size_t i = wave_; i < waves_.size(); ++i)
        for (const auto& id : waves_[i]) {
            auto* es = dynamic_cast<EnemySpawnEntity*>(w.entity(id));
            if (!es || es->defeated()) continue;
            // The current wave: its enemies still alive; a wave to come: all of its own.
            if (i > wave_ || es->enemyAlive(w)) ++n;
        }
    return n;
}

std::vector<const Enemy*> CombatZoneEntity::fightingEnemies(GameWorld& w) const {
    std::vector<const Enemy*> out;
    if (!started_ || cleared_ || wave_ >= waves_.size()) return out;
    for (const auto& id : waves_[wave_])
        if (auto* es = dynamic_cast<EnemySpawnEntity*>(w.entity(id)))
            if (const Enemy* e = es->enemy(w); e && e->alive() && !e->dormant()) out.push_back(e);
    return out;
}

Vec2 CombatZoneEntity::returnPoint(GameWorld& w, const Enemy& e, Vec2 spawn) const {
    MoveOptions opt;
    opt.isEnemy = true;
    const float hw = e.box.w * 0.5f, h = e.box.h;
    auto at = [&](Vec2 feet) { return Rect{feet.x - hw, feet.y - h, e.box.w, h}; };
    auto fits = [&](Vec2 feet) {
        Rect c = at(feet);
        return box.containsRect(c) && !w.physics().overlapsSolid(c, opt) && (e.def().flying || w.physics().groundBelow(c, opt));
    };
    if (fits(spawn)) return spawn;
    // The nearest free place of the arena: columns 16 pixels apart (the same order on every
    // machine); a walker on the first floor down its column, a flyer anywhere along it.
    Vec2 best = spawn;
    float bestD = 1e30f;
    auto consider = [&](Vec2 feet) {
        const float d = (feet - spawn).lengthSq();
        if (d < bestD && fits(feet)) {
            bestD = d;
            best = feet;
        }
    };
    for (float x = box.x + hw; x <= box.right() - hw; x += 16.0f) {
        if (e.def().flying) {
            for (float y = box.y + h; y <= box.bottom(); y += 16.0f) consider({x, y});
            continue;
        }
        const Vec2 from{x, box.y + 1.0f}, to{x, box.bottom() + 2.0f};
        if (auto t = w.physics().raycast(from, to, true)) consider({x, std::round(from.y + (to.y - from.y) * *t)});
    }
    return best;
}

void CombatZoneEntity::bringBack(GameWorld& w) {
    // A few pixels of slack: an enemy pressed against a wall or a door stays where it is.
    const Rect keep = box.expanded(24.0f);
    for (const auto& wave : waves_)
        for (const auto& id : wave) {
            auto* es = dynamic_cast<EnemySpawnEntity*>(w.entity(id));
            Enemy* e = es ? es->enemy(w) : nullptr;
            if (!e || !e->alive() || e->dormant() || e->skyborne() || keep.contains(e->center())) continue;
            const Vec2 from = e->center();
            const Vec2 feet = returnPoint(w, *e, es->spawnFeet());
            e->box.x = feet.x - e->box.w * 0.5f;
            e->box.y = feet.y - e->box.h;
            e->prevBox = e->box;
            e->vel = {0.0f, 0.0f};
            // Seen: it vanishes where it was and appears in the arena (display only).
            w.particles().emit("enemy_spawn", from, 1.0f);
            w.particles().emit("enemy_spawn", e->center(), 1.0f);
            w.playSound("sounds/summon.wav", e->center(), 0.6f);
        }
}

bool CombatZoneEntity::handleAction(GameWorld& w, const std::string& action, const Json&) {
    if (action == "activate" || action == "start") {
        if (suspended_) return true;  // only Nova coming back resumes it (never behind her)
        bool was = playerInside;
        playerInside = true;
        onPlayerEnter(w);
        playerInside = was;
        return true;
    }
    return false;
}

// 2.72 (raids): an arena cleared stays cleared: its doors open, its enemies gone, never again.
Json CombatZoneEntity::keepState(GameWorld&) const { return cleared_ ? Json{{"cleared", true}} : Json(nullptr); }

void CombatZoneEntity::restoreKept(GameWorld& w, const Json& state) {
    if (!jget<bool>(state, "cleared", false) || cleared_) return;
    cleared_ = started_ = true;
    suspended_ = false;
    waveDelay_ = 0.0f;
    wave_ = waves_.size();
    for (const auto& d : doors_)
        if (auto* door = dynamic_cast<DoorEntity*>(w.entity(d))) door->setOpen(w, true, true);
    for (const auto& wave : waves_)
        for (const auto& id : wave)
            if (auto* es = dynamic_cast<EnemySpawnEntity*>(w.entity(id)))
                if (Enemy* e = es->enemy(w); e && e->alive()) e->despawn();
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
        if (const EnemyDefinition* d = w.enemyDefinition(obj_.propStr("enemy", "glowmite"))) {
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
    if (Enemy* old = enemy(w)) {
        // Its creatures go with it (2.17: a boss fight restarted after the player's death
        // starts without the previous fight's minions).
        for (const auto& m : w.enemies())
            if (m->summonedBy == old->uid() && m->alive()) m->despawn();
        old->despawn();
    }
    Enemy* e = w.spawnEnemy(obj_.propStr("enemy", "glowmite"), obj_.rect.bottomCenter(), obj_.prop<bool>("faceLeft", false),
                            id(), obj_.prop<float>("patrol", 96.0f));
    enemyUid_ = e ? e->uid() : 0;
    if (e && dormant) e->setDormant();
}

void EnemySpawnEntity::respawnKeepingWounds(GameWorld& w, bool dormant) {
    Enemy* old = enemy(w);
    if (!old || !old->def().isBoss) {
        respawn(w, dormant);
        return;
    }
    if (!old->alive()) {
        defeated_ = true;
        return;
    }
    float hp = old->hp();
    int phase = old->phaseIndex();
    respawn(w, dormant);
    if (Enemy* e = enemy(w)) e->setWounds(hp, phase);
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

// 2.72 (raids): an enemy beaten stays beaten when the group comes back (an arena not cleared
// calls its waves again itself).
Json EnemySpawnEntity::keepState(GameWorld& w) const {
    if (enemyUid_ == 0) return nullptr;
    const Enemy* e = enemy(w);
    return !e || !e->alive() ? Json{{"gone", true}} : Json(nullptr);
}

void EnemySpawnEntity::restoreKept(GameWorld& w, const Json& state) {
    if (!jget<bool>(state, "gone", false)) return;
    if (Enemy* e = enemy(w); e && e->alive()) e->despawn();
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
