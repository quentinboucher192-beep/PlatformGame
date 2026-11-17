#include "game/gameplay/BossTraining.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "game/content/ContentDatabase.h"
#include "game/content/CreaturePlaces.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/player/Player.h"
#include "saves/SaveFile.h"

namespace cosmic {

std::optional<CreaturePlace> bossTrainingPlace(const CreatureAtlas& atlas, const ContentDatabase& content, const std::string& bossId) {
    auto it = atlas.places.find(bossId);
    if (it == atlas.places.end()) return std::nullopt;
    const CreaturePlace* pick = nullptr;
    for (const CreaturePlace& p : it->second) {
        if (!p.official || p.summoned || !content.level(p.levelId)) continue;
        if (!pick || (pick->planetId.empty() && !p.planetId.empty())) pick = &p;
    }
    if (!pick) return std::nullopt;
    return *pick;
}

std::vector<std::string> fitTrainingPowers(const ContentDatabase& content, const std::vector<std::string>& wanted, int slots) {
    // The same rules as in a level: granted one after the other on a player of its own, each
    // power on a key of its own (so two powers of the old special button go together).
    Player probe;
    probe.setKeysPerSlot(true);
    for (const std::string& id : wanted)
        if (const PowerDefinition* d = content.power(id)) probe.grantPower(*d);
    std::vector<std::string> out;
    // In the order chosen (a power replaced by a later one on the same input is gone).
    for (const std::string& id : wanted)
        if (probe.hasPower(id) && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    if (slots > 0 && static_cast<int>(out.size()) > slots) out.resize(static_cast<size_t>(slots));
    return out;
}

namespace {

EnemySpawnEntity* bossSpawn(GameWorld& w, const BossTrainingSetup& s) { return dynamic_cast<EnemySpawnEntity*>(w.entity(s.bossObject)); }

/// Where the level itself starts (its player start).
Vec2 levelStart(const GameWorld& w) {
    Vec2 start{w.level().bounds.x + 64.0f, w.level().bounds.y + 64.0f};
    for (const LevelObject& o : w.level().objects)
        if (o.type == "player_start") start = o.rect.bottomCenter();
    return start;
}

/// In front of the boss's arena (the combat zone holding it), on the ground, on the side the
/// level comes from. nullopt when no such place is found.
std::optional<Vec2> arenaFront(GameWorld& w, const LevelObject& boss) {
    const LevelData& level = w.level();
    const LevelObject* zone = nullptr;
    for (const LevelObject& o : level.objects) {
        if (o.type != "combat_zone") continue;
        std::string waves = o.propStr("waves");
        bool listed = !waves.empty() && waves.find(boss.id) != std::string::npos;
        if (listed || (waves.empty() && o.rect.contains(boss.rect.center()))) {
            zone = &o;
            break;
        }
    }
    if (!zone) return std::nullopt;
    Vec2 start = levelStart(w);
    const Vec2 size = w.player().config().size;
    for (float side : {start.x <= zone->rect.center().x ? -1.0f : 1.0f}) {
        for (float off : {72.0f, 120.0f, 180.0f, 260.0f}) {
            float x = side < 0.0f ? zone->rect.x - off : zone->rect.right() + off;
            Vec2 from{x, zone->rect.y + 8.0f}, to{x, zone->rect.bottom() + 640.0f};
            if (w.physics().overlapsSolid(Rect::fromBottomCenter(from, {2.0f, 2.0f}))) continue;
            auto hit = w.physics().raycast(from, to, true);
            if (!hit) continue;
            Vec2 feet{x, from.y + (to.y - from.y) * *hit - 1.0f};
            if (w.physics().overlapsSolid(Rect::fromBottomCenter(feet, size))) continue;
            return feet;
        }
    }
    return std::nullopt;
}

/// A place picked in the arena still holds the player when the run starts: the body inside
/// the boss's arena, in no wall (the full check is probeTrainingArena's, on the settings
/// screen; this one only refuses what could not be a start).
bool arenaSpawnFits(GameWorld& w, const Enemy& boss, Vec2 feet) {
    const std::optional<Rect> zone = w.arenaOf(boss);
    const Rect body = Rect::fromBottomCenter(feet, w.player().config().size);
    if (!zone || !zone->containsRect(body) || w.physics().overlapsSolid(body)) {
        Log::warn("Entraînement : lieu d'apparition ({}, {}) refusé, départ habituel", feet.x, feet.y);
        return false;
    }
    return true;
}

/// A surface a place may stand on: firm (no moving platform, no bounce pad, no door, gate or
/// wall that breaks), a crumbling platform only if it gives way to an event alone (Singularis's
/// floor: it holds until the level breaks it, and then it is gone from the probe already).
bool firmGround(const Collider& c) {
    if (!c.enabled || c.dynamic) return false;
    if (c.tags == ColliderTag::None) return true;
    if (c.tags != ColliderTag::Crumble || !c.owner) return false;
    return static_cast<const CrumblePlatformEntity*>(c.owner)->object().prop<bool>("onEvent", false);
}

/// Where the body must not be when the run starts: what hurts (spikes, liquids, active plasma
/// zones, lasers), what it would set off or take (pickups, power tablets, checkpoints, the exit,
/// triggers, doors, gates), what moves it (bounce pads, updrafts, slow zones, ladders) and the
/// way of the moving platforms. Decor, lights, signs, zones of the camera, of the arena or of a
/// secret, enemies (apart) and levers are harmless.
std::vector<Rect> keepOutAreas(GameWorld& w) {
    static const char* const kHarmless[] = {"decor",       "light", "emitter", "sign",  "combat_zone", "camera_zone",
                                            "secret_area", "enemy", "lever",   "receptor", "grapple_point"};
    std::vector<Rect> out;
    for (const auto& e : w.entities()) {
        if (e->removed) continue;
        const std::string& type = e->type();
        if (std::any_of(std::begin(kHarmless), std::end(kHarmless), [&](const char* h) { return type == h; })) continue;
        const LevelObject& o = e->object();
        if (type == "damage_zone") {
            if (e->active) out.push_back(e->box);  // off: plasma not risen yet (it may rise later in the fight)
        } else if (type == "shard" || type == "relic" || type == "health_orb" || type == "energy_orb" || type == "heart_container") {
            if (e->active && e->visible) out.push_back(e->overlapBox());  // hidden ones are brought later by the fight
        } else if (type == "laser") {
            // Its beam, at full length (it may come on at any moment).
            const float a = degToRad(o.rotation);
            const Vec2 dir{std::cos(a), std::sin(a)};
            const Vec2 from = e->box.center() - dir * (e->box.w * 0.5f - 8.0f), to = from + dir * (e->box.w - 8.0f);
            out.push_back(Rect{std::min(from.x, to.x), std::min(from.y, to.y), std::abs(to.x - from.x), std::abs(to.y - from.y)}.expanded(8.0f));
        } else if (type == "moving_platform") {
            // Its whole way (both ends, or around its orbit).
            const float dx = o.prop<float>("dx", 0.0f), dy = o.prop<float>("dy", 0.0f);
            Rect way = o.prop<bool>("orbit", false)
                           ? o.rect.translated({-std::abs(dx), -std::abs(dy)}).united(o.rect.translated({std::abs(dx), std::abs(dy)}))
                           : o.rect.united(o.rect.translated({dx, dy}));
            out.push_back(way.united(e->box));
        } else {
            out.push_back(e->overlapBox().united(e->box));
        }
    }
    return out;
}

}  // namespace

bool applyBossTraining(GameWorld& w, const BossTrainingSetup& s) {
    EnemySpawnEntity* spawn = bossSpawn(w, s);
    Enemy* boss = spawn ? spawn->enemy(w) : nullptr;
    if (!boss || boss->def().id != s.bossId) return false;
    w.setTraining(true);
    // The start: the place picked in the arena (2.41), facing the boss; else the checkpoint
    // before the arena, reached silently (the place a death would bring back to), else right in
    // front of the arena, else the level's own start.
    LevelEntity* checkpoint = s.checkpointId.empty() ? nullptr : w.entity(s.checkpointId);
    if (s.arenaSpawn && arenaSpawnFits(w, *boss, *s.arenaSpawn)) {
        w.startAt(*s.arenaSpawn, boss->center().x < s.arenaSpawn->x);
    } else if (checkpoint) {
        w.startAt(checkpoint->object().rect.bottomCenter());
        checkpoint->handleAction(w, "activate", Json::object());
    } else if (auto front = arenaFront(w, spawn->object())) {
        w.startAt(*front);
    } else {
        w.startAt(levelStart(w));
    }
    Player& p = w.player();
    // Hearts: the player's own maximum, moved by the bonus (heart containers still add one).
    int base = p.maxHealth() - p.bonusHealth;
    p.bonusHealth = std::clamp(s.hearts, kTrainingMinHearts, kTrainingMaxHearts) - base;
    p.health = p.maxHealth();
    // Slots and powers: exactly the ones chosen (the level's start powers are not given).
    int slots = std::clamp(s.slots, 1, LevelData::kMaxPowerSlots);
    p.clearPowers();
    p.setPowerSlots(slots);
    for (const std::string& id : fitTrainingPowers(w.content(), s.powers, slots))
        if (const PowerDefinition* d = w.content().power(id)) p.grantPower(*d);
    // A single phase: the boss starts it with the life it has when it enters it.
    jumpBossToPhase(w, *boss, s.phase);
    return true;
}

void jumpBossToPhase(GameWorld& w, Enemy& boss, int phase) {
    const auto& phases = boss.def().phases;
    if (phase <= 0 || phase >= static_cast<int>(phases.size())) return;
    // 2.55: a phase that follows a rebirth (the Phénix's « Cendres ») starts with the health the
    // boss is reborn with, as in the whole fight.
    const float rebirth = phases[static_cast<size_t>(phase - 1)].rebirth;
    boss.setWounds(boss.maxHp() * (rebirth > 0.0f ? rebirth : phases[static_cast<size_t>(phase)].hpThreshold), phase);
    // The arena as the phases before left it (2.33: Singularis's floor collapsed, its plasma,
    // its platforms in orbit): the level's answers to those phases, at once.
    if (!boss.objectId().empty())
        for (int k = 1; k <= phase; ++k) w.emit(boss.objectId(), "bossPhase", {{"phase", k + 1}});
}

const Enemy* bossTrainingEnemy(GameWorld& w, const BossTrainingSetup& s) {
    EnemySpawnEntity* spawn = bossSpawn(w, s);
    return spawn ? spawn->enemy(w) : nullptr;
}

bool bossTrainingWon(GameWorld& w, const BossTrainingSetup& s) {
    const Enemy* e = bossTrainingEnemy(w, s);
    if (!e || !e->alive()) return true;  // beaten (a boss only leaves its level beaten)
    return s.phase >= 0 && e->phaseIndex() > s.phase;
}

// --------------------------------------------------------------- « Lieu d'apparition » (2.41)

int TrainingArena::find(Vec2 feet) const {
    for (size_t i = 0; i < spots.size(); ++i)
        if (std::abs(spots[i].feet.x - feet.x) < 1.0f && std::abs(spots[i].feet.y - feet.y) < 1.0f) return static_cast<int>(i);
    return -1;
}

int TrainingArena::nearest(Vec2 p, float maxDistance) const {
    int best = -1;
    float bd = maxDistance * maxDistance;
    for (size_t i = 0; i < spots.size(); ++i) {
        // The body, not the feet: a click on the player's height picks that place.
        float d = (spots[i].feet - Vec2{0.0f, body.y * 0.5f} - p).lengthSq();
        if (d <= bd) {
            bd = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int TrainingArena::step(int from, int dx, int dy) const {
    if (spots.empty()) return -1;
    if (from < 0 || from >= static_cast<int>(spots.size())) return suggested();
    const Vec2 c = spots[static_cast<size_t>(from)].feet;
    int best = -1;
    float bestScore = 1e30f;
    for (size_t i = 0; i < spots.size(); ++i) {
        const Vec2 d = spots[i].feet - c;
        const float along = dx != 0 ? d.x * static_cast<float>(dx) : d.y * static_cast<float>(dy);
        if (along < 0.5f) continue;
        const float across = dx != 0 ? std::abs(d.y) : std::abs(d.x);
        // Sideways: along the same surface first; up or down: the surface right above or below.
        const float score = along + (dx != 0 ? 3.0f : 2.0f) * across;
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int TrainingArena::suggested() const {
    int best = -1;
    for (size_t i = 0; i < spots.size(); ++i) {
        if (best < 0) {
            best = static_cast<int>(i);
            continue;
        }
        const Vec2 a = spots[i].feet, b = spots[static_cast<size_t>(best)].feet;
        if (a.y > b.y + 0.5f || (std::abs(a.y - b.y) <= 0.5f && std::abs(a.x - boss.center().x) > std::abs(b.x - boss.center().x))) best = static_cast<int>(i);
    }
    return best;
}

std::optional<TrainingArena> probeTrainingArena(GameWorld& w, const BossTrainingSetup& s) {
    // The run as it would start from its usual place (the boss at its phase, the level's
    // answers to the phases before it given at once), silent.
    w.silent = true;
    BossTrainingSetup usual = s;
    usual.arenaSpawn.reset();
    if (!applyBossTraining(w, usual)) return std::nullopt;
    EnemySpawnEntity* spawn = bossSpawn(w, s);
    Enemy* boss = spawn ? spawn->enemy(w) : nullptr;
    const std::optional<Rect> zone = boss ? w.arenaOf(*boss) : std::nullopt;
    if (!zone) return std::nullopt;
    TrainingArena a;
    a.zone = *zone;
    a.boss = boss->box;
    a.usualStart = w.player().feet();
    a.body = w.player().config().size;
    // At the very start: what is solid (a floor about to give way too) and the other enemies.
    const Rect around = zone->expanded(64.0f);
    std::vector<Rect> solidAtStart, enemies;
    w.physics().forEachIn(around, [&](const Collider& c) {
        if (c.enabled && c.kind == ColliderKind::Solid && !(c.tags & ColliderTag::EnemyOnly)) solidAtStart.push_back(c.box);
    });
    for (const auto& e : w.enemies())
        if (e.get() != boss && e->alive() && e->box.intersects(around)) enemies.push_back(e->box.expanded(24.0f));
    // A second later, the arena as the phase leaves it (what the level does a little after),
    // the player standing still at the usual start, outside.
    for (int i = 0; i < 120; ++i) w.fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
    const std::vector<Rect> keepOut = keepOutAreas(w);
    auto hits = [](const std::vector<Rect>& list, const Rect& r) { return std::any_of(list.begin(), list.end(), [&](const Rect& o) { return o.intersects(r); }); };
    const Vec2 size = w.player().config().size;
    const float headroom = 3.0f * size.y;  // no crawl space under a floor
    const Rect bossArea = a.boss.expanded(TrainingArena::kBossMargin);
    // Firm under each edge of the feet, at their height (not hanging over a ledge's end).
    auto standsOn = [&](float x, float feetY) -> const Collider* {
        const Collider* g = w.physics().groundBelow(Rect{x - 1.0f, feetY - size.y, 2.0f, size.y});
        return g && firmGround(*g) && std::abs(g->box.y - feetY) < 0.5f ? g : nullptr;
    };
    for (float x = zone->x + TrainingArena::kStep * 0.5f; x < zone->right(); x += TrainingArena::kStep) {
        // The surfaces under this column, from the top.
        std::vector<float> tops;
        w.physics().forEachIn(Rect{x - size.x * 0.5f, zone->y, size.x, zone->h + 1.0f}, [&](const Collider& c) {
            if (firmGround(c) && c.box.y > zone->y + size.y && c.box.y <= zone->bottom() + 0.5f) tops.push_back(c.box.y);
        });
        std::sort(tops.begin(), tops.end());
        tops.erase(std::unique(tops.begin(), tops.end(), [](float p, float q) { return std::abs(p - q) < 0.5f; }), tops.end());
        for (float y : tops) {
            const Vec2 feet{x, y};
            const Rect body = Rect::fromBottomCenter(feet, size);
            if (!zone->containsRect(body) || w.physics().overlapsSolid(body) || hits(solidAtStart, body)) continue;
            const Collider* left = standsOn(body.x + 1.0f, y);
            const Collider* right = standsOn(body.right() - 1.0f, y);
            if (!left || !right) continue;
            const Rect above{body.x, y - headroom, body.w, headroom - size.y};
            if (w.physics().overlapsSolid(above) || hits(solidAtStart, above)) continue;
            if (body.intersects(bossArea) || hits(enemies, body) || hits(keepOut, body)) continue;
            a.spots.push_back({feet, left->kind == ColliderKind::OneWay && right->kind == ColliderKind::OneWay});
        }
    }
    return a;
}

// --------------------------------------------------------------- the last choices

namespace {
SaveFile prefsFile() { return SaveFile("boss_training", BossTrainingPrefs::kVersion); }
fs::path prefsPath() { return Paths::savesDir() / "boss_training.json"; }
}  // namespace

const BossTrainingChoice* BossTrainingPrefs::find(const std::string& bossId) const {
    auto it = choices_.find(bossId);
    return it == choices_.end() ? nullptr : &it->second;
}

Json BossTrainingPrefs::toJson() const {
    Json b = Json::object();
    for (const auto& [id, c] : choices_) {
        b[id] = {{"hearts", c.hearts}, {"slots", c.slots}, {"powers", c.powers}, {"phase", c.phase}};
        if (c.spawn) b[id]["spawn"] = {c.spawn->x, c.spawn->y};
    }
    return {{"bosses", b}};
}

void BossTrainingPrefs::fromJson(const Json& j) {
    choices_.clear();
    if (!j.contains("bosses") || !j["bosses"].is_object()) return;
    for (const auto& [id, v] : j["bosses"].items()) {
        BossTrainingChoice c;
        c.hearts = std::clamp(jget<int>(v, "hearts", 0), 0, kTrainingMaxHearts);
        c.slots = std::clamp(jget<int>(v, "slots", 0), 0, LevelData::kMaxPowerSlots);
        c.powers = jget<std::vector<std::string>>(v, "powers", {});
        c.phase = std::max(-1, jget<int>(v, "phase", -1));
        // The place picked in the arena: checked against the arena as it is now when the
        // settings open (a level changed since: the usual start again).
        if (auto s = v.find("spawn"); s != v.end() && s->is_array() && s->size() == 2 && (*s)[0].is_number() && (*s)[1].is_number()) {
            const float x = (*s)[0].get<float>(), y = (*s)[1].get<float>();
            if (std::isfinite(x) && std::isfinite(y)) c.spawn = Vec2{x, y};
        }
        choices_[id] = c;
    }
}

void BossTrainingPrefs::load() {
    if (auto d = prefsFile().load(prefsPath())) fromJson(*d);
}

bool BossTrainingPrefs::save() const { return prefsFile().save(prefsPath(), toJson()); }

}  // namespace cosmic
