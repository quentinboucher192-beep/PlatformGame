#pragma once

#include "engine/core/Scene.h"

namespace cosmic {

class GameServices;
class GameScene;

/// Shown over the level when a power tablet is touched with every power slot taken (or
/// with a carried power on the same button): the player replaces one of the carried
/// powers or leaves the new one on its tablet. The game waits meanwhile; the answer goes
/// into the next input frame, so the replay of the run contains it.
class PowerChoiceScene : public Scene {
public:
    PowerChoiceScene(GameServices& g, GameScene& game) : g_(g), game_(game) {}
    void onEnter() override;
    void onExit() override;
    void update(float dt) override { t_ += dt; }
    void render(float alpha) override;
    bool isOverlay() const override { return true; }

private:
    void answer(uint8_t choice);

    GameServices& g_;
    GameScene& game_;
    float t_ = 0.0f;
    bool answered_ = false;
};

}  // namespace cosmic
