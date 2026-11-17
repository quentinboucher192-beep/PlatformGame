#pragma once

#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Scene.h"
#include "game/achievements/Achievements.h"

namespace cosmic {

class GameServices;
class GameScene;
struct PauseLayout;

/// Pause menu overlay: resume, restart, settings, back to menu / editor.
class PauseScene : public Scene {
public:
    PauseScene(GameServices& g, GameScene& game) : g_(g), game_(game) {}
    void onEnter() override;
    void onExit() override;
    /// Back from a screen opened over it (Succès…): the focus goes back to where it was.
    void onResume() override;
    void update(float dt) override;
    void render(float alpha) override;
    bool isOverlay() const override { return true; }

private:
    void drawMapPanel(const Rect& panel);
    /// « Succès suivis » (2.52): each achievement followed, its numbers and its bar, in `r`.
    void drawTracked(const Rect& r, const PauseLayout& L, const std::vector<const AchievementDef*>& rows, bool note);
public:

private:
    GameServices& g_;
    GameScene& game_;
    float t_ = 0.0f;
    bool confirmQuit_ = false;
    float mapPan_ = 0.0f;  // world units the map was scrolled by
    bool keysOpened_ = false;  // automated screenshots: « Touches de ce niveau » opened once
    std::string resumeFocus_;  // the focus to give back when a screen opened over it closes
    // The achievements' counters with this run's progress, and what their conditions read.
    AchievementStats liveStats_;
    AchievementInputs achInputs_;
};

}  // namespace cosmic
