#pragma once

#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "engine/core/Scene.h"
#include "engine/input/InputScript.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/powers/PowerKeys.h"
#include "game/race/Race.h"
#include "game/records/BossStats.h"
#include "game/records/Records.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/screens/GhostRun.h"
#include "ui/screens/PauseMap.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

class GameServices;
class GameScene;

/// Boss training: the end of a training run (Recommencer, Changer les réglages, Retour).
std::unique_ptr<Scene> makeBossTrainingEnd(GameServices& g, GameScene& game, const BossTrainingResult& result);

/// The end of a daily challenge run (2.32): time, new best, tier, streak, sending state.
struct ChallengeEndData {
    DailyChallenge challenge;
    bool practice = false;
    bool completed = false;
    std::string failReason;
    int64_t timeMs = 0;
    int deaths = 0;
    int damageTaken = 0;
    ChallengeTier tier = ChallengeTier::None;
    std::string attemptId;  // the run (its sending state is followed live)
    ChallengeService::RunReport report;
    /// « Plus loin » (2.34): the unfinished attempt of this run (a fall; without checkpoints,
    /// its first life), its progress and whether it went further than my best one.
    std::optional<ChallengeService::AttemptReport> attempt;
    ChallengeService::Streak streak;
    std::shared_ptr<ReplayData> replay;
};
std::unique_ptr<Scene> makeChallengeEnd(GameServices& g, GameScene& game, ChallengeEndData data);

/// Plays a level: simulation, rendering, HUD, pause menu and end of level.
class GameScene : public Scene {
public:
    GameScene(GameServices& g, std::string levelId, PlayOptions opt);
    ~GameScene() override;

    void onEnter() override;
    void onExit() override;
    void onResume() override;
    void handleEvent(const SDL_Event& e) override;
    void fixedUpdate(float dt) override;
    void update(float dt) override;
    void render(float alpha) override;
    void renderDebug() override;
    /// A run in progress is never disturbed by online notifications (they show afterwards).
    bool holdsNotifications() const override { return true; }

    void restart();
    void quitToMenu();
    bool editorTest() const { return opt_.editorTest; }
    bool sectionTraining() const { return opt_.training; }  // « Répéter un segment »: quitting returns to its setup
    /// Answer of the power choice window (InputFrame::choice), used by the next step.
    void answerPowerOffer(uint8_t choice) { pendingChoice_ = choice; }
    const std::string& levelId() const { return levelId_; }
    // Keys of the powers (2.31): each power keeps its own key in the level (the level's, the
    // player's, the suggestion); the slot keys of the input follow the powers carried.
    const LevelData& level() const { return level_; }
    const PowerKeyMap& powerKeys() const { return powerKeys_; }
    /// Powers given besides the level's own (boss training): « Touches de ce niveau » lists them too.
    const std::vector<std::string>& powerKeyExtra() const { return powerKeyExtra_; }
    /// Resolves the keys again (the player changed theirs, or the settings) and binds them.
    void refreshPowerKeys();
    // Ghost Racing (pause menu → Fantômes).
    bool hasGhosts() const { return ghostRun_.active(); }
    bool ghostsVisible() const { return ghostRun_.visible(); }
    void setGhostsVisible(bool on) { ghostRun_.setVisible(on); }
    void applyGhostSettings();
    GameWorld* world() { return world_.get(); }
    /// What the player has seen of the level so far (the pause menu's map).
    const ExploredMap& explored() const { return explored_; }
    /// The level as it is, drawn small in `dst` (the pause map): `win` is the part of the world shown.
    void renderMapView(const Rect& dst, const Rect& win);

private:
    ExploredMap explored_;
    void finishRun(bool completed);
    std::string nextLevelId() const;

    GameServices& g_;
    std::string levelId_;
    PlayOptions opt_;
    /// Not in any planet's campaign (made in the editor, imported): leaving goes back to
    /// the WORKSHOP, no "next level", and "Continuer" keeps pointing at the campaign.
    bool workshop_ = false;
    uint8_t pendingChoice_ = 0;
    /// Route of the progress bar, computed off the main thread when the level starts.
    std::future<LevelProgressTrack> trackFuture_;
    LevelData level_;
    std::unique_ptr<GameWorld> world_;
    GameRenderer renderer_;
    Hud hud_;
    std::shared_ptr<ReplayData> lastReplay_;  // exact replay of the finished run (results screen)
    bool mounted_ = false;
    bool ended_ = false;
    bool loadFailed_ = false;
    std::string loadError_;
    float skipHold_ = 0.0f;
    float playTime_ = 0.0f;
    InputScript script_;
    // Cosmic Online: trace of the run (built-in levels started from the beginning only).
    ReplayRecorder recorder_;
    std::string attemptId_;
    std::optional<LevelFingerprint> fingerprint_;
    /// What the recorded replay is for: an official ranked run, a community level run, or
    /// the editor's completion run of a player level (needed to publish it).
    /// Local: any other level played from its start (workshop, imported), recorded for the
    /// ghosts only (nothing is sent).
    enum class RecordMode { None, Official, Community, EditorCompletion, Local, Challenge };
    RecordMode recordMode_ = RecordMode::None;
    std::string gameplayHash_;
    // EXTRÊME level (one life) played from its start: the attempt joins the « course à la
    // vérification », whether it finishes or not (sent at the end, on a death or when left).
    void sendRaceAttempt(bool finished, std::vector<uint8_t> replay);
    void abandonRaceAttempt();
    RaceTracker race_;
    /// Boss fights of the run: statistics, sound of a phase beaten without a hit.
    BossFightTracker bossFights_;
    void bossFeedback();
    /// A boss out of view: a textured arrow on the screen's border, turned towards it.
    void drawBossArrow(const WorldView& view);
    std::future<std::vector<Vec2>> raceRouteFuture_;
    // Ghost Racing: ghosts of the run, this level version's hash, my best ghost's time.
    void startGhosts();
    void saveGhosts(const RunResult& r);
    GhostRun ghostRun_;
    // Collection (2.19, display only): the equipped suit, trail and victory effect, and the
    // creatures met during this run (the gallery).
    void applyCosmetics();
    void noteCreaturesMet();
    TexturePtr suitTexture_;
    std::string trailLook_, victoryLook_;
    TrailFx trail_;
    float victoryT_ = -1.0f;
    Vec2 victoryAt_;
    float metTimer_ = 0.0f;
    bool collectionDirty_ = false;
    std::string ghostHash_;
    int64_t pbGhostMs_ = 0;
    // Achievements: what this run adds to the lifetime counters (once, at its end).
    void noteAchievementStats(bool completed, const RecordUpdate* upd);
    bool statsNoted_ = false;
    // Boss training (PlayOptions::bossTraining): ends when the phase chosen (or the boss) is
    // beaten, or at the player's first death; then its end screen. Never a record.
    void updateBossTraining();
    void endBossTraining(bool won);
    float fightStart_ = -1.0f;
    int trainingPhase_ = 0;
    std::optional<BossTrainingResult> bossTrainingEnd_;
    float bossTrainingDelay_ = 0.0f;
    // Daily challenge (PlayOptions::challenge): its rules, its replay (format 4), its end.
    void startChallenge();
    void finishChallenge(bool completed);
    void drawChallengeBanner();
    std::string challengeHash_;      // fingerprint of the level as installed (sent with the run)
    bool challengeSendable_ = false;  // that fingerprint is the challenge's
    /// 2.45: the challenge's revision of a campaign level, played from a file kept on this
    /// computer (bundled or downloaded) while another revision is current: its fingerprint.
    std::string challengeRevisionHash_;
    // « Plus loin » (2.34, a challenge with a route): how far this attempt goes (display and
    // online only, the simulation is not touched). Without checkpoints an attempt is one
    // life: it is decided at the first death (the run goes on); else at a fall, a restart or
    // a return to the menu. Its replay (format 4) goes with it when it beats my best attempt.
    void takeChallengeAttempt(bool firstLife);
    RaceTracker challengeTrack_;
    std::future<std::vector<Vec2>> challengeRouteFuture_;
    bool attemptDone_ = false;
    std::optional<ChallengeService::AttemptReport> attemptReport_;
    // Training (Ghost Racing): a segment from a checkpoint, again and again, never a record.
    void startTraining();
    void trainingSegmentDone();
    void drawTrainingBanner();
    /// Section training: the segment's references (best segment, in my record, best training
    /// time), always in view, clear of the HUD, of the tag `tagRect` and of the banner.
    void drawTrainingRefs(const Rect& tagRect, const Rect& banner);
    Rect trainingRefsRect_, trainingTagRect_, trainingBannerRect_;  // where they were drawn last
    float trainingBestBefore_ = -1.0f;  // best training time before this attempt (-1: none)
    bool trainingNewBest_ = false;
    float segmentBanner_ = 0.0f;
    float segmentTime_ = 0.0f;
    float segmentBest_ = -1.0f;
    int64_t runJumps_ = 0;
    bool wasGliding_ = false;  // the glider opens without a « powerUsed » event: counted here
    std::map<std::string, int64_t> runPowers_;
    std::vector<std::string> runCheckpoints_;  // checkpoint ids, in the order reached
    std::string raceKind_, raceLevelId_, raceHash_, raceTitle_;
    // Live spectating (2.36): this run streamed for the friends who may watch (display and
    // online only: the inputs and digests are the replay's, the simulation is not touched).
    void startSpectate();
    void endSpectate(const char* reason);
    unsigned spectateToken_ = 0;     // 0: not streamed
    size_t spectateChecks_ = 0;      // digests of the recorder already given
    // Keys of the powers (2.31): see refreshPowerKeys.
    void syncPowerKeys();
    PowerKeyMap powerKeys_;
    std::vector<std::string> powerKeyExtra_;
    int raceVersion_ = 0;
};

}  // namespace cosmic
