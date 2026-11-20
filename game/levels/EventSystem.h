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
    /// 2.80: an action of the event of `source` (an object id): a passage it closes waits for the team on
    /// that object's side (the action's params get « from »).
    void runAction(GameWorld& w, const EventAction& a, const std::string& source);
    int firedCount() const;

    /// Every supported action type (for the editor).
    static const std::vector<std::pair<std::string, std::string>>& actionTypes();
    static const std::vector<std::pair<std::string, std::string>>& triggerTypes();

private:
    bool matches(const LevelEvent& e, const Json& params) const;
    struct Pending {
        float timer;
        std::vector<EventAction> actions;
        std::string source;  // 2.80: the object of the event (a passage it closes waits on its side)
    };
    std::vector<LevelEvent> events_;
    std::vector<bool> fired_;
    std::vector<Pending> pending_;
};

}  // namespace cosmic
