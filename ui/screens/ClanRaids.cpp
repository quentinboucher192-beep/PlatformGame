#include "ui/screens/ClanRaids.h"

#include <algorithm>
#include <cmath>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/Clans.h"
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
const char* const kFormats[] = {"court", "standard", "legendaire"};
const int kLocks[] = {10, 15, 30, 60};

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
    if (titan) {
        Json heroJson;
        if (lay.hero)
            for (const Json& w : at(data_, "world"))
                if (jstr(w, "id") == lay.heroId) heroJson = w;
        // The world event running: a click on a branch of the titan opens its map (2.68: all seven).
        const bool running = lay.hero && raids::stateAt(raids::parseEvent(heroJson), now) == raids::State::Running;
        const std::vector<std::string> open = running ? kTitanParts : std::vector<std::string>{};
        const std::string part = titan_.draw(g_, "asterion", {r.right() - titanW - ui.s(16), r.y + ui.s(16), titanW, r.h - ui.s(40) - replayH},
                                             asterion::statesOf(heroJson), open);
        if (!part.empty()) g_.app.pushScene(makeRaidBranch(g_, lay.heroId, part));
    }
    const float x = r.x + pad, right = r.right() - (titan ? titanW + ui.s(28) : pad + replayW + ui.s(12)), room = right - x;
    const float by = r.bottom() - ui.s(22) - ui.s(48);  // the buttons' top
    float y = r.y + ui.s(22);
    auto fits = [&](float h, float limit) { return y + h <= limit; };
    pill(ui, x, y + ui.s(14), "ÉVÉNEMENT MONDIAL", kOrange, kInk, right);
    y += ui.s(44);
    raids::Event e;
    if (lay.hero)
        for (const Json& w : at(data_, "world"))
            if (jstr(w, "id") == lay.heroId) e = raids::parseEvent(w);
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
                       "Aucun événement mondial programmé pour l'instant. Les administrateurs du jeu l'annoncent ici et dans vos notifications ; "
                       "les raids de votre clan se programment juste en dessous.",
                       FontFace::Body, 16, room, lines),
                  {x, y}, 16, ui.s(24), kDim);
        return;
    }
    const raids::State st = raids::stateAt(e, now);
    {
        float px = x + textW(ui, "ÉVÉNEMENT MONDIAL", 14, FontFace::Bold) + ui.s(30);
        const float w = pill(ui, px, r.y + ui.s(36), raids::stateLabel(st), stateFill(st), stateText(st), right);
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

void ClanRaids::drawClanRaids(const Rect& r, const raids::TabLayout& lay) {
    UIContext& ui = g_.ui;
    float y = section(ui, r, "RAIDS DU CLAN", kSky);
    const float x = r.x + ui.s(14), w = r.w - ui.s(28);
    const float footH = lay.canOrganize ? ui.s(58) : 0.0f;
    const Rect list{x, y, w, r.bottom() - ui.s(14) - footH - y};
    const int64_t now = nowUtc(skew_);
    const auto evs = raids::clanRaidList(data_, lay.heroId);
    if (!lay.clanBoxes || evs.empty()) {
        std::string t = !lay.clanBoxes ? lay.clanNote
                                       : (lay.canOrganize ? "Aucun raid du clan à venir. Programmez-en un : les membres sont prévenus et s'inscrivent eux-mêmes."
                                                          : "Aucun raid du clan à venir. " + lay.clanNote);
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
            fit(ui, info, {row.x + ui.s(12), row.y + ui.s(50)}, 13, row.w - ui.s(24), kDim);
        }
        ui.endScroll();
    }
    if (lay.canOrganize) {
        UIContext::ButtonStyle st;
        st.small = true;
        if (ui.button("organize", {x, r.bottom() - ui.s(14) - ui.s(44), w, ui.s(44)}, "Organiser un raid du clan…", st)) openOrganize();
    }
}

void ClanRaids::drawInvitations(const Rect& r, const raids::TabLayout& lay) {
    UIContext& ui = g_.ui;
    float y = section(ui, r, "INVITATIONS ET RÉCOMPENSES", kSky);
    const float x = r.x + ui.s(14), w = r.w - ui.s(28);
    // The rewards to claim: none before the combats, said as it is.
    const Rect rc{x, r.bottom() - ui.s(14) - ui.s(84), w, ui.s(84)};
    ui.r().fillRoundedRect(rc, ui.s(10), Color{42, 36, 20, 245});
    ui.r().drawRoundedRect(rc, ui.s(10), kGold.withAlphaF(0.6f), ui.s(1));
    ui.r().fillCircle({rc.x + ui.s(30), rc.center().y}, ui.s(18), Color{120, 96, 44, 255});
    const float tx = rc.x + ui.s(60), tw = rc.right() - ui.s(12) - tx;
    fit(ui, "Aucune récompense à récupérer", {tx, rc.y + ui.s(22)}, 15, tw, kText, FontFace::Bold);
    drawLines(ui, wrap(ui, "Les récompenses arriveront avec les combats d'Asterion.", FontFace::Body, 13, tw, 2), {tx, rc.y + ui.s(36)}, 13, ui.s(18),
              Color{232, 220, 184, 255});
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
    float y = section(ui, r, "HISTORIQUE DU CLAN", kSky);
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
        const float rowH = ui.s(34);
        ui.beginScroll("history", list, static_cast<float>(hist.size()) * rowH);
        float ry = list.y - ui.scrollOffset();
        for (const Json& j : hist) {
            const raids::Event e = raids::parseEvent(j);
            const float cy = ry + rowH * 0.5f;
            ry += rowH;
            if (!e.valid || cy < list.y || cy > list.bottom()) continue;
            const std::string result = e.cancelled ? "Annulé" : plural(e.clanPlayers, "inscrit", "inscrits");
            const float resW = std::min(textW(ui, result, 14, FontFace::Bold), w * 0.4f);
            fit(ui, result, {x + w - ui.s(12), cy}, 14, resW, e.cancelled ? Color{255, 138, 138, 255} : kGold, FontFace::Bold, Align::Right);
            const float dw = ui.s(56);
            fit(ui, raids::dayMonth(raids::toSystemLocal(e.startsAt)), {x, cy}, 14, dw, kDim);
            fit(ui, raids::eventName(e), {x + dw + ui.s(6), cy}, 14, w - ui.s(12) - resW - dw - ui.s(18), kText);
        }
        ui.endScroll();
    }
    drawLines(ui, wrap(ui, "Les résultats des combats arriveront avec eux.", FontFace::Body, 13, w, 2), {x, r.bottom() - ui.s(10) - footH + ui.s(4)}, 13,
              ui.s(18), kDim);
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

// ------------------------------------------------------------------ « Organiser un raid du clan… »

void ClanRaids::openOrganize() {
    organizing_ = true;
    formatIdx_ = 1;
    lockIdx_ = 2;
    places_ = 8.0f;
    formError_.clear();
    const raids::Civil tomorrow = raids::toSystemLocal(nowUtc(skew_) + 86400);
    date_ = raids::dateField(tomorrow);
    clock_ = "21:00";
}

void ClanRaids::drawModal() {
    UIContext& ui = g_.ui;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 175});
    const int64_t now = nowUtc(skew_);
    const raids::Civil here = raids::toSystemLocal(now);
    const float w = std::min(ui.screenW() - ui.s(40), ui.s(720)), tw = w - ui.s(60);
    const auto intro = wrap(ui,
                            "Les membres du clan sont prévenus ; chacun s'inscrit lui-même jusqu'au verrouillage. L'heure est celle de cet ordinateur (" +
                                raids::offsetLabel(here.offsetMinutes) + ") : le jeu l'envoie en UTC.",
                            FontFace::Body, 16, tw, 4);
    const float introH = static_cast<float>(intro.size()) * ui.s(22);
    const float h = ui.s(70) + introH + ui.s(14) + ui.s(104) + 3 * ui.s(78) + ui.s(70) + ui.s(80);
    const Rect box{(ui.screenW() - w) * 0.5f, std::max(ui.s(10), (ui.screenH() - h) * 0.5f), w, h};
    ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(box, ui.s(18), kOrange.withAlphaF(0.7f), ui.s(1.5f));
    ui.pushId("raids/organize");
    const float x = box.x + ui.s(30);
    fit(ui, "Organiser un raid du clan", {x, box.y + ui.s(38)}, 26, tw, kText, FontFace::Title);
    drawLines(ui, intro, {x, box.y + ui.s(64)}, 16, ui.s(22), ui.theme().textDim);
    float y = box.y + ui.s(70) + introH + ui.s(14);
    auto caption = [&](const std::string& t) { fit(ui, t, {x, y + ui.s(10)}, 15, tw, ui.theme().textDim); };
    // The format: three choices, then its duration.
    caption("Format");
    static const char* kNames[] = {"Court", "Standard", "Légendaire"};
    const float fw = (tw - 2 * ui.s(10)) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.selected = formatIdx_ == i;
        if (ui.button(std::string("format/") + kFormats[i], {x + static_cast<float>(i) * (fw + ui.s(10)), y + ui.s(24), fw, ui.s(44)}, kNames[i], st))
            formatIdx_ = i;
    }
    raids::Event shape;
    shape.format = kFormats[formatIdx_];
    shape.formatMin = formatIdx_ == 0 ? 25 : formatIdx_ == 1 ? 45 : 75;
    shape.formatMax = formatIdx_ == 0 ? 30 : formatIdx_ == 1 ? 60 : 90;
    fit(ui, raids::formatLine(shape), {x, y + ui.s(86)}, 14, tw, kDim);
    y += ui.s(104);
    // The date and the time, in this computer's zone.
    const float dw = std::min(ui.s(240), tw * 0.5f);
    fit(ui, "Date (JJ/MM/AAAA)", {x, y + ui.s(10)}, 15, dw - ui.s(10), ui.theme().textDim);
    fit(ui, "Heure (votre heure)", {x + dw + ui.s(16), y + ui.s(10)}, 15, tw - dw - ui.s(16), ui.theme().textDim);
    ui.textField("date", {x, y + ui.s(24), dw, ui.s(44)}, date_, "21/11/2026", 10);
    ui.textField("clock", {x + dw + ui.s(16), y + ui.s(24), std::min(ui.s(140), tw - dw - ui.s(16)), ui.s(44)}, clock_, "21:00", 5);
    y += ui.s(78);
    caption("Places (2 à 50 joueurs)");
    ui.numberField("places", {x, y + ui.s(24), std::min(tw, ui.s(260)), ui.s(44)}, places_, 1.0f, static_cast<float>(raids::kMinPlaces),
                   static_cast<float>(raids::kMaxPlaces), "%.0f");
    y += ui.s(78);
    std::vector<std::string> locks;
    for (int m : kLocks) locks.push_back(std::to_string(m) + " min avant le début");
    ui.cycle("lock", {x, y + ui.s(18), tw, ui.s(48)}, "Verrouillage des inscriptions", lockIdx_, locks);
    lockIdx_ = std::clamp(lockIdx_, 0, 3);
    y += ui.s(78);
    // The start, read in this computer's zone (daylight saving time included), then in UTC.
    std::string problem;
    int64_t utc = 0;
    int yy = 0, mo = 0, dd = 0, hh = 0, mi = 0;
    if (!raids::parseDate(date_, yy, mo, dd)) problem = "Date attendue : JJ/MM/AAAA.";
    else if (!raids::parseClock(clock_, hh, mi)) problem = "Heure attendue : HH:MM.";
    else {
        raids::Civil wall;
        wall.year = yy;
        wall.month = mo;
        wall.day = dd;
        wall.hour = hh;
        wall.minute = mi;
        utc = raids::fromSystemLocal(wall);
        const int lock = kLocks[lockIdx_];
        if (utc < now + 60LL * std::max(raids::kMinAheadMinutes, lock + 5))
            problem = "Au moins " + std::to_string(std::max(raids::kMinAheadMinutes, lock + 5)) + " minutes à l'avance.";
        else if (utc > now + 90LL * 86400) problem = "Au plus 90 jours à l'avance.";
    }
    if (problem.empty()) {
        drawLines(ui, wrap(ui, "Début : " + whenLine(utc), FontFace::Body, 15, tw, 2), {x, y}, 15, ui.s(22), kGold);
    }
    const std::string shown = !formError_.empty() ? formError_ : problem;
    fit(ui, shown, {x, y + ui.s(56)}, 15, tw, ui.theme().danger);
    const float by = box.bottom() - ui.s(66);
    UIContext::ButtonStyle ok;
    ok.primary = true;
    ok.enabled = !sending_ && problem.empty();
    const float okW = textW(ui, "Programmer", 17, FontFace::Bold) + ui.s(44);
    const float cW = textW(ui, "Annuler", 17, FontFace::Bold) + ui.s(44);
    const bool go = ui.button("ok", {box.right() - ui.s(30) - okW, by, okW, ui.s(48)}, "Programmer", ok);
    const bool cancel = ui.button("cancel", {box.right() - ui.s(30) - okW - ui.s(12) - cW, by, cW, ui.s(48)}, "Annuler") ||
                        (ui.backPressed() && !ui.editingText());
    ui.popId();
    if (cancel) {
        organizing_ = false;
        return;
    }
    if (!go || !ok.enabled) return;
    sending_ = true;
    formError_.clear();
    const Json body = {{"format", kFormats[formatIdx_]},
                       {"starts_at", raids::isoUtc(utc)},
                       {"capacity", static_cast<int>(std::lround(places_))},
                       {"lock_minutes", kLocks[lockIdx_]}};
    g_.online.send("POST", "/api/v1/raids/events", body, OnlineService::guard(life_, [this](const ApiResult& r) {
                       sending_ = false;
                       if (!r.ok) {
                           formError_ = r.message;
                           return;
                       }
                       organizing_ = false;
                       say_("Raid programmé : les membres du clan sont prévenus.", false);
                       load();
                       openEvent(jstr(at(r.data, "event"), "id"));
                   }));
}

}  // namespace cosmic
