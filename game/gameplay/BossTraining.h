#pragma once

// Boss training (boss statistics → « Entraînement »): the boss fought again in its own level,
// started just before its arena, with the hearts, power slots and powers the player chose, all
// its phases or a single one. A training run never counts: the world is marked
// (GameWorld::training()) and every recorder checks it (boss statistics, achievement
// counters, records, progression, creatures met); it is never recorded nor sent online.
// 2.41: the player may choose where to appear in the arena (« Lieu d'apparition »), among the
// places of firm ground computed from the level (probeTrainingArena): a start state of the run.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/gameplay/StartPos.h"

namespace cosmic {

class ContentDatabase;
class Enemy;
class GameWorld;
struct CreatureAtlas;
struct CreaturePlace;
struct LevelData;

/// What a training run is set up with (PlayOptions::bossTraining), applied when the world is
/// set up, before its first step.
struct BossTrainingSetup {
    std::string bossId;        // the boss fought (its definition)
    std::string levelId;       // the level it is placed in
    std::string bossObject;    // its level object
    std::string checkpointId;  // the checkpoint the run starts from ("" = in front of the arena)
    Vec2 spawn;                // feet of the player at that checkpoint
    int hearts = 10;
    int slots = 2;                    // power slots (1..LevelData::kMaxPowerSlots)
    std::vector<std::string> powers;  // carried at the start, at most `slots`
    int phase = -1;                   // -1: every phase; else the only phase fought (0 = the first)
    /// « Lieu d'apparition » (2.41): the player's feet in the arena, where the run starts facing
    /// the boss (the world's respawn point too); nullopt: the usual start above.
    std::optional<Vec2> arenaSpawn;
};

/// How a training run ended (its end screen).
struct BossTrainingResult {
    bool won = false;
    BossTrainingSetup setup;  // to start again, or change the settings
    std::string bossName;
    float time = 0.0f;        // seconds from the start of the fight
    int damage = 0;           // hearts lost
    int phase = 0;            // the phase reached (0 = the first)
    int phases = 0;
    std::string phaseName;
};

/// Where a boss is trained: its own place in a campaign level first (not its echoes), else any
/// official place of it. nullopt: it is in no level of the game.
std::optional<CreaturePlace> bossTrainingPlace(const CreatureAtlas& atlas, const ContentDatabase& content, const std::string& bossId);

/// The powers really carried with `wanted` granted in order, as in a level since 2.24 (each
/// power on a key of its own: none replaces another; each once), at most `slots` of them (the
/// first ones kept).
std::vector<std::string> fitTrainingPowers(const ContentDatabase& content, const std::vector<std::string>& wanted, int slots);

/// Section training (« Répéter un segment »): how an attempt starts. Applied to a world just
/// loaded and seeded, before its first step, by the run itself (GameScene) and, since 2.60.3,
/// by its replay (ReplayPlayback): the same start, so that the replay plays it exactly.
struct SectionTrainingStart {
    std::string checkpointId;  // the checkpoint it starts from, reached silently ("" = the level's start)
    bool hasSpawn = false;     // the player placed at `spawn` (the checkpoint); else the level's start
    Vec2 spawn;
    std::vector<std::string> powers;  // chosen ones, or the tablets placed before the start on the route
    bool powersChosen = false;        // exactly `powers`; else they are added to the level's start powers
    /// 2.91: an attempt from a StartPos (game/gameplay/StartPos.h): Nova set as it was taken
    /// (the fields above are then unused; the attempt has no start checkpoint).
    std::optional<StartPosState> startPos;
    Json toJson() const;
    /// Never trusts the file: nullopt when a field is damaged.
    static std::optional<SectionTrainingStart> fromJson(const Json& j);
};
void applySectionTraining(GameWorld& w, const SectionTrainingStart& s);

/// Hearts a training can give (the player's own maximum is the default).
constexpr int kTrainingMinHearts = 1;
constexpr int kTrainingMaxHearts = 20;

/// Sets up a training run in a world just loaded, before its first step: the run marked as
/// training, the player at the start chosen (the place picked in the arena, facing the boss;
/// else the checkpoint before the arena, reached silently, or in front of the arena when no
/// checkpoint comes after the bosses before it), its hearts, slots and powers, the boss at the
/// start of the phase chosen. False when the boss is not in this level.
bool applyBossTraining(GameWorld& w, const BossTrainingSetup& s);

/// One place of the arena where the player may appear (« Lieu d'apparition », 2.41).
struct TrainingSpot {
    Vec2 feet;           // the player's feet
    bool ledge = false;  // on a platform (one-way), not on the ground of the arena
};

/// The boss's arena as a training run finds it, and the places where the player may appear.
struct TrainingArena {
    static constexpr float kStep = 16.0f;        // between two places along a surface
    static constexpr float kBossMargin = 40.0f;  // kept clear around the boss's body
    Rect zone;                        // the combat zone holding the boss
    Rect boss;                        // the boss's body when the run starts
    Vec2 usualStart;                  // the player's feet at the usual start
    Vec2 body{10.0f, 22.0f};          // the player's body (its size)
    std::vector<TrainingSpot> spots;  // left to right, then top to bottom

    /// The spot at `feet` (within a pixel); -1 when none is there.
    int find(Vec2 feet) const;
    /// The spot whose body (its centre) is nearest to `p`; -1 when there is none within
    /// `maxDistance`.
    int nearest(Vec2 p, float maxDistance = 1e9f) const;
    /// The arrows from spot `from`: (±1, 0) along its surface (else the nearest spot that way),
    /// (0, ±1) the nearest surface above or below; -1 when no spot lies that way.
    int step(int from, int dx, int dy) const;
    /// The spot proposed first: on the lowest surface, the farthest from the boss.
    int suggested() const;
};

/// The arena of the training `s` in `w` (a world of its level just loaded in Play mode, used
/// for nothing else): set up as the run would be (from its usual start), given a moment to take
/// the shape of the phase chosen (the level's answers to the phases before it: a floor that
/// collapses, plasma that rises…), then every valid place listed, on a grid of kStep pixels:
/// standing on firm ground (no moving, bouncing or crumbling surface), the whole body inside
/// the arena and free (no wall, nothing solid just above the head, nothing solid there at the
/// start either), out of the hazards (spikes, liquids, active plasma, lasers…) and of what the
/// body would set off (pickups, doors, checkpoints, triggers, ladders, updrafts…), away from
/// the boss's body and from the other enemies. nullopt when the boss or its arena is not there.
std::optional<TrainingArena> probeTrainingArena(GameWorld& w, const BossTrainingSetup& s);

/// The boss jumps to the start of `phase` (0 = the first) with the life it has there, and the
/// level answers the phases before it at once (2.33: an arena changed by the fight, as the
/// giga boss's): the training of a single phase, and the automated captures (--boss-phase).
void jumpBossToPhase(GameWorld& w, Enemy& boss, int phase);

/// The boss of the training (nullptr when it is gone).
const Enemy* bossTrainingEnemy(GameWorld& w, const BossTrainingSetup& s);
/// The training is won: the phase chosen is beaten (or the boss, for every phase).
bool bossTrainingWon(GameWorld& w, const BossTrainingSetup& s);

/// The last choices of the player, per boss (saves/boss_training.json).
struct BossTrainingChoice {
    int hearts = 0;  // 0: not chosen yet
    int slots = 0;
    std::vector<std::string> powers;
    int phase = -1;
    std::optional<Vec2> spawn;  // « Lieu d'apparition » (2.41): nullopt = the usual start
};
class BossTrainingPrefs {
public:
    static constexpr int kVersion = 1;
    void load();
    bool save() const;
    const BossTrainingChoice* find(const std::string& bossId) const;
    void set(const std::string& bossId, const BossTrainingChoice& c) { choices_[bossId] = c; }
    Json toJson() const;
    void fromJson(const Json& j);

private:
    std::map<std::string, BossTrainingChoice> choices_;
};

}  // namespace cosmic
