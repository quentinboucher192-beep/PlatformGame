// 2.69: the world rankings of a world event (docs/RAIDS.md §14, spec §11, maquettes « Résultats »
// and « Fin »), opened from the sheet of the event and from the notification of its official
// result. Everything shown comes from the server (GET /raids/events/{id}/world), from validated
// results only: the world's progress and the final result, the ranking of every clan, of the groups
// (instances), of the players in every category (never the damage alone), my clan's page. What
// still waits for the majority of its instance is said apart (« en attente de validation ») and is
// never in a ranking; while the event runs the ranking is « en cours », « Résultat officiel » once
// the server froze it. A catch-up session shows its own ranking and the official result it never
// changes. At the bottom, my rewards of this event (« Tout récupérer »: granted once by the server).
//
// Keyboard, gamepad and mouse: the tabs, the categories, the pages, « Tout récupérer » and « Retour »
// are buttons (focus, Entrée / A); Échap / B goes back. Every text is measured into its column or
// its room (raidui::fit, wrap): nothing overlaps.

#include <algorithm>
#include <set>
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

/// A column of a table: its header, its share of the width, its alignment.
struct Column {
    std::string header;
    float weight;
    Align align;
};
struct Row {
    std::vector<std::string> cells;
    bool mine = false;
    bool verbatim = false;  // a player's or a clan's name: never translated
};

class RaidWorldScene : public Scene {
public:
    RaidWorldScene(GameServices& g, std::string eventId) : g_(g), eventId_(std::move(eventId)) {}

    void onEnter() override {
        bg_.init(g_);
        g_.ui.setFocus("world/back");
        load();
    }
    void onResume() override { load(); }

    void update(float dt) override {
        bg_.update(dt);
        timer_ += dt;
        // Until the official result is frozen, the ranking may move: read again every 15 s.
        if (g_.app.topScene() == this && timer_ >= 15.0f && (!w_.final || w_.myPending)) load();
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{12, 10, 30, 160});
        ui.inputEnabled = g_.app.topScene() == this;
        ui.pushId("world");
        bool back = false;
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(26);
        const Rect page{m, ui.s(20), W - 2 * m, H - ui.s(40)};
        // The header: « Retour », the title, the state of the ranking at the right (measured first).
        const float bw = textW(ui, "‹ Retour", 16) + ui.s(36);
        if (ui.button("back", {page.x, page.y, bw, ui.s(44)}, "‹ Retour")) back = true;
        const float hx = page.x + bw + ui.s(16);
        const std::string state = loaded_ ? raids::rankingStateText(w_) : "";
        const float sw = std::min(textW(ui, state, 14, FontFace::Bold), (page.right() - hx) * 0.5f);
        if (!state.empty()) fit(ui, state, {page.right(), page.y + ui.s(22)}, 14, sw, w_.final ? kGreen : kGold, FontFace::Bold, Align::Right);
        const std::string title = w_.catchup ? "Session de rattrapage · Asterion" : "Classement mondial · Asterion";
        fit(ui, title, {hx, page.y + ui.s(22)}, 26, page.right() - hx - sw - ui.s(16), kText, FontFace::Title);
        if (!loaded_) {
            fit(ui, error_.empty() ? "Chargement du classement mondial…" : error_, {page.x, page.y + ui.s(80)}, 17, page.w, error_.empty() ? kDim : ui.theme().danger);
        } else {
            float y = page.y + ui.s(58);
            y = drawOutcome({page.x, y, page.w, ui.s(70)}) + ui.s(10);
            y = drawFigures({page.x, y, page.w, ui.s(72)}) + ui.s(10);
            // The rewards at the bottom (their height first), the rankings in between.
            const float rewardsH = ui.s(104);
            const Rect rewards{page.x, page.bottom() - rewardsH, page.w, rewardsH};
            drawRankings({page.x, y, page.w, rewards.y - ui.s(10) - y});
            drawRewards(rewards);
        }
        ui.popId();
        if (!back && ui.backPressed()) back = true;
        ui.end();
        if (back) g_.app.popScene();
    }

private:
    void load() {
        timer_ = 0.0f;
        const int gen = ++gen_;
        g_.online.get("/api/v1/raids/events/" + eventId_ + "/world", OnlineService::guard(life_, [this, gen](const ApiResult& r) {
                          if (gen != gen_) return;
                          if (!r.ok) {
                              error_ = r.message;
                              return;
                          }
                          w_ = raids::parseWorld(r.data);
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

    /// The final result (or the progress so far) and my clan's place, in a banner. Returns its bottom.
    float drawOutcome(const Rect& r) {
        UIContext& ui = g_.ui;
        const bool fell = w_.outcome == "vaincu";
        ui.r().fillRoundedRectGradient(r, ui.s(14), fell ? Color{70, 52, 20, 245} : Color{58, 21, 48, 245}, Color{18, 24, 58, 245});
        ui.r().drawRoundedRect(r, ui.s(14), fell ? kGold : kOrange, ui.s(1.5f));
        const float x = r.x + ui.s(18);
        // My clan at the right (measured first), the result in the room left.
        std::string mine;
        if (w_.myClanRank > 0)
            mine = "[" + w_.myClanTag + "] · " + raids::ordinal(w_.myClanRank) + (w_.final ? " mondial" : " pour l'instant") + " · " + raids::groupDigits(w_.myClanPoints) +
                   " pts";
        else if (!w_.myClanTag.empty())
            mine = "[" + w_.myClanTag + "] · pas encore classé";
        const float mw = mine.empty() ? 0.0f : std::min(textW(ui, mine, 16, FontFace::Bold), r.w * 0.4f);
        if (!mine.empty()) fit(ui, mine, {r.right() - ui.s(18), r.center().y}, 16, mw, kGold, FontFace::Bold, Align::Right);
        const float room = r.w - ui.s(36) - (mw > 0.0f ? mw + ui.s(18) : 0.0f);
        std::string big = w_.outcomeText;
        if (w_.catchup) big = "Rattrapage · " + big;
        fit(ui, big, {x, r.y + ui.s(24)}, 24, room, fell ? kGold : kPeach, FontFace::Title);
        std::string sub = raids::worldProgressLine(w_);
        if (w_.catchup && !w_.officialText.empty())
            sub += " · résultat officiel : " + w_.officialText + " (" + std::to_string(w_.officialProgress) + " %)";
        fit(ui, sub, {x, r.y + ui.s(52)}, 14, room, kText);
        return r.bottom();
    }

    /// Five figures: progress, gigaboss, zones, participants, my rank. Returns the bottom.
    float drawFigures(const Rect& r) {
        UIContext& ui = g_.ui;
        const float gap = ui.s(10), cw = (r.w - 4 * gap) / 5.0f;
        struct Fig {
            std::string caption, value;
            Color c;
        };
        const Fig figs[5] = {
            {"Progression mondiale", std::to_string(w_.progress) + " %", kGold},
            {"Gigaboss vaincus", std::to_string(w_.gigabossDefeated) + " / " + std::to_string(w_.gigaboss), w_.gigabossDefeated > 0 ? kGreen : kText},
            {"Zones contrôlées", std::to_string(w_.zonesControlled) + " / " + std::to_string(w_.zones), kText},
            {"Participants validés", std::to_string(w_.clansPlaying) + " clans · " + std::to_string(w_.participants) + " joueurs", kText},
            {"Votre rang", w_.myRank > 0 ? "#" + std::to_string(w_.myRank) : w_.myPending ? "en attente" : "—", w_.myRank > 0 ? kGold : kDim},
        };
        for (int i = 0; i < 5; ++i) {
            const Rect c{r.x + static_cast<float>(i) * (cw + gap), r.y, cw, r.h};
            ui.r().fillRoundedRect(c, ui.s(12), kCard);
            fit(ui, figs[i].caption, {c.x + ui.s(12), c.y + ui.s(20)}, 13, c.w - ui.s(24), kDim);
            fit(ui, figs[i].value, {c.x + ui.s(12), c.y + ui.s(48)}, 20, c.w - ui.s(24), figs[i].c, FontFace::Title);
        }
        return r.bottom();
    }

    void drawRankings(const Rect& r) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(14), kPanel);
        const float x = r.x + ui.s(14), w = r.w - ui.s(28);
        float y = r.y + ui.s(12);
        // The tabs (chips that wrap), then the categories of the individual ranking.
        static const std::vector<std::string> kTabs = {"Clans", "Groupes", "Individuel", "Mon clan"};
        std::vector<bool> on(kTabs.size(), false);
        tab_ = std::clamp(tab_, 0, static_cast<int>(kTabs.size()) - 1);
        on[static_cast<size_t>(tab_)] = true;
        float bottom = y;
        const int t = chips(ui, "tab", x, y, x + w, kTabs, on, bottom);
        if (t >= 0 && t != tab_) {
            tab_ = t;
            page_ = 0;
        }
        y = bottom + ui.s(8);
        const auto& cats = raids::resultCategories();
        if (tab_ == 2 || tab_ == 3) {
            std::vector<std::string> labels;
            std::vector<bool> con;
            for (size_t i = 0; i < cats.size(); ++i) {
                labels.push_back(cats[i].label);
                con.push_back(static_cast<int>(i) == cat_);
            }
            const int c = chips(ui, "cat", x, y, x + w, labels, con, bottom);
            if (c >= 0) {
                cat_ = c;
                page_ = 0;
            }
            y = bottom + ui.s(8);
        }
        // What waits for its majority: apart, above the ranking, never in it.
        if (const std::string p = raids::pendingText(w_); !p.empty()) {
            const auto lines = wrap(ui, p, FontFace::Bold, 14, w - ui.s(20), 2);
            const float h = static_cast<float>(lines.size()) * ui.s(20) + ui.s(12);
            ui.r().fillRoundedRect({x, y, w, h}, ui.s(8), Color{70, 50, 14, 235});
            drawLines(ui, lines, {x + ui.s(10), y + ui.s(6)}, 14, ui.s(20), kPending, FontFace::Bold);
            y += h + ui.s(8);
        }
        const Rect body{x, y, w, r.bottom() - ui.s(12) - y};
        std::vector<Column> cols;
        std::vector<Row> rows;
        std::string empty;
        const std::string cat = cats[static_cast<size_t>(std::clamp(cat_, 0, static_cast<int>(cats.size()) - 1))].id;
        if (tab_ == 0) {
            cols = {{"#", 0.6f, Align::Left}, {"Clan", 3.2f, Align::Left},   {"Points", 1.4f, Align::Right},  {"Joueurs", 1.1f, Align::Right},
                    {"Objectifs (en premier)", 2.0f, Align::Right}, {"Gigaboss", 1.0f, Align::Right}};
            for (const raids::WorldClan& c : w_.clans) {
                Row row;
                row.cells = {std::to_string(c.rank), "[" + c.tag + "] " + c.name, raids::groupDigits(c.points), std::to_string(c.players) + " / " + std::to_string(c.registered),
                             std::to_string(c.objectives) + " (" + std::to_string(c.first) + ")", std::to_string(c.gigaboss)};
                row.mine = c.mine;
                row.verbatim = true;
                rows.push_back(std::move(row));
            }
            empty = "Aucun clan inscrit à cet événement.";
        } else if (tab_ == 1) {
            cols = {{"#", 0.6f, Align::Left},       {"Groupe", 1.6f, Align::Left},  {"Branche", 2.0f, Align::Left},  {"Points", 1.3f, Align::Right},
                    {"Joueurs", 1.0f, Align::Right}, {"Objectifs", 1.1f, Align::Right}, {"Gigaboss", 1.3f, Align::Right}};
            for (const raids::WorldGroup& gr : w_.groups) {
                Row row;
                row.cells = {std::to_string(gr.rank), "[" + gr.clanTag + "]·" + std::to_string(gr.group), gr.branchName, raids::groupDigits(gr.points),
                             std::to_string(gr.players), std::to_string(gr.objectives),
                             gr.bossPhase >= 4 ? std::string("vaincu") : gr.bossPhase > 0 ? "phase " + std::to_string(gr.bossPhase) + " / 3" : std::string("—")};
                row.mine = gr.me || gr.mine;
                row.verbatim = false;
                rows.push_back(std::move(row));
            }
            empty = "Aucun groupe n'est encore entré en jeu.";
        } else {
            std::vector<raids::ResultPlayer> list = raids::rankedPlayers(w_.players, cat);
            if (tab_ == 3) {
                list.erase(std::remove_if(list.begin(), list.end(), [&](const raids::ResultPlayer& p) { return p.clanTag.empty() || p.clanTag != w_.myClanTag; }), list.end());
                for (size_t i = 0; i < list.size(); ++i) list[i].rank = static_cast<int>(i) + 1;
            }
            const bool score = cat == "score";
            cols = {{"#", 0.6f, Align::Left}, {"Joueur", 3.0f, Align::Left}};
            if (!score) cols.push_back({cats[static_cast<size_t>(cat_)].label, 3.0f, Align::Left});
            cols.push_back({"Contrib.", 1.3f, Align::Right});
            for (const raids::ResultPlayer& p : list) {
                Row row;
                row.cells = {std::to_string(p.rank), p.username + (p.clanTag.empty() ? "" : " [" + p.clanTag + "]")};
                if (!score) row.cells.push_back(raids::categoryText(p, cat));
                row.cells.push_back(raids::groupDigits(p.score));
                row.mine = p.me;
                row.verbatim = true;
                rows.push_back(std::move(row));
            }
            empty = tab_ == 3 ? (w_.myClanTag.empty() ? "Rejoignez un clan pour voir son classement." : "Aucun joueur de votre clan n'a encore de résultat validé.")
                              : "Aucun joueur classé : aucun tableau de contributions n'a encore été validé.";
        }
        Rect tableArea = body;
        if (tab_ == 3 && !w_.myClanTitles.empty()) {
            // My clan's titles won in this event, above its players.
            std::string titles = "Titres du clan : ";
            for (size_t i = 0; i < w_.myClanTitles.size(); ++i) titles += (i ? " · " : "") + w_.myClanTitles[i];
            fit(ui, titles, {x, body.y + ui.s(10)}, 14, w, kGold, FontFace::Bold);
            tableArea.y += ui.s(26);
            tableArea.h -= ui.s(26);
        }
        drawTable(tableArea, cols, rows, empty);
    }

    /// A ranking: its header, its rows in their columns (each text fitted to its column), its pages.
    void drawTable(const Rect& r, const std::vector<Column>& cols, const std::vector<Row>& rows, const std::string& empty) {
        UIContext& ui = g_.ui;
        if (r.h < ui.s(60)) return;
        float total = 0.0f;
        for (const Column& c : cols) total += c.weight;
        const float pad = ui.s(10), gapCol = ui.s(10);
        const float inner = r.w - 2 * pad - gapCol * static_cast<float>(cols.size() - 1);
        std::vector<float> xs, ws;
        float cx = r.x + pad;
        for (const Column& c : cols) {
            const float cw = inner * c.weight / total;
            xs.push_back(cx);
            ws.push_back(cw);
            cx += cw + gapCol;
        }
        auto at = [&](size_t i, Align a) { return a == Align::Right ? xs[i] + ws[i] : xs[i]; };
        const float headH = ui.s(24);
        for (size_t i = 0; i < cols.size(); ++i) fit(ui, cols[i].header, {at(i, cols[i].align), r.y + ui.s(10)}, 12, ws[i], kDim, FontFace::Bold, cols[i].align);
        const float navH = ui.s(40), rowH = ui.s(34), gap = ui.s(4);
        const float listTop = r.y + headH;
        const int fitRows = std::max(1, static_cast<int>((r.bottom() - navH - ui.s(8) - listTop) / (rowH + gap)));
        const int pages = std::max(1, (static_cast<int>(rows.size()) + fitRows - 1) / fitRows);
        page_ = std::clamp(page_, 0, pages - 1);
        float ry = listTop;
        if (rows.empty()) drawLines(ui, wrap(ui, empty, FontFace::Body, 15, r.w, 2), {r.x + pad, ry + ui.s(6)}, 15, ui.s(22), kDim);
        for (int k = page_ * fitRows; k < std::min(static_cast<int>(rows.size()), (page_ + 1) * fitRows); ++k) {
            const Row& row = rows[static_cast<size_t>(k)];
            const Rect band{r.x, ry, r.w, rowH};
            ui.r().fillRoundedRect(band, ui.s(8), row.mine ? kMine : kRow);
            for (size_t i = 0; i < cols.size() && i < row.cells.size(); ++i) {
                const bool name = i == 1;
                const Color c = i == 0 ? (k < 3 ? kGold : kText) : row.mine && name ? kGold : kText;
                fit(ui, row.cells[i], {at(i, cols[i].align), band.center().y}, i == 0 || name ? 15 : 14, ws[i], c, i == 0 || name ? FontFace::Bold : FontFace::Body,
                    cols[i].align, name && row.verbatim);
            }
            ry += rowH + gap;
        }
        if (pages > 1) {
            const float by = r.bottom() - navH;
            const std::string label = "Page " + std::to_string(page_ + 1) + " / " + std::to_string(pages);
            const float lw = textW(ui, label, 14) + ui.s(20);
            const float bw = std::min(ui.s(160), (r.w - lw - ui.s(20)) * 0.5f);
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = page_ > 0;
            if (ui.button("prev", {r.x, by, bw, navH}, "‹ Précédents", st)) --page_;
            st.enabled = page_ + 1 < pages;
            if (ui.button("next", {r.right() - bw, by, bw, navH}, "Suivants ›", st)) ++page_;
            fit(ui, label, {r.x + r.w * 0.5f, by + navH * 0.5f}, 14, r.w - 2 * bw - ui.s(20), kDim, FontFace::Body, Align::Center);
        }
    }

    void drawRewards(const Rect& r) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(14), Color{42, 36, 20, 245});
        ui.r().drawRoundedRect(r, ui.s(14), kGold.withAlphaF(0.6f), ui.s(1));
        const float x = r.x + ui.s(16);
        const std::string label = w_.rewards.size() > 1 ? "Tout récupérer" : "Récupérer";
        const float bw = w_.rewards.empty() ? 0.0f : std::min(textW(ui, label, 16, FontFace::Bold) + ui.s(44), r.w * 0.3f);
        const float room = r.right() - ui.s(16) - (bw > 0.0f ? bw + ui.s(16) : 0.0f) - x;
        fit(ui, "VOS RÉCOMPENSES DE L'ÉVÉNEMENT", {x, r.y + ui.s(20)}, 15, room, kGold, FontFace::Title);
        const float cy = r.y + ui.s(48);
        if (w_.rewards.empty()) {
            fit(ui, "Aucune récompense à récupérer pour cet événement.", {x, cy}, 15, room, kText, FontFace::Bold);
        } else {
            float px = x;
            for (const raids::Reward& rw : w_.rewards) {
                const float pw = pill(ui, px, cy, rw.name, Color{120, 96, 44, 255}, kText, x + room, 14);
                if (pw <= 0.0f) break;
                px += pw + ui.s(8);
            }
        }
        const std::string t = !note_.empty() ? note_
                                             : "Exclusives et accordées une seule fois par le serveur, à partir des résultats validés. Les sessions de rattrapage "
                                               "donnent des versions argentées et ne changent pas le résultat officiel.";
        drawLines(ui, wrap(ui, t, FontFace::Body, 13, room, 2), {x, r.y + ui.s(66)}, 13, ui.s(17), Color{232, 220, 184, 255});
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
    raids::WorldRanking w_;
    bool loaded_ = false, claiming_ = false;
    std::string error_, note_;
    float timer_ = 0.0f;
    int gen_ = 0;
    int tab_ = 0, cat_ = 0, page_ = 0;
};

}  // namespace

std::unique_ptr<Scene> makeRaidWorld(GameServices& g, const std::string& eventId) { return std::make_unique<RaidWorldScene>(g, eventId); }

}  // namespace cosmic
