#pragma once

// Boss training (boss statistics → « Entraînement »): the boss fought again in its own level,
// started just before its arena, with the hearts, power slots and powers the player chose, all
// its phases or a single one. A training run never counts: the world is marked
// (GameWorld::training()) and every recorder checks it (boss statistics, achievement
// counters, records, progression, creatures met); it is never recorded nor sent online.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

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

/// Hearts a training can give (the player's own maximum is the default).
constexpr int kTrainingMinHearts = 1;
constexpr int kTrainingMaxHearts = 20;

/// Sets up a training run in a world just loaded, before its first step: the run marked as
/// training, the player at the start chosen (the checkpoint before the arena, reached silently,
/// or in front of the arena when no checkpoint comes after the bosses before it), its hearts,
/// slots and powers, the boss at the start of the phase chosen. False when the boss is not in
/// this level.
bool applyBossTraining(GameWorld& w, const BossTrainingSetup& s);

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
