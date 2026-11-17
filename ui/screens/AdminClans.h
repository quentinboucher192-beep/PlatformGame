#pragma once
// ADMINISTRATION › Clans (2.66): the clans of Cosmic Online with their chief, searched by tag or
// name; an administrator renames a clan (its name, its tag) or dissolves it, after a
// confirmation and with an optional reason. Each action goes to the Journal (« Clans »). The
// server checks the role on every request.

#include <functional>
#include <memory>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

class GameServices;

class AdminClans {
public:
    AdminClans(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);

    /// The tab is shown: the list is read (again with `force`).
    void enter(bool force = false);
    void draw(const Rect& inner);
    /// A rename or a dissolution waits for its confirmation.
    bool modal() const { return mode_ != Mode::None; }
    void drawModal();

private:
    enum class Mode { None, Rename, Dissolve };
    void load(bool more);
    void drawRow(const Json& c, const Rect& row);

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::function<bool(const ApiResult&)> lostRole_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);

    Json clans_ = Json::array();  // GET /admin/clans
    int total_ = 0;
    bool loaded_ = false, busy_ = false, more_ = false;
    int gen_ = 0;
    std::string query_, lastQuery_;
    float queryT_ = 0.0f;
    bool queryDirty_ = false;

    Mode mode_ = Mode::None;
    Json target_;
    std::string name_, tag_, reason_, error_;
};

}  // namespace cosmic
