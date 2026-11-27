#pragma once

#include <cstdint>

#include "engine/core/Scene.h"

namespace cosmic {

class GameServices;
class GameWorld;
struct PowerKeyMap;

/// 2.75: what asks the choice (a level, GameScene; a raid's armory, RaidGameScene): its world, the
/// power keys shown, and where the answer goes (the next input frame of the player offered).
class PowerChoiceHost {
public:
    virtual ~PowerChoiceHost() = default;
    virtual GameWorld* world() = 0;
    virtual const PowerKeyMap& powerKeys() const = 0;
    virtual void answerPowerOffer(uint8_t choice) = 0;
    /// 2.95: the AI plays this player's seat (a raid test, the administrator AFK): it answers the offer
    /// itself, through its frame; the window closes (it never waits for a choice nobody gives).
    virtual bool offerAnsweredByAi() const { return false; }
};

/// Shown over the level when a power tablet is touched with every power slot taken: the
/// player replaces one of the carried powers (the new one takes its slot; each power keeps
/// its own key, 2.31) or leaves the new one on its tablet. Each card shows the power's key, or
/// — dashed gold frame — the move or « passif » of a power that has none. The game
/// waits meanwhile; the answer goes into the next input frame, so the replay of the run
/// contains it.
class PowerChoiceScene : public Scene {
public:
    PowerChoiceScene(GameServices& g, PowerChoiceHost& game) : g_(g), game_(game) {}
    void onEnter() override;
    void onExit() override;
    void update(float dt) override { t_ += dt; }
    void render(float alpha) override;
    bool isOverlay() const override { return true; }

private:
    void answer(uint8_t choice);

    GameServices& g_;
    PowerChoiceHost& game_;
    float t_ = 0.0f;
    bool answered_ = false;
};

}  // namespace cosmic
