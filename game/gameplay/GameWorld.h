#pragma once

#include <functional>
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
    float duration = 3.0f;
    float age = 0.0f;
};

/// The simulation of a level: player, enemies, entities, projectiles, physics, camera,
/// timer, events and cinematics. Also used by the level editor (render-only preview)
/// and by the enemy editor (test arena).
class GameWorld {
public:
    GameWorld(WorldServices services, WorldMode mode);
    ~GameWorld();
    GameWorld(const GameWorld&) = delete;
    GameWorld& operator=(const GameWorld&) = delete;

    bool load(const LevelData& level);
    void fixedUpdate(const PlayerInput& in, float dt);
    void update(float dt);

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
    float time() const { return time_; }
    Random& rng() { return rng_; }

    std::vector<std::unique_ptr<LevelEntity>>& entities() { return entities_; }
    const std::vector<std::unique_ptr<LevelEntity>>& entities() const { return entities_; }
    LevelEntity* entity(const std::string& id);
    std::vector<std::unique_ptr<Enemy>>& enemies() { return enemies_; }
    const std::vector<std::unique_ptr<Enemy>>& enemies() const { return enemies_; }
    std::vector<Projectile>& projectiles() { return projectiles_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    std::vector<WorldMessage>& messages() { return messages_; }

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
    void flash(Color c, float duration);
    void rumble(float low, float high, int ms);
    void message(WorldMessage msg);
    void hint(const std::string& text, float duration = 3.5f);
    Color flashColor() const;

    // ----------------------------------------------------- level logic
    /// Signals an event source (see LevelEvent): object id + trigger.
    void emit(const std::string& source, const std::string& trigger, const Json& params = Json::object());
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
    void setMusic(const std::string& rel);
    void restoreLevelMusic();
    LevelEntity* nearestInteractable() const { return interactable_; }
    void lockCameraZone(const CameraZone* zone);
    void beginPlay();  // after the intro: start the timer, give control
    /// Skips the intro and starts playing at `feet` (editor "test from here").
    void startAt(Vec2 feet);
    bool playerHidden() const { return player_.state() == PlayerState::Hidden; }
    void addSecretFound(const std::string& id, const std::string& message);
    float lastSplitDelta() const { return lastSplitDelta_; }
    void setReferenceSplits(std::vector<float> splits) { referenceSplits_ = std::move(splits); }
    float splitFlashTimer() const { return splitFlash_; }

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

    WorldServices services_;
    WorldMode mode_;
    WorldState state_ = WorldState::Intro;
    LevelData level_;
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
    Random rng_{0xC0FFEE};

    Vec2 respawn_;
    bool respawnFaceLeft_ = false;
    std::string checkpointId_;
    float time_ = 0.0f;
    float hitStop_ = 0.0f;
    Color flash_{255, 255, 255, 0};
    float flashTimer_ = 0.0f, flashDuration_ = 0.0f;
    Enemy* boss_ = nullptr;
    LevelEntity* interactable_ = nullptr;
    std::optional<CameraZone> lockedZone_;
    const CameraZone* activeZone_ = nullptr;
    std::string failReason_;
    std::string currentMusic_;
    std::vector<float> referenceSplits_;
    float lastSplitDelta_ = 0.0f;
    float splitFlash_ = 0.0f;
    bool expiredHandled_ = false;
    int fallGuard_ = 0;

    friend class GameRenderer;
};

}  // namespace cosmic
