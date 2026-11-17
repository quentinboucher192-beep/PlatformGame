#pragma once
// The example of the attack unfolded on a boss's sheet (2.60, Statistiques des boss › Fiche):
// one at a time. It is prepared without holding up the screen: the boss's level is read and
// parsed on a worker thread (kept for its next example); then its world is loaded, the boss and
// Nova placed and its drawing prepared, each in a frame of its own (they create textures: the
// main thread's work), and its terrain baked and the scene brought to the attack's first wind-up
// a few milliseconds per frame; meanwhile the frame says what is being prepared. Only the
// example shown is played, and only while its frame is on screen. The scene itself is
// game/gameplay/BossAttackDemo.h (a training world: never recorded, silent).

#include <future>
#include <memory>
#include <optional>
#include <string>

#include "engine/core/Math.h"
#include "game/gameplay/BossAttackDemo.h"
#include "game/gameplay/GameRenderer.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class ContentDatabase;
class GameServices;
class UIContext;
struct CreatureAtlas;
struct EnemyDefinition;

class BossAttackPreview {
public:
    enum class Status { Idle, Searching, Loading, Preparing, Running, Unavailable };

    explicit BossAttackPreview(GameServices& g);
    ~BossAttackPreview();
    BossAttackPreview(const BossAttackPreview&) = delete;
    BossAttackPreview& operator=(const BossAttackPreview&) = delete;

    /// The example to play: attack `attackId` of `def`, in its arena (`atlas`: where every boss
    /// is fought, null while it is read). Nothing changes when it is the one already shown;
    /// `def` null or `attackId` empty stops the example.
    void request(const EnemyDefinition* def, const std::string& attackId, const std::shared_ptr<const CreatureAtlas>& atlas);
    void stop();
    /// 120 Hz: one step of the scene.
    void fixedUpdate();
    /// Each frame: the preparation (a few milliseconds), the scene's animations.
    void update(float dt);
    /// The scene in `frame` (its arena around the boss and Nova); while it is prepared or when
    /// it cannot be shown, what is happening, in words.
    void draw(UIContext& ui, const Rect& frame, float alpha);

    Status status() const { return status_; }
    const std::string& bossId() const { return bossId_; }
    const std::string& attackId() const { return attackId_; }
    /// Why it cannot be shown (Unavailable).
    const std::string& reason() const { return reason_; }
    /// The state in a few words (« Chargement de son arène… », « Phase 3 », « Dernier souffle »…).
    std::string caption() const;
    bool paused() const { return paused_; }
    void setPaused(bool on) { paused_ = on; }
    /// Times the attack has begun in this example.
    int shown() const { return demo_ ? demo_->shown() : 0; }
    /// The example is played in the boss's own arena (else a neutral one).
    bool ownArena() const { return ownArena_; }

private:
    void begin();
    void prepare();
    void unavailable(const std::string& why);

    GameServices& g_;
    Status status_ = Status::Idle;
    std::string bossId_, attackId_, reason_;
    std::shared_ptr<const CreatureAtlas> atlas_;
    // Its level: read on a worker thread, kept for the next example of the same boss.
    std::string levelId_, bossObject_;
    std::future<std::optional<LevelData>> reading_;
    std::string cachedLevelId_;
    std::optional<LevelData> cachedLevel_;
    bool ownArena_ = false;
    // A private copy of the content (the boss of the example is a revision of it).
    std::shared_ptr<ContentDatabase> content_;
    std::unique_ptr<BossAttackDemo> demo_;
    std::unique_ptr<GameRenderer> renderer_;
    size_t terrainLeft_ = 0;
    bool paused_ = false;
    float spinner_ = 0.0f;
    int unseen_ = 0;  // frames since its frame was last on screen
    Rect cam_;  // the part of the world shown (followed smoothly)
    bool camValid_ = false;
    float aspect_ = 16.0f / 9.0f;  // of the frame it was last drawn in
};

}  // namespace cosmic
