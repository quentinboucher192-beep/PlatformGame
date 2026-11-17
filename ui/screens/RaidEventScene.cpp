// The sheet of a raid event (phase 1 of the clan raids, maquette « Événement mondial
// Asterion »): its title, story and figures (start in the computer's own time and in UTC,
// countdown, format, participation), the instructions of my clan's raid leader, the rewards
// text of the server, and at the right my clan's side: register or unregister (myself, or my
// clan by its chief or an officer), « Inviter des membres… », the groups of 8 with their leader,
// the clans taking part, the invitations of a raid in common, and the player's reminders.
// The combat itself comes in a later version: « en cours » says so.
//
// Every rule is the server's (server/src/raids.ts): the screen offers what the player's role
// allows and shows what the server answers. Placement: each text is measured into the room left
// by what is placed before it (shortened, made smaller or wrapped on counted lines).

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/Clans.h"
#include "game/online/Raids.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

using namespace raidui;

const Json& at(const Json& j, const char* key) {
    static const Json null;
    if (!j.is_object()) return null;
    auto it = j.find(key);
    return it == j.end() ? null : *it;
}

std::string plural(int n, const std::string& one, const std::string& many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

class RaidEventScene : public Scene {
public:
    RaidEventScene(GameServices& g, std::string id) : g_(g), id_(std::move(id)) {}

    void onEnter() override {
        bg_.init(g_);
        g_.ui.setFocus("raid/back");
        load();
        loadPrefs();
    }
    void onResume() override { load(); }

    void update(float dt) override {
        bg_.update(dt);
        timer_ += dt;
        if (g_.app.topScene() == this && timer_ >= 30.0f) load();
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        const bool top = g_.app.topScene() == this;
        ui.inputEnabled = top && modal_ == Modal::None;
        ui.pushId("raid");
        bool back = false;
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(28);
        const float asideW = std::min(ui.s(520), W * 0.34f);
        const Rect aside{W - m - asideW, ui.s(24), asideW, H - ui.s(48) - ui.s(30)};
        const Rect left{m, ui.s(24), aside.x - ui.s(18) - m, H - ui.s(48) - ui.s(30)};
        const float bw = textW(ui, "Retour", 16, FontFace::Bold) + ui.s(40);
        if (ui.button("back", {left.x, left.y, bw, ui.s(44)}, "Retour")) back = true;
        if (!loaded_) {
            fit(ui, error_.empty() ? "Chargement de l'événement…" : error_, {left.x + bw + ui.s(20), left.y + ui.s(22)}, 17, left.right() - left.x - bw - ui.s(20),
                error_.empty() ? ui.theme().textDim : ui.theme().danger);
        } else {
            drawLeft(left, left.x + bw + ui.s(14));
            drawAside(aside);
        }
        drawStatus();
        ui.popId();
        ui.inputEnabled = top;
        if (modal_ != Modal::None) drawModal();
        else if (!back && ui.backPressed() && !ui.editingText()) back = true;
        ui.end();
        if (back) g_.app.popScene();
    }

private:
    enum class Modal { None, Edit, Invite, Cancel, Withdraw };

    // ---------------------------------------------------------------- data
    void say(const std::string& m, bool error) {
        status_ = m;
        statusError_ = error;
        statusT_ = g_.ui.time();
    }

    void load() {
        timer_ = 0.0f;
        const int gen = ++gen_;
        g_.online.get("/api/v1/raids/events/" + id_, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != gen_) return;
                          if (!r.ok) {
                              if (!loaded_) error_ = r.message;
                              else say(r.message, true);
                              return;
                          }
                          data_ = r.data;
                          loaded_ = true;
                          error_.clear();
                          if (!r.fromCache) skew_ = skewOf(r.data);
                      }));
    }

    void loadPrefs() {
        if (!g_.online.loggedIn()) return;
        g_.online.get("/api/v1/raids/prefs", OnlineService::guard(life_, [this](const ApiResult& r) {
                          if (r.ok) prefs_ = r.data.value("prefs", Json());
                      }));
    }

    /// A write of the sheet; `done` is said, then the sheet is read again.
    void send(const std::string& method, const std::string& path, const Json& body, std::string done, std::function<void(const ApiResult&)> after = {}) {
        busy_ = true;
        g_.online.send(method, "/api/v1/raids/events/" + id_ + path, body,
                       OnlineService::guard(life_, [this, done = std::move(done), after = std::move(after)](const ApiResult& r) {
                           busy_ = false;
                           if (!r.ok) {
                               modalError_ = r.message;
                               say(r.message, true);
                               return;
                           }
                           modal_ = Modal::None;
                           if (after) after(r);
                           else say(done, false);
                           load();
                       }));
    }

    void act(const raids::Action& a) {
        if (a.id == "login") return g_.app.pushScene(makeOnlineAccount(g_, !g_.settings.online.onboardingDone, false));
        if (a.id == "register") send("POST", "/register", Json::object(), "Vous êtes inscrit : les rappels arriveront ici.");
        else if (a.id == "unregister") send("DELETE", "/register", Json::object(), "Inscription annulée.");
        else if (a.id == "register_clan") send("POST", "/clan", Json::object(), "Clan inscrit : ses membres sont prévenus et s'inscrivent eux-mêmes.");
    }

    const Json& my() const { return at(data_, "my"); }
    std::string clanId() const { return jstr(at(my(), "clan"), "id"); }
    std::string clanRole() const { return jstr(at(my(), "clan"), "role"); }
    std::string clanTag() const { return jstr(at(my(), "clan"), "tag"); }
    bool manager() const { return jget<bool>(my(), "can_manage", false); }

    // ---------------------------------------------------------------- left
    void drawLeft(const Rect& left, float pillsX) {
        UIContext& ui = g_.ui;
        const raids::Event e = raids::parseEvent(at(data_, "event"));
        const int64_t now = nowUtc(skew_);
        const raids::State st = raids::stateAt(e, now);
        // The pills after « Retour ».
        {
            float px = pillsX;
            const float cy = left.y + ui.s(22);
            const bool world = e.kind == "world";
            const std::string kind = world ? "ÉVÉNEMENT MONDIAL" : "RAID DU CLAN " + clans::bracket(e.organizerTag);
            float w = pill(ui, px, cy, kind, world ? kOrange : Color{42, 58, 122, 255}, world ? kInk : kText, left.right());
            if (w > 0.0f) px += w + ui.s(8);
            const std::string sl = st == raids::State::Open ? "Inscriptions ouvertes jusqu'à " + whenShort(e.lockAt) : raids::stateLabel(st);
            if (pill(ui, px, cy, sl, stateFill(st), stateText(st), left.right()) == 0.0f)
                pill(ui, px, cy, raids::stateLabel(st), stateFill(st), stateText(st), left.right());
        }
        float y = left.y + ui.s(62);
        fit(ui, e.displayName, {left.x, y + ui.s(26)}, 52, left.w, kText, FontFace::Title);
        y += ui.s(58);
        fit(ui, e.displayTitle, {left.x, y + ui.s(14)}, 22, left.w, kPeach, FontFace::Title);
        y += ui.s(38);
        const auto desc = wrap(ui, jstr(data_, "description"), FontFace::Body, 17, std::min(left.w, ui.s(900)), 3);
        y += drawLines(ui, desc, {left.x, y}, 17, ui.s(25), Color{216, 219, 245, 255}) + ui.s(12);
        if (st == raids::State::Running) {
            const Rect band{left.x, y, left.w, ui.s(46)};
            ui.r().fillRoundedRect(band, ui.s(10), Color{122, 26, 26, 235});
            fit(ui, "Le combat arrive dans une prochaine version : le raid est ouvert, rien à jouer pour l'instant.", {band.x + ui.s(16), band.center().y}, 16,
                band.w - ui.s(32), kText, FontFace::Bold);
            y += ui.s(58);
        }
        // Four figures.
        {
            const float gap = ui.s(10), cw = (left.w - 3 * gap) / 4.0f, ch = ui.s(100);
            const raids::Civil utc = raids::civilAt(e.startsAt, 0);
            const raids::Civil loc = raids::toSystemLocal(e.startsAt);
            const raids::Countdown cd = raids::countdownOf(e, now, true);
            struct Card {
                std::string caption, value, sub;
                bool gold;
            };
            const Card cards[4] = {
                {"Début (votre heure)", whenShort(e.startsAt), raids::offsetLabel(loc.offsetMinutes) + " · " + raids::hhmm(utc) + " UTC", false},
                {cd.value.empty() ? "État" : cd.caption, cd.value.empty() ? cd.caption : cd.value, "verrouillage " + std::to_string(e.lockMinutes) + " min avant",
                 true},
                {"Format", raids::formatLine(e), raids::difficultyLabel(e.difficulty), false},
                {"Participation",
                 e.kind == "world" ? plural(e.clans, "clan", "clans") + " · " + plural(e.players, "joueur", "joueurs")
                                   : (e.capacity > 0 ? std::to_string(e.players) + " / " + std::to_string(e.capacity) + " inscrits" : plural(e.players, "inscrit", "inscrits")),
                 "groupes de 8 joueurs", false},
            };
            for (int i = 0; i < 4; ++i) {
                const Rect c{left.x + static_cast<float>(i) * (cw + gap), y, cw, ch};
                ui.r().fillRoundedRect(c, ui.s(12), Color{22, 29, 66, 240});
                const float tx = c.x + ui.s(12), tw = c.w - ui.s(24);
                fit(ui, cards[i].caption, {tx, c.y + ui.s(20)}, 13, tw, Color{154, 160, 200, 255});
                fit(ui, cards[i].value, {tx, c.y + ui.s(50)}, cards[i].gold ? 20 : 18, tw, cards[i].gold ? kGold : kText,
                    cards[i].gold ? FontFace::Title : FontFace::Bold);
                fit(ui, cards[i].sub, {tx, c.y + ui.s(80)}, 13, tw, Color{154, 160, 200, 255});
            }
            y += ch + ui.s(14);
        }
        // The instructions and the rewards.
        const float gap = ui.s(12), sw = (left.w - gap) * 0.5f, sh = left.bottom() - y;
        if (sh < ui.s(120)) return;
        const Rect s1{left.x, y, sw, sh}, s2{left.x + sw + gap, y, sw, sh};
        const Json& entry = at(my(), "entry");
        const std::string leader = jstr(at(entry, "leader"), "username");
        float ty = section(ui, s1, leader.empty() ? "CONSIGNES DU CLAN" : "CONSIGNES DU CHEF DE RAID · " + leader, kSky);
        const bool canEdit = manager() && entry.is_object() && st != raids::State::Finished && st != raids::State::Cancelled;
        const float footH = canEdit ? ui.s(58) : 0.0f;
        const float tw = s1.w - ui.s(28);
        const int lines = std::max(0, static_cast<int>((s1.bottom() - ui.s(14) - footH - ty) / ui.s(24)));
        const std::string instructions = jstr(entry, "instructions");
        if (!instructions.empty()) drawLines(ui, wrap(ui, instructions, FontFace::Body, 16, tw, lines, true), {s1.x + ui.s(14), ty}, 16, ui.s(24), kText);
        else
            drawLines(ui,
                      wrap(ui, entry.is_object() ? "Aucune consigne pour l'instant." : "Les consignes sont réservées aux membres d'un clan inscrit.", FontFace::Body, 15,
                           tw, lines),
                      {s1.x + ui.s(14), ty}, 15, ui.s(22), kDim);
        if (canEdit) {
            UIContext::ButtonStyle bs;
            bs.small = true;
            if (ui.button("edit", {s1.x + ui.s(14), s1.bottom() - ui.s(14) - ui.s(44), tw, ui.s(44)}, "Modifier les consignes (chef, officiers)", bs)) openEdit();
        }
        ty = section(ui, s2, "RÉCOMPENSES", kSky);
        const int rl = std::max(0, static_cast<int>((s2.bottom() - ui.s(14) - ty) / ui.s(24)));
        drawLines(ui, wrap(ui, jstr(data_, "rewards_text"), FontFace::Body, 16, s2.w - ui.s(28), rl), {s2.x + ui.s(14), ty}, 16, ui.s(24), kText);
    }

    // ---------------------------------------------------------------- aside
    void drawAside(const Rect& r) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(16), Color{16, 21, 47, 240});
        ui.r().drawRoundedRect(r, ui.s(16), kBorder, ui.s(1.2f));
        const raids::Event e = raids::parseEvent(at(data_, "event"));
        const int64_t now = nowUtc(skew_);
        const raids::State st = raids::stateAt(e, now);
        const float x = r.x + ui.s(16), w = r.w - ui.s(32), right = r.right() - ui.s(16);
        const bool registered = jget<bool>(my(), "registered", false);
        const Json& entry = at(my(), "entry");
        // The title, « Vous êtes inscrit » measured first at its right.
        float rw = 0.0f;
        if (registered) rw = fit(ui, "Vous êtes inscrit", {right, r.y + ui.s(28)}, 14, w * 0.4f, kGreen, FontFace::Bold, Align::Right).x;
        const std::string title = clanId().empty() ? "VOTRE CLAN" : clans::bracket(clanTag()) + " · " + plural(e.clanPlayers, "INSCRIT", "INSCRITS");
        fit(ui, title, {x, r.y + ui.s(28)}, 17, w - rw - ui.s(10), kText, FontFace::Title);
        float y = r.y + ui.s(54);
        // The player's button, and « Inviter des membres… » (chief, officers, raid leader).
        const raids::Action a = raids::primaryAction(e, now, g_.online.loggedIn(), clanId(), clanRole());
        const bool call = jget<bool>(my(), "can_call", false) && entry.is_object() && (st == raids::State::Open || st == raids::State::Scheduled);
        const float gap = ui.s(8), bw = call ? (w - gap) * 0.5f : w;
        UIContext::ButtonStyle s1;
        s1.enabled = a.enabled && !a.id.empty() && !busy_;
        s1.danger = a.id == "unregister";
        s1.primary = a.id == "register" || a.id == "register_clan";
        if (ui.button("act", {x, y, bw, ui.s(46)}, a.label, s1)) act(a);
        if (call) {
            UIContext::ButtonStyle s2;
            s2.enabled = !busy_;
            if (ui.button("call", {x + bw + gap, y, bw, ui.s(46)}, "Inviter des membres…", s2))
                send("POST", "/call", Json::object(), "", [this](const ApiResult& res) {
                    const int n = jget<int>(res.data, "notified", 0);
                    say(n > 0 ? plural(n, "membre invité", "membres invités") + " à s'inscrire."
                              : "Tous les membres non inscrits ont déjà une invitation non lue.",
                        false);
                });
        }
        y += ui.s(54);
        const auto note = wrap(ui, a.note, FontFace::Body, 13, w, 2);
        y += drawLines(ui, note, {x, y}, 13, ui.s(18), kDim) + (note.empty() ? 0.0f : ui.s(6));

        // The reminders at the bottom (their height first: the chips wrap).
        float blockY = r.bottom() - ui.s(12);
        std::vector<std::string> labels;
        std::vector<bool> on;
        if (g_.online.loggedIn()) {
            const std::vector<int> chosen = raids::reminderPrefs(prefs_);
            for (int mins : raids::reminderChoices()) {
                labels.push_back(raids::reminderLabel(mins));
                on.push_back(std::find(chosen.begin(), chosen.end(), mins) != chosen.end());
            }
            int rows = 1;
            float cx = x;
            for (const std::string& l : labels) {
                const float cw = std::min(textW(ui, l, 14, FontFace::Bold) + ui.s(26), w);
                if (cx > x && cx + cw > right) {
                    cx = x;
                    ++rows;
                }
                cx += cw + ui.s(8);
            }
            blockY = r.bottom() - ui.s(14) - ui.s(30) - static_cast<float>(rows) * ui.s(44);
            ui.r().drawLine({x, blockY}, {right, blockY}, kBorder, ui.s(1));
            fit(ui, "Rappels", {x, blockY + ui.s(18)}, 14, w, kText, FontFace::Bold);
            float bottom = blockY;
            const int clicked = chips(ui, "prefs", x, blockY + ui.s(34), right, labels, on, bottom);
            if (clicked >= 0) {
                const int mins = raids::reminderChoices()[static_cast<size_t>(clicked)];
                Json list = Json::array();
                for (int c : chosen)
                    if (c != mins) list.push_back(c);
                if (!on[static_cast<size_t>(clicked)]) list.push_back(mins);
                prefs_["reminders"] = list;
                g_.online.send("PUT", "/api/v1/raids/prefs", {{"reminders", list}}, OnlineService::guard(life_, [this](const ApiResult& res) {
                                   if (!res.ok) {
                                       say(res.message, true);
                                       loadPrefs();
                                   } else prefs_ = res.data.value("prefs", prefs_);
                               }));
            }
        }

        // The middle: groups, clans, invitations, the clan's own actions (scrolled).
        const Rect view{x, y, w + ui.s(8), blockY - ui.s(10) - y};
        if (view.h < ui.s(40)) return;
        const float content = middle(view, e, st, false);
        ui.beginScroll("middle", view, content);
        middle(view, e, st, true);
        ui.endScroll();
    }

    /// The middle of the aside from the top of `view` (scrolled when `draw`); its height.
    float middle(const Rect& view, const raids::Event& e, raids::State st, bool draw) {
        UIContext& ui = g_.ui;
        const float x = view.x, w = view.w - ui.s(14);
        const float top = draw ? view.y - ui.scrollOffset() : view.y;
        float y = top;
        const Json& entry = at(my(), "entry");
        const bool beforeLock = st == raids::State::Open || st == raids::State::Scheduled;
        auto visible = [&](float y0, float h) { return draw && y0 + h >= view.y && y0 <= view.bottom(); };
        // The groups of 8.
        const auto groups = raids::parseGroups(at(data_, "groups"));
        if (groups.empty()) {
            const std::string t = clanId().empty()        ? "Rejoignez un clan pour participer aux raids."
                                  : !entry.is_object()    ? (e.kind == "world" ? "Votre clan n'est pas encore inscrit à cet événement." : "Votre clan ne participe pas à ce raid.")
                                                          : "Personne de votre clan n'est inscrit pour l'instant.";
            const auto lines = wrap(ui, t, FontFace::Body, 15, w, 3);
            if (visible(y, static_cast<float>(lines.size()) * ui.s(22))) drawLines(ui, lines, {x, y}, 15, ui.s(22), kDim);
            y += static_cast<float>(lines.size()) * ui.s(22) + ui.s(10);
        }
        const float cellH = ui.s(34), cellGap = ui.s(6);
        for (const raids::Group& g : groups) {
            const float h = ui.s(44) + 2 * (cellH + cellGap) + ui.s(6);
            if (visible(y, h)) {
                const Rect card{x, y, w, h};
                ui.r().fillRoundedRect(card, ui.s(12), Color{22, 29, 66, 240});
                fit(ui, "Groupe " + std::to_string(g.number) + " · chef " + g.leader.username, {card.x + ui.s(12), card.y + ui.s(22)}, 15, card.w - ui.s(24), kText,
                    FontFace::Bold);
                const float cw = (card.w - ui.s(24) - 3 * cellGap) / 4.0f;
                for (int i = 0; i < raids::kGroupSize; ++i) {
                    const Rect c{card.x + ui.s(12) + static_cast<float>(i % 4) * (cw + cellGap), card.y + ui.s(42) + static_cast<float>(i / 4) * (cellH + cellGap),
                                 cw, cellH};
                    const bool filled = i < static_cast<int>(g.members.size());
                    const bool lead = filled && g.members[static_cast<size_t>(i)].id == g.leader.id;
                    ui.r().fillRoundedRect(c, ui.s(8), lead ? Color{58, 42, 106, 255} : filled ? Color{35, 48, 94, 255} : Color{21, 27, 58, 255});
                    fit(ui, filled ? g.members[static_cast<size_t>(i)].username : std::string("place libre"), c.center(), 13, c.w - ui.s(8),
                        filled ? kText : Color{110, 118, 160, 255}, FontFace::Body, Align::Center, filled);
                }
            }
            y += h + ui.s(8);
        }
        {
            const auto lines = wrap(ui,
                                    "Groupes de 8 dans l'ordre des inscriptions : le chef de raid mène le groupe 1, le premier inscrit de chaque autre groupe mène "
                                    "le sien. La répartition en instances arrivera avec les combats.",
                                    FontFace::Body, 13, w, 4);
            if (visible(y, static_cast<float>(lines.size()) * ui.s(19))) drawLines(ui, lines, {x, y}, 13, ui.s(19), Color{154, 160, 200, 255});
            y += static_cast<float>(lines.size()) * ui.s(19) + ui.s(14);
        }
        // The clans taking part.
        const Json& parts = at(data_, "participants");
        if (parts.is_array() && !parts.empty()) {
            if (visible(y, ui.s(28))) fit(ui, e.kind == "world" ? "CLANS INSCRITS" : "CLANS DU RAID", {x, y + ui.s(12)}, 14, w, kSky, FontFace::Title);
            y += ui.s(30);
            for (const Json& p : parts) {
                if (visible(y, ui.s(28))) {
                    const std::string n = plural(jget<int>(p, "players", 0), "inscrit", "inscrits");
                    const float nw = fit(ui, n, {x + w, y + ui.s(13)}, 14, w * 0.35f, kDim, FontFace::Body, Align::Right).x;
                    fit(ui, clans::tagged(jstr(p, "name"), jstr(p, "tag")), {x, y + ui.s(13)}, 14, w - nw - ui.s(12), kText, FontFace::Body, Align::Left, true);
                }
                y += ui.s(28);
            }
            y += ui.s(10);
        }
        // A clan raid: its invitations (organizer), or the one my clan received.
        const bool organizer = jget<bool>(my(), "is_organizer", false);
        const Json& invs = at(data_, "invitations");
        if (organizer && invs.is_array() && !invs.empty()) {
            if (visible(y, ui.s(28))) fit(ui, "INVITATIONS", {x, y + ui.s(12)}, 14, w, kSky, FontFace::Title);
            y += ui.s(30);
            for (const Json& i : invs) {
                if (visible(y, ui.s(28))) {
                    const std::string s = jstr(i, "status");
                    const std::string label = s == "accepted" ? "acceptée" : s == "declined" ? "refusée" : "en attente";
                    const float nw = fit(ui, label, {x + w, y + ui.s(13)}, 14, w * 0.35f, s == "accepted" ? kGreen : kDim, FontFace::Body, Align::Right).x;
                    const Json& c = at(i, "clan");
                    fit(ui, clans::tagged(jstr(c, "name"), jstr(c, "tag")), {x, y + ui.s(13)}, 14, w - nw - ui.s(12), kText, FontFace::Body, Align::Left, true);
                }
                y += ui.s(28);
            }
            y += ui.s(10);
        }
        UIContext::ButtonStyle bs;
        bs.small = true;
        bs.enabled = !busy_;
        auto button = [&](const std::string& id, const std::string& label, bool danger) {
            UIContext::ButtonStyle s = bs;
            s.danger = danger;
            bool clicked = false;
            if (visible(y, ui.s(44))) clicked = ui.button(id, {x, y, w, ui.s(44)}, label, s);
            y += ui.s(52);
            return clicked;
        };
        if (organizer && manager() && beforeLock && button("invite", "Inviter un clan…", false)) openInvite();
        const Json& inv = at(data_, "invitation");
        if (inv.is_object() && jstr(inv, "status") == "pending") {
            const std::string from = clans::bracket(jstr(at(inv, "from"), "tag"));
            const auto lines = wrap(ui, from + " invite votre clan à ce raid commun.", FontFace::Bold, 15, w, 2);
            if (visible(y, static_cast<float>(lines.size()) * ui.s(22))) drawLines(ui, lines, {x, y}, 15, ui.s(22), kText, FontFace::Bold);
            y += static_cast<float>(lines.size()) * ui.s(22) + ui.s(6);
            if (manager() && beforeLock) {
                const float hw = (w - ui.s(8)) * 0.5f;
                if (visible(y, ui.s(44))) {
                    UIContext::ButtonStyle ok = bs;
                    ok.primary = true;
                    if (ui.button("accept", {x, y, hw, ui.s(44)}, "Accepter", ok))
                        send("POST", "/invitation", {{"accept", true}}, "Invitation acceptée : vos membres sont prévenus.");
                    if (ui.button("decline", {x + hw + ui.s(8), y, hw, ui.s(44)}, "Refuser", bs)) send("POST", "/invitation", {{"accept", false}}, "Invitation refusée.");
                }
                y += ui.s(52);
            } else {
                if (visible(y, ui.s(24))) fit(ui, "Le chef ou un officier répond.", {x, y + ui.s(11)}, 13, w, kDim);
                y += ui.s(30);
            }
        }
        // My clan's own actions (chief, officers).
        if (manager() && entry.is_object() && !organizer && beforeLock && button("withdraw", "Retirer mon clan…", true)) {
            modal_ = Modal::Withdraw;
            modalError_.clear();
        }
        if (manager() && organizer && st != raids::State::Finished && st != raids::State::Cancelled && button("cancel", "Annuler le raid…", true)) {
            modal_ = Modal::Cancel;
            modalError_.clear();
        }
        return y - top;
    }

    // ---------------------------------------------------------------- modals
    void openEdit() {
        const Json& entry = at(my(), "entry");
        instructions_ = jstr(entry, "instructions");
        leaders_.clear();
        leaders_.push_back({"", "Aucun"});
        const std::string cur = jstr(at(entry, "leader"), "id");
        leaderIdx_ = 0;
        if (!cur.empty()) leaders_.push_back({cur, jstr(at(entry, "leader"), "username")});
        for (const Json& p : at(data_, "roster"))
            if (jstr(p, "id") != cur) leaders_.push_back({jstr(p, "id"), jstr(p, "username")});
        if (!cur.empty()) leaderIdx_ = 1;
        modalError_.clear();
        modal_ = Modal::Edit;
    }

    void openInvite() {
        inviteTag_.clear();
        modalError_.clear();
        modal_ = Modal::Invite;
    }

    void drawModal() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 175});
        const float w = std::min(ui.screenW() - ui.s(40), ui.s(700)), tw = w - ui.s(60);
        std::string title, intro, okLabel;
        bool danger = false;
        float fieldsH = 0.0f;
        switch (modal_) {
            case Modal::Edit:
                title = "Consignes du clan";
                intro = "Le chef de raid et les consignes ne sont visibles que par les membres de votre clan.";
                okLabel = "Enregistrer";
                fieldsH = 2 * ui.s(78);
                break;
            case Modal::Invite:
                title = "Inviter un clan";
                intro = "Son chef ou un de ses officiers accepte ou refuse ; s'il accepte, ses membres peuvent s'inscrire à ce raid commun.";
                okLabel = "Inviter";
                fieldsH = ui.s(78);
                break;
            case Modal::Cancel:
                title = "Annuler le raid";
                intro = "Annuler ce raid ? Les inscrits et les clans invités sont prévenus ; il reste dans l'historique du clan.";
                okLabel = "Annuler le raid";
                danger = true;
                break;
            case Modal::Withdraw:
                title = "Retirer mon clan";
                intro = "Retirer votre clan de ce raid ? Les inscriptions de vos membres sont annulées et ils sont prévenus.";
                okLabel = "Retirer le clan";
                danger = true;
                break;
            case Modal::None: return;
        }
        const auto lines = wrap(ui, intro, FontFace::Body, 16, tw, 4);
        const float introH = static_cast<float>(lines.size()) * ui.s(22);
        const float h = ui.s(74) + introH + ui.s(14) + fieldsH + ui.s(34) + ui.s(76);
        const Rect box{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), danger ? ui.theme().danger : kOrange.withAlphaF(0.7f), ui.s(1.5f));
        ui.pushId("raid/modal");
        const float x = box.x + ui.s(30);
        fit(ui, title, {x, box.y + ui.s(40)}, 26, tw, kText, FontFace::Title);
        drawLines(ui, lines, {x, box.y + ui.s(68)}, 16, ui.s(22), ui.theme().textDim);
        float y = box.y + ui.s(74) + introH + ui.s(14);
        std::string problem;
        if (modal_ == Modal::Edit) {
            std::vector<std::string> names;
            for (const auto& l : leaders_) names.push_back(l.second);
            ui.cycle("leader", {x, y + ui.s(18), tw, ui.s(48)}, "Chef de raid", leaderIdx_, names);
            leaderIdx_ = std::clamp(leaderIdx_, 0, static_cast<int>(leaders_.size()) - 1);
            y += ui.s(78);
            fit(ui, "Consignes (" + std::to_string(raids::kMaxInstructions) + " caractères au plus)", {x, y + ui.s(10)}, 15, tw, ui.theme().textDim);
            ui.textField("instructions", {x, y + ui.s(24), tw, ui.s(44)}, instructions_, "Groupe 1 : bras droit, puis le torse…", raids::kMaxInstructions);
            y += ui.s(78);
        } else if (modal_ == Modal::Invite) {
            fit(ui, "Tag du clan (2 à 5 lettres ou chiffres)", {x, y + ui.s(10)}, 15, tw, ui.theme().textDim);
            ui.textField("tag", {x, y + ui.s(24), std::min(tw, ui.s(240)), ui.s(44)}, inviteTag_, "TAG", 5);
            y += ui.s(78);
            problem = clans::tagError(inviteTag_);
            if (!inviteTag_.empty() && clans::bracket(inviteTag_) == clans::bracket(clanTag())) problem = "C'est votre propre clan.";
            if (inviteTag_.empty()) problem = " ";
        }
        fit(ui, !modalError_.empty() ? modalError_ : problem, {x, y + ui.s(12)}, 15, tw, ui.theme().danger);
        const float by = box.bottom() - ui.s(66);
        UIContext::ButtonStyle ok;
        ok.primary = !danger;
        ok.danger = danger;
        ok.enabled = !busy_ && problem.empty();
        const float okW = textW(ui, okLabel, 17, FontFace::Bold) + ui.s(44);
        const std::string backLabel = danger ? "Garder" : "Annuler";
        const float cW = textW(ui, backLabel, 17, FontFace::Bold) + ui.s(44);
        const bool go = ui.button("ok", {box.right() - ui.s(30) - okW, by, okW, ui.s(48)}, okLabel, ok);
        const bool cancel = ui.button("cancel", {box.right() - ui.s(30) - okW - ui.s(12) - cW, by, cW, ui.s(48)}, backLabel) ||
                            (ui.backPressed() && !ui.editingText());
        ui.popId();
        if (cancel) {
            modal_ = Modal::None;
            return;
        }
        if (!go || !ok.enabled) return;
        modalError_.clear();
        switch (modal_) {
            case Modal::Edit: {
                Json body = {{"instructions", instructions_}};
                const std::string id = leaders_[static_cast<size_t>(leaderIdx_)].first;
                body["leader_id"] = id.empty() ? Json() : Json(id);
                send("PATCH", "/clan", body, "Consignes enregistrées.");
                break;
            }
            case Modal::Invite:
                send("POST", "/invites", {{"tag", inviteTag_}}, "Invitation envoyée à " + clans::bracket(inviteTag_) + ".");
                break;
            case Modal::Cancel:
                send("DELETE", "", Json::object(), "Raid annulé : les inscrits sont prévenus.");
                break;
            case Modal::Withdraw:
                send("DELETE", "/clan", Json::object(), "Votre clan s'est retiré du raid.");
                break;
            case Modal::None: break;
        }
    }

    void drawStatus() {
        UIContext& ui = g_.ui;
        if (status_.empty() || ui.time() - statusT_ > 6.0f) return;
        const float room = ui.screenW() - ui.s(80);
        fit(ui, status_, {ui.screenW() * 0.5f, ui.screenH() - ui.s(26)}, 17, room, statusError_ ? ui.theme().danger : ui.theme().success, FontFace::Bold,
            Align::Center);
    }

    GameServices& g_;
    std::string id_;
    MenuBackground bg_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    Json data_, prefs_;
    bool loaded_ = false, busy_ = false;
    int gen_ = 0;
    int64_t skew_ = 0;
    float timer_ = 0.0f;
    std::string error_;
    Modal modal_ = Modal::None;
    std::string modalError_, instructions_, inviteTag_;
    std::vector<std::pair<std::string, std::string>> leaders_;  // id, username ("" : nobody)
    int leaderIdx_ = 0;
    std::string status_;
    bool statusError_ = false;
    float statusT_ = -100.0f;
};

}  // namespace

std::unique_ptr<Scene> makeRaidEvent(GameServices& g, const std::string& eventId) { return std::make_unique<RaidEventScene>(g, eventId); }

}  // namespace cosmic
