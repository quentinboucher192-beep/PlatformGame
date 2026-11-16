#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/camera/Camera.h"
#include "engine/core/Random.h"
#include "engine/input/InputManager.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/rendering/ParticleSystem.h"
#include "game/enemies/BossHazards.h"
#include "game/gameplay/ChallengeRules.h"
#include "game/gameplay/Damage.h"
#include "game/gameplay/Entity.h"
#include "game/gameplay/Projectile.h"
#include "game/gameplay/RunStats.h"
#include "game/levels/Cinematic.h"
#include "game/levels/EventSystem.h"
#include "game/levels/LevelData.h"
#include "game/levels/LevelTimer.h"
#include "game/player/Player.h"

namespace cosmic {

class AudioSystem;
class ResourceManager;
class ContentDatabase;
class Enemy;
struct EnemyDefinition;

enum class WorldMode { Play, Editor, Arena };
enum class WorldState { Intro, Playing, Outro, Completed, Failed };

/// Services the world uses (owned by the application / scene).
struct WorldServices {
    ResourceManager& res;
    AudioSystem& audio;
    const ContentDatabase& content;
    InputManager* input = nullptr;  // rumble
    float screenShake = 1.0f;
    float particleQuality = 1.0f;
};

/// Messages shown by the HUD (texts, hints, dialogues, boss titles).
struct WorldMessage {
    enum class Style { Hint, Title, Dialogue, Boss, Secret } style = Style::Hint;
    std::string speaker;
    std::string text;
    std::string subtitle;
    std::string detail;  // a power picked up: its characteristics (energy, damage, reach...)
    /// A power picked up: its id and the slot it went to; the HUD shows how to use it (the
    /// key of that slot, or the move / « passif » for a power that does not use it).
    std::string power;
    int powerSlot = -1;
    float duration = 3.0f;
    float age = 0.0f;
};

/// The simulation of a level: player, enemies, entities, projectiles, physics, camera,
/// timer, events and cinematics. Also used by the level editor (render-only preview)
/// and by the enemy editor (test arena).
/// The level ceiling is this many pixels above the top of the level bounds (designed routes
/// may brush the top edge; nothing can go farther up).
constexpr float kCeilingMargin = 48.0f;

class GameWorld {
public:
    GameWorld(WorldServices services, WorldMode mode);
    ~GameWorld();
    GameWorld(const GameWorld&) = delete;
    GameWorld& operator=(const GameWorld&) = delete;

    bool load(const LevelData& level);
    void fixedUpdate(const PlayerInput& in, float dt);
    void update(float dt);

    /// Keys of the powers in this run (see Player::setKeysPerSlot): true for every run
    /// played since 2.24 (each power slot has its key, InputFrame bits PowerSlot1..8), false
    /// (the default, and the tests written before) for the replays recorded before, where
    /// each power had its own button. Set it before load(); set after load and before the
    /// first step, the start powers are granted again under the new rule if it changes them.
    void setKeysPerSlot(bool on);
    bool keysPerSlot() const { return keysPerSlot_; }

    // ------------------------------------------------------------ access
    const LevelData& level() const { return level_; }
    WorldMode mode() const { return mode_; }
    WorldState state() const { return state_; }
    Player& player() { return player_; }
    const Player& player() const { return player_; }
    PhysicsWorld& physics() { return physics_; }
    const PhysicsWorld& physics() const { return physics_; }
    Camera& camera() { return camera_; }
    ParticleSystem& particles() { return particles_; }
    LevelTimer& timer() { return timer_; }
    const LevelTimer& timer() const { return timer_; }
    RunStats& stats() { return stats_; }
    const RunStats& stats() const { return stats_; }
    EventSystem& events() { return events_; }
    CinematicPlayer& cinematic() { return cinematic_; }
    const CinematicPlayer& cinematic() const { return cinematic_; }
    ResourceManager& resources() { return services_.res; }
    AudioSystem& audio() { return services_.audio; }
    const ContentDatabase& content() const { return services_.content; }
    /// The definition an enemy has in this level (2.45): a campaign level bound to an enemy
    /// revision published online plays that revision, any other level the game's own.
    const EnemyDefinition* enemyDefinition(const std::string& id) const;
    float time() const { return time_; }
    /// Gameplay randomness (attack choices, drops...). Only used from fixedUpdate: with the
    /// same seed and the same inputs, a run plays out identically (replays, speedruns).
    Random& rng() { return rng_; }
    /// Cosmetic randomness (sound pitch, particles): never affects the simulation.
    Random& fxRng() { return fxRng_; }
    /// Seeds the gameplay generator (call before the first step).
    void seedGameplay(uint64_t seed) {
        seed_ = seed;
        rng_.reseed(seed);
    }
    uint64_t gameplaySeed() const { return seed_; }
    /// A training run (boss statistics → Entraînement), or a challenge run: nothing it does
    /// counts. Every recorder checks it (boss statistics, achievement counters, records,
    /// progression, creatures met). The simulation reads it once: a boss training ends at the
    /// player's first fall (its end screen), the player does not come back (2.41: nobody walks
    /// back into the arena behind that screen). Training runs are never recorded.
    void setTraining(bool on) { training_ = on; }
    /// 2.48: a hardcore (EXTRÊME) level played as a practice run: after a fall, Nova comes back
    /// where she last stood on solid ground, invulnerable for a moment, the level as it was
    /// (bosses keep their wounds). A training run: never a record, never ranked.
    void setPracticeRespawn(bool on) {
        practiceRespawn_ = on;
        if (on) training_ = true;
    }
    bool practiceRespawn() const { return practiceRespawn_; }
    bool training() const { return training_ || challenge_.has_value(); }
    /// A daily challenge (2.32, simulation 11): its rules apply now (call it right after
    /// load(), before the first step: hearts, slots, imposed powers) and for the rest of the
    /// run (heart containers give nothing; stations off; checkpoints off, a death going back
    /// to the start with the challenge's start state). The run then counts for nothing else
    /// (training()). A world without a challenge plays exactly as before.
    void setChallenge(const ChallengeRules& rules);
    const ChallengeRules* challenge() const { return challenge_ ? &*challenge_ : nullptr; }
    uint64_t gameplayRngState() const { return rng_.state(); }

    std::vector<std::unique_ptr<LevelEntity>>& entities() { return entities_; }
    const std::vector<std::unique_ptr<LevelEntity>>& entities() const { return entities_; }
    LevelEntity* entity(const std::string& id);
    std::vector<std::unique_ptr<Enemy>>& enemies() { return enemies_; }
    const std::vector<std::unique_ptr<Enemy>>& enemies() const { return enemies_; }
    std::vector<Projectile>& projectiles() { return projectiles_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    std::vector<WorldMessage>& messages() { return messages_; }
    /// Lasting effects of the guardians' attacks (2.19): pulls, frozen ground, lava tide,
    /// walls of thorns. Part of the simulation; cleared when the player respawns.
    BossHazards& hazards() { return hazards_; }
    const BossHazards& hazards() const { return hazards_; }

    // --------------------------------------------------------- spawning
    Enemy* spawnEnemy(const EnemyDefinition& def, Vec2 feet, bool faceLeft, const std::string& objectId = {},
                      float patrol = 96.0f);
    Enemy* spawnEnemy(const std::string& defId, Vec2 feet, bool faceLeft, const std::string& objectId = {},
                      float patrol = 96.0f);
    void spawnProjectile(const Projectile& p);
    /// Enemy by unique id, including the ones spawned during this step (not yet in enemies()).
    Enemy* findEnemy(uint32_t uid) const;
    void spawnDrop(const std::string& type, Vec2 pos);

    // ------------------------------------------------------------ combat
    /// Damages enemies overlapping `area`; returns how many were hit.
    int damageEnemies(const Rect& area, const DamageInfo& dmg, const void* source = nullptr);
    /// Lets power-sensitive entities react (gates, receptors).
    bool powerHit(const std::string& power, const Rect& area);
    bool damagePlayer(const DamageInfo& dmg);
    void onEnemyKilled(Enemy& e);

    // ------------------------------------------------------- feedback
    void playSound(const std::string& rel, Vec2 worldPos, float volume = 1.0f, float pitch = 1.0f);
    void playSound(const std::string& rel, float volume = 1.0f, float pitch = 1.0f);
    /// Keeps a looping sound playing while called every step (stops shortly after).
    void sustainSound(const std::string& key, const std::string& rel, float volume = 1.0f);
    void shake(float trauma);
    void hitStop(float seconds) { hitStop_ = std::max(hitStop_, seconds); }
    /// Time slow power: enemies and their shots run at `factor` speed for `seconds`.
    void slowTime(float seconds, float factor) {
        timeSlow_ = std::max(timeSlow_, seconds);
        timeSlowFactor_ = factor;
    }
    float timeSlowRemaining() const { return timeSlow_; }
    /// Decoy (power « Leurre holographique », 2.19): for `seconds`, the enemies aim at `center`
    /// instead of Nova. Part of the simulation (set from the player's inputs only).
    void setDecoy(Vec2 center, float seconds, float facing = 1.0f) {
        decoyPos_ = center;
        decoyTimer_ = seconds;
        decoyDuration_ = seconds;
        decoyFacing_ = facing < 0.0f ? -1.0f : 1.0f;
    }
    void clearDecoy() { decoyTimer_ = 0.0f; }
    bool decoyActive() const { return decoyTimer_ > 0.0f; }
    Vec2 decoyPos() const { return decoyPos_; }
    float decoyRemaining() const { return decoyTimer_; }
    float decoyDuration() const { return decoyDuration_; }
    float decoyFacing() const { return decoyFacing_; }
    /// What an attack must touch to break the decoy: Nova's standing box around it.
    Rect decoyBox() const;
    /// Decoys broken by an attack (an enemy shot or blow reached the hologram).
    int decoysBroken() const { return decoysBroken_; }
    /// What the enemies aim at: the decoy while there is one, else Nova's centre.
    Vec2 enemyAim() const;
    void flash(Color c, float duration);
    void rumble(float low, float high, int ms);
    void message(WorldMessage msg);
    void hint(const std::string& text, float duration = 3.5f);
    Color flashColor() const;

    // ----------------------------------------------------- level logic
    /// Signals an event source (see LevelEvent): object id + trigger.
    void emit(const std::string& source, const std::string& trigger, const Json& params = Json::object());
    /// Optional listener of every emitted event (replay recording); never changes the game.
    std::function<void(const std::string& source, const std::string& trigger)> eventObserver;
    void setCheckpoint(const std::string& id, Vec2 feet);
    Vec2 respawnPoint() const { return respawn_; }
    const std::string& checkpointId() const { return checkpointId_; }
    void onPlayerDeathAnimationDone();
    void respawnPlayer();
    void reachExit(const LevelEntity& exitEntity);
    void failLevel(const std::string& reason);
    const std::string& failReason() const { return failReason_; }
    void startBoss(Enemy& boss);
    Enemy* activeBoss() const { return boss_; }
    /// Arena of the boss fight in progress (the camera frames it), if any.
    const CameraZone* bossCameraZone() const { return boss_ && bossZone_ ? &*bossZone_ : nullptr; }
    /// The combat arena holding an enemy (named in its waves, or containing it).
    std::optional<Rect> arenaOf(const Enemy& e) const;
    void setMusic(const std::string& rel);
    void restoreLevelMusic();
    LevelEntity* nearestInteractable() const { return interactable_; }
    void lockCameraZone(const CameraZone* zone);
    void beginPlay();  // after the intro: start the timer, give control
    /// Skips the intro and starts playing at `feet` (editor "test from here").
    void startAt(Vec2 feet);
    /// The same, facing left or right, the respawn facing that way too (a boss training started
    /// in its arena, facing the boss: 2.41).
    void startAt(Vec2 feet, bool faceLeft);
    bool playerHidden() const { return player_.state() == PlayerState::Hidden; }
    void addSecretFound(const std::string& id, const std::string& message);
    float lastSplitDelta() const { return lastSplitDelta_; }
    void setReferenceSplits(std::vector<float> splits) { referenceSplits_ = std::move(splits); }
    float splitFlashTimer() const { return splitFlash_; }

    // ------------------------------------------------- power slots
    /// A power tablet touched while every slot is taken (or with a power on the same
    /// input): the simulation waits for the player's choice, which arrives with the next
    /// input frame (InputFrame::choice) so replays reproduce it exactly.
    struct PowerOffer {
        std::string tabletId;  // entity offering the power
        std::string powerId;
        int conflictSlot = -1;  // >= 0: only this power can be replaced (same input)
    };
    const std::optional<PowerOffer>& powerOffer() const { return powerOffer_; }
    void offerPower(const std::string& tabletId, const std::string& powerId, int conflictSlot);
    /// Applies a choice: 1..8 replaces that slot, InputFrame::kChoiceLeave keeps the
    /// carried powers. Returns false if no offer is pending.
    bool resolvePowerOffer(uint8_t choice);
    /// Bots and tests: answers every power offer at once with this choice (0 = wait for the
    /// input frames, as in a real game).
    uint8_t autoPowerChoice = 0;
    /// Demos (power guide): no sound.
    bool silent = false;

    // ---------------------------------------------------------- render
    /// Fill colour of the screen behind the parallax layers.
    Color backgroundColor() const { return level_.backgroundColor; }

    /// Callback used by the arena (enemy editor) to reset when the player dies.
    std::function<void()> onPlayerDied;

private:
    void buildStatic();
    void createEntities();
    void updatePlayerOverlaps(float dt);
    void updateProjectiles(float dt);
    void explodeProjectile(const Projectile& p, Vec2 at);
    void updateEnemies(float dt);
    void updateCamera(float dt);
    void carryBodies();
    void checkKillPlane();
    void computeEnvironment();
    /// 2.19: an enemy shot or blow touching the decoy breaks it.
    void updateDecoy();
    /// 2.19: a frozen enemy is a platform (a one-way collider on the top of its box).
    void syncFrozenPlatforms();

    Vec2 decoyPos_;
    float decoyTimer_ = 0.0f;
    float decoyDuration_ = 0.0f;
    float decoyFacing_ = 1.0f;
    int decoysBroken_ = 0;
    struct FrozenPlatform {
        uint32_t enemy = 0;     // Enemy::uid()
        uint32_t collider = 0;  // physics collider on its top
    };
    std::vector<FrozenPlatform> frozenPlatforms_;
    WorldServices services_;
    WorldMode mode_;
    WorldState state_ = WorldState::Intro;
    LevelData level_;
    std::map<std::string, int> enemyBinding_;  // the enemy revisions of the level (2.45)
    PhysicsWorld physics_;
    Camera camera_;
    ParticleSystem particles_;
    LevelTimer timer_;
    RunStats stats_;
    EventSystem events_;
    CinematicPlayer cinematic_;
    Player player_;
    std::vector<std::unique_ptr<LevelEntity>> entities_;
    std::unordered_map<std::string, LevelEntity*> entityById_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    std::vector<std::unique_ptr<Enemy>> pendingEnemies_;
    std::vector<Projectile> projectiles_;
    std::vector<Projectile> pendingProjectiles_;
    std::vector<WorldMessage> messages_;
    BossHazards hazards_;
    Random rng_{0xC0FFEE};
    Random fxRng_{0xF1F1F1};
    uint64_t seed_ = 0xC0FFEE;
    bool training_ = false;
    std::optional<ChallengeRules> challenge_;
    /// Challenge without checkpoints: a death goes back to the start with its start state,
    /// the power tablets as the level placed them (none lost, none exchanged).
    void restartChallenge();
    uint32_t nextEnemyUid_ = 1;
    int dropCounter_ = 0;

    Vec2 respawn_;
    bool respawnFaceLeft_ = false;
    std::string checkpointId_;
    float time_ = 0.0f;
    float hitStop_ = 0.0f;
    float timeSlow_ = 0.0f;
    float timeSlowFactor_ = 1.0f;
    std::optional<PowerOffer> powerOffer_;
    bool keysPerSlot_ = false;
    Color flash_{255, 255, 255, 0};
    float flashTimer_ = 0.0f, flashDuration_ = 0.0f;
    Enemy* boss_ = nullptr;
    LevelEntity* interactable_ = nullptr;
    std::optional<CameraZone> lockedZone_;
    /// The arena of the boss being fought: the camera shows all of it (display only).
    std::optional<CameraZone> bossZone_;
    const CameraZone* activeZone_ = nullptr;
    std::string failReason_;
    std::string currentMusic_;
    std::vector<float> referenceSplits_;
    float lastSplitDelta_ = 0.0f;
    float splitFlash_ = 0.0f;
    bool expiredHandled_ = false;
    bool timerPending_ = false;
    bool practiceRespawn_ = false;  // 2.48 (setPracticeRespawn)
    Vec2 safeFeet_{};               // where Nova last stood on solid ground (practice respawn)
    bool hasSafeFeet_ = false;
    void practiceRespawnNow();  // 2.47.1: the clock waits for the end of Nova's materialisation
    int fallGuard_ = 0;

    friend class GameRenderer;
};

}  // namespace cosmic
