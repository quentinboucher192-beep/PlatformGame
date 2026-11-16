#pragma once
// ADMINISTRATION › Campagne (2.45): the levels of the campaign by planet with their revision
// online (a game release's, or one an administrator published: who, when, the choice about the
// records). « Modifier dans l'éditeur » opens the level in the game's editor, which publishes a
// new revision (the games download it, no update needed); « Historique » lists the revisions,
// and « Revenir à cette révision » makes an earlier one current again, after the same required
// choice: reset the records, or keep them. The server checks the role on every request.

#include <functional>
#include <memory>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

class GameServices;

class AdminCampaign {
public:
    AdminCampaign(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);

    /// The tab is shown: the list is read (again with `force`). `select`: a level to select.
    void enter(bool force = false, const std::string& select = {});
    void draw(const Rect& inner);
    /// The dialog of « Revenir à cette révision » takes the input.
    bool modal() const { return revertVersion_ > 0; }
    void drawModal();

private:
    void load();
    void loadHistory(const std::string& levelId);
    void edit(const Json& level);
    void openEditor(const std::string& levelId, int version, const std::string& hash);
    void revert();
    const Json* levelById(const std::string& id) const;
    /// « Version du jeu 2.20.0 · 15/11/2026 » or « Publiée par X · 16/11 · 02:14 · records gardés ».
    std::string sourceLine(const Json& rev, const Json& change) const;
    void drawLevels(const Rect& r);
    void drawHistory(const Rect& r);

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::function<bool(const ApiResult&)> lostRole_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);

    Json planets_ = Json::array();
    bool loaded_ = false, busy_ = false, opening_ = false;
    std::string selected_;
    Json history_;
    bool historyBusy_ = false;
    int historyGen_ = 0;
    // « Revenir à cette révision »: the revision, then the required choice (-1 none, 0 reset, 1 keep).
    int revertVersion_ = 0;
    int revertChoice_ = -1;
    bool reverting_ = false;
};

}  // namespace cosmic
