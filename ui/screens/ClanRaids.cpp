#include "ui/screens/ClanRaids.h"

#include <algorithm>
#include <set>
#include <cmath>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/Clans.h"
#include "ui/online/RaidReviewsPanel.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/AsterionFilm.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {
/// 2.68: the seven branches of the titan (a click on one opens its map while the raid runs).
const std::vector<std::string> kTitanParts = {"crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite"};
}  // namespace

using namespace raidui;

namespace {

constexpr float kPoll = 60.0f;  // the tab is read again every minute while it is shown

std::string plural(int n, const std::string& one, const std::string& many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

const Json& at(const Json& j, const char* key) {
    static const Json null;
    if (!j.is_object()) return null;
    auto it = j.find(key);
    return it == j.end() ? null : *it;
}

}  // namespace

ClanRaids::ClanRaids(GameServices& g, std::function<void(const std::string&, bool)> say) : g_(g), say_(std::move(say)) {}

std::string ClanRaids::clanId() const { return jstr(at(data_, "clan"), "id"); }
std::string ClanRaids::clanRole() const { return jstr(at(data_, "clan"), "role"); }
std::string ClanRaids::clanTag() const { return jstr(at(data_, "clan"), "tag"); }

raidreviews::Summary ClanRaids::reviewsOf(const std::string& eventId) const {
    for (const char* list : {"world", "clan_events", "history"})
        for (const Json& j : at(data_, list))
            if (jstr(j, "id") == eventId) return raidreviews::parseSummary(at(j, "reviews"));
    return {};
}

void ClanRaids::enter(bool force) {
    if (loaded_ && !force) return;
    load();
    loadNotes();
}

void ClanRaids::update(float dt, bool shown) {
    timer_ += dt;
    if (shown && timer_ >= kPoll) {
        timer_ = 0.0f;
        load();
        loadNotes();
    }
}

void ClanRaids::load() {
    const int gen = ++gen_;
    g_.online.get("/api/v1/raids", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != gen_) return;
                      if (!r.ok) {
                          if (!loaded_) error_ = r.message;
                          return;
                      }
                      data_ = r.data;
                      loaded_ = true;
                      cache_ = r.fromCache;
                      error_.clear();
                      if (!r.fromCache) skew_ = skewOf(r.data);
                      unread_ = jget<int>(r.data, "unread", 0);
                      if (!r.fromCache && g_.online.loggedIn()) rememberClaimed(r.data.value("rewards_claimed", Json::array()));
                  }));
}

void ClanRaids::rememberClaimed(const Json& list) {
    // 2.68: the raid rewards this account claimed (the raid badges unlock from them; kept offline).
    if (!list.is_array()) return;
    std::set<std::string> ids;
    for (const Json& v : list)
        if (v.is_string() && v.get<std::string>().size() <= 64) ids.insert(v.get<std::string>());
    if (ids == g_.collection.raidRewards) return;
    g_.collection.raidRewards = std::move(ids);
    g_.collection.save();
    g_.checkAchievements();
}

void ClanRaids::claimRewards() {
    if (claiming_ || !g_.online.loggedIn()) return;
    claiming_ = true;
    g_.online.send("POST", "/api/v1/raids/rewards/claim", Json::object(), OnlineService::guard(life_, [this](const ApiResult& r) {
                       claiming_ = false;
                       if (!r.ok) {
                           say_("Récompenses non récupérées : " + r.message, true);
                           return;
                       }
                       const int n = jget<int>(r.data, "claimed", 0);
                       say_(n > 1   ? std::to_string(n) + " récompenses récupérées : badge au profil, titres dans le vestiaire."
                            : n == 1 ? "Récompense récupérée : elle est dans votre profil et le vestiaire."
                                     : "Ces récompenses étaient déjà récupérées.",
                            false);
                       rememberClaimed(r.data.value("claimed_rewards", Json::array()));
                       g_.challenges.refreshMine();  // the titles won (GET /players/me/rewards) follow
                       load();
                   }));
}

void ClanRaids::loadNotes() {
    if (!g_.online.loggedIn()) {
        notes_ = Json::array();
        notesLoaded_ = true;
        return;
    }
    const int gen = ++notesGen_;
    g_.online.get("/api/v1/notifications?filter=raids&limit=30", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                      if (gen != notesGen_ || !r.ok) return;
                      notes_ = r.data.value("notifications", Json::array());
                      notesLoaded_ = true;
                  }));
}

void ClanRaids::openEvent(const std::string& id) {
    if (!id.empty()) g_.app.pushScene(makeRaidEvent(g_, id));
}

void ClanRaids::act(const raids::Action& a, const std::string& eventId) {
    if (a.id == "login") {
        g_.app.pushScene(makeOnlineAccount(g_, !g_.settings.online.onboardingDone, false));
        return;
    }
    std::string method = "POST", path = "/api/v1/raids/events/" + eventId, done;
    if (a.id == "register") {
        path += "/register";
        done = "Vous êtes inscrit : les rappels arriveront ici.";
    } else if (a.id == "unregister") {
        method = "DELETE";
        path += "/register";
        done = "Inscription annulée.";
    } else if (a.id == "register_clan") {
        path += "/clan";
        done = "Clan inscrit : ses membres sont prévenus et s'inscrivent eux-mêmes.";
    } else return;
    acting_ = true;
    g_.online.send(method, path, Json::object(), OnlineService::guard(life_, [this, done](const ApiResult& r) {
                       acting_ = false;
                       if (!r.ok) return say_(r.message, true);
                       say_(done, false);
                       load();
                   }));
}

void ClanRaids::answer(const std::string& eventId, bool accept) {
    acting_ = true;
    g_.online.send("POST", "/api/v1/raids/events/" + eventId + "/invitation", {{"accept", accept}},
                   OnlineService::guard(life_, [this, accept](const ApiResult& r) {
                       acting_ = false;
                       if (!r.ok) return say_(r.message, true);
                       say_(accept ? "Invitation acceptée : vos membres sont prévenus." : "Invitation refusée.", false);
                       load();
                   }));
}

void ClanRaids::markAllRead() {
    g_.online.send("POST", "/api/v1/notifications/read-all?filter=raids", Json::object(), OnlineService::guard(life_, [this](const ApiResult& r) {
                       if (!r.ok) return say_(r.message, true);
                       unread_ = 0;
                       g_.online.noteNotificationsUnread(jget<int>(r.data, "unread", g_.online.unreadNotifications()));
                       for (auto& n : notes_) n["read"] = true;
                   }));
}

void ClanRaids::openNote(const Json& n) {
    const std::string id = jstr(n, "id");
    if (!jget<bool>(n, "read", false) && !id.empty()) {
        for (auto& x : notes_)
            if (jstr(x, "id") == id) x["read"] = true;
        unread_ = std::max(0, unread_ - 1);
        g_.online.noteNotificationsUnread(std::max(0, g_.online.unreadNotifications() - 1));
        g_.online.send("POST", "/api/v1/notifications/" + id + "/read", Json::object(), OnlineService::guard(life_, [](const ApiResult&) {}));
    }
    const Json& d = at(n, "data");
    if (jstr(d, "event") != "cancelled") openEvent(jstr(d, "event_id"));
}

void ClanRaids::putPrefs(const Json& body) {
    g_.online.send("PUT", "/api/v1/raids/prefs", body, OnlineService::guard(life_, [this](const ApiResult& r) {
                       if (!r.ok) {
                           say_(r.message, true);
                           load();
                           return;
                       }
                       data_["prefs"] = r.data.value("prefs", Json());
                   }));
}

// ------------------------------------------------------------------ drawing

void ClanRaids::draw(const Rect& body) {
    if (asterionfilm::hold(g_)) return;  // 2.68: Asterion's film first (its first viewing), before anything of the tab
    UIContext& ui = g_.ui;
    ui.pushId("raids");
    const raids::TabLayout lay = raids::tabLayout(data_, g_.online.loggedIn());
    const float gap = ui.s(16);
    const float asideW = std::min(ui.s(380), body.w * 0.3f);
    const Rect aside{body.right() - asideW, body.y, asideW, body.h};
    const Rect left{body.x, body.y, body.w - asideW - gap, body.h};
    if (!loaded_) {
        fit(ui, error_.empty() ? "Chargement des raids…" : error_, {left.x + ui.s(8), left.y + ui.s(24)}, 17, left.w - ui.s(16),
            error_.empty() ? ui.theme().textDim : ui.theme().danger);
        // 2.73: the sheets of Asterion and its gigaboss are the game's own data: offline too.
        sheetsButton("sheets", {left.x + ui.s(8), left.y + ui.s(52), std::min(left.w - ui.s(16), sheetsButtonWidth()), ui.s(44)});
    } else {
        const float heroH = std::min(ui.s(330), std::max(ui.s(250), left.h * 0.48f));
        drawHero({left.x, left.y, left.w, heroH}, lay);
        const float bgap = ui.s(14);
        const float by = left.y + heroH + bgap, bh = left.bottom() - by;
        const float bw = (left.w - 2 * bgap) / 3.0f;
        drawClanRaids({left.x, by, bw, bh}, lay);
        drawInvitations({left.x + bw + bgap, by, bw, bh}, lay);
        drawHistory({left.x + 2 * (bw + bgap), by, bw, bh}, lay);
    }
    drawNotes(aside);
    titan_.drawTooltip(g_);
    ui.popId();
}

void ClanRaids::drawHero(const Rect& r, const raids::TabLayout& lay) {
    UIContext& ui = g_.ui;
    ui.r().fillRoundedRectGradient(r, ui.s(18), Color{58, 21, 48, 245}, Color{18, 24, 58, 245});
    ui.r().drawRoundedRect(r, ui.s(18), kOrange, ui.s(2));
    const int64_t now = nowUtc(skew_);
    const float pad = ui.s(26);
    const bool titan = r.w > ui.s(760);
    const float titanW = titan ? std::min(r.w * 0.34f, ui.s(400)) : 0.0f;
    // 2.68: « Revoir la cinématique » (Asterion's film) under the titan, else at the card's bottom right.
    const float replayH = ui.s(36), replayW = std::min(asterionfilm::replayButtonWidth(ui), titan ? titanW : r.w * 0.3f);
    asterionfilm::replayButton(g_, "hero/film",
                               titan ? Rect{r.right() - ui.s(16) - (titanW + replayW) * 0.5f, r.bottom() - ui.s(16) - replayH, replayW, replayH}
                                     : Rect{r.right() - ui.s(26) - replayW, r.bottom() - ui.s(46) - replayH * 0.5f, replayW, replayH});
    // 2.73: « Fiches d'Asterion » (the titan and its seven gigaboss) above the titan, else at the top right.
    const float sheetsH = ui.s(36), sheetsW = std::min(sheetsButtonWidth(), titan ? titanW : r.w * 0.3f);
    sheetsButton("hero/sheets", titan ? Rect{r.right() - ui.s(16) - (titanW + sheetsW) * 0.5f, r.y + ui.s(16), sheetsW, sheetsH}
                                      : Rect{r.right() - ui.s(26) - sheetsW, r.y + ui.s(22), sheetsW, sheetsH});
    if (titan) {
        Json heroJson;
        if (lay.hero)
            for (const Json& w : at(data_, "world"))
                if (jstr(w, "id") == lay.heroId) heroJson = w;
        // The world event running: a click on a branch of the titan opens its map (2.68: all seven).
        const bool running = lay.hero && raids::stateAt(raids::parseEvent(heroJson), now) == raids::State::Running;
        const std::vector<std::string> open = running ? kTitanParts : std::vector<std::string>{};
        const std::string part = titan_.draw(g_, "asterion",
                                             {r.right() - titanW - ui.s(16), r.y + ui.s(16) + sheetsH + ui.s(8), titanW, r.h - ui.s(48) - replayH - sheetsH},
                                             asterion::statesOf(heroJson), open);
        if (!part.empty()) g_.app.pushScene(makeRaidBranch(g_, lay.heroId, part));
    }
    const float x = r.x + pad, right = r.right() - (titan ? titanW + ui.s(28) : pad + std::max(replayW, sheetsW) + ui.s(12)), room = right - x;
    const float by = r.bottom() - ui.s(22) - ui.s(48);  // the buttons' top
    float y = r.y + ui.s(22);
    auto fits = [&](float h, float limit) { return y + h <= limit; };
    raids::Event e;
    if (lay.hero)
        for (const Json& w : at(data_, "world"))
            if (jstr(w, "id") == lay.heroId) e = raids::parseEvent(w);
    // 2.69: a catch-up session (« session de rattrapage ») of an official world event, a suspended one.
    const std::string kind = e.valid && e.catchup ? "SESSION DE RATTRAPAGE" : "ÉVÉNEMENT MONDIAL";
    const float kindW = pill(ui, x, y + ui.s(14), kind, kOrange, kInk, right);
    y += ui.s(44);
    if (!e.valid) {
        if (fits(ui.s(44), r.bottom() - ui.s(20))) {
            fit(ui, "ASTERION", {x, y + ui.s(20)}, 40, room, kText, FontFace::Title);
            y += ui.s(46);
        }
        if (fits(ui.s(30), r.bottom() - ui.s(20))) {
            fit(ui, "LE DÉVOREUR DE MONDES", {x, y + ui.s(12)}, 19, room, kPeach, FontFace::Title);
            y += ui.s(40);
        }
        const int lines = static_cast<int>((r.bottom() - ui.s(20) - y) / ui.s(24));
        drawLines(ui,
                  wrap(ui,
                       "Aucun événement mondial programmé pour l'instant. L'équipe du jeu publie les raids : chaque événement est annoncé ici et "
                       "dans vos notifications, puis votre clan s'y inscrit.",
                       FontFace::Body, 16, room, lines),
                  {x, y}, 16, ui.s(24), kDim);
        return;
    }
    const raids::State st = raids::stateAt(e, now);
    {
        float px = x + kindW + ui.s(8);
        const std::string sl = e.suspended && st == raids::State::Running ? "Suspendu" : raids::stateLabel(st);
        const float w = pill(ui, px, r.y + ui.s(36), sl, stateFill(st), stateText(st), right);
        if (w > 0.0f) px += w + ui.s(8);
        pill(ui, px, r.y + ui.s(36), raids::formatLine(e), Color{42, 47, 90, 255}, kText, right);
    }
    if (fits(ui.s(44), by - ui.s(6))) {
        fit(ui, e.displayName, {x, y + ui.s(20)}, 40, room, kText, FontFace::Title);
        y += ui.s(46);
    }
    if (fits(ui.s(28), by - ui.s(6))) {
        fit(ui, e.displayTitle, {x, y + ui.s(12)}, 19, room, kPeach, FontFace::Title);
        y += ui.s(32);
    }
    if (fits(ui.s(26), by - ui.s(6))) {
        fit(ui, whenLine(e.startsAt), {x, y + ui.s(12)}, 16, room, Color{216, 219, 245, 255});
        y += ui.s(30);
    }
    if (fits(ui.s(36), by - ui.s(6))) {
        const raids::Countdown cd = raids::countdownOf(e, now, false);
        const float cw = fit(ui, cd.caption, {x, y + ui.s(18)}, 15, room * 0.45f, kDim).x;
        fit(ui, cd.value, {x + cw + ui.s(12), y + ui.s(18)}, 26, room - cw - ui.s(12), kGold, FontFace::Title);
        y += ui.s(42);
    }
    if (fits(ui.s(24), by - ui.s(6))) {
        std::vector<std::string> parts = {raids::difficultyLabel(e.difficulty), plural(e.clans, "clan", "clans") + " · " + plural(e.players, "joueur inscrit", "joueurs inscrits")};
        if (!clanId().empty())
            parts.push_back(e.clanRegistered ? clans::bracket(clanTag()) + " : " + plural(e.clanPlayers, "inscrit", "inscrits") + ", " +
                                                   plural(e.clanGroups, "groupe", "groupes")
                                             : clans::bracket(clanTag()) + " : clan pas encore inscrit");
        float px = x;
        for (const std::string& p : parts) {
            const float w = textW(ui, p, 15);
            if (px + w > right) break;
            ui.label(p, {px, y + ui.s(12)}, 15, Color{216, 219, 245, 255}, Align::Left, FontFace::Body, VAlign::Middle);
            px += w + ui.s(22);
        }
        // 2.77.4: the players' reviews of the event (★ average · count), once there are some, in the room left.
        Json heroJson;
        for (const Json& w : at(data_, "world"))
            if (jstr(w, "id") == e.id) heroJson = w;
        const raidreviews::Summary rs = raidreviews::parseSummary(at(heroJson, "reviews"));
        if (rs.count > 0) {
            const raidreviewsui::SummaryLayout L = raidreviewsui::layoutSummary(ui, px, y + ui.s(12), rs, 15, right - px, false);
            raidreviewsui::drawSummary(ui, L, kGold, Color{216, 219, 245, 255});
        }
        y += ui.s(28);
    }
    UIContext::ButtonStyle open;
    open.primary = true;
    const float bw1 = std::min(textW(ui, "Voir l'événement", 17, FontFace::Bold) + ui.s(44), room);
    if (ui.button("hero/open", {x, by, bw1, ui.s(48)}, "Voir l'événement", open)) openEvent(e.id);
    const raids::Action a = raids::primaryAction(e, now, g_.online.loggedIn(), clanId(), clanRole());
    const float bx = x + bw1 + ui.s(12);
    const float bw2 = std::min(textW(ui, a.label, 17, FontFace::Bold) + ui.s(44), right - bx);
    if (bw2 > ui.s(80)) {
        UIContext::ButtonStyle st2;
        st2.enabled = a.enabled && !a.id.empty() && !acting_;
        st2.danger = a.id == "unregister";
        if (ui.button("hero/act", {bx, by, bw2, ui.s(48)}, a.label, st2)) act(a, e.id);
        const float nx = bx + bw2 + ui.s(14);
        const int lines = std::max(1, std::min(2, static_cast<int>(ui.s(48) / ui.s(19))));
        const auto note = wrap(ui, a.note, FontFace::Body, 14, right - nx, lines);
        drawLines(ui, note, {nx, by + ui.s(24) - static_cast<float>(note.size()) * ui.s(9.5f)}, 14, ui.s(19), kDim);
    }
}

float ClanRaids::sheetsButtonWidth() {
    return textW(g_.ui, "Fiches d'Asterion", 15, FontFace::Bold) + g_.ui.s(36);
}

void ClanRaids::sheetsButton(const std::string& id, const Rect& r) {
    UIContext& ui = g_.ui;
    UIContext::ButtonStyle st;
    st.small = true;
    const std::string label = "Fiches d'Asterion";
    st.fontSize = std::max(ui.fitFont(label, FontFace::Bold, 15, r.w - ui.s(16), 0.7f), 11.5f / ui.s(1.0f));
    if (ui.button(id, r, label, st)) g_.app.pushScene(makeRaidSheets(g_));
    ui.newBadge(id, r, "raids.sheets", true, true);  // « NOUVEAU » (2.73)
}

void ClanRaids::drawClanRaids(const Rect& r, const raids::TabLayout& lay) {
    UIContext& ui = g_.ui;
    // 2.77.4: the events my clan takes part in (the raids are the team's events; the clan raids
    // scheduled before stay here until their end). No « Organiser un raid du clan… » any more.
    float y = section(ui, r, "ÉVÉNEMENTS DU CLAN", kSky);
    const float x = r.x + ui.s(14), w = r.w - ui.s(28);
    const Rect list{x, y, w, r.bottom() - ui.s(14) - y};
    const int64_t now = nowUtc(skew_);
    const auto evs = raids::clanRaidList(data_, lay.heroId);
    if (!lay.clanBoxes || evs.empty()) {
        std::string t = !lay.clanBoxes ? lay.clanNote
                                       : "Aucun autre événement pour votre clan. Les raids sont des événements publiés par l'équipe du jeu : le chef ou un "
                                         "officier y inscrit le clan, puis chaque membre s'inscrit.";
        drawLines(ui, wrap(ui, t, FontFace::Body, 15, w, static_cast<int>(list.h / ui.s(22))), {x, y}, 15, ui.s(22), kDim);
    } else {
        const float rowH = ui.s(70), gap = ui.s(8);
        ui.beginScroll("clanlist", list, static_cast<float>(evs.size()) * (rowH + gap));
        float ry = list.y - ui.scrollOffset();
        for (const raids::Event& e : evs) {
            const Rect row{list.x, ry, list.w - ui.s(12), rowH};
            ry += rowH + gap;
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            bool hover = false;
            if (ui.hotspot("raid/" + e.id, row, &hover)) openEvent(e.id);
            ui.r().fillRoundedRect(row, ui.s(10), hover ? Color{36, 45, 96, 250} : kCard);
            const raids::State st = raids::stateAt(e, now);
            const std::string sl = raids::stateLabel(st);
            const float pw = textW(ui, sl, 13, FontFace::Bold) + ui.s(22);
            pill(ui, row.right() - ui.s(10) - pw, row.y + ui.s(22), sl, stateFill(st), stateText(st), row.right(), 13);
            std::string name = raids::eventName(e);
            if (e.kind == "world") name += " · mondial";
            else if (!e.organizerTag.empty() && e.organizerId != clanId()) name += " · " + clans::bracket(e.organizerTag);
            fit(ui, name, {row.x + ui.s(12), row.y + ui.s(22)}, 16, row.w - ui.s(34) - pw, kText, FontFace::Bold);
            std::string info = whenShort(e.startsAt) + " · " +
                               (e.capacity > 0 ? std::to_string(e.players) + " / " + std::to_string(e.capacity) + " inscrits" : plural(e.clanPlayers, "inscrit", "inscrits"));
            if (!e.clanLeader.empty()) info += " · chef : " + e.clanLeader;
            // 2.77.4: its reviews (★ average · count) at the right of the line, measured first.
            const raidreviews::Summary rs = reviewsOf(e.id);
            raidreviewsui::SummaryLayout L;
            if (rs.count > 0) L = raidreviewsui::layoutSummary(ui, row.right() - ui.s(12), row.y + ui.s(50), rs, 13, (row.w - ui.s(24)) * 0.45f, true);
            const float sumW = L.shown.empty() ? 0.0f : L.width() + ui.s(12);
            fit(ui, info, {row.x + ui.s(12), row.y + ui.s(50)}, 13, row.w - ui.s(24) - sumW, kDim);
            raidreviewsui::drawSummary(ui, L, kGold, kDim);
        }
        ui.endScroll();
    }
}

void ClanRaids::drawInvitations(const Rect& r, const raids::TabLayout& lay) {
    UIContext& ui = g_.ui;
    float y = section(ui, r, "INVITATIONS ET RÉCOMPENSES", kSky);
    const float x = r.x + ui.s(14), w = r.w - ui.s(28);
    // 2.68: the rewards to claim, granted by the server from validated results (« Récupérer »).
    const auto rewards = raids::parseRewards(at(data_, "rewards"));
    const Rect rc{x, r.bottom() - ui.s(14) - ui.s(84), w, ui.s(84)};
    ui.r().fillRoundedRect(rc, ui.s(10), Color{42, 36, 20, 245});
    ui.r().drawRoundedRect(rc, ui.s(10), kGold.withAlphaF(0.6f), ui.s(1));
    ui.r().fillCircle({rc.x + ui.s(30), rc.center().y}, ui.s(18), rewards.empty() ? Color{120, 96, 44, 255} : kGold);
    const std::string claimLabel = "Récupérer";
    const float cbw = rewards.empty() ? 0.0f : std::min(textW(ui, claimLabel, 15, FontFace::Bold) + ui.s(32), rc.w * 0.4f);
    const float tx = rc.x + ui.s(60), tw = rc.right() - ui.s(12) - (cbw > 0.0f ? cbw + ui.s(10) : 0.0f) - tx;
    if (rewards.empty()) {
        fit(ui, "Aucune récompense à récupérer", {tx, rc.y + ui.s(22)}, 15, tw, kText, FontFace::Bold);
        drawLines(ui, wrap(ui, "Elles viennent des résultats validés des raids : la chute de ses gigaboss, votre contribution.", FontFace::Body, 13, tw, 2),
                  {tx, rc.y + ui.s(36)}, 13, ui.s(18), Color{232, 220, 184, 255});
    } else {
        const std::string title = rewards.size() == 1 ? rewards[0].name : std::to_string(rewards.size()) + " récompenses à récupérer";
        fit(ui, title, {tx, rc.y + ui.s(22)}, 15, tw, kText, FontFace::Bold);
        std::string sub;
        if (rewards.size() == 1) sub = rewards[0].why;
        else
            for (const raids::Reward& rw : rewards) sub += (sub.empty() ? "" : " · ") + rw.name;
        drawLines(ui, wrap(ui, sub, FontFace::Body, 13, tw, 2), {tx, rc.y + ui.s(36)}, 13, ui.s(18), Color{232, 220, 184, 255});
        UIContext::ButtonStyle bs;
        bs.primary = true;
        bs.small = true;
        bs.enabled = !claiming_;
        if (ui.button("claim", {rc.right() - ui.s(10) - cbw, rc.center().y - ui.s(22), cbw, ui.s(44)}, claimLabel, bs)) claimRewards();
    }
    const Rect list{x, y, w, rc.y - ui.s(10) - y};
    const Json& inv = at(data_, "invitations");
    if (!lay.clanBoxes) {
        drawLines(ui, wrap(ui, lay.clanNote, FontFace::Body, 15, w, static_cast<int>(list.h / ui.s(22))), {x, y}, 15, ui.s(22), kDim);
        return;
    }
    if (!inv.is_array() || inv.empty()) {
        drawLines(ui, wrap(ui, "Aucune invitation à un raid commun.", FontFace::Body, 15, w, 2), {x, y}, 15, ui.s(22), kDim);
        return;
    }
    const float rowH = lay.canAnswer ? ui.s(118) : ui.s(84), gap = ui.s(8);
    ui.beginScroll("invites", list, static_cast<float>(inv.size()) * (rowH + gap));
    float ry = list.y - ui.scrollOffset();
    for (const Json& i : inv) {
        const raids::Event e = raids::parseEvent(at(i, "event"));
        const Rect row{list.x, ry, list.w - ui.s(12), rowH};
        ry += rowH + gap;
        if (!e.valid || row.bottom() < list.y || row.y > list.bottom()) continue;
        ui.r().fillRoundedRect(row, ui.s(10), kCard);
        const float rx = row.x + ui.s(12), rw = row.w - ui.s(24);
        fit(ui, clans::bracket(e.organizerTag) + " invite " + clans::bracket(clanTag()) + " à un raid commun", {rx, row.y + ui.s(20)}, 15, rw, kText,
            FontFace::Bold);
        fit(ui, raids::eventName(e) + " · " + whenShort(e.startsAt) + " (votre heure)", {rx, row.y + ui.s(46)}, 13, rw, kDim);
        if (lay.canAnswer) {
            const float bw = (rw - ui.s(8)) * 0.5f;
            UIContext::ButtonStyle ok;
            ok.primary = true;
            ok.small = true;
            ok.enabled = !acting_;
            UIContext::ButtonStyle no;
            no.small = true;
            no.enabled = !acting_;
            if (ui.button("accept/" + e.id, {rx, row.y + ui.s(66), bw, ui.s(42)}, "Accepter", ok)) answer(e.id, true);
            if (ui.button("decline/" + e.id, {rx + bw + ui.s(8), row.y + ui.s(66), bw, ui.s(42)}, "Refuser", no)) answer(e.id, false);
        } else {
            fit(ui, "Le chef ou un officier répond.", {rx, row.y + ui.s(68)}, 13, rw, kDim);
        }
    }
    ui.endScroll();
}

void ClanRaids::drawHistory(const Rect& r, const raids::TabLayout& lay) {
    UIContext& ui = g_.ui;
    float y = section(ui, r, "HISTORIQUE DES RAIDS", kSky);
    const float x = r.x + ui.s(14), w = r.w - ui.s(28);
    const float footH = ui.s(44);
    const Rect list{x, y, w, r.bottom() - ui.s(10) - footH - y};
    const Json& hist = at(data_, "history");
    if (!lay.clanBoxes) {
        drawLines(ui, wrap(ui, lay.clanNote, FontFace::Body, 15, w, static_cast<int>(list.h / ui.s(22))), {x, y}, 15, ui.s(22), kDim);
        return;
    }
    if (!hist.is_array() || hist.empty()) {
        drawLines(ui, wrap(ui, "Aucun raid terminé pour l'instant.", FontFace::Body, 15, w, 2), {x, y}, 15, ui.s(22), kDim);
    } else {
        // 2.69: each event opens its sheet (its results, its world ranking); a world event closed shows
        // its official result (frozen by the server) and my clan's rank in it.
        const float rowH = ui.s(34);
        ui.beginScroll("history", list, static_cast<float>(hist.size()) * rowH);
        float ry = list.y - ui.scrollOffset();
        for (const Json& j : hist) {
            const raids::Event e = raids::parseEvent(j);
            const Rect row{x, ry + ui.s(2), w - ui.s(6), rowH - ui.s(4)};
            const float cy = ry + rowH * 0.5f;
            ry += rowH;
            if (!e.valid || cy < list.y || cy > list.bottom()) continue;
            bool hover = false;
            if (ui.hotspot("hist/" + e.id, row, &hover)) openEvent(e.id);
            if (hover) ui.r().fillRoundedRect(row, ui.s(7), Color{34, 44, 92, 200});
            const std::string result = e.cancelled ? "Annulé"
                                       : e.result.valid ? raids::resultLine(e.result, e.clanRegistered ? clanTag() : std::string())
                                                        : plural(e.clanPlayers, "inscrit", "inscrits");
            const float resW = std::min(textW(ui, result, 14, FontFace::Bold), w * 0.55f);
            fit(ui, result, {x + w - ui.s(12), cy}, 14, resW, e.cancelled ? Color{255, 138, 138, 255} : kGold, FontFace::Bold, Align::Right);
            const float dw = ui.s(56);
            fit(ui, raids::dayMonth(raids::toSystemLocal(e.startsAt)), {x + ui.s(4), cy}, 14, dw, kDim);
            // 2.77.4: its reviews (★ average · count) before the result, when the row has the room.
            const float nameRoom = w - ui.s(12) - resW - dw - ui.s(22);
            const raidreviews::Summary rs = raidreviews::parseSummary(at(j, "reviews"));
            raidreviewsui::SummaryLayout L;
            if (rs.count > 0 && nameRoom > ui.s(200))
                L = raidreviewsui::layoutSummary(ui, x + w - ui.s(12) - resW - ui.s(12), cy, rs, 13, nameRoom * 0.45f, true);
            const float sumW = L.shown.empty() ? 0.0f : L.width() + ui.s(10);
            raidreviewsui::drawSummary(ui, L, kGold, kDim);
            fit(ui, raids::eventName(e), {x + dw + ui.s(10), cy}, 14, nameRoom - sumW, kText);
        }
        ui.endScroll();
    }
    drawLines(ui,
              wrap(ui, "Chaque événement mondial terminé garde son résultat officiel, figé par le serveur : ouvrez-le pour son classement.", FontFace::Body, 13, w, 2),
              {x, r.bottom() - ui.s(10) - footH + ui.s(4)}, 13, ui.s(18), kDim);
}

void ClanRaids::drawNotes(const Rect& r) {
    UIContext& ui = g_.ui;
    ui.r().fillRoundedRect(r, ui.s(16), Color{16, 21, 47, 240});
    ui.r().drawRoundedRect(r, ui.s(16), kBorder, ui.s(1.2f));
    const float x = r.x + ui.s(16), w = r.w - ui.s(32), right = r.right() - ui.s(16);
    const bool logged = g_.online.loggedIn();
    // « Tout marquer lu » first (measured), the title in the room left.
    const std::string allRead = "Tout marquer lu";
    const float bw = std::min(textW(ui, allRead, 13, FontFace::Bold) + ui.s(24), w * 0.45f);
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = 13;
    st.enabled = logged && unread_ > 0;
    if (ui.button("readall", {right - bw, r.y + ui.s(14), bw, ui.s(36)}, allRead, st)) markAllRead();
    fit(ui, "NOTIFICATIONS DE RAID", {x, r.y + ui.s(32)}, 16, w - bw - ui.s(10), kText, FontFace::Title);

    // The reminders at the bottom: their height first (the chips wrap).
    const Json& prefs = at(data_, "prefs");
    const std::vector<int> chosen = raids::reminderPrefs(prefs);
    const bool members = jget<bool>(prefs, "member_updates", true);
    std::vector<std::string> labels;
    std::vector<bool> on;
    for (int m : raids::reminderChoices()) {
        labels.push_back(raids::reminderLabel(m));
        on.push_back(std::find(chosen.begin(), chosen.end(), m) != chosen.end());
    }
    labels.push_back(members ? "Inscriptions des membres : regroupées" : "Inscriptions des membres : désactivées");
    on.push_back(members);
    int rows = 1;
    {
        float cx = x;
        for (const std::string& l : labels) {
            const float cw = std::min(textW(ui, l, 14, FontFace::Bold) + ui.s(26), w);
            if (cx > x && cx + cw > right) {
                cx = x;
                ++rows;
            }
            cx += cw + ui.s(8);
        }
    }
    const auto note = wrap(ui, "Rappels dans le jeu : centre de notifications et bandeau. Pas de notification Windows pour l'instant.", FontFace::Body, 12, w, 3);
    const float blockH = ui.s(34) + static_cast<float>(rows) * ui.s(44) + static_cast<float>(note.size()) * ui.s(17) + ui.s(14);
    const float blockY = r.bottom() - blockH;
    ui.r().drawLine({x, blockY}, {right, blockY}, kBorder, ui.s(1));
    fit(ui, "Mes rappels", {x, blockY + ui.s(20)}, 14, w, kText, FontFace::Bold);
    float bottom = blockY;
    const int clicked = chips(ui, "prefs", x, blockY + ui.s(36), right, labels, on, bottom, logged);
    drawLines(ui, note, {x, bottom + ui.s(8)}, 12, ui.s(17), Color{154, 160, 200, 255});
    if (clicked >= 0 && clicked < static_cast<int>(raids::reminderChoices().size())) {
        const int m = raids::reminderChoices()[static_cast<size_t>(clicked)];
        std::vector<int> next;
        for (int c : chosen)
            if (c != m) next.push_back(c);
        if (!on[static_cast<size_t>(clicked)]) next.push_back(m);
        Json list = Json::array();
        for (int c : next) list.push_back(c);
        data_["prefs"]["reminders"] = list;
        putPrefs({{"reminders", list}});
    } else if (clicked >= 0) {
        data_["prefs"]["member_updates"] = !members;
        putPrefs({{"member_updates", !members}});
    }

    // The notifications between the title and the reminders.
    const Rect list{x, r.y + ui.s(62), w, blockY - ui.s(10) - (r.y + ui.s(62))};
    if (!logged || !notesLoaded_ || notes_.empty()) {
        const std::string t = !logged ? "Connectez-vous pour recevoir les notifications de raid." : !notesLoaded_ ? "Chargement…" : "Aucune notification de raid.";
        drawLines(ui, wrap(ui, t, FontFace::Body, 15, w, 3), {x, list.y}, 15, ui.s(22), kDim);
        return;
    }
    const int64_t now = nowUtc(skew_);
    struct Row {
        std::vector<std::string> lines;
        float h;
    };
    std::vector<Row> rows2;
    float total = 0.0f;
    const float textX = ui.s(30);
    for (const Json& n : notes_) {
        Row row{wrap(ui, jstr(n, "text"), FontFace::Bold, 14, w - textX - ui.s(22), 2), 0.0f};
        row.h = ui.s(12) + static_cast<float>(row.lines.size()) * ui.s(19) + ui.s(22);
        total += row.h + ui.s(8);
        rows2.push_back(std::move(row));
    }
    ui.beginScroll("notes", list, total);
    float ry = list.y - ui.scrollOffset();
    for (size_t i = 0; i < rows2.size() && i < notes_.size(); ++i) {
        const Json& n = notes_[i];
        const Rect row{list.x, ry, list.w - ui.s(12), rows2[i].h};
        ry += rows2[i].h + ui.s(8);
        if (row.bottom() < list.y || row.y > list.bottom()) continue;
        const bool unread = !jget<bool>(n, "read", false);
        bool hover = false;
        if (ui.hotspot("note/" + jstr(n, "id"), row, &hover)) openNote(n);
        ui.r().fillRoundedRect(row, ui.s(10), hover ? Color{40, 50, 100, 250} : unread ? Color{29, 37, 80, 245} : Color{21, 27, 58, 245});
        ui.r().fillCircle({row.x + ui.s(15), row.y + ui.s(20)}, ui.s(4), unread ? kOrange : Color{74, 84, 136, 255});
        drawLines(ui, rows2[i].lines, {row.x + textX, row.y + ui.s(8)}, 14, ui.s(19), kText, FontFace::Bold);
        std::string when;
        if (auto t = raids::parseUtc(jstr(n, "created_at"))) when = raids::agoText(std::max<int64_t>(0, now - *t));
        if (jget<int>(n, "count", 1) > 1) when += " · regroupées";
        fit(ui, when, {row.x + textX, row.bottom() - ui.s(14)}, 12, row.w - textX - ui.s(10), kDim);
    }
    ui.endScroll();
}

}  // namespace cosmic
