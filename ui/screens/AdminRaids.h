#pragma once
// ADMINISTRATION › Raids (phase 1 of the clan raids, maquette « Administration des raids »): the
// world events of « Asterion ». An administrator schedules one in their own local time (the
// game sends UTC, with a preview of the start in several zones), sets its format, lock time,
// capacity and difficulty, opens or closes its registrations, publishes it (every clan is
// told) or cancels it. Each action goes to the Journal (« Raids »); the server checks the role on
// every request. 2.68: « Tester Asterion » (administrators only): a branch of the raid's catalog
// and one of its zones (the fight of Pyroclast included) played solo and locally, invincible when
// asked, the gigaboss scaled for 2 to 8 simulated players and started at a chosen phase; never
// reported (no consensus, state, ranking, contribution nor reward). 2.69 (phase 5, maquette
// « Administration »): the late arrivals of an event; « Valider la carte » (the server checks the
// titan's catalogs; publishing needs a valid map); the live panel « En direct » with real figures
// only (players in play, instances with their zone, players and the latency the games measured,
// gigaboss down, reconnections, refused reports, the journal of the admin actions and anomalies);
// « Suspendre » / « Reprendre »; a catch-up session after the event. The right column has three tabs:
// « Événements », « En direct », « Tester Asterion ».

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

class GameServices;

class AdminRaids {
public:
    AdminRaids(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);

    /// The tab is shown: the events are read (again with `force`).
    void enter(bool force = false);
    void draw(const Rect& inner);
    /// The cancellation of an event waits for its confirmation.
    bool modal() const { return confirm_; }
    void drawModal();

private:
    void load();
    void loadDetail();
    void select(const Json& e);
    void clearForm();
    /// The form as the server takes it; `utc` 0 when the start is unreadable.
    Json formJson(int64_t utc) const;
    void save(const Json& body);
    void drawForm(const Rect& r);
    void drawList(const Rect& r);
    void drawDetail(const Rect& r);
    void drawTest(const Rect& r);  // 2.68
    // 2.69: the live panel of the selected event and the administrators' actions on it.
    void loadLive();
    void drawLive(const Rect& r);
    void action(const std::string& path, const Json& body, const std::string& done);

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::function<bool(const ApiResult&)> lostRole_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);

    Json events_ = Json::array();  // GET /admin/raids
    Json detail_;                  // GET /admin/raids/{id}
    bool loaded_ = false, busy_ = false;
    int gen_ = 0, detailGen_ = 0;
    int64_t skew_ = 0;

    // The form (a new event when selected_ is empty).
    std::string selected_;
    Json saved_;  // formJson of the selected event as the server has it
    int formatIdx_ = 1, lockIdx_ = 2, difficultyIdx_ = 2;
    std::string date_, clock_, rewards_;
    bool open_ = true;
    float capacity_ = 0.0f, clanCapacity_ = 50.0f;

    bool confirm_ = false;
    std::string reason_, confirmError_;
    // 2.69: what the modal confirms (cancel, suspend, a catch-up session and its length).
    enum class Confirm { Cancel, Suspend, Catchup };
    Confirm confirmKind_ = Confirm::Cancel;
    int catchupIdx_ = 2;
    // 2.69: the late arrivals (an index in the form's list), the right column's tab, the live panel.
    int lateIdx_ = 0;
    int sideTab_ = 0;
    Json live_;
    std::string liveFor_;
    int liveGen_ = 0;
    float liveAt_ = -1000.0f;

    // 2.68: « Tester Asterion » (indices in the branch catalog, its zones, 2..8 players, phases).
    int testBranch_ = 0, testZone_ = -1, testPlayers_ = 0, testPhase_ = 0;
};

}  // namespace cosmic
