// 2.68: the results of a clan raid (docs/RAIDS.md §8, maquette « Résultats »), opened at the end
// of the fight against Pyroclast and from the sheet of an event. Everything shown comes from the
// server (GET /raids/events/{id}/results), from tables its consensus accepted only: the clans
// (their players' contribution points), the players in every category (contribution, damage,
// damage avoided, support, objectives, gigaboss down, time, rescues, progression: never only the
// damage), the fights against the gigaboss. A table of my instance still waiting for its majority
// is said « en attente de validation » and counts for nothing yet; while the event runs the
// ranking is « en cours », final only once it ended. At the bottom, my rewards of this event with
// « Tout récupérer » (granted once by the server, POST /raids/rewards/claim).
//
// Keyboard, gamepad and mouse: the category chips, the pages of the ranking, « Tout récupérer » and
// « Retour » are buttons (focus, Entrée / A); Échap / B goes back. Every text is measured into its
// room (raidui::fit, wrap): nothing overlaps.

#include <algorithm>
#include <string>
#include <vector>

#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/Raids.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

using namespace raidui;

const Color kMine{42, 42, 90, 250};
const Color kRow{22, 29, 66, 245};
const Color kPending{255, 196, 92, 255};

class RaidResultsScene : public Scene {
public:
    RaidResultsScene(GameServices& g, std::string eventId) : g_(g), eventId_(std::move(eventId)) {}

    void onEnter() override {
        bg_.init(g_);
        g_.ui.setFocus("results/back");
        load();
    }
    void onResume() override { load(); }

    void update(float dt) override {
        bg_.update(dt);
        timer_ += dt;
        // Until the event is over (or a table of mine is waiting), the ranking may move: every 15 s.
        if (g_.app.topScene() == this && timer_ >= 15.0f && (!res_.final || res_.pending)) load();
    }

    void load() {
        timer_ = 0.0f;
        const int gen = ++gen_;
        g_.online.get("/api/v1/raids/events/" + eventId_ + "/results", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != gen_) return;
                          if (!r.ok) {
                              error_ = r.message;
                              return;
                          }
                          res_ = raids::parseResults(r.data);
                          rememberClaimed(r.data.value("rewards_claimed", Json::array()));
                          loaded_ = true;
                          error_.clear();
                      }),
                      false);
    }

    /// The rewards this account claimed: the raid badges unlock from them (kept for offline play).
    void rememberClaimed(const Json& list) {
        if (!list.is_array()) return;
        std::set<std::string> ids;
        for (const Json& v : list)
            if (v.is_string() && v.get<std::string>().size() <= 64) ids.insert(v.get<std::string>());
        if (ids == g_.collection.raidRewards) return;
        g_.collection.raidRewards = std::move(ids);
        g_.collection.save();
        g_.checkAchievements();
    }

    void claimAll() {
        if (claiming_) return;
        claiming_ = true;
        g_.online.send("POST", "/api/v1/raids/rewards/claim", Json::object(), OnlineService::guard(life_, [this](const ApiResult& r) {
                           claiming_ = false;
                           if (!r.ok) {
                               note_ = "Récompenses non récupérées : " + r.message;
                               return;
                           }
                           const int n = jget<int>(r.data, "claimed", 0);
                           note_ = n > 0 ? (n > 1 ? std::to_string(n) + " récompenses récupérées : elles sont dans votre profil et le vestiaire."
                                                  : "Récompense récupérée : elle est dans votre profil et le vestiaire.")
                                         : "Déjà récupérées.";
                           rememberClaimed(r.data.value("claimed_rewards", Json::array()));
                           g_.challenges.refreshMine();  // the items won (GET /players/me/rewards) follow
                           load();
                       }));
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{12, 10, 30, 160});
        ui.inputEnabled = g_.app.topScene() == this;
        ui.pushId("results");
        bool back = false;
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(26);
        const Rect page{m, ui.s(20), W - 2 * m, H - ui.s(40)};
        // The header: « Retour », the title, the state at the right (measured first).
        const float bw = textW(ui, "‹ Retour", 16) + ui.s(36);
        if (ui.button("back", {page.x, page.y, bw, ui.s(44)}, "‹ Retour")) back = true;
        const float hx = page.x + bw + ui.s(16);
        std::string state = !loaded_ ? "" : res_.final ? "Classement final" : "Classement en cours · résultats validés par le serveur";
        const float sw = std::min(textW(ui, state, 14), (page.right() - hx) * 0.45f);
        if (!state.empty()) fit(ui, state, {page.right(), page.y + ui.s(22)}, 14, sw, res_.final ? kGreen : kGold, FontFace::Bold, Align::Right);
        fit(ui, "Résultats du raid · Asterion", {hx, page.y + ui.s(22)}, 26, page.right() - hx - sw - ui.s(16), kText, FontFace::Title);
        if (!loaded_) {
            fit(ui, error_.empty() ? "Chargement des résultats…" : error_, {page.x, page.y + ui.s(80)}, 17, page.w, error_.empty() ? kDim : ui.theme().danger);
        } else {
            float y = page.y + ui.s(60);
            y = drawSummary({page.x, y, page.w, ui.s(74)}) + ui.s(12);
            // The rewards at the bottom (their height first), the two columns in between.
            const float rewardsH = ui.s(118);
            const Rect rewards{page.x, page.bottom() - rewardsH, page.w, rewardsH};
            const float colsH = rewards.y - ui.s(12) - y;
            const float leftW = std::min(ui.s(470), page.w * 0.36f);
            drawClans({page.x, y, leftW, colsH});
            drawPlayers({page.x + leftW + ui.s(14), y, page.w - leftW - ui.s(14), colsH});
            drawRewards(rewards);
        }
        ui.popId();
        if (!back && ui.backPressed()) back = true;
        ui.end();
        if (back) g_.app.popScene();
    }

    /// Four figures: the gigaboss, the clans, the participants, my rank. Returns the bottom.
    float drawSummary(const Rect& r) {
        UIContext& ui = g_.ui;
        const float gap = ui.s(10), cw = (r.w - 3 * gap) / 4.0f;
        struct Fig {
            std::string caption, value;
            Color c;
        };
        const Fig figs[4] = {
            // 2.69: several gigaboss in an event (Pyroclast, Skullmaw, Dreadfang, Heartbreaker…).
            {"Gigaboss",
             res_.defeatedBosses.size() == 1 ? res_.defeatedBosses.front() + " vaincu"
             : res_.defeatedBosses.size() > 1 ? std::to_string(res_.defeatedBosses.size()) + " gigaboss vaincus"
             : res_.defeated                  ? "Gigaboss vaincu"
             : res_.fights.empty()            ? "Gigaboss intacts"
                                              : "Gigaboss au combat",
             res_.defeated ? kGreen : kOrange},
            {"Clans classés", std::to_string(res_.clans.size()), kText},
            {"Participants validés", std::to_string(res_.players.size()), kText},
            {"Votre rang", res_.myRank > 0 ? "#" + std::to_string(res_.myRank) : res_.pending ? "en attente" : "—", res_.myRank > 0 ? kGold : kDim},
        };
        for (int i = 0; i < 4; ++i) {
            const Rect c{r.x + i * (cw + gap), r.y, cw, r.h};
            ui.r().fillRoundedRect(c, ui.s(12), kCard);
            fit(ui, figs[i].caption, {c.x + ui.s(14), c.y + ui.s(20)}, 13, c.w - ui.s(28), kDim);
            fit(ui, figs[i].value, {c.x + ui.s(14), c.y + ui.s(50)}, 22, c.w - ui.s(28), figs[i].c, FontFace::Title);
        }
        return r.bottom();
    }

    void drawClans(const Rect& r) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(14), kPanel);
        float y = section(ui, r, "CLASSEMENT DES CLANS", kSky);
        const float x = r.x + ui.s(14), w = r.w - ui.s(28);
        const auto note = wrap(ui, "Points de contribution calculés par le serveur (objectifs, gigaboss, soutien, dégâts), pas seulement les dégâts.", FontFace::Body,
                               13, w, 2);
        y += drawLines(ui, note, {x, y}, 13, ui.s(18), kDim) + ui.s(8);
        // The fights against the gigaboss at the bottom (their lines first).
        std::vector<std::string> fights = res_.fights;
        if (fights.size() > 4) fights.resize(4);
        const float fightsH = fights.empty() ? 0.0f : ui.s(30) + static_cast<float>(fights.size()) * ui.s(22);
        const float listBottom = r.bottom() - ui.s(12) - fightsH;
        const float rowH = ui.s(40), gap = ui.s(6);
        if (res_.clans.empty())
            drawLines(ui, wrap(ui, "Aucun résultat validé pour l'instant : les tableaux arrivent quand la majorité de chaque instance les confirme.", FontFace::Body, 15, w,
                               3),
                      {x, y}, 15, ui.s(22), kDim);
        for (const raids::ResultClan& c : res_.clans) {
            if (y + rowH > listBottom) break;
            const Rect row{x, y, w, rowH};
            ui.r().fillRoundedRect(row, ui.s(8), kRow);
            const std::string pts = raids::groupDigits(c.points) + " pts";
            const float pw = std::min(textW(ui, pts, 15, FontFace::Bold), w * 0.4f);
            fit(ui, std::to_string(c.rank), {row.x + ui.s(12), row.center().y}, 16, ui.s(30), kGold, FontFace::Title);
            fit(ui, pts, {row.right() - ui.s(12), row.center().y}, 15, pw, kText, FontFace::Bold, Align::Right);
            fit(ui, "[" + c.tag + "] · " + std::to_string(c.players) + (c.players > 1 ? " joueurs" : " joueur") + (c.gigaboss ? " · gigaboss ✓" : ""),
                {row.x + ui.s(46), row.center().y}, 15, row.w - ui.s(46) - pw - ui.s(24), kText, FontFace::Bold, Align::Left, false);
            y += rowH + gap;
        }
        if (!fights.empty()) {
            float fy = r.bottom() - ui.s(12) - fightsH;
            fit(ui, "COMBATS CONTRE PYROCLAST", {x, fy + ui.s(12)}, 13, w, kPeach, FontFace::Bold);
            fy += ui.s(30);
            for (const std::string& f : fights) {
                fit(ui, f, {x, fy + ui.s(9)}, 14, w, kText, FontFace::Body, Align::Left, true);
                fy += ui.s(22);
            }
        }
    }

    void drawPlayers(const Rect& r) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(14), kPanel);
        float y = section(ui, r, "CLASSEMENT INDIVIDUEL", kSky);
        const float x = r.x + ui.s(14), w = r.w - ui.s(28);
        // The categories (chips that wrap): never only the damage.
        const auto& cats = raids::resultCategories();
        std::vector<std::string> labels;
        std::vector<bool> on;
        for (size_t i = 0; i < cats.size(); ++i) {
            labels.push_back(cats[i].label);
            on.push_back(static_cast<int>(i) == cat_);
        }
        float bottom = y;
        const int clicked = chips(ui, "cat", x, y, x + w, labels, on, bottom);
        if (clicked >= 0) {
            cat_ = clicked;
            page_ = 0;
        }
        y = bottom + ui.s(10);
        if (res_.pending) {
            const std::string t = "Votre dernier tableau est en attente de validation (" + std::to_string(res_.pendingReports) +
                                  " rapport(s) reçu(s)) : il compte dès que la majorité de votre instance l'a confirmé.";
            const auto lines = wrap(ui, t, FontFace::Bold, 14, w - ui.s(20), 2);
            const float h = static_cast<float>(lines.size()) * ui.s(20) + ui.s(12);
            ui.r().fillRoundedRect({x, y, w, h}, ui.s(8), Color{70, 50, 14, 235});
            drawLines(ui, lines, {x + ui.s(10), y + ui.s(6)}, 14, ui.s(20), kPending, FontFace::Bold);
            y += h + ui.s(8);
        }
        const std::string cat = cats[static_cast<size_t>(std::clamp(cat_, 0, static_cast<int>(cats.size()) - 1))].id;
        const auto ranked = raids::rankedBy(res_, cat);
        // The pages' buttons at the bottom when the ranking is longer than the room.
        const float navH = ui.s(40);
        const float rowH = ui.s(34), gap = ui.s(4);
        const float headH = ui.s(24);
        const float listTop = y + headH;
        const int fitRows = std::max(1, static_cast<int>((r.bottom() - ui.s(12) - navH - ui.s(8) - listTop) / (rowH + gap)));
        const int pages = std::max(1, (static_cast<int>(ranked.size()) + fitRows - 1) / fitRows);
        page_ = std::clamp(page_, 0, pages - 1);
        // Columns: rank, player, the category's value, the contribution.
        const float rankW = ui.s(44), scoreW = std::min(ui.s(110), w * 0.18f), valueW = cat == "score" ? 0.0f : std::min(ui.s(300), w * 0.38f);
        const float nameW = w - rankW - scoreW - valueW - ui.s(24);
        fit(ui, "#", {x + ui.s(6), y + ui.s(10)}, 12, rankW, kDim, FontFace::Bold);
        fit(ui, "Joueur", {x + rankW, y + ui.s(10)}, 12, nameW, kDim, FontFace::Bold);
        if (valueW > 0.0f) fit(ui, cats[static_cast<size_t>(cat_)].label, {x + rankW + nameW + ui.s(12), y + ui.s(10)}, 12, valueW, kDim, FontFace::Bold);
        fit(ui, "Contrib.", {x + w - ui.s(6), y + ui.s(10)}, 12, scoreW, kDim, FontFace::Bold, Align::Right);
        float ry = listTop;
        if (ranked.empty())
            drawLines(ui, wrap(ui, "Aucun joueur classé : aucun tableau de contributions n'a encore été validé.", FontFace::Body, 15, w, 2), {x, ry + ui.s(6)}, 15,
                      ui.s(22), kDim);
        for (int i = page_ * fitRows; i < std::min(static_cast<int>(ranked.size()), (page_ + 1) * fitRows); ++i) {
            const raids::ResultPlayer& p = ranked[static_cast<size_t>(i)];
            const Rect row{x, ry, w, rowH};
            ui.r().fillRoundedRect(row, ui.s(8), p.me ? kMine : kRow);
            fit(ui, std::to_string(p.rank), {row.x + ui.s(8), row.center().y}, 15, rankW - ui.s(10), p.rank <= 3 ? kGold : kText, FontFace::Bold);
            const std::string name = p.username + (p.clanTag.empty() ? "" : " [" + p.clanTag + "]");
            fit(ui, name, {row.x + rankW, row.center().y}, 15, nameW, p.me ? kGold : kText, p.me ? FontFace::Bold : FontFace::Body, Align::Left, true);
            if (valueW > 0.0f) fit(ui, raids::categoryText(p, cat), {row.x + rankW + nameW + ui.s(12), row.center().y}, 14, valueW, kText);
            fit(ui, raids::groupDigits(p.score), {row.right() - ui.s(8), row.center().y}, 15, scoreW, kText, FontFace::Bold, Align::Right);
            ry += rowH + gap;
        }
        if (pages > 1) {
            const float by = r.bottom() - ui.s(12) - navH;
            const std::string label = "Page " + std::to_string(page_ + 1) + " / " + std::to_string(pages);
            const float lw = textW(ui, label, 14) + ui.s(20);
            const float bw = std::min(ui.s(160), (w - lw - ui.s(20)) * 0.5f);
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = page_ > 0;
            if (ui.button("prev", {x, by, bw, navH}, "‹ Précédents", st)) --page_;
            st.enabled = page_ + 1 < pages;
            if (ui.button("next", {x + w - bw, by, bw, navH}, "Suivants ›", st)) ++page_;
            fit(ui, label, {x + w * 0.5f, by + navH * 0.5f}, 14, w - 2 * bw - ui.s(20), kDim, FontFace::Body, Align::Center);
        }
    }

    void drawRewards(const Rect& r) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(14), Color{42, 36, 20, 245});
        ui.r().drawRoundedRect(r, ui.s(14), kGold.withAlphaF(0.6f), ui.s(1));
        const float x = r.x + ui.s(16);
        // The button at the right (its width first), the rewards and the note in the room left.
        const std::string label = res_.rewards.size() > 1 ? "Tout récupérer" : "Récupérer";
        const float bw = res_.rewards.empty() ? 0.0f : std::min(textW(ui, label, 16, FontFace::Bold) + ui.s(44), r.w * 0.3f);
        const float room = r.right() - ui.s(16) - (bw > 0.0f ? bw + ui.s(16) : 0.0f) - x;
        fit(ui, "VOS RÉCOMPENSES", {x, r.y + ui.s(20)}, 15, room, kGold, FontFace::Title);
        float px = x;
        const float cy = r.y + ui.s(52);
        if (res_.rewards.empty()) {
            fit(ui, "Aucune récompense à récupérer pour cet événement.", {x, cy}, 15, room, kText, FontFace::Bold);
        } else {
            for (const raids::Reward& w : res_.rewards) {
                const float pw = pill(ui, px, cy, w.name, Color{120, 96, 44, 255}, kText, x + room, 14);
                if (pw <= 0.0f) break;
                px += pw + ui.s(8);
            }
        }
        const std::string t = !note_.empty() ? note_
                                             : "Accordées une seule fois par le serveur, à partir des résultats validés, même après une reconnexion. Une fois "
                                               "récupérées : badge au profil, titres dans le vestiaire.";
        drawLines(ui, wrap(ui, t, FontFace::Body, 13, room, 2), {x, r.y + ui.s(70)}, 13, ui.s(18), Color{232, 220, 184, 255});
        if (bw > 0.0f) {
            UIContext::ButtonStyle bs;
            bs.primary = true;
            bs.enabled = !claiming_;
            if (ui.button("claim", {r.right() - ui.s(16) - bw, r.center().y - ui.s(26), bw, ui.s(52)}, label, bs)) claimAll();
        }
    }

    GameServices& g_;
    std::string eventId_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    MenuBackground bg_;
    raids::Results res_;
    bool loaded_ = false, claiming_ = false;
    std::string error_, note_;
    float timer_ = 0.0f;
    int gen_ = 0;
    int cat_ = 0, page_ = 0;
};

}  // namespace

std::unique_ptr<Scene> makeRaidResults(GameServices& g, const std::string& eventId) { return std::make_unique<RaidResultsScene>(g, eventId); }

}  // namespace cosmic
