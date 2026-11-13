#pragma once

#include "engine/rendering/Renderer.h"

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

    float displayedHealth_ = 5.0f;
    float displayedEnergy_ = 100.0f;
    float bossBarDelayed_ = 1.0f;
    float shardPulse_ = 0.0f;
    int lastShards_ = 0;
    int lastSecond_ = -1;
    float time_ = 0.0f;
    float skipHold_ = 0.0f;

public:
    float skipProgress() const { return skipHold_; }
    void setSkipHold(float v) { skipHold_ = v; }
};

}  // namespace cosmic
