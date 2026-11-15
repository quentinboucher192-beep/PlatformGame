#include "game/gameplay/BossTraining.h"

#include <algorithm>

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

}  // namespace

bool applyBossTraining(GameWorld& w, const BossTrainingSetup& s) {
    EnemySpawnEntity* spawn = bossSpawn(w, s);
    Enemy* boss = spawn ? spawn->enemy(w) : nullptr;
    if (!boss || boss->def().id != s.bossId) return false;
    w.setTraining(true);
    // The start: the checkpoint before the arena, reached silently (the place a death would
    // bring back to), else right in front of the arena, else the level's own start.
    LevelEntity* checkpoint = s.checkpointId.empty() ? nullptr : w.entity(s.checkpointId);
    if (checkpoint) {
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
    const auto& phases = boss->def().phases;
    if (s.phase > 0 && s.phase < static_cast<int>(phases.size()))
        boss->setWounds(boss->maxHp() * phases[static_cast<size_t>(s.phase)].hpThreshold, s.phase);
    return true;
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
    for (const auto& [id, c] : choices_) b[id] = {{"hearts", c.hearts}, {"slots", c.slots}, {"powers", c.powers}, {"phase", c.phase}};
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
        choices_[id] = c;
    }
}

void BossTrainingPrefs::load() {
    if (auto d = prefsFile().load(prefsPath())) fromJson(*d);
}

bool BossTrainingPrefs::save() const { return prefsFile().save(prefsPath(), toJson()); }

}  // namespace cosmic
