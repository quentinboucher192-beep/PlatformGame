#pragma once

#include <future>
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
#include "game/records/Records.h"
#include "ui/screens/PauseMap.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

class GameServices;

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
    /// Answer of the power choice window (InputFrame::choice), used by the next step.
    void answerPowerOffer(uint8_t choice) { pendingChoice_ = choice; }
    const std::string& levelId() const { return levelId_; }
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
    enum class RecordMode { None, Official, Community, EditorCompletion };
    RecordMode recordMode_ = RecordMode::None;
    std::string gameplayHash_;
};

}  // namespace cosmic
