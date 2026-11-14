#pragma once

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
    const std::string& levelId() const { return levelId_; }
    GameWorld* world() { return world_.get(); }

private:
    void finishRun(bool completed);
    std::string nextLevelId() const;

    GameServices& g_;
    std::string levelId_;
    PlayOptions opt_;
    /// Not in any planet's campaign (made in the editor, imported): leaving goes back to
    /// the WORKSHOP, no "next level", and "Continuer" keeps pointing at the campaign.
    bool workshop_ = false;
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
