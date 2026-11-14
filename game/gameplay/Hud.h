#pragma once

#include "engine/rendering/Renderer.h"
#include "game/gameplay/LevelProgress.h"
#include "saves/HudLayout.h"

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

    /// Where an element is drawn: its anchor on screen, its size factor and, for the
    /// progress bar, the length of the rail.
    struct Place {
        Vec2 o;
        float k = 1.0f;
        float width = 0.0f;
    };
    Place place(UIContext& ui, const GameWorld& w, const GameplaySettings& s, HudElement e) const;
    /// Screen area an element covers at a place (HUD editor, overlap checks); empty when
    /// the element has nothing to show in this world.
    Rect area(UIContext& ui, const GameWorld& w, HudElement e, const Place& p) const;
    /// Anchor of a place whose area has its top-left corner at `topLeft` (dragging).
    Vec2 anchorForArea(UIContext& ui, const GameWorld& w, HudElement e, const Place& p, Vec2 topLeft) const;

private:
    void drawVitals(UIContext& ui, GameWorld& w, Vec2 o, float k);
    void drawPowers(UIContext& ui, GameWorld& w, Vec2 o, float k);
    void drawTimer(UIContext& ui, GameWorld& w, const LevelRecord* record, Vec2 o, float k);
    void drawCollectibles(UIContext& ui, GameWorld& w, Vec2 o, float k);
    void drawBoss(UIContext& ui, GameWorld& w, Vec2 o, float k);
    void drawMessages(UIContext& ui, GameWorld& w);
    void drawCinematic(UIContext& ui, GameWorld& w);
    void drawInteractPrompt(UIContext& ui, GameWorld& w, const WorldView& view);
    /// Level progress bar (placement 1 top, 2 bottom): route %, checkpoints, ambushes,
    /// bosses and the exit portal.
    void drawProgress(UIContext& ui, GameWorld& w, Vec2 o, float k, float width);

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
