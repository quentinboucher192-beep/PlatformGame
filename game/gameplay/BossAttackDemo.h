#pragma once
// The example of a boss's attack on its sheet (2.60, Statistiques des boss › Fiche): a small
// scene of the game engine, the boss in its own arena (its level, as a training run starts it,
// game/gameplay/BossTraining.h) or in a neutral arena when it is in no level of the game, at the
// phase that first uses the attack (its last stand for an attack of its « Dernier souffle »),
// giving that attack again and again; Nova stands in the arena and dodges what is simple to
// dodge (she jumps over what comes along the ground, ducks under what comes at head height,
// steps out of the marks on the ground, keeps moving under the missiles).
//
// The scene never counts: its world is a training run (GameWorld::training(): every recorder
// ignores it) where Nova cannot be hurt, silent (an audio system never started), with no input
// device (no rumble), and the boss plays a copy of its definition that knows this attack only
// (an enemy revision of a private copy of the content: the game's own content is untouched).
// Nothing of it reaches the records, the statistics, the achievements or a real run.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Math.h"
#include "engine/input/InputManager.h"
#include "game/enemies/BossAttackSheet.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class ContentDatabase;
class Enemy;
class GameWorld;
class ResourceManager;

/// The boss's definition as the example plays it: every phase knows `attackId` only (its last
/// stand too, for an attack of its last stand), without rage (the phase's own pace).
EnemyDefinition demoDefinition(const EnemyDefinition& d, const std::string& attackId, const AttackPreviewPlan& plan);

/// How far from the boss Nova stands for `a` (world pixels, between the centres).
float demoNovaDistance(const AttackDef& a);

/// The part of the world shown in a frame of proportions `aspect` (width / height): the boss
/// and Nova with room around them, from `minWidth` to `maxWidth` wide (Nova always in view: of
/// a boss too far or too high, what fits), inside `arena` (a little beyond its edges when the
/// frame is wider than the arena).
Rect demoWindow(const Rect& arena, const Rect& boss, const Rect& nova, float aspect, float minWidth = 520.0f, float maxWidth = 1000.0f);

class BossAttackDemo {
public:
    /// The revision under which the private copy of the content holds the boss of the example.
    static constexpr int kRevision = 999999;
    /// Fixed steps the example gets to start the attack a first time before it gives up.
    static constexpr int kMaxWarmSteps = 120 * 25;

    BossAttackDemo();
    ~BossAttackDemo();
    BossAttackDemo(const BossAttackDemo&) = delete;
    BossAttackDemo& operator=(const BossAttackDemo&) = delete;

    /// Sets the scene up. `content`: a private copy of the game's content (the example adds
    /// the boss's revision to it); `level`: the boss's own level with `bossObject` its level
    /// object, or nullopt for a neutral arena. False (and `error`, in words) when it cannot be.
    bool setup(std::shared_ptr<ContentDatabase> content, ResourceManager& res, const EnemyDefinition& def, const std::string& attackId,
               std::optional<LevelData> level, const std::string& bossObject, std::string* error = nullptr);

    /// Brings the scene to the first wind-up of the attack, `steps` fixed steps at most; true
    /// once it has begun (failed(): it never did within kMaxWarmSteps).
    bool warmUp(int steps);
    bool ready() const { return ready_; }
    bool failed() const { return !failure_.empty(); }
    const std::string& failure() const { return failure_; }
    /// One fixed step (1/120 s): Nova's moves, the attack given again once the boss is free.
    void step();

    GameWorld* world() { return world_.get(); }
    const GameWorld* world() const { return world_.get(); }
    const Enemy* boss() const;
    /// The arena of the fight (the combat zone holding the boss, or the neutral arena).
    Rect arena() const { return arena_; }
    /// Times the attack has begun (its wind-up), and seconds of example played since ready.
    int shown() const { return shown_; }
    float time() const { return time_; }
    /// The phase the boss is in (0 = the first), and whether it is in its last stand.
    int phase() const;
    bool lastStand() const;
    const std::string& attackId() const { return attackId_; }
    /// The world ended (a time limit, an exit…): the example has to be set up again.
    bool over() const;

private:
    Enemy* bossMut() const;
    void lookAround();
    PlayerInput novaInput();
    bool bossFree(const Enemy& b) const;
    void tryForce(Enemy& b);

    AudioSystem silence_;  // never started: nothing of the example is heard
    std::shared_ptr<ContentDatabase> content_;
    std::unique_ptr<EnemyDefinition> neutralDef_;  // the boss of a neutral arena (outlives the world)
    std::unique_ptr<GameWorld> world_;
    std::string attackId_;
    std::string attackName_;
    AttackPreviewPlan plan_;
    uint32_t bossUid_ = 0;
    Rect arena_;
    Vec2 home_;               // Nova's place
    bool ready_ = false;
    std::string failure_;
    int warmSteps_ = 0;
    int shown_ = 0;
    bool wasAttacking_ = false;
    float time_ = 0.0f;
    float freeTime_ = 0.0f;
    // Nova's moves.
    int jumpHold_ = 0;        // steps the jump is still held
    int crouch_ = 0;          // steps she stays down
    int dodgeDir_ = 0;        // a step aside under way (-1 / +1), for dodgeSteps_ more steps
    int dodgeSteps_ = 0;
    float weave_ = 0.0f;      // under the missiles: she keeps moving
};

}  // namespace cosmic
