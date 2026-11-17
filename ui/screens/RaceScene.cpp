// « Course à la vérification » of an EXTRÊME level (one life), pushed over the current
// screen: every verified attempt ranked by its progress score (route 40 points, bosses'
// health removed and ambush waves beaten 60 points), the player who first finished the
// level (its verifier) and one's attempts still being checked. Every number comes from
// the server, which computes the scores from the replays (server/src/race.ts).

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/online/Replay.h"
#include "engine/core/I18n.h"
#include "ui/online/OnlineWidgets.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayDownloadUi.h"

namespace cosmic {

namespace {

std::string decimal(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    std::string t = b;
    if (auto p = t.find('.'); p != std::string::npos) t[p] = ',';
    return t;
}

std::string dateFr(const std::string& iso) {
    if (iso.size() < 16) return "—";
    return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4) + " " + iso.substr(11, 5);
}

std::string runTime(int64_t ms) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d", static_cast<int>(ms / 60000), static_cast<int>(ms / 1000 % 60));
    return buf;
}

const Color kRouteColor{90, 200, 255, 255};
const Color kBossColor{255, 140, 60, 255};
const Color kAmbushColor{190, 120, 255, 255};

class RaceScene : public Scene {
public:
    RaceScene(GameServices& g, std::string kind, std::string levelId, int version, std::string levelHash)
        : g_(g), ui_(g), kind_(std::move(kind)), levelId_(std::move(levelId)), version_(version), levelHash_(std::move(levelHash)) {}

    bool isOverlay() const override { return true; }

    void onEnter() override {
        g_.ui.setFocus("race/close");
        load();
    }
    void onResume() override { load(); }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 205});
        float W = std::min(ui.screenW() - ui.s(60), ui.s(1500)), H = std::min(ui.screenH() - ui.s(60), ui.s(980));
        box_ = {(ui.screenW() - W) * 0.5f, (ui.screenH() - H) * 0.5f, W, H};
        ui.r().fillRoundedRect(box_, ui.s(18), Color{9, 14, 32, 248});
        ui.r().drawRoundedRect(box_, ui.s(18), kBossColor.withAlphaF(0.55f), ui.s(1.5f));
        ui.pushId("race");
        UIContext::ButtonStyle cs;
        cs.small = true;
        Rect close{box_.right() - ui.s(170), box_.y + ui.s(22), ui.s(150), ui.s(42)};
        bool leave = ui.button("close", close, "Fermer", cs);
        Rect refresh{close.x - ui.s(200), close.y, ui.s(186), close.h};
        cs.enabled = !loading_;
        if (ui.button("refresh", refresh, loading_ ? "Chargement…" : "Actualiser", cs)) load();
        drawHeader(refresh.x - ui.s(20));
        if (!data_.is_object()) {
            std::string m = loading_ ? "Chargement du classement…" : error_.empty() ? "Classement indisponible." : error_;
            ui.label(m, box_.center(), ui.fitFont(m, FontFace::Body, 20, box_.w - ui.s(80)), loading_ ? ui.theme().textDim : ui.theme().danger,
                     Align::Center, FontFace::Body, VAlign::Middle);
        } else {
            drawBody();
        }
        ReplayDownloadUi::drawTip(g_);  // the download icons' tooltip, over the rest
        if (leave || (ui.backPressed() && !ui.editingText())) g_.app.popScene();
        ui.popId();
        ui.end();
    }

private:
    void load() {
        loading_ = true;
        // 2.62: Solo, Duo or Trio (never mixed): the teams' board for 2 or 3 players.
        std::string q = size_ > 1 ? "/api/v1/coop/board?size=" + std::to_string(size_) + "&kind=" + kind_ + "&level_id=" + levelId_
                                  : "/api/v1/races/board?kind=" + kind_ + "&level_id=" + levelId_;
        if (kind_ == "community" && version_ > 0) q += "&version=" + std::to_string(version_);
        if (kind_ == "official" && !levelHash_.empty()) q += "&level_hash=" + levelHash_;
        int gen = ++generation_;
        g_.online.get(q, OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != generation_) return;
                          loading_ = false;
                          if (!r.ok) {
                              error_ = r.message;
                              if (!data_.is_object()) data_ = Json();
                              return;
                          }
                          error_.clear();
                          data_ = r.data;
                      }),
                      false);
    }

    void drawHeader(float right) {
        UIContext& ui = g_.ui;
        float x = box_.x + ui.s(36), y = box_.y + ui.s(43);
        float bw = ui.extremeBadge({x, y}, ui.s(46));
        float tx = x + bw + ui.s(18);
        const std::string title = "COURSE À LA VÉRIFICATION";
        ui.label(title, {tx, y - ui.s(2)}, ui.fitFont(title, FontFace::Title, 30, right - tx), Color::white(), Align::Left, FontFace::Title, VAlign::Middle);
        std::string sub;
        if (data_.is_object()) {
            const Json& race = data_["race"];
            sub = jstr(race, "title");
            if (kind_ == "community") sub += " · version " + std::to_string(jget<int>(race, "version", 0));
            else sub += " · campagne";
        }
        // 2.62: Solo, Duo, Trio: three boards that never mix, at the right of this line.
        const float tabsW = std::min(ui.s(420), box_.w * 0.4f);
        const Rect tabs{box_.right() - ui.s(36) - tabsW, box_.y + ui.s(74), tabsW, ui.s(38)};
        int mode = size_ - 1;
        if (ui.tabs("size", tabs, {"Solo", "Duo", "Trio"}, mode) && mode + 1 != size_) {
            size_ = mode + 1;
            data_ = Json();
            load();
        }
        if (!sub.empty())
            ui.label(ui.elide(sub, FontFace::Body, 18, tabs.x - ui.s(20) - x), {x, box_.y + ui.s(92)}, 18, ui.theme().textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
    }

    /// 2.62: the Duo / Trio board (docs/COOP.md § 6): teams, their most advanced player's progress.
    void drawTeamBody() {
        UIContext& ui = g_.ui;
        float x = box_.x + ui.s(36), w = box_.w - ui.s(72);
        float y = box_.y + ui.s(126);
        const std::string rules =
            "Une équipe = ses joueurs exacts. Progression = le joueur le plus avancé ; à égalité, le moins de réanimations, puis le plus tôt. "
            "Une équipe incomplète (un joueur parti) passe derrière une équipe complète au même score. Chaque joueur confirme la tentative avec son compte.";
        y += ui.labelWrapped(rules, {x, y}, w, 15, ui.theme().textDim).y + ui.s(14);
        const Json& list = data_.value("ranking", Json::array());
        const float replayW = ui.s(104), gap = ui.s(10);
        const float cDate = x + w - ui.s(14) - replayW - gap;
        const float cRank = x + ui.s(14), cTeam = x + ui.s(84), cScore = x + w * 0.56f, cBar = cScore + ui.s(24);
        const float barW = std::max(ui.s(60), std::min(w * 0.18f, cDate - ui.s(130) - cBar));
        auto head = [&](const std::string& t, float hx, Align a) { ui.label(t, {hx, y + ui.s(10)}, 15, ui.theme().textDim, a, FontFace::Bold, VAlign::Middle); };
        head("RANG", cRank, Align::Left);
        head("ÉQUIPE", cTeam, Align::Left);
        head("SCORE", cScore, Align::Right);
        head("PROGRESSION", cBar, Align::Left);
        head("DATE", cDate, Align::Right);
        y += ui.s(26);
        const float footH = ui.s(46);
        const Rect area{x, y, w, box_.bottom() - footH - ui.s(10) - y};
        const float rh = ui.s(70), rgap = ui.s(6);
        if (list.empty()) {
            const std::string m = size_ == 2 ? "Aucune équipe de deux n'est encore classée sur ce niveau." : "Aucune équipe de trois n'est encore classée sur ce niveau.";
            ui.label(m, {area.center().x, area.y + ui.s(60)}, ui.fitFont(m, FontFace::Body, 19, area.w - ui.s(40)), ui.theme().textDim, Align::Center,
                     FontFace::Body, VAlign::Middle);
        } else {
            ui.beginScroll("teams", area, static_cast<float>(list.size()) * (rh + rgap));
            float cy = area.y - ui.scrollOffset();
            for (const auto& e : list) {
                const Rect row{area.x, cy, area.w - ui.s(14) - replayW - gap, rh};
                cy += rh + rgap;
                const std::string runId = jstr(e, "run_id");
                if (row.bottom() >= area.y && row.y <= area.bottom()) drawTeamRow(e, row, cRank, cTeam, cScore, cBar, barW, cDate);
                UIContext::ButtonStyle vs;
                vs.small = true;
                vs.enabled = jget<bool>(e, "has_replay", false) && !runId.empty() && fetching_.empty();
                const Rect vb{row.right() + gap, row.y + (rh - ui.s(46)) * 0.5f, replayW, ui.s(46)};
                vs.fontSize = ui.fitFont("Voir", FontFace::Bold, 20, replayW - ui.s(20));
                if (ui.button("tv/" + runId, vb, fetching_ == runId && !runId.empty() ? "…" : "Voir", vs)) watchTeam(e);
            }
            ui.endScroll();
        }
        int pending = jget<int>(data_, "my_pending", 0);
        std::string foot = !g_.online.loggedIn() ? "Connectez-vous à Cosmic Online pour voir vos équipes."
                           : pending > 0         ? std::to_string(pending) + (pending > 1 ? " tentatives d'équipe en cours de vérification." : " tentative d'équipe en cours de vérification.")
                                                 : "Le serveur vérifie chaque tentative d'équipe avec les entrées et la trajectoire de chaque joueur.";
        if (!msg_.empty()) foot = msg_;
        ui.label(foot, {x, box_.bottom() - footH * 0.5f - ui.s(6)}, ui.fitFont(foot, FontFace::Body, 16, w), ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
    }

    void drawTeamRow(const Json& e, const Rect& row, float cRank, float cTeam, float cScore, float cBar, float barW, float cDate) {
        UIContext& ui = g_.ui;
        const bool me = jget<bool>(e, "me", false);
        ui.r().fillRoundedRect(row, ui.s(10), me ? Color{18, 48, 74, 255} : Color{10, 17, 36, 255});
        ui.r().drawRoundedRect(row, ui.s(10), me ? ui.theme().accent : Color{22, 32, 63, 255}, std::max(1.0f, ui.s(1.0f)));
        const float cy = row.center().y;
        const int rank = jget<int>(e, "rank", 0);
        const Color rc = rank == 1 ? Color{255, 210, 90, 255} : rank == 2 ? Color{205, 215, 230, 255} : rank == 3 ? Color{205, 127, 50, 255} : ui.theme().textDim;
        ui.label(std::to_string(rank), {cRank + ui.s(20), cy}, rank <= 3 ? 26 : 22, rc, Align::Center, FontFace::Bold, VAlign::Middle);
        std::string names;
        for (const auto& p : e.value("players", Json::array())) names += (names.empty() ? "" : " · ") + jstr(p, "username");
        const std::string tag = jstr(e, "clan_tag");  // 2.63: the clan of all its players (else empty)
        const float room = cScore - ui.s(90) - cTeam;
        {
            i18n::Verbatim theirs;  // players' names
            const std::string line = tag.empty() ? names : "[" + tag + "] " + names;
            ui.label(ui.elide(line, FontFace::Bold, 19, room), {cTeam, cy - ui.s(10)}, 19, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        }
        const int revives = jget<int>(e, "revives", 0);
        std::string note = std::to_string(revives) + (revives > 1 ? " réanimations" : " réanimation");
        if (jget<bool>(e, "finished", false)) note += " · terminé";
        if (jget<bool>(e, "incomplete", false)) note += " · équipe incomplète";
        if (me) note += " · votre équipe";
        ui.label(ui.elide(note, FontFace::Body, 14, room), {cTeam, cy + ui.s(14)}, 14,
                 jget<bool>(e, "incomplete", false) ? Color{255, 180, 84, 255} : ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        const double score = jget<double>(e, "score", 0.0);
        ui.label(decimal(score) + " %", {cScore, cy}, 20, ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
        const Rect bar{cBar, cy - ui.s(4), barW, ui.s(8)};
        ui.r().fillRoundedRect(bar, ui.s(4), Color{4, 7, 18, 255});
        ui.r().fillRoundedRect({bar.x, bar.y, bar.w * static_cast<float>(std::clamp(score / 100.0, 0.0, 1.0)), bar.h}, ui.s(4), kRouteColor);
        ui.label(dateFr(jstr(e, "at")).substr(0, 10), {cDate, cy}, 15, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
    }

    void watchTeam(const Json& e) {
        const std::string runId = jstr(e, "run_id");
        std::string names;
        for (const auto& p : e.value("players", Json::array())) names += (names.empty() ? "" : " · ") + jstr(p, "username");
        const double score = jget<double>(e, "score", 0.0);
        fetching_ = runId;
        msg_ = "Téléchargement du replay…";
        g_.online.getBinary(
            "/api/v1/coop/runs/" + runId + "/replay",
            [this, names, score, alive = std::weak_ptr<int>(life_)](const HttpResponse& r) {
                if (alive.expired()) return;
                fetching_.clear();
                if (r.status != 200 || r.body.empty()) {
                    msg_ = r.networkFailure() ? "Connexion impossible : replay indisponible pour l'instant." : "Replay indisponible (" + std::to_string(r.status) + ").";
                    return;
                }
                std::string err;
                auto rep = ReplayData::decode(std::vector<uint8_t>(r.body.begin(), r.body.end()), &err);
                if (!rep) {
                    msg_ = "Replay illisible : " + err;
                    return;
                }
                msg_.clear();
                g_.app.pushScene(makeLocalReplayViewer(g_, std::move(*rep), "Course à la vérification – " + names + " – " + decimal(score) + " %"));
            },
            16u << 20);
    }

    void drawBody() {
        if (size_ > 1) {
            drawTeamBody();
            return;
        }
        UIContext& ui = g_.ui;
        const Json& race = data_["race"];
        float x = box_.x + ui.s(36), w = box_.w - ui.s(72);
        float y = box_.y + ui.s(118);

        // ------------------------------------------------ the verifier (or the race)
        Rect band{x, y, w, ui.s(66)};
        const Json& ver = data_.value("verifier", Json());
        bool verified = ver.is_object();
        Color bc = verified ? ui.theme().warm : kBossColor;
        ui.r().fillRoundedRect(band, ui.s(12), bc.withAlphaF(0.12f));
        ui.r().drawRoundedRect(band, ui.s(12), bc.withAlphaF(0.6f), std::max(1.0f, ui.s(1.2f)));
        if (verified) {
            const Json& p = ver.value("player", Json());
            std::string name = p.is_object() ? jstr(p, "username") : "Compte supprimé";
            if (p.is_object()) ui_.drawAvatar(jstr(p, "avatar", "avatar_01"), jstr(p, "id"), {band.x + ui.s(14), band.y + ui.s(11), ui.s(44), ui.s(44)});
            float lx = band.x + ui.s(72);
            std::string head = "VÉRIFIÉ PAR " + name;
            std::string when = "Premier à finir le niveau d'une seule traite, le " + dateFr(jstr(ver, "at")) + ".";
            ui.label(ui.elide(head, FontFace::Bold, 21, band.right() - lx - ui.s(16)), {lx, band.y + ui.s(22)}, 21, ui.theme().warm, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            ui.label(when, {lx, band.y + ui.s(47)}, ui.fitFont(when, FontFace::Body, 16, band.right() - lx - ui.s(16)), ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
        } else {
            std::string head = "PAS ENCORE VÉRIFIÉ";
            std::string text = "Le premier joueur qui termine le niveau en devient le vérificateur. En attendant, chaque tentative compte.";
            float lx = band.x + ui.s(20);
            ui.label(head, {lx, band.y + ui.s(22)}, 21, kBossColor, Align::Left, FontFace::Bold, VAlign::Middle);
            ui.label(text, {lx, band.y + ui.s(47)}, ui.fitFont(text, FontFace::Body, 16, band.w - ui.s(40)), ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
        }
        y = band.bottom() + ui.s(16);

        // ------------------------------------------------------- what is counted
        int nb = static_cast<int>(race.value("bosses", Json::array()).size());
        int na = jget<int>(race, "ambushes", 0);
        int np = jget<int>(race, "participants", 0);
        std::string facts = std::to_string(nb) + (nb > 1 ? " boss" : " boss") + "  ·  " + std::to_string(na) + (na > 1 ? " embuscades" : " embuscade") +
                            "  ·  " + std::to_string(np) + (np > 1 ? " participants" : " participant") + "  ·  une seule vie";
        ui.label(facts, {x, y + ui.s(12)}, ui.fitFont(facts, FontFace::Bold, 18, w * 0.5f), ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        // Legend of the bar, right-aligned after the facts.
        float lx = box_.right() - ui.s(36);
        auto legend = [&](const std::string& t, Color c) {
            float tw = ui.text().measure(t, FontFace::Body, ui.s(15)).x;
            lx -= tw;
            ui.label(t, {lx, y + ui.s(12)}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            lx -= ui.s(22);
            ui.r().fillRoundedRect({lx, y + ui.s(5), ui.s(14), ui.s(14)}, ui.s(3), c);
            lx -= ui.s(22);
        };
        if (na > 0) legend("Embuscades (vagues battues)", kAmbushColor);
        if (nb > 0) legend("Boss (PV retirés)", kBossColor);
        legend(nb + na > 0 ? "Parcours (40 pts)" : "Parcours", kRouteColor);
        y += ui.s(38);

        // ---------------------------------------------------------------- columns
        const Json& list = data_.value("ranking", Json::array());
        // Last columns: « Voir », the replay of the attempt (2.32.2), and its download (2.45).
        const float replayW = ui.s(104), replayGap = ui.s(10), dlW = ui.s(46);
        float cDate = x + w - ui.s(14) - replayW - replayGap - dlW - replayGap;
        // The bar stops before the date (170 px, right-aligned in the row).
        float cRank = x + ui.s(14), cPlayer = x + ui.s(84), cScore = x + w * 0.42f, cBar = cScore + ui.s(24);
        float barW = std::max(ui.s(60), std::min(w * 0.30f, cDate - ui.s(14) - ui.s(170) - ui.s(16) - cBar));
        auto head = [&](const std::string& t, float hx, Align a) { ui.label(t, {hx, y + ui.s(10)}, 15, ui.theme().textDim, a, FontFace::Bold, VAlign::Middle); };
        head("RANG", cRank, Align::Left);
        head("JOUEUR", cPlayer, Align::Left);
        head("SCORE", cScore, Align::Right);
        head("PROGRESSION", cBar, Align::Left);
        head("DATE", cDate, Align::Right);
        y += ui.s(26);
        float footH = ui.s(46);
        Rect area{x, y, w, box_.bottom() - footH - ui.s(10) - y};
        float rh = ui.s(70), gap = ui.s(6);
        std::string chosen;
        if (list.empty()) {
            std::string m = "Personne n'est encore classé. Toute tentative compte, même sans arriver au bout.";
            ui.label(m, {area.center().x, area.y + ui.s(60)}, ui.fitFont(m, FontFace::Body, 19, area.w - ui.s(40)), ui.theme().textDim, Align::Center,
                     FontFace::Body, VAlign::Middle);
        } else {
            ui.beginScroll("ranking", area, static_cast<float>(list.size()) * (rh + gap));
            float cy = area.y - ui.scrollOffset();
            for (const auto& e : list) {
                Rect row{area.x, cy, area.w - ui.s(14) - replayW - replayGap - dlW - replayGap, rh};
                cy += rh + gap;
                const std::string runId = jstr(e, "run_id");
                // Off the view, only the buttons (the keyboard and the gamepad reach them).
                const bool visible = row.bottom() >= area.y && row.y <= area.bottom();
                if (visible) drawRow(e, row, cRank, cPlayer, cScore, cBar, barW, chosen);
                UIContext::ButtonStyle vs;
                vs.small = true;
                vs.enabled = jget<bool>(e, "has_replay", false) && !runId.empty() && fetching_.empty();
                Rect vb{row.right() + replayGap, row.y + (rh - ui.s(46)) * 0.5f, replayW, ui.s(46)};
                const std::string vl = fetching_ == runId && !runId.empty() ? "…" : "Voir";
                vs.fontSize = ui.fitFont("Voir", FontFace::Bold, 20, replayW - ui.s(20));
                if (ui.button("v/" + runId, vb, vl, vs)) watch(e);
                Rect db{vb.right() + replayGap, row.y + (rh - ui.s(40)) * 0.5f, dlW, ui.s(40)};
                ReplayDownloadUi::icon(g_, "dl/" + runId, db, raceDownload(e), jget<bool>(e, "has_replay", false) && !runId.empty());
            }
            ui.endScroll();
        }

        // ------------------------------------------------------------------- foot
        int pending = jget<int>(data_, "my_pending", 0);
        std::string foot;
        if (!g_.online.loggedIn()) foot = "Connectez-vous à Cosmic Online pour entrer dans la course.";
        else if (pending > 0) foot = std::to_string(pending) + (pending > 1 ? " de vos tentatives sont en cours de vérification." : " de vos tentatives est en cours de vérification.");
        else foot = "Le serveur recalcule chaque score depuis le replay de la tentative ; les tentatives refusées ne sont pas classées.";
        if (!msg_.empty()) foot = msg_;
        ui.label(foot, {x, box_.bottom() - footH * 0.5f - ui.s(6)}, ui.fitFont(foot, FontFace::Body, 16, w), ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        if (!chosen.empty()) g_.app.pushScene(makePlayerProfile(g_, chosen));
    }

    void drawRow(const Json& e, const Rect& row, float cRank, float cPlayer, float cScore, float cBar, float barW, std::string& chosen) {
        UIContext& ui = g_.ui;
        const Json& p = e.value("player", Json::object());
        std::string pid = jstr(p, "id");
        bool me = jget<bool>(e, "me", false);
        bool finished = jget<bool>(e, "finished", false);
        bool isVerifier = data_["verifier"].is_object() && data_["verifier"].value("player", Json()).is_object() &&
                          jstr(data_["verifier"]["player"], "id") == pid;
        UIContext::ButtonStyle st;
        st.selected = me;
        if (ui.button("p/" + pid, row, "", st)) chosen = pid;
        float cy = row.center().y;
        int rank = jget<int>(e, "rank", 0);
        Color rc = rank == 1 ? Color{255, 210, 90, 255} : rank == 2 ? Color{205, 215, 230, 255} : rank == 3 ? Color{205, 127, 50, 255} : ui.theme().textDim;
        ui.label(std::to_string(rank), {cRank + ui.s(20), cy}, rank <= 3 ? 26 : 22, rc, Align::Center, FontFace::Bold, VAlign::Middle);
        ui_.drawAvatar(jstr(p, "avatar", "avatar_01"), pid, {cPlayer, cy - ui.s(22), ui.s(44), ui.s(44)});
        float nx = cPlayer + ui.s(56);
        // Name, then a chip: the verifier, or a finished attempt.
        std::string chip = isVerifier ? "VÉRIFICATEUR" : finished ? "TERMINÉ" : "";
        float chipW = chip.empty() ? 0.0f : ui.text().measure(chip, FontFace::Bold, ui.s(12)).x + ui.s(18);
        float room = cScore - ui.s(150) - nx - (chipW > 0 ? chipW + ui.s(10) : 0.0f);
        // 2.66: « [TAG] name » (the tag of the player's clan), as the player chose them.
        std::string name = ui_.displayName(p, rank, me);
        Vec2 ns;
        {
            i18n::Verbatim playerWords(me || g_.settings.online.showPseudonyms);
            float nf = ui.fitFont(name, FontFace::Bold, 19, room, 0.8f);
            name = ui.elide(name, FontFace::Bold, nf, room);
            ns = ui.label(name, {nx, cy - ui.s(11)}, nf, me ? ui.theme().accent : ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            ui_.clanTagLink("r" + pid, p, name, {nx, cy - ui.s(11)}, nf, FontFace::Bold, room);  // 2.63.3: the clan's sheet
        }
        if (!chip.empty()) {
            Color cc = isVerifier ? ui.theme().warm : ui.theme().success;
            Rect cr{nx + ns.x + ui.s(10), cy - ui.s(22), chipW, ui.s(22)};
            ui.r().fillRoundedRect(cr, ui.s(11), cc.withAlphaF(0.22f));
            ui.label(chip, cr.center(), 12, cc, Align::Center, FontFace::Bold, VAlign::Middle);
        }
        std::string sub = "temps " + runTime(jget<int64_t>(e, "time_ms", 0));
        ui.label(sub, {nx, cy + ui.s(14)}, 14, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        // Score.
        double score = jget<double>(e, "score", 0.0);
        ui.label(decimal(score) + " %", {cScore, cy}, 24, finished ? ui.theme().success : Color::white(), Align::Right, FontFace::Bold, VAlign::Middle);
        // The bar: what each part brought (route, bosses, ambushes) out of 100 points.
        const Json& d = e.value("detail", Json::object());
        const Json& bosses = d.value("bosses", Json::array());
        const Json& ambush = d.value("ambushes", Json::array());
        double route = jget<double>(e, "route", 0.0);
        double shares = 2.0 * static_cast<double>(bosses.size()) + static_cast<double>(ambush.size());
        double pRoute = 0, pBoss = 0, pAmb = 0, removedSum = 0;
        int wavesDone = 0, wavesAll = 0, bossesBeaten = 0;
        for (const auto& b : bosses) {
            double r = std::clamp(jget<double>(b, "removed", 0.0), 0.0, 1.0);
            removedSum += r;
            if (r >= 0.999) ++bossesBeaten;
        }
        for (const auto& a : ambush) {
            int wv = std::max(1, jget<int>(a, "waves", 1));
            int dn = std::clamp(jget<int>(a, "done", 0), 0, wv);
            wavesDone += dn;
            wavesAll += wv;
            pAmb += static_cast<double>(dn) / wv;
        }
        if (shares > 0) {
            pRoute = 40.0 * route;
            pBoss = 60.0 * 2.0 * removedSum / shares;
            pAmb = 60.0 * pAmb / shares;
        } else {
            pRoute = 100.0 * route;
        }
        Rect bar{cBar, cy - ui.s(15), barW, ui.s(12)};
        ui.r().fillRoundedRect(bar, ui.s(6), Color{24, 32, 58, 255});
        if (finished) {
            ui.r().fillRoundedRect(bar, ui.s(6), ui.theme().success);
        } else {
            float bx = bar.x;
            auto seg = [&](double pts, Color c) {
                float sw = static_cast<float>(std::clamp(pts, 0.0, 100.0) / 100.0) * bar.w;
                if (sw < 0.5f) return;
                ui.r().fillRect({bx, bar.y, std::min(sw, bar.right() - bx), bar.h}, c);
                bx += sw;
            };
            seg(pRoute, kRouteColor);
            seg(pBoss, kBossColor);
            seg(pAmb, kAmbushColor);
        }
        // Detail under the bar.
        std::string det = "Parcours " + decimal(route * 100.0) + " %";
        if (!bosses.empty()) {
            det += "  ·  Boss " + std::to_string(bossesBeaten) + "/" + std::to_string(bosses.size());
            det += " (PV retirés " + decimal(100.0 * removedSum / static_cast<double>(bosses.size())) + " %)";
        }
        if (!ambush.empty()) det += "  ·  Vagues " + std::to_string(wavesDone) + "/" + std::to_string(wavesAll);
        float dateW = ui.s(170);
        float detRoom = row.right() - ui.s(14) - dateW - cBar;
        ui.label(ui.elide(det, FontFace::Body, ui.fitFont(det, FontFace::Body, 14, detRoom, 0.85f), detRoom), {cBar, cy + ui.s(14)},
                 ui.fitFont(det, FontFace::Body, 14, detRoom, 0.85f), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        // Date.
        std::string date = dateFr(jstr(e, "at"));
        ui.label(date, {row.right() - ui.s(14), cy}, ui.fitFont(date, FontFace::Body, 15, dateW - ui.s(10)), ui.theme().textDim, Align::Right, FontFace::Body,
                 VAlign::Middle);
    }

    /// « Télécharger » (2.45): an attempt's replay kept on this computer, with its score.
    ReplayDownloadRequest raceDownload(const Json& e) {
        const std::string runId = jstr(e, "run_id");
        const Json& p = jfield(e, "player", Json::object());
        const std::string who = jstr(p, "username");
        const LevelEntry* lvl = kind_ == "official" ? g_.content.level(levelId_) : nullptr;
        std::string name = lvl ? lvl->name : std::string();
        if (name.empty() && data_.is_object()) name = jstr(jfield(data_, "race", Json::object()), "title");
        const std::string shown = who.empty() ? std::string() : ui_.displayName(who, jget<int>(e, "rank", 0), jget<bool>(e, "me", false));
        // A community level's replay names the level as installed on its player's computer.
        return ReplayDownloadUi::request("race", runId, "/api/v1/races/runs/" + runId + "/replay", who, shown, lvl ? levelId_ : std::string(), name,
                                         "Course à la vérification – " + decimal(jget<double>(e, "score", 0.0)) + " %", jget<int64_t>(e, "time_ms", 0),
                                         jstr(e, "at"));
    }

    /// Downloads the replay of a ranked attempt, then plays it (the level must be installed).
    void watch(const Json& e) {
        const std::string runId = jstr(e, "run_id");
        const Json& p = jfield(e, "player", Json::object());
        std::string who = jstr(p, "username");
        double score = jget<double>(e, "score", 0.0);
        // 2.45: downloaded on this computer, it plays from its copy (no network needed).
        if (auto rep = ReplayDownloadUi::localCopy(g_, ReplayLibrary::makeId("race", runId))) {
            g_.app.pushScene(makeLocalReplayViewer(g_, std::move(*rep), "Course à la vérification – " + who + " – " + decimal(score) + " %"));
            return;
        }
        fetching_ = runId;
        msg_ = "Téléchargement du replay…";
        g_.online.getBinary(
            "/api/v1/races/runs/" + runId + "/replay",
            [this, who, score, alive = std::weak_ptr<int>(life_)](const HttpResponse& r) {
                if (alive.expired()) return;
                fetching_.clear();
                if (r.status != 200 || r.body.empty()) {
                    msg_ = r.status == 403 ? "Ce joueur a masqué son profil : replay indisponible."
                         : r.networkFailure() ? "Connexion impossible : replay indisponible pour l'instant."
                                              : "Replay indisponible (" + std::to_string(r.status) + ").";
                    return;
                }
                std::string err;
                auto rep = ReplayData::decode(std::vector<uint8_t>(r.body.begin(), r.body.end()), &err);
                if (!rep) {
                    msg_ = "Replay illisible : " + err;
                    return;
                }
                msg_.clear();
                g_.app.pushScene(makeLocalReplayViewer(g_, std::move(*rep), "Course à la vérification – " + who + " – " + decimal(score) + " %"));
            },
            16u << 20);
    }

    GameServices& g_;
    OnlineUi ui_;
    std::string fetching_, msg_;  // the attempt whose replay is downloading; the foot's message
    std::string kind_, levelId_;
    int version_ = 0;
    std::string levelHash_;
    Rect box_;
    Json data_;
    std::string error_;
    bool loading_ = false;
    int generation_ = 0;
    int size_ = 1;  // 2.62: 1 Solo, 2 Duo, 3 Trio
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace

std::unique_ptr<Scene> makeRaceBoard(GameServices& g, const std::string& kind, const std::string& levelId, int version, const std::string& levelHash) {
    return std::make_unique<RaceScene>(g, kind, levelId, version, levelHash);
}

}  // namespace cosmic
