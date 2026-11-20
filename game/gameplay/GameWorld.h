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
#include "game/gameplay/WorldListener.h"
#include "game/levels/Cinematic.h"
#include "game/levels/EventSystem.h"
#include "game/levels/LevelData.h"
#include "game/levels/LevelTimer.h"
#include "game/online/LevelFingerprint.h"
#include "game/player/Player.h"

namespace cosmic {

class AudioSystem;
class ResourceManager;
class ContentDatabase;
class Enemy;
class CombatZoneEntity;
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
    /// 2.71: the rules of this run, as the version of the simulation that played it
    /// (kSimulationVersion, the default). A replay plays with the rules of the version that
    /// recorded it (ReplayData::simVersion; 0 for a replay recorded before 2.71, which did not
    /// say it): its strikes keep the rules of their time (Player::handleAttack). Changes no
    /// digest by itself.
    void setSimulationVersion(int version) { simVersion_ = version; }
    int simulationVersion() const { return simVersion_; }

    // ------------------------------------------------------------ access
    const LevelData& level() const { return level_; }
    WorldMode mode() const { return mode_; }
    WorldState state() const { return state_; }
    /// The current player: player 1 outside any context; in a team (2.62, docs/COOP.md), the
    /// player being simulated during its step, the target of an enemy during that enemy's step,
    /// each player in turn inside forEachPlayer(). Alone, always the only player.
    Player& player() { return *cur_; }
    const Player& player() const { return *cur_; }
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
    /// 2.51: « Invincible » (administrators, training runs only): Nova takes no damage, and a fall
    /// into the void brings her back on her last solid ground. Never in a challenge run nor in a
    /// run that counts: a training run is never recorded nor ranked.
    void setInvincible(bool on) { invincible_ = on; }
    bool invincible() const { return invincible_ && training_ && !challenge_.has_value(); }
    /// 2.75: whether the current player is invincible: in a raid (an administrator's test) only seat 0,
    /// the administrator — the simulated players fall, go down and are revived like real players.
    bool invinciblePlayer() const { return invincible() && (!raidMode_ || curIndex_ == 0); }
    /// Puts an invincible Nova back on her last solid ground (instead of a fall); false when not invincible.
    bool rescueInvincible();
    /// 2.64, the tutorial (LevelData::tutorial): a fall never kills; Nova comes back at once on
    /// the last solid ground she stood on (never a moving platform nor a bubble), a moment
    /// invulnerable.
    void rescueGently();
    /// 2.64: an explanation card of the tutorial asked by the level (event action « tutorial »),
    /// each card once. The scene playing the level takes it after the step and stops the
    /// simulation while the card is open (display only: nothing of the run reads it back).
    void requestTutorialCard(const std::string& card);
    std::optional<std::string> takeTutorialCard();
    bool tutorialCardPending() const { return !tutorialCards_.empty(); }
    /// The cards already asked for (shown or waiting), in their order.
    const std::vector<std::string>& tutorialCardsAsked() const { return tutorialAsked_; }
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


    // ----------------------------------------------- 2.62: cooperation (docs/COOP.md)
    /// 2.65: 8 players at most (a raid instance, docs/RAIDS.md); the cooperation of the EXTRÊME
    /// levels stays at 2 or 3 (its lobbies), with the same rules and digests as before.
    static constexpr int kMaxTeam = 8;
    static constexpr int kTeamRevives = 2;            // per team and per attempt
    static constexpr float kReviveSeconds = 2.0f;     // an ally stays this long near the soul
    static constexpr float kReviveReachX = 40.0f;     // « near »: feet within these distances
    static constexpr float kReviveReachY = 48.0f;
    static constexpr int kStandbyLimitTicks = 60 * 120;  // 60 s of standby: the player is gone
    static constexpr float kOffFrameSeconds = 3.0f;   // out of the team's frame this long: back
    /// The team's frame (simulation rule): the widest view of the camera, minus margins.
    static constexpr float kFrameWidth = Camera::kViewWidth / Camera::kMinFitZoom - 64.0f;
    static constexpr float kFrameHeight = Camera::kViewHeight / Camera::kMinFitZoom - 96.0f;
    /// Vacant (2.65, raids only): a seat nobody holds (yet, or again after 60 s of absence): not in
    /// play, invisible, ignored. A player who takes it joins at the zone's place (fresh vitals).
    enum class Presence : uint8_t { Playing, Down, Standby, Gone, Vacant };
    struct TeamMember {
        Player* player = nullptr;
        Presence presence = Presence::Playing;
        bool absent = false;       // its inputs carry InputFrame::Standby (the relay's decision)
        bool standbyInput = false; // this step's flag (fixedUpdateTeam)
        bool vacantInput = false;  // 2.65 (raids): this step's InputFrame::Vacant flag
        int standbyTicks = 0;      // steps of the current absence
        Vec2 soul;                 // Down: where its soul waits (feet)
        float revive = 0.0f;       // Down: seconds an ally has stayed near the soul
        float offFrame = 0.0f;     // Playing: seconds out of the team's frame
        bool inFrame = true;       // part of the group the frame keeps (the camera frames it)
        bool fell = false;         // its last fall was into the void (the soul goes to safe ground)
        int reviver = -1;          // 2.77: the ally reviving it this step (« Interagir » held near its soul)
        Vec2 safeFeet;             // where it last stood on solid ground (not on an ally)
        bool hasSafeFeet = false;
        LevelEntity* interactable = nullptr;
        uint32_t body = 0;         // collider of its body (ColliderTag::Teammate)
        int revived = 0, regrouped = 0, returned = 0;  // counters (replays, statistics)
    };
    /// Players of this world (1 to 3), before load(). 1, the default, is the solo game: it
    /// plays exactly as before (same digests).
    void setTeamSize(int n);
    int teamSize() const { return team_.empty() ? 1 : static_cast<int>(team_.size()); }
    bool coop() const { return team_.size() > 1; }
    Player& playerAt(int i) { return team_.empty() ? player_ : *team_[static_cast<size_t>(i)].player; }
    const Player& playerAt(int i) const { return team_.empty() ? player_ : *team_[static_cast<size_t>(i)].player; }
    /// The team state of player `i` (a team only).
    const TeamMember& member(int i) const { return team_[static_cast<size_t>(i)]; }
    /// Index of the current player (see player()).
    int currentPlayer() const { return curIndex_; }
    int revivesLeft() const { return revivesLeft_; }
    int revivesUsed() const { return kTeamRevives - revivesLeft_; }
    /// 2.75 (raids): a player down comes back as many times as a teammate revives them (no limit:
    /// revivesLeft() stays as it was, the revivals are counted per player, TeamMember::revived).
    bool revivesUnlimited() const { return raidMode_; }
    /// Whether a soul can be revived now (a raid: always; a team: while revivals are left).
    bool canRevive() const { return raidMode_ || revivesLeft_ > 0; }
    /// Revivals done in this world (every player's).
    int revivalsDone() const {
        int n = 0;
        for (const TeamMember& m : team_) n += m.revived;
        return n;
    }
    /// 2.75: the rules of cooperation a team plays with (kCoopSimulationVersion; a team replay: the
    /// version it recorded, ReplayData::coopVersion). From 2 the players pass through each other (no
    /// body, nobody stands on an ally's head); 1 (the replays of 2.62 to 2.74): solid for each other.
    /// A raid world always lets its players pass through each other.
    void setCoopVersion(int version) { coopVersion_ = version; }
    int coopVersion() const { return coopVersion_; }
    bool solidTeammates() const { return !raidMode_ && coopVersion_ < 2; }
    /// A player was gone for good (60 s of standby): the attempt is ranked « incomplète ».
    bool teamIncomplete() const;
    /// Players in play (not down, in standby or gone), in order; each one is the current
    /// player during `f`. Alone: the only player, whatever its state (as the code was).
    template <class F>
    void forEachPlayer(F&& f) {
        if (team_.empty()) {
            f(player_);
            return;
        }
        Player* saved = cur_;
        const int savedIndex = curIndex_;
        for (size_t i = 0; i < team_.size(); ++i) {
            if (team_[i].presence != Presence::Playing) continue;
            cur_ = team_[i].player;
            curIndex_ = static_cast<int>(i);
            f(*team_[i].player);
        }
        cur_ = saved;
        curIndex_ = savedIndex;
    }
    /// Every player of the team but the gone ones (hidden ones too), each the current one
    /// during `f` (cinematics, scripted gifts). Alone: the only player.
    template <class F>
    void forEachTeammate(F&& f) {
        if (team_.empty()) {
            f(player_);
            return;
        }
        Player* saved = cur_;
        const int savedIndex = curIndex_;
        for (size_t i = 0; i < team_.size(); ++i) {
            if (team_[i].presence == Presence::Gone || team_[i].presence == Presence::Vacant) continue;
            cur_ = team_[i].player;
            curIndex_ = static_cast<int>(i);
            f(*team_[i].player);
        }
        cur_ = saved;
        curIndex_ = savedIndex;
    }
    /// The player an enemy at `from` aims at: the nearest in play (alive, then materialising,
    /// then falling), ties to the lowest number. Alone: the only player.
    Player& nearestPlayer(Vec2 from) { return playerAt(targetIndex(from)); }
    int targetIndex(Vec2 from) const;
    /// Whether a player in play satisfies `touches` (alone: the only player, as the code always
    /// tested it, alive or not).
    template <class Pred>
    bool anyPlayerWhere(Pred&& touches) {
        bool any = false;
        forEachPlayer([&](Player& p) {
            if (!any && touches(p)) any = true;
        });
        return any;
    }
    /// Hurts every player in play that `touches` (each the current player during its blow): an
    /// enemy's blow reaches every player it touches, not only its target. True if one was hurt.
    /// Alone: the test, then the blow, as before.
    template <class Pred>
    bool hurtPlayersWhere(Pred&& touches, const DamageInfo& d) {
        bool hurt = false;
        forEachPlayer([&](Player& p) {
            if (touches(p) && damagePlayer(d)) hurt = true;
        });
        return hurt;
    }
    bool playerIn(const Rect& area) {
        return anyPlayerWhere([&](const Player& p) { return area.intersects(p.box); });
    }
    bool hurtPlayersIn(const Rect& area, const DamageInfo& d) {
        return hurtPlayersWhere([&](const Player& p) { return area.intersects(p.box); }, d);
    }
    /// The current player for a scope (restored after).
    class PlayerScope {
    public:
        PlayerScope(GameWorld& w, int index) : w_(w), prev_(w.cur_), prevIndex_(w.curIndex_) {
            w.cur_ = &w.playerAt(index);
            w.curIndex_ = index;
        }
        ~PlayerScope() {
            w_.cur_ = prev_;
            w_.curIndex_ = prevIndex_;
        }
        PlayerScope(const PlayerScope&) = delete;
        PlayerScope& operator=(const PlayerScope&) = delete;

    private:
        GameWorld& w_;
        Player* prev_;
        int prevIndex_;
    };
    /// One step of a team: an input and a standby flag (InputFrame::Standby) per player; 2.65,
    /// raids: a vacant flag (InputFrame::Vacant) per player, or none.
    void fixedUpdateTeam(const PlayerInput* inputs, const bool* standby, int n, float dt, const bool* vacant = nullptr);

    // ----------------------------------------------- 2.65: raid instances (docs/RAIDS.md)
    /// A raid world (before load(), with setTeamSize): seats may be vacant (joined late, or
    /// freed after 60 s of absence), every player keeps their own camera (no team frame: no
    /// regrouping), and a team all down fails the zone (the raid run brings it back).
    void setRaidMode(bool on) { raidMode_ = on; }
    bool raidMode() const { return raidMode_; }
    /// 2.78 (raids): the players of the instance may be in several zones (one world each): the run
    /// judges the group's fall across them (raid::RaidRun), so this world never fails on its own when
    /// nobody is left in play in it (its players down wait there for an ally from another zone).
    void setRaidGroupRule(bool on) { raidGroupRule_ = on; }
    bool raidGroupRule() const { return raidGroupRule_; }
    /// Display only: the player the camera follows in a raid (this computer's player).
    void setCameraFollow(int index) { cameraFollow_ = index; }
    /// What a player carries from a zone to the next (vitals, powers, presence).
    struct MemberCarry {
        int health = 0;
        float energy = 0.0f;
        std::vector<std::string> powers;
        Presence presence = Presence::Playing;
        bool absent = false;
        int standbyTicks = 0;
    };
    MemberCarry carryOf(int i) const;
    /// Gives back a player's carry after load(), at `feet` (a free spot near it).
    void restoreMember(int i, const MemberCarry& c, Vec2 feet);
    /// Where a player of the team may appear near `feet` (never in a wall nor in an ally).
    Vec2 freeSpot(Vec2 feet, int self) const;

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
    /// 2.62.2: the level's ambushes (arenas without a boss, CombatZoneEntity::ambush()), and the
    /// one being fought (the one Nova is in first), nullptr when none.
    const std::vector<CombatZoneEntity*>& ambushes() const { return ambushes_; }
    CombatZoneEntity* activeAmbush() const;
    void setMusic(const std::string& rel);
    void restoreLevelMusic();
    LevelEntity* nearestInteractable() const {
        return team_.empty() ? interactable_ : team_[static_cast<size_t>(curIndex_)].interactable;
    }
    void lockCameraZone(const CameraZone* zone);
    void beginPlay();  // after the intro: start the timer, give control
    /// Skips the intro and starts playing at `feet` (editor "test from here").
    void startAt(Vec2 feet);
    /// The same, facing left or right, the respawn facing that way too (a boss training started
    /// in its arena, facing the boss: 2.41).
    void startAt(Vec2 feet, bool faceLeft);
    bool playerHidden() const { return player().state() == PlayerState::Hidden; }
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
        int player = 0;         // 2.62: the player of the team it is offered to
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
    /// 2.62: a team's catch-up (stepped by another thread, docs/COOP.md): no sound, no
    /// rumble, no particle; nothing of the simulation changes.
    void setQuiet(bool on) {
        quiet_ = on;
        silent = on;
        particles_.setQuality(on ? 0.0f : services_.particleQuality);
    }

    // ---------------------------------------------------------- render
    /// Fill colour of the screen behind the parallax layers.
    Color backgroundColor() const { return level_.backgroundColor; }

    /// Callback used by the arena (enemy editor) to reset when the player dies.
    std::function<void()> onPlayerDied;

    // ------------------------------------------- 2.54: statistics (never the simulation)
    /// The listener of this run (game/records/PlayStats.h), or nullptr. Told what happens (a blow
    /// to Nova and where it came from, an instant fall, an attack of hers and what it reached);
    /// nothing it does is read back: a run plays the same with or without it.
    WorldListener* listener = nullptr;
    /// Nova's attacks are tagged: a swing or a power used gets a new tag (told to the listener),
    /// its shots carry it, and damage dealt while it is the current tag is credited to it.
    uint32_t newAttackTag(const std::string& power);
    uint32_t attackTag() const { return attackTag_; }
    /// The current tag while it lives (restored after).
    struct AttackScope {
        AttackScope(GameWorld& w, uint32_t tag) : w_(w), prev_(w.attackTag_) { w.attackTag_ = tag; }
        ~AttackScope() { w_.attackTag_ = prev_; }
        AttackScope(const AttackScope&) = delete;
        AttackScope& operator=(const AttackScope&) = delete;

    private:
        GameWorld& w_;
        uint32_t prev_;
    };
    /// Enemy::takeDamage: health taken from an enemy (credited to the current attack tag).
    void noteEnemyDamaged(const Enemy& e, const DamageInfo& dmg, float amount);
    /// 2.68: a blow stopped by the current player's shield (told to the listener).
    void noteBlocked(float amount) {
        if (listener) listener->blowBlocked(*this, amount);
    }
    /// 2.68 (raids, game/raid/Gigaboss): the players in play inside `area` take a blow of `boss`
    /// (its jets of magma): for the statistics and the raid's contributions, a blow of the boss.
    bool hurtPlayersByBoss(const Rect& area, const DamageInfo& d, const Enemy* boss, const std::string* attack);
    /// Player::kill: Nova falls (told to the listener when no blow caused it).
    void notePlayerKilled();

private:
    void buildStatic();
    void createEntities();
    void updatePlayerOverlaps(float dt);
    void updateProjectiles(float dt);
    void explodeProjectile(const Projectile& p, Vec2 at);
    /// 2.50: an enemy shot that leaves a burning cloud where it bursts (the dragon's fireballs).
    void cloudFromShot(const Projectile& p, Vec2 at);
    void updateEnemies(float dt);
    void updateCamera(float dt);
    void carryBodies();
    void checkKillPlane();
    void computeEnvironment();
    /// 2.19: an enemy shot or blow touching the decoy breaks it.
    void updateDecoy();
    /// 2.19: a frozen enemy is a platform (a one-way collider on the top of its box).
    void syncFrozenPlatforms();

    // 2.62: the team (empty alone: player_ is the only player).
    std::vector<TeamMember> team_;
    std::vector<std::unique_ptr<Player>> extraPlayers_;
    int teamWanted_ = 1;
    Player* cur_ = &player_;
    int curIndex_ = 0;
    int revivesLeft_ = kTeamRevives;
    int coopVersion_ = kCoopSimulationVersion;  // 2.75
    std::vector<PlayerInput> teamInputs_;
    bool raidMode_ = false;
    bool raidGroupRule_ = false;  // 2.78
    int cameraFollow_ = -1;
    void loadTeam();
    void vacateMember(int i);
    void stepTeamPlayers(float dt);
    // 2.77 (rules of cooperation 3): a safe place (firm ground out of every liquid and danger), the
    // dangers of the level (cached), a soul lifted above a liquid rising over it.
    bool safeStand(const Player& p) const;
    bool inDanger(const Rect& body, bool whole) const;
    const std::vector<LevelEntity*>& harmful() const;
    void liftSoul(TeamMember& m);
    mutable std::vector<LevelEntity*> harmful_;
    mutable size_t harmfulFor_ = static_cast<size_t>(-1);
    size_t entityGen_ = 0;  // 2.77: entities added or removed during the run
    void teamBodies(int self, bool on);
    void updateTeamStandby();
    void updateTeamRules(float dt);
    void updateTeamCamera(float dt);
    void memberDown(int i);
    void reviveMember(int i);
    void regroupMember(int i, int toward);
    void checkTeamDown();
    bool solidMember(int i) const;
    bool teammateBody(uint32_t collider) const;
    /// The player an enemy shot touches first (alone: the only player, if alive).
    Player* shotTarget(const Projectile& p);
    int playerIndex(const Player& p) const;
    friend uint64_t simulationStateHash(const GameWorld& world);

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
    std::vector<CombatZoneEntity*> ambushes_;  // 2.62.2 (load)
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
    bool invincible_ = false;
    bool quiet_ = false;
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
    int simVersion_ = kSimulationVersion;  // 2.71
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
    std::vector<std::string> tutorialCards_;  // 2.64: asked, not yet taken by the scene
    std::vector<std::string> tutorialAsked_;  // 2.64: every card asked (each once)
    void practiceRespawnNow();  // 2.47.1: the clock waits for the end of Nova's materialisation
    int fallGuard_ = 0;
    // 2.54, statistics only (WorldListener): what is stepping now, so that a blow to Nova knows
    // where it comes from; the tags of Nova's attacks; the serial of the shots.
    struct HurtScope {
        HurtScope(GameWorld& w, HurtOrigin o, const Enemy* e = nullptr, uint32_t shot = 0, const std::string* attack = nullptr);
        ~HurtScope();
        HurtScope(const HurtScope&) = delete;
        HurtScope& operator=(const HurtScope&) = delete;
        GameWorld& w;
        HurtOrigin origin;
        const Enemy* enemy;
        uint32_t shot;
        const std::string* attack;
    };
    HurtSource hurtSource(const DamageInfo* dmg) const;
    HurtOrigin hurtOrigin_ = HurtOrigin::Other;
    const Enemy* hurtEnemy_ = nullptr;
    uint32_t hurtShot_ = 0;
    const std::string* hurtAttack_ = nullptr;
    int inDamage_ = 0;
    uint32_t attackTag_ = 0;
    uint32_t attackSerial_ = 0;
    uint32_t shotSerial_ = 0;

    friend class GameRenderer;
};

}  // namespace cosmic
