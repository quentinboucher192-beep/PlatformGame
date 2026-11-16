#pragma once
// 2.47: an account is private to ONE computer (docs/online/DEVICES.md). ONLINE SETTINGS ›
// « Cet ordinateur »: the computer the account is bound to (its name), local and online mode,
// the copy of the progression on the server, the security alerts; and the summary shown at
// the login after some local play (« De retour en ligne »).

#include <functional>
#include <memory>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

class GameServices;
class OnlineUi;
class Scene;

class OnlineComputerTab {
public:
    OnlineComputerTab(GameServices& g, OnlineUi& ui, std::function<void(const std::string&, bool)> say);
    /// The tab is shown: the server's view is read again.
    void enter();
    void draw(const Rect& content);

private:
    void load();
    GameServices& g_;
    OnlineUi& ui_;
    std::function<void(const std::string&, bool)> say_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    Json data_;
    bool loading_ = false, busy_ = false;
    std::string name_;
};

/// « De retour en ligne » (GameServices::reconnectSummary), pushed after a login.
std::unique_ptr<Scene> makeReconnectSummary(GameServices& g);

}  // namespace cosmic
