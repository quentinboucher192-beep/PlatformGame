#pragma once

#include <optional>
#include <string>

#include "engine/core/Scene.h"
#include "game/levels/LevelData.h"
#include "game/online/OnlineRecord.h"
#include "game/records/Records.h"

namespace cosmic {

class GameServices;
class GameScene;

struct ResultsData {
    RunResult run;
    RecordUpdate update;
    LevelRecord previous;
    LevelData level;
    std::string failReason;
    std::string nextLevel;
    bool editorTest = false;
    /// Performance kept for Cosmic Online (nullopt: not an improvement / not rankable).
    std::optional<OnlineAttempt> online;
};

/// End-of-level screen: animated statistics, comparison with the personal record, the
/// reference time and the medal goals, medal award and next actions.
class ResultsScene : public Scene {
public:
    ResultsScene(GameServices& g, GameScene& game, ResultsData data) : g_(g), game_(game), d_(std::move(data)) {}
    void onEnter() override;
    void update(float dt) override;
    void render(float alpha) override;
    bool isOverlay() const override { return true; }
    bool updatesSceneBelow() const override { return true; }

private:
    void drawOnlineStatus(const Rect& panel);

    GameServices& g_;
    GameScene& game_;
    ResultsData d_;
    float t_ = 0.0f;
    int lastTick_ = -1;
    bool medalPlayed_ = false;
};

}  // namespace cosmic
