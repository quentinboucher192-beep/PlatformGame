#pragma once
// ADMINISTRATION › Ennemis (2.45, docs/online/ENEMY_REVISIONS.md): the native entities of the game
// with their definition online (the game's own, or a revision an administrator published: who,
// when). « Modifier » opens the enemy editor on one of them in its administration mode, where the
// definition is edited and published as a new revision with the campaign levels that use it
// (each with its choice about the records); « Historique » lists its revisions. The server checks
// the role on every request.

#include <functional>
#include <memory>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

class GameServices;

class AdminEnemies {
public:
    AdminEnemies(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);

    /// The tab is shown: the list is read (again with `force`).
    void enter(bool force = false);
    void draw(const Rect& inner);

private:
    void load();
    void loadHistory(const std::string& id);
    void edit(const std::string& id);
    /// The server's row of a native (nullptr: no revision online, the game's definition).
    const Json* serverRow(const std::string& id) const;
    void drawList(const Rect& r);
    void drawHistory(const Rect& r);

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::function<bool(const ApiResult&)> lostRole_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);

    Json enemies_ = Json::array();  // GET /admin/enemies
    bool loaded_ = false, busy_ = false, opening_ = false;
    std::string selected_;
    Json history_;
    bool historyBusy_ = false;
    int historyGen_ = 0;
};

}  // namespace cosmic
