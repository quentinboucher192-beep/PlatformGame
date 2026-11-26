#pragma once

#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "engine/core/Scene.h"
#include "engine/input/InputScript.h"
#include "game/achievements/Achievements.h"
#include "game/gameplay/EnemyArrows.h"
#include "game/gameplay/GameRenderer.h"
#include "ui/screens/PowerChoiceScene.h"
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
#include "ui/screens/TrainingSession.h"

namespace cosmic {

class GameServices;
class GameScene;
class PlayStatsRecorder;

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
/// 2.91: Pause › « Startpos et mesures » (a section training): the StartPos and the measure points
/// of this level version, to start from one, delete, rename, move; the display options.
std::unique_ptr<Scene> makeTrainingMarks(GameServices& g, GameScene& game);

/// Plays a level: simulation, rendering, HUD, pause menu and end of level.
class GameScene : public Scene, public PowerChoiceHost {
public:
    GameScene(GameServices& g, std::string levelId, PlayOptions opt);
    ~GameScene() override;

    void onEnter() override;
    void onExit() override;
    void onResume() override;
    void handleEvent(const SDL_Event& e) override;
    /// The giga boss whose story is being told over the run ("" when none): the run waits.
    const std::string& storyBoss() const { return storyBoss_; }
    void fixedUpdate(float dt) override;
    void update(float dt) override;
    void render(float alpha) override;
    void renderDebug() override;
    /// A run in progress is never disturbed by online notifications (they show afterwards).
    bool holdsNotifications() const override { return true; }
    // Pause, results, power choice... are scenes pushed on top: the pointer comes back with them.
    bool hidesCursor() const override { return true; }

    void restart();
    // 2.64, the tutorial (PlayOptions::tutorial) and its explanation cards (event action
    // « tutorial », in any level): the card due or open ("" none); while it is, the run waits
    // (not a step is simulated, as under the pause menu).
    bool tutorial() const { return opt_.tutorial; }
    /// 2.81: an administrator's replay of the tutorial (PlayOptions::tutorialReplay): nothing kept.
    bool tutorialReplay() const { return opt_.tutorial && opt_.tutorialReplay; }
    const std::string& tutorialCard() const { return tutorialCard_; }
    /// A card's rank among the level's cards (1 = the first) and their number (« 3 / 9 »).
    std::pair<int, int> tutorialCardRank(const std::string& id) const;
    /// The tutorial left from its pause menu: skipped for good, back to the main menu.
    void leaveTutorial();
    /// 2.48: the same level again as a practice run (hardcore levels).
    void restartPractice();
    void quitToMenu();
    bool editorTest() const { return opt_.editorTest; }
    bool sectionTraining() const { return opt_.training; }  // « Répéter un segment »: quitting returns to its setup
    /// 2.91: the training's StartPos and measure points (Pause › « Startpos et mesures »).
    TrainingSession& trainingSession() { return session_; }
    /// This level version (GhostStore::levelKey), empty when nothing is kept.
    std::string levelKey() const;
    /// The attempt again from the StartPos `id` (0: from the training's own start).
    void restartFrom(int startPosId);
    const std::string& levelName() const { return level_.name; }
    /// Answer of the power choice window (InputFrame::choice), used by the next step.
    void answerPowerOffer(uint8_t choice) override { pendingChoice_ = choice; }
    const std::string& levelId() const { return levelId_; }
    // Keys of the powers (2.31): each power keeps its own key in the level (the level's, the
    // player's, the suggestion); the slot keys of the input follow the powers carried.
    const LevelData& level() const { return level_; }
    const PowerKeyMap& powerKeys() const override { return powerKeys_; }
    /// Powers given besides the level's own (boss training): « Touches de ce niveau » lists them too.
    const std::vector<std::string>& powerKeyExtra() const { return powerKeyExtra_; }
    /// 2.51: « Invincible » (administrators, training runs only): can it be switched here, is it on.
    bool invincibleAllowed() const;
    bool invincible() const { return world_ && world_->invincible(); }
    void setInvincible(bool on);
    /// 2.50: the powers in hand, by slot (index = slot), for « Touches de ce niveau ».
    std::vector<std::string> carriedPowers() const;
    /// Resolves the keys again (the player changed theirs, or the settings) and binds them.
    void refreshPowerKeys();
    // Ghost Racing (pause menu → Fantômes).
    bool hasGhosts() const { return ghostRun_.active(); }
    bool ghostsVisible() const { return ghostRun_.visible(); }
    void setGhostsVisible(bool on) { ghostRun_.setVisible(on); }
    void applyGhostSettings();
    GameWorld* world() override { return world_.get(); }
    /// 2.51: the achievements' counters with what this run has added so far, as leaving it now
    /// would count them (the pause menu's « Succès suivis »); the saved ones when it counts for nothing.
    AchievementStats liveAchievementStats() const;
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
    /// Training (2.60.3): a section training's attempt, kept on this computer only (its best
    /// attempt's replay), never sent.
    enum class RecordMode { None, Official, Community, EditorCompletion, Local, Challenge, Training };
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
    // « Histoire des giga-boss » (2.51): the fight against a giga boss whose story has not been
    // seen starts: its story is told over the run, which waits (not a step is simulated nor an
    // input recorded until it ends, as under the pause menu). `storyBoss_`: the boss whose story
    // is due or being told; `storyOpen_`: its scene was opened (cleared when it closes).
    void checkBossStory();
    // 2.64: the tutorial's cards (see tutorialCard()) and Bip, the small robot floating by Nova
    // (display only).
    void checkTutorialCard();
    void finishTutorial();
    void drawTutorialWorld(const WorldView& view, float alpha);
    std::string tutorialCard_;
    bool tutorialCardOpen_ = false;
    std::vector<std::string> tutorialOrder_;
    Vec2 bipPos_;
    bool bipPlaced_ = false;
    float bipTime_ = 0.0f;
    std::string storyBoss_;
    bool forcedPhase_ = false;  // 2.58: a capture that forced a boss phase (still one once the phase is reached)
    bool storyOpen_ = false;
    /// A boss out of view: a textured arrow on the screen's border, turned towards it.
    void drawBossArrow(const WorldView& view);
    Rect bossArrowRect_;  // where it was drawn last (empty: not drawn), kept clear by the enemy arrows
    /// 2.52: the enemies already met that are out of view (display only): who was seen is noted
    /// each frame; one arrow per pack on the border (« ×N »), clear of the HUD, of the boss arrow,
    /// of the ghosts' box and of the banners.
    void drawEnemyArrows(const WorldView& view);
    /// 2.81.2: the name of each power over its tablet or station (its icon and cost when there is
    /// room), never over the HUD, the arrows, the ghosts' box or the banners (display only).
    void drawPowerNames(const WorldView& view);
    EnemyArrows::Tracker metEnemies_;    // a new attempt is a new scene: nobody met yet
    std::vector<Rect> enemyArrowAreas_;  // where they were drawn last (the ghosts' names keep clear)
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
    /// 2.60.3: the Sum of Best of this level version when the run started, full runs and
    /// section trainings together (the ghosts' panel, the segments' summary); -1 unknown.
    float sobAtStart_ = -1.0f;
    // Achievements: what this run adds to the lifetime counters (once, at its end).
    void noteAchievementStats(bool completed, const RecordUpdate* upd);
    /// Adds this run's counters to `a` (false: a run that counts for nothing); `uses`: the
    /// player's power uses of the run.
    bool addRunCounters(AchievementStats& a, bool completed, int64_t& uses) const;
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
    /// How this section training's attempt starts (the run, and its replay: 2.60.3).
    SectionTrainingStart trainingStart() const;
    void trainingSegmentDone();
    void drawTrainingBanner();
    /// Section training: the segment's references (best segment, in my record, best training
    /// time), always in view, clear of the HUD, of the tag `tagRect` and of the banner.
    void drawTrainingRefs(const Rect& tagRect, const Rect& banner);
    Rect trainingRefsRect_, trainingTagRect_, trainingBannerRect_;  // where they were drawn last
    float trainingBestBefore_ = -1.0f;  // the range's best before this attempt (-1: none)
    bool trainingNewBest_ = false;
    // 2.91: the personal Sum of Best of this attempt, training's StartPos and measure points.
    TrainingSession session_;
    /// This attempt's exact replay so far (encoded; empty: not recorded), for the bests it sets.
    std::vector<uint8_t> attemptReplay() const;
    /// The attempt ends (once): its passage joins the personal bests.
    PersonalBests::Added commitBests(float exitTime, const std::vector<uint8_t>* replay);
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
    /// 2.54: the player's statistics of this run (read only, game/records/PlayStats.h). Last
    /// member: gone before the world it listens to.
    std::unique_ptr<PlayStatsRecorder> playStats_;
};

}  // namespace cosmic
