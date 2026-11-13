#pragma once

#include "engine/core/Scene.h"

namespace cosmic {

class GameServices;
class GameScene;

/// Pause menu overlay: resume, restart, settings, back to menu / editor.
class PauseScene : public Scene {
public:
    PauseScene(GameServices& g, GameScene& game) : g_(g), game_(game) {}
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void render(float alpha) override;
    bool isOverlay() const override { return true; }

private:
    GameServices& g_;
    GameScene& game_;
    float t_ = 0.0f;
    bool confirmQuit_ = false;
};

}  // namespace cosmic
