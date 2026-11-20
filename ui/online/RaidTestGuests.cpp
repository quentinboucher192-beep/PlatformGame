#include "ui/online/RaidTestGuests.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/Raids.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

using namespace raidui;

namespace {

constexpr int kMaxGuests = 7;          // the relay's seats, the administrator's aside (raidTests.ts MAX_GUESTS)
constexpr float kPollSeconds = 3.0f;   // the states read again
constexpr float kSearchDelay = 0.35f;  // after the last key typed

const Color kWarn{255, 176, 74, 255};
const Color kRed{255, 106, 106, 255};

/// The actions a guest's row may show (the widest one sets the column's width).
const char* const kActions[] = {"Retirer", "Annuler", "Retirer du test", "Réinviter"};

bool searchable(const std::string& q) {
    static const std::regex re("^[A-Za-z0-9_.-]{1,20}$");
    return std::regex_match(q, re);
}

std::string pct(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') out += static_cast<char>(c);
        else {
            char b[4];
            std::snprintf(b, sizeof b, "%%%02X", c);
            out += b;
        }
    }
    return out;
}

/// A pill of `t` filling `r` (its text fitted, never past it).
void pillIn(UIContext& ui, const Rect& r, const std::string& t, Color fill, Color text) {
    if (r.w <= 0.0f || t.empty()) return;
    ui.r().fillRoundedRect(r, ui.s(8), fill);
    fit(ui, t, r.center(), 13, r.w - ui.s(14), text, FontFace::Bold, Align::Center);
}

}  // namespace

RaidTestGuests::RaidTestGuests(GameServices& g) : g_(g) {}
RaidTestGuests::~RaidTestGuests() = default;

void RaidTestGuests::setShape(std::vector<std::string> groups, int seatsPerGroup) {
    if (groups.empty()) groups.push_back("Groupe 1");
    groups_ = std::move(groups);
    seats_ = std::max(1, seatsPerGroup);
    for (Guest& x : guests_) x.group = std::clamp(x.group, 0, static_cast<int>(groups_.size()) - 1);
}

void RaidTestGuests::setChosen(const std::vector<RaidTestGuest>& list) {
    guests_.clear();
    for (const RaidTestGuest& r : list) {
        Guest x;
        x.id = r.id;
        x.name = r.name;
        x.admin = r.admin;
        x.group = r.group;
        x.invincible = r.invincible;
        guests_.push_back(x);
    }
}

std::vector<RaidTestGuest> RaidTestGuests::chosen() const {
    std::vector<RaidTestGuest> out;
    for (const Guest& x : guests_)
        if (x.status.empty()) out.push_back({x.id, x.name, x.admin, x.group, x.invincible});
    return out;
}

void RaidTestGuests::attach(const std::string& testId) {
    testId_ = testId;
    for (Guest& x : guests_)
        if (x.status.empty()) send(x);
    pollIn_ = kPollSeconds;
    loaded_ = false;  // the picker leaves out those invited to this test (the server's list)
}

int RaidTestGuests::active() const {
    const int64_t now = nowUtc(skew_);
    int n = 0;
    for (const Guest& x : guests_)
        if (x.status.empty() || x.status == "accepted" || (x.status == "pending" && x.expires > now)) ++n;
    return n;
}

std::string RaidTestGuests::summary() const {
    const int64_t now = nowUtc(skew_);
    int chosen = 0, waiting = 0, playing = 0, accepted = 0;
    for (const Guest& x : guests_) {
        if (x.status.empty()) ++chosen;
        else if (x.status == "pending" && x.expires > now) ++waiting;
        else if (x.status == "accepted") {
            ++accepted;
            if (x.playing && x.connected) ++playing;
        }
    }
    const int n = chosen + waiting + accepted;
    if (n == 0) return {};
    std::string s = std::to_string(n) + (n > 1 ? " joueurs invités" : " joueur invité");
    std::vector<std::string> parts;
    if (chosen) parts.push_back(std::to_string(chosen) + " invité(s) au lancement");
    if (playing) parts.push_back(std::to_string(playing) + " en jeu");
    if (accepted > playing) parts.push_back(std::to_string(accepted - playing) + " absent(s)");
    if (waiting) parts.push_back(std::to_string(waiting) + " en attente");
    for (size_t i = 0; i < parts.size(); ++i) s += (i == 0 ? " : " : ", ") + parts[i];
    return s;
}

void RaidTestGuests::open() {
    open_ = true;
    status_.clear();
    if (!loaded_ || searched_ != query_) search();
    focusSearch_ = true;
}

void RaidTestGuests::say(const std::string& text, bool error) {
    status_ = text;
    statusError_ = error;
}

int RaidTestGuests::inGroup(int group, const std::string& except) const {
    const int64_t now = nowUtc(skew_);
    int n = 0;
    for (const Guest& x : guests_)
        if (x.id != except && x.group == group && (x.status.empty() || x.status == "accepted" || (x.status == "pending" && x.expires > now))) ++n;
    return n;
}

bool RaidTestGuests::roomFor(int group, const std::string& except, std::string* why) const {
    int others = 0;
    const int64_t now = nowUtc(skew_);
    for (const Guest& x : guests_)
        if (x.id != except && (x.status.empty() || x.status == "accepted" || (x.status == "pending" && x.expires > now))) ++others;
    if (others >= kMaxGuests) {
        if (why) *why = "Un test reçoit 7 joueurs invités au plus.";
        return false;
    }
    if (inGroup(group, except) >= seats_ - 1) {
        if (why)
            *why = seats_ <= 1 ? "Aucun joueur simulé à remplacer : ajoutez-en au moins un avant d'inviter."
                               : "Ce groupe n'a plus de joueur simulé à remplacer : choisissez-en un autre.";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------- the server

void RaidTestGuests::update(float dt) {
    if (!testId_.empty()) {
        pollIn_ -= dt;
        if (pollIn_ <= 0.0f) {
            pollIn_ = kPollSeconds;
            load();
        }
    }
    if (open_ && query_ != searched_) {
        searchIn_ -= dt;
        if (searchIn_ <= 0.0f) search();
    } else {
        searchIn_ = kSearchDelay;
    }
}

void RaidTestGuests::load() {
    g_.online.get("/api/v1/admin/raid-tests/" + testId_, OnlineService::guard(life_, [this](const ApiResult& r) {
                      if (r.ok && !r.fromCache) merge(r.data);
                  }),
                  false);
}

void RaidTestGuests::merge(const Json& data) {
    if (const int64_t s = skewOf(data); s != 0) skew_ = s;
    const Json list = data.value("invitations", Json::array());
    if (!list.is_array()) return;
    for (const Json& j : list) {
        const Json p = j.value("player", Json::object());
        const std::string id = jstr(p, "id");
        if (id.empty()) continue;
        auto it = std::find_if(guests_.begin(), guests_.end(), [&](const Guest& x) { return x.id == id; });
        if (it == guests_.end()) {
            guests_.push_back({});
            it = guests_.end() - 1;
            it->id = id;
        }
        if (it->busy) continue;  // its own answer comes
        it->name = jstr(p, "username", it->name);
        it->admin = jstr(p, "role") == "admin";
        it->group = std::clamp(jget<int>(j, "group", it->group), 0, static_cast<int>(groups_.size()) - 1);
        it->invincible = jget<bool>(j, "invincible", it->invincible);
        it->status = jstr(j, "status", it->status);
        it->expires = raids::parseUtc(jstr(j, "expires_at")).value_or(0);
        const Json playing = j.value("playing", Json());
        it->playing = playing.is_object();
        it->connected = it->playing && jget<bool>(playing, "connected", false);
    }
}

void RaidTestGuests::send(Guest& x) {
    x.busy = true;
    const std::string id = x.id;
    g_.online.send("POST", "/api/v1/admin/raid-tests/" + testId_ + "/invitations", {{"player_id", x.id}, {"group", x.group}, {"invincible", x.invincible}},
                   OnlineService::guard(life_, [this, id](const ApiResult& r) {
                       auto it = std::find_if(guests_.begin(), guests_.end(), [&](const Guest& g) { return g.id == id; });
                       if (it == guests_.end()) return;
                       it->busy = false;
                       if (!r.ok) {
                           if (it->status.empty()) it->status = "cancelled";  // never sent: shown as such, « Réinviter »
                           say(it->name + " : " + r.message, true);
                           return;
                       }
                       it->status = "pending";
                       const Json inv = r.data.value("invitation", Json::object());
                       it->expires = raids::parseUtc(jstr(inv, "expires_at")).value_or(nowUtc(skew_) + 180);
                       say("Invitation envoyée à " + it->name + " : réponse attendue sous 3 minutes.", false);
                   }));
}

void RaidTestGuests::invite(const Candidate& c) {
    const int group = std::clamp(defaultGroup_, 0, static_cast<int>(groups_.size()) - 1);
    std::string why;
    if (!roomFor(group, c.id, &why)) return say(why, true);
    auto it = std::find_if(guests_.begin(), guests_.end(), [&](const Guest& x) { return x.id == c.id; });
    if (it == guests_.end()) {
        guests_.push_back({});
        it = guests_.end() - 1;
    }
    it->id = c.id;
    it->name = c.name;
    it->admin = c.admin;
    it->group = group;
    it->invincible = false;  // « pas invincible » by default
    it->status.clear();
    candidates_.erase(std::remove_if(candidates_.begin(), candidates_.end(), [&](const Candidate& k) { return k.id == c.id; }), candidates_.end());
    if (testId_.empty()) return say(c.name + " sera invité au lancement du test.", false);
    send(*it);
}

void RaidTestGuests::cancel(size_t index) {
    Guest& x = guests_[index];
    if (testId_.empty()) {
        say(x.name + " retiré de la liste.", false);
        guests_.erase(guests_.begin() + static_cast<std::ptrdiff_t>(index));
        loaded_ = false;
        return;
    }
    const int64_t now = nowUtc(skew_);
    if (x.status == "declined" || x.status == "cancelled" || x.status == "expired" || (x.status == "pending" && x.expires <= now)) {
        std::string why;
        if (!roomFor(x.group, x.id, &why)) return say(why, true);
        return send(x);  // « Réinviter »
    }
    x.busy = true;
    const std::string id = x.id;
    g_.online.send("DELETE", "/api/v1/admin/raid-tests/" + testId_ + "/invitations/" + id, Json(), OnlineService::guard(life_, [this, id](const ApiResult& r) {
                       auto it = std::find_if(guests_.begin(), guests_.end(), [&](const Guest& g) { return g.id == id; });
                       if (it == guests_.end()) return;
                       it->busy = false;
                       if (!r.ok) return say(it->name + " : " + r.message, true);
                       const bool wasIn = it->status == "accepted";
                       it->status = "cancelled";
                       it->playing = false;
                       say(wasIn ? it->name + " a quitté le test : un joueur simulé reprend sa place." : "Invitation de " + it->name + " annulée.", false);
                   }));
}

void RaidTestGuests::patch(size_t index, bool group) {
    Guest& x = guests_[index];
    if (testId_.empty() || x.status.empty()) return;  // chosen only: sent with the launch
    x.busy = true;
    const std::string id = x.id;
    const Json body = group ? Json{{"group", x.group}} : Json{{"invincible", x.invincible}};
    g_.online.send("PATCH", "/api/v1/admin/raid-tests/" + testId_ + "/invitations/" + id, body, OnlineService::guard(life_, [this, id, group](const ApiResult& r) {
                       auto it = std::find_if(guests_.begin(), guests_.end(), [&](const Guest& g) { return g.id == id; });
                       if (it == guests_.end()) return;
                       it->busy = false;
                       if (!r.ok) {
                           say(it->name + " : " + r.message, true);
                           load();  // what the server keeps
                           return;
                       }
                       const Json inv = r.data.value("invitation", Json::object());
                       it->group = std::clamp(jget<int>(inv, "group", it->group), 0, static_cast<int>(groups_.size()) - 1);
                       it->invincible = jget<bool>(inv, "invincible", it->invincible);
                       say(group ? it->name + " : " + groups_[static_cast<size_t>(it->group)] + "."
                                 : it->name + (it->invincible ? " est invincible." : " n'est plus invincible."),
                           false);
                   }));
}

void RaidTestGuests::search() {
    searched_ = query_;
    searchIn_ = kSearchDelay;
    if (!query_.empty() && !searchable(query_)) {
        candidates_.clear();
        hasMore_ = false;
        loaded_ = true;
        return say("Recherche : lettres, chiffres, « _ », « . » et « - » seulement.", true);
    }
    if (!g_.online.loggedIn()) {
        candidates_.clear();
        hasMore_ = false;
        loaded_ = true;
        return say("Connectez-vous à Cosmic Online pour voir les joueurs connectés.", true);
    }
    const int gen = ++gen_;
    loading_ = true;
    std::string path = "/api/v1/admin/raid-test-players?limit=30&offset=" + std::to_string(loaded_ && !candidates_.empty() && searchMore_ ? candidates_.size() : 0);
    if (!query_.empty()) path += "&q=" + pct(query_);
    if (!testId_.empty()) path += "&test=" + testId_;
    const bool more = searchMore_;
    searchMore_ = false;
    g_.online.get(path, OnlineService::guard(life_, [this, gen, more](const ApiResult& r) {
                      if (gen != gen_) return;
                      loading_ = false;
                      loaded_ = true;
                      if (!r.ok) return say(r.message, true);
                      if (!more) candidates_.clear();
                      total_ = jget<int>(r.data, "total", 0);
                      hasMore_ = jget<bool>(r.data, "has_more", false);
                      const std::string me = g_.online.playerId();
                      for (const Json& p : r.data.value("players", Json::array())) {
                          Candidate c{jstr(p, "id"), jstr(p, "username"), jstr(p, "role") == "admin"};
                          if (c.id.empty() || c.id == me) continue;
                          const bool chosen = std::any_of(guests_.begin(), guests_.end(), [&](const Guest& x) {
                              return x.id == c.id && (x.status.empty() || x.status == "pending" || x.status == "accepted");
                          });
                          const bool listed = std::any_of(candidates_.begin(), candidates_.end(), [&](const Candidate& k) { return k.id == c.id; });
                          if (!chosen && !listed) candidates_.push_back(c);
                      }
                  }),
                  false);
}

// ---------------------------------------------------------------------- texts

std::string RaidTestGuests::stateText(const Guest& x, int64_t now) {
    if (x.status.empty()) return "invité au lancement";
    if (x.status == "pending") {
        if (x.expires <= now) return "expirée";
        const int64_t left = x.expires - now;
        char b[16];
        std::snprintf(b, sizeof b, "%d:%02d", static_cast<int>(left / 60), static_cast<int>(left % 60));
        return std::string("en attente · ") + b;
    }
    if (x.status == "accepted") return x.playing ? (x.connected ? "acceptée · en jeu" : "acceptée · absent") : "acceptée · arrive";
    if (x.status == "declined") return "refusée";
    if (x.status == "expired") return "expirée";
    return "annulée";
}

std::string RaidTestGuests::actionText(const Guest& x) {
    if (x.status.empty()) return kActions[0];
    if (x.status == "pending") return kActions[1];
    if (x.status == "accepted") return kActions[2];
    return kActions[3];
}

// ---------------------------------------------------------------------- layout

RaidTestGuests::Layout RaidTestGuests::layout(UIContext& ui, bool attached) {
    Layout L;
    const float W = ui.screenW(), H = ui.screenH();
    const float w = std::min(ui.s(1160), W - ui.s(32)), h = std::min(ui.s(700), H - ui.s(32));
    L.box = {(W - w) * 0.5f, (H - h) * 0.5f, w, h};
    const float p = ui.s(22), x = L.box.x + p, iw = w - p * 2;
    const std::string closeT = "Fermer";
    const float closeW = std::min(iw * 0.3f, std::max(ui.s(130), textW(ui, closeT, 18, FontFace::Bold) + ui.s(40)));
    L.close = {L.box.right() - p - closeW, L.box.y + ui.s(16), closeW, ui.s(44)};
    L.closeFont = ui.fitFont(closeT, FontFace::Bold, 18, closeW - ui.s(20), 0.55f);
    L.title = {x, L.box.y + ui.s(16), L.close.x - ui.s(16) - x, ui.s(44)};
    const std::string intro =
        attached ? "Un joueur invité prend la place d'un joueur simulé de son groupe ; il a 3 minutes pour accepter. Vous changez son groupe et "
                   "« Invincible » pendant le test, pour tous les ordinateurs au même pas. Un test ne compte jamais."
                 : "Les invitations partent au lancement : le test passe alors par le serveur (sans invité, il reste local et hors ligne). Un "
                   "joueur invité prend la place d'un joueur simulé de son groupe. Un test ne compte jamais.";
    L.introLines = wrap(ui, intro, FontFace::Body, 14, iw, 2);
    float y = L.title.bottom() + ui.s(6);
    for (size_t i = 0; i < L.introLines.size(); ++i, y += ui.s(20)) L.intro.push_back({x, y, iw, ui.s(20)});
    y += ui.s(10);
    L.status = {x, L.box.bottom() - p - ui.s(22), iw, ui.s(22)};
    const float bottom = L.status.y - ui.s(10), gap = ui.s(20);
    L.stacked = iw < ui.s(520) + ui.s(360) + gap;
    if (!L.stacked) {
        const float listW = std::floor((iw - gap) * 0.57f);
        L.list = {x, y, listW, bottom - y};
        L.pick = {x + listW + gap, y, iw - listW - gap, bottom - y};
    } else {
        const float hh = std::floor((bottom - y - gap) * 0.5f);
        L.list = {x, y, iw, hh};
        L.pick = {x, y + hh + gap, iw, bottom - y - hh - gap};
    }
    L.listHead = {L.list.x, L.list.y, L.list.w, ui.s(24)};
    L.pickHead = {L.pick.x, L.pick.y, L.pick.w, ui.s(24)};
    L.search = {L.pick.x, L.pickHead.bottom() + ui.s(8), L.pick.w, ui.s(44)};
    L.pickList = {L.pick.x, L.search.bottom() + ui.s(10), L.pick.w, std::max(0.0f, L.pick.bottom() - L.search.bottom() - ui.s(10))};
    L.rowH = ui.s(96);
    L.pickRowH = ui.s(58);
    return L;
}

RaidTestGuests::RowLayout RaidTestGuests::rowLayout(UIContext& ui, const Rect& row, bool groups, const std::string& role, const std::string& state,
                                                    const std::string& action, const std::vector<std::string>& groupNames) {
    RowLayout R;
    R.row = row;
    const float pad = ui.s(12), in = row.w - pad * 2, gap = ui.s(8);
    // First line: the state's pill at the right (at most half the line), the role's pill, the name in what is left.
    const float cy = row.y + ui.s(24), ph = ui.s(28);
    const float stateW = std::min(in * 0.5f, textW(ui, state, 13, FontFace::Bold) + ui.s(22));
    R.state = {row.right() - pad - stateW, cy - ph * 0.5f, stateW, ph};
    const float roleW = std::min(in * 0.2f, textW(ui, role, 13, FontFace::Bold) + ui.s(22));
    R.role = {R.state.x - gap - roleW, cy - ph * 0.5f, roleW, ph};
    R.name = {row.x + pad, cy - ph * 0.5f, std::max(0.0f, R.role.x - gap - row.x - pad), ph};
    // Second line: the action (as wide as the widest action), the box, the group's choice in what is left.
    const float ly = row.y + ui.s(46), lh = ui.s(40);
    float actionW = 0.0f;
    for (const char* a : kActions) actionW = std::max(actionW, textW(ui, a, 18, FontFace::Bold));
    actionW = std::min(in * 0.36f, actionW + ui.s(28));
    R.action = {row.right() - pad - actionW, ly, actionW, lh};
    R.actionFont = ui.fitFont(action, FontFace::Bold, 18, actionW - ui.s(20), 0.55f);
    // The box: its switch (UIContext::toggle: 116 px of track and margins) and its label at full size if it can.
    const float invW = std::min(groups ? in * 0.36f : in - actionW - gap, textW(ui, "Invincible", 22, FontFace::Body) + ui.s(116));
    R.invincible = {R.action.x - gap - invW, ly, invW, lh};
    if (groups) {
        R.group = {row.x + pad, ly, std::max(0.0f, R.invincible.x - gap - row.x - pad), lh};
    } else {
        R.invincible.x = row.x + pad;  // a zone: no group to choose, the box first
    }
    (void)groupNames;
    return R;
}

RaidTestGuests::PickRowLayout RaidTestGuests::pickRowLayout(UIContext& ui, const Rect& row, const std::string& role) {
    PickRowLayout R;
    R.row = row;
    const float pad = ui.s(10), in = row.w - pad * 2, gap = ui.s(8), h = ui.s(40);
    const std::string t = "Inviter";
    const float bw = std::min(in * 0.4f, textW(ui, t, 18, FontFace::Bold) + ui.s(36));
    R.invite = {row.right() - pad - bw, row.y + (row.h - h) * 0.5f, bw, h};
    R.inviteFont = ui.fitFont(t, FontFace::Bold, 18, bw - ui.s(20), 0.55f);
    const float roleW = std::min(in * 0.25f, textW(ui, role, 13, FontFace::Bold) + ui.s(22));
    const float cy = row.y + row.h * 0.5f, ph = ui.s(28);
    R.role = {R.invite.x - gap - roleW, cy - ph * 0.5f, roleW, ph};
    R.name = {row.x + pad, cy - ph * 0.5f, std::max(0.0f, R.role.x - gap - row.x - pad), ph};
    return R;
}

// ---------------------------------------------------------------------- the window

void RaidTestGuests::draw() {
    if (!open_) return;
    UIContext& ui = g_.ui;
    const bool attached = !testId_.empty();
    const Layout L = layout(ui, attached);
    const int64_t now = nowUtc(skew_);
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 180});
    ui.r().fillRoundedRect(L.box, ui.s(18), Color{9, 14, 32, 250});
    ui.r().drawRoundedRect(L.box, ui.s(18), kGold, ui.s(1.5f));
    ui.pushId("raidtestguests");
    if (focusSearch_) {
        focusSearch_ = false;
        ui.setFocus("search");
    }
    fit(ui, "JOUEURS INVITÉS AU TEST", {L.title.x, L.title.center().y}, 24, L.title.w, kGold, FontFace::Title);
    UIContext::ButtonStyle cs;
    cs.small = true;
    cs.fontSize = L.closeFont;
    bool closeNow = ui.button("close", L.close, "Fermer", cs);
    for (size_t i = 0; i < L.introLines.size(); ++i) fit(ui, L.introLines[i], {L.intro[i].x, L.intro[i].center().y}, 14, L.intro[i].w, kDim);

    // ------------------------------------------------------------ the players invited
    const bool zone = groups_.size() <= 1;
    fit(ui, "INVITÉS · " + std::to_string(active()) + " / " + std::to_string(kMaxGuests) + " · " + std::to_string(std::max(0, seats_ - 1)) +
                (zone ? " joueur(s) simulé(s) à remplacer" : " joueur(s) simulé(s) à remplacer par groupe"),
        {L.listHead.x, L.listHead.center().y}, 15, L.listHead.w, kText, FontFace::Bold);
    const Rect view{L.list.x, L.listHead.bottom() + ui.s(8), L.list.w, std::max(0.0f, L.list.bottom() - L.listHead.bottom() - ui.s(8))};
    if (guests_.empty()) {
        const auto lines = wrap(ui, "Personne pour l'instant : choisissez des joueurs connectés dans la liste « Joueurs connectés ».", FontFace::Body, 15,
                                view.w - ui.s(20), 3);
        drawLines(ui, lines, {view.x + ui.s(10), view.y + ui.s(10)}, 15, ui.s(21), kDim);
    } else {
        const float gap = ui.s(8);
        ui.beginScroll("list", view, static_cast<float>(guests_.size()) * (L.rowH + gap));
        float y = view.y - ui.scrollOffset();
        for (size_t i = 0; i < guests_.size(); ++i, y += L.rowH + gap) {
            Guest& x = guests_[i];
            const Rect row{view.x, y, view.w - ui.s(14), L.rowH};
            const std::string role = x.admin ? "ADMIN" : "JOUEUR";
            const std::string state = stateText(x, now);
            const std::string action = actionText(x);
            const RowLayout R = rowLayout(ui, row, !zone, role, state, action, groups_);
            if (row.bottom() < view.y || row.y > view.bottom()) continue;
            ui.r().fillRoundedRect(row, ui.s(10), Color{22, 28, 60, 255});
            fit(ui, x.name, {R.name.x, R.name.center().y}, 16, R.name.w, kText, FontFace::Bold, Align::Left, true);
            pillIn(ui, R.role, role, x.admin ? Color{90, 60, 140, 255} : Color{40, 52, 96, 255}, kText);
            const bool live = x.status == "accepted" || (x.status == "pending" && x.expires > now);
            pillIn(ui, R.state, state, x.status == "accepted" ? Color{30, 92, 64, 255} : live || x.status.empty() ? Color{70, 60, 30, 255} : Color{70, 34, 40, 255},
                   x.status == "accepted" ? kGreen : live || x.status.empty() ? kWarn : kRed);
            ui.pushId(x.id);
            const bool ro = ui.readOnly;
            ui.readOnly = ro || x.busy;
            if (!zone) {
                int gi = std::clamp(x.group, 0, static_cast<int>(groups_.size()) - 1);
                if (ui.cycle("group", R.group, "", gi, groups_) && gi != x.group) {
                    std::string why;
                    if (!roomFor(gi, x.id, &why)) say(why, true);
                    else {
                        x.group = gi;
                        patch(i, true);
                    }
                }
            }
            bool inv = x.invincible;
            if (ui.toggle("invincible", R.invincible, "Invincible", inv)) {
                x.invincible = inv;
                patch(i, false);
            }
            UIContext::ButtonStyle as;
            as.small = true;
            as.fontSize = R.actionFont;
            as.danger = x.status == "accepted" || x.status == "pending";
            as.enabled = !x.busy;
            const bool act = ui.button("action", R.action, action, as);
            ui.readOnly = ro;
            ui.popId();
            if (act) {
                cancel(i);
                break;  // the list may have changed
            }
        }
        ui.endScroll();
    }

    // ------------------------------------------------------------ the players connected now
    fit(ui, "JOUEURS CONNECTÉS" + (loaded_ ? " · " + std::to_string(total_) : std::string()), {L.pickHead.x, L.pickHead.center().y}, 15, L.pickHead.w, kText,
        FontFace::Bold);
    ui.textField("search", L.search, query_, "Rechercher un joueur par son nom", 20);
    {
        const float gap = ui.s(6);
        const float moreH = hasMore_ ? ui.s(52) : 0.0f;
        ui.beginScroll("pick", L.pickList, static_cast<float>(candidates_.size()) * (L.pickRowH + gap) + moreH);
        float y = L.pickList.y - ui.scrollOffset();
        if (candidates_.empty()) {
            const std::string t = loading_ ? "Chargement des joueurs connectés…"
                                  : !loaded_ ? ""
                                  : query_.empty() ? "Aucun autre joueur connecté pour l'instant."
                                                   : "Aucun joueur connecté de ce nom.";
            const auto lines = wrap(ui, t, FontFace::Body, 15, L.pickList.w - ui.s(20), 3);
            drawLines(ui, lines, {L.pickList.x + ui.s(10), L.pickList.y + ui.s(10)}, 15, ui.s(21), kDim);
        }
        int clicked = -1;
        for (size_t i = 0; i < candidates_.size(); ++i, y += L.pickRowH + gap) {
            const Candidate& c = candidates_[i];
            const Rect row{L.pickList.x, y, L.pickList.w - ui.s(14), L.pickRowH};
            const std::string role = c.admin ? "ADMIN" : "JOUEUR";
            const PickRowLayout R = pickRowLayout(ui, row, role);
            if (row.bottom() < L.pickList.y || row.y > L.pickList.bottom()) continue;
            ui.r().fillRoundedRect(row, ui.s(10), Color{22, 28, 60, 255});
            fit(ui, c.name, {R.name.x, R.name.center().y}, 16, R.name.w, kText, FontFace::Bold, Align::Left, true);
            pillIn(ui, R.role, role, c.admin ? Color{90, 60, 140, 255} : Color{40, 52, 96, 255}, kText);
            UIContext::ButtonStyle bs;
            bs.small = true;
            bs.primary = true;
            bs.fontSize = R.inviteFont;
            ui.pushId(c.id);
            if (ui.button("invite", R.invite, "Inviter", bs)) clicked = static_cast<int>(i);
            ui.popId();
        }
        if (hasMore_) {
            UIContext::ButtonStyle ms;
            ms.small = true;
            ms.enabled = !loading_;
            const std::string t = "Plus de joueurs";
            ms.fontSize = ui.fitFont(t, FontFace::Bold, 18, L.pickList.w - ui.s(54), 0.55f);
            if (ui.button("more", {L.pickList.x, y + ui.s(4), L.pickList.w - ui.s(14), ui.s(44)}, t, ms)) {
                searchMore_ = true;
                search();
            }
        }
        ui.endScroll();
        if (clicked >= 0) invite(Candidate(candidates_[static_cast<size_t>(clicked)]));
    }
    if (!status_.empty()) {
        i18n::Verbatim names(false);
        fit(ui, status_, {L.status.x, L.status.center().y}, 14, L.status.w, statusError_ ? kRed : kGreen);
    }
    ui.popId();
    if (closeNow || (ui.backPressed() && !ui.editingText())) close();
}

}  // namespace cosmic
