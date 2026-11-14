#pragma once

#include "engine/rendering/Renderer.h"
#include "game/gameplay/LevelProgress.h"

namespace cosmic {

class GameWorld;
class UIContext;
struct LevelRecord;
struct GameplaySettings;

/// In-game heads-up display: health, energy, powers with cooldowns and button prompts,
/// chronometer (with medal targets and split comparison), collectibles, boss gauge,
/// messages, dialogues and cinematic overlays.
class Hud {
public:
    void update(GameWorld& w, float dt);
    void render(UIContext& ui, GameWorld& w, const WorldView& view, const LevelRecord* record,
                const GameplaySettings& settings);

private:
    void drawVitals(UIContext& ui, GameWorld& w);
    void drawPowers(UIContext& ui, GameWorld& w);
    void drawTimer(UIContext& ui, GameWorld& w, const LevelRecord* record);
    void drawCollectibles(UIContext& ui, GameWorld& w);
    void drawBoss(UIContext& ui, GameWorld& w);
    void drawMessages(UIContext& ui, GameWorld& w);
    void drawCinematic(UIContext& ui, GameWorld& w);
    void drawInteractPrompt(UIContext& ui, GameWorld& w, const WorldView& view);
    /// Level progress bar (placement 1 top, 2 bottom): route %, checkpoints, ambushes,
    /// bosses and the exit portal.
    void drawProgress(UIContext& ui, GameWorld& w, int placement);

    float displayedHealth_ = 5.0f;
    float displayedEnergy_ = 100.0f;
    float bossBarDelayed_ = 1.0f;
    float shardPulse_ = 0.0f;
    int lastShards_ = 0;
    int lastSecond_ = -1;
    float time_ = 0.0f;
    float skipHold_ = 0.0f;

    LevelProgressTrack track_;
    size_t trackHint_ = 0;
    float progress_ = 0.0f;      // current place on the route (0..1)
    float bestProgress_ = 0.0f;  // furthest place reached
    float shownProgress_ = 0.0f;

public:
    void setProgressTrack(LevelProgressTrack t) {
        track_ = std::move(t);
        trackHint_ = 0;
    }
    float skipProgress() const { return skipHold_; }
    void setSkipHold(float v) { skipHold_ = v; }
};

}  // namespace cosmic
