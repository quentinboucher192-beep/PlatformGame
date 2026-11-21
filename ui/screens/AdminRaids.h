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
// « Événements », « En direct », « Tester Asterion ». 2.77.4: a fourth, « Avis »: every review of the
// selected event (masked ones and reports included), each comment masked or restored (Journal). 2.80 (docs/RAIDS.md
// « Tester avec des joueurs invités »): « Joueurs invités » in both tests (one zone, the whole raid): real players
// connected now chosen before the launch (their group, « Invincible »), invited with it — the test then goes through
// the server (ui/online/RaidTestGuests). 2.83 (maquette « Administration des raids », générique): everything
// the screen says of a raid comes from the raid catalogue (game/raid/RaidCatalog, data/raids): the « Raid »
// list (each raid with its icon), its poster under it (art, name, subtitle, pitch, its counts, formats and
// difficulty, « Fiche du raid › »), its formats and difficulty range in the form, « Tester le raid » with its
// icon, the raid and format of each event. No raid is named in this screen's code (tests/test_admin_raids.cpp).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/rendering/Text.h"
#include "game/online/OnlineTypes.h"
#include "ui/online/RaidReviewsPanel.h"

namespace cosmic {

class GameServices;
class RaidTestGuests;
class UIContext;
namespace raid {
struct CatalogEntry;
}

/// 2.83: one text of the raid's poster: its rect, its text as drawn (fitted, else shortened; `verbatim`: already
/// translated, by wrap()) and its size (unscaled).
struct RaidPosterText {
    Rect r;
    std::string text;
    float size = 14.0f;
    FontFace face = FontFace::Body;
    bool verbatim = false;
};

/// 2.83: the raid's poster (« affiche ») placed in a card of width `r.w` from (r.x, r.y): its accent bar, its
/// art and the tag on it, then its texts one under the other (name, subtitle, pitch, chips, « Fiche du raid › »),
/// each measured into the room it has. `height`: the card's own (r.h is not read).
struct RaidPosterLayout {
    Rect card, accent, art;
    RaidPosterText tag;  // on the art (r.w 0: no room for it)
    std::vector<RaidPosterText> name, subtitle, pitch, chips;
    RaidPosterText link;  // r.w 0: the raid has no sheet
    std::vector<Rect> chipBoxes;
    float height = 0.0f;
};
RaidPosterLayout layoutRaidPoster(UIContext& ui, const Rect& r, const raid::CatalogEntry& e);
/// Its chips: « 8 parties », « 7 Gigaboss + le Cœur », its formats (« Court · Standard · Légendaire »), « Difficulté 1 à 5 ».
std::vector<std::string> raidPosterChips(const raid::CatalogEntry& e);

/// 2.83: where the form's top was drawn last (tests/test_admin_raids.cpp checks nothing overlaps).
struct AdminRaidsLayout {
    Rect form, fields, raidBox, right;
    RaidPosterLayout poster;
    std::vector<Rect> plates;  // the formats of the raid
    std::vector<std::string> plateLabels;
    std::vector<float> plateFonts;
    float sideTabsBottom = 0.0f;
};

class AdminRaids {
public:
    AdminRaids(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);
    ~AdminRaids();

    /// The tab is shown: the events are read (again with `force`).
    void enter(bool force = false);
    void draw(const Rect& inner);
    /// The cancellation of an event waits for its confirmation.
    bool modal() const;
    void drawModal();
    /// 2.77: the settings of « Simuler un raid complet » in `r` (also the capture of the documentation).
    void drawSim(const Rect& r, bool capture = false);
    /// 2.83: where the form's top was drawn last (the raid's list, its poster, its formats).
    const AdminRaidsLayout& lastLayout() const { return layout_; }

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
    // 2.83: the raid of the catalogue chosen in the form (its formats, its difficulty range), its art drawn.
    const raid::CatalogEntry* formRaid() const;
    void setRaid(int index);
    bool drawArt(const std::string& image, const Rect& crop, const Rect& dst);
    void drawIcon(const raid::CatalogEntry* e, const Rect& dst);
    void drawPoster(const RaidPosterLayout& L, const raid::CatalogEntry& e);
    // 2.69: the live panel of the selected event and the administrators' actions on it.
    void loadLive();
    void drawLive(const Rect& r);
    void action(const std::string& path, const Json& body, const std::string& done);
    void drawReviews(const Rect& r);  // 2.77.4

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
    // 2.83: the raid of the form (an index in the catalogue) and whether its list is open; the layout drawn last.
    int raidIdx_ = 0;
    bool raidOpen_ = false;
    AdminRaidsLayout layout_;
    Json live_;
    std::string liveFor_;
    int liveGen_ = 0;
    float liveAt_ = -1000.0f;

    // 2.68: « Tester le raid » (indices in the branch catalog, its zones, 2..8 players, phases).
    int testBranch_ = 0, testZone_ = -1, testPlayers_ = 1, testPhase_ = 0;  // 2.75: testPlayers_ = simulated players (0..7)
    uint32_t testParts_ = 0x7Fu;  // 2.74 (phase 6): the Cœur d'Asterion's test: the parts simulated as fallen
    int testRelics_ = 0;
    // 2.77: « Une zone » or « Simuler un raid complet » (docs/RAIDS.md §21); the level of the simulated
    // players (both); the raid's groups, simulated players per group, speed of the groups not watched.
    int testMode_ = 0, testSkill_ = 1, simGroups_ = 6, simBots_ = 3, simSpeed_ = 0;
    int testDifficulty_ = 2;  // titan 13: « Difficulté » of the test (index: 1 to 5 / 5)
    /// Titan 13: the « Difficulté » row of a test (1 to 5) and what it changes under it; returns the y under them.
    float difficultyRow(const Rect& row);
    // 2.81.2: « Raid à tester » (2.83: an index in the raid catalogue) and whether its list is open.
    int testRaid_ = 0;
    bool testRaidOpen_ = false;
    bool simBotsInvincible_ = false;
    // 2.77.4: « Avis » (the players' reviews of the selected event, administrators' view).
    RaidReviewsPanel reviews_;
    // 2.80: the players invited to the next test (its window), and the row that opens it.
    std::unique_ptr<RaidTestGuests> guests_;
    void guestsRow(const Rect& r, bool fullRaid);
};

}  // namespace cosmic
