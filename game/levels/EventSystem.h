#pragma once

#include <string>
#include <vector>

#include "game/levels/LevelData.h"

namespace cosmic {

class GameWorld;

/// Executes the level's events: "when <source> <trigger> then <actions>". Sources are
/// object ids (triggers, levers, receptors, arenas, enemies, collectibles), power ids
/// (trigger "powerUsed") or "level" (levelStart, time). Actions may be delayed.
class EventSystem {
public:
    void load(const std::vector<LevelEvent>& events);
    void signal(GameWorld& w, const std::string& source, const std::string& trigger, const Json& params);
    void fixedUpdate(GameWorld& w, float dt, float levelTime);
    void runAction(GameWorld& w, const EventAction& a);
    int firedCount() const;

    /// Every supported action type (for the editor).
    static const std::vector<std::pair<std::string, std::string>>& actionTypes();
    static const std::vector<std::pair<std::string, std::string>>& triggerTypes();

private:
    bool matches(const LevelEvent& e, const Json& params) const;
    struct Pending {
        float timer;
        std::vector<EventAction> actions;
    };
    std::vector<LevelEvent> events_;
    std::vector<bool> fired_;
    std::vector<Pending> pending_;
};

}  // namespace cosmic
