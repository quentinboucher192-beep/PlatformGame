// Main menu → Statistiques: every boss of the game with the player's counters, kept on this
// computer (game/records/BossStats.h): fights, victories, defeats, victories without being
// hit, and for each phase how many times it was beaten and beaten without a hit.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/GameServices.h"
#include "game/gameplay/BossBadge.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"

namespace cosmic {

namespace {

std::string seconds(float s) {
    if (s <= 0.0f) return "—";
    int t = static_cast<int>(std::lround(s * 10.0f));
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d,%d", t / 600, (t / 10) % 60, t % 10);
    return b;
}

std::string dateFr(const std::string& d) {
    // FileUtil::nowString(): "YYYY-MM-DD HH:MM:SS".
    if (d.size() < 16) return d.empty() ? "—" : d;
    return d.substr(8, 2) + "/" + d.substr(5, 2) + "/" + d.substr(0, 4) + " " + d.substr(11, 5);
}

class BossStatsScene : public Scene {
public:
    explicit BossStatsScene(GameServices& g) : g_(g) {}

    void onEnter() override {
        bg_.init(g_);
        for (const auto* d : g_.content.enemies())
            if (d->isBoss) bosses_.push_back(d);
        // Bosses met first (most victories), then the others by name.
        std::stable_sort(bosses_.begin(), bosses_.end(), [&](const EnemyDefinition* a, const EnemyDefinition* b) {
            const BossRecord* ra = g_.bossStats.find(a->id);
            const BossRecord* rb = g_.bossStats.find(b->id);
            int fa = ra ? ra->fights : 0, fb = rb ? rb->fights : 0;
            if ((fa > 0) != (fb > 0)) return fa > 0;
            int ka = ra ? ra->kills : 0, kb = rb ? rb->kills : 0;
            if (ka != kb) return ka > kb;
            return a->name < b->name;
        });
        if (!g_.onlineOpen.empty() && g_.onlineOpen.rfind("boss:", 0) == 0) {  // automated screenshots
            std::string id = g_.onlineOpen.substr(5);
            for (size_t i = 0; i < bosses_.size(); ++i)
                if (bosses_[i]->id == id) selected_ = static_cast<int>(i);
            g_.onlineOpen.clear();
        }
        g_.ui.setFocus("bstats/b" + std::to_string(selected_));
    }
    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        ui.pushId("bstats");
        float W = ui.screenW(), H = ui.screenH();
        float m = ui.s(48);
        ui.label("STATISTIQUES DES BOSS", {m, ui.s(52)}, 40, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        UIContext::ButtonStyle bs;
        bs.small = true;
        Rect back{W - m - ui.s(200), ui.s(30), ui.s(200), ui.s(46)};
        bool leave = ui.button("back", back, "Retour", bs);
        drawSummary({m, ui.s(100), W - 2 * m, ui.s(56)});
        float top = ui.s(176);
        Rect list{m, top, std::min(ui.s(560), (W - 2 * m) * 0.4f), H - top - ui.s(40)};
        Rect detail{list.right() + ui.s(24), top, W - m - list.right() - ui.s(24), H - top - ui.s(40)};
        drawList(list);
        if (!bosses_.empty()) drawDetail(detail, *bosses_[static_cast<size_t>(std::clamp(selected_, 0, static_cast<int>(bosses_.size()) - 1))]);
        if (leave || ui.backPressed()) g_.app.switchScene(makeMainMenu(g_), 0.3f);
        ui.popId();
        ui.end();
    }

private:
    void drawSummary(const Rect& r) {
        UIContext& ui = g_.ui;
        int met = 0, beaten = 0, kills = 0, noHit = 0, phases = 0;
        for (const auto* d : bosses_)
            if (const BossRecord* rec = g_.bossStats.find(d->id)) {
                met += rec->fights > 0 ? 1 : 0;
                beaten += rec->kills > 0 ? 1 : 0;
                kills += rec->kills;
                noHit += rec->noHitKills;
                phases += rec->noHitPhases();
            }
        struct Chip {
            std::string label, value;
            Color c;
        };
        std::vector<Chip> chips = {
            {"Boss vaincus", std::to_string(beaten) + " / " + std::to_string(bosses_.size()), ui.theme().accent},
            {"Victoires", std::to_string(kills), ui.theme().warm},
            {"Combats sans dégât", std::to_string(noHit), ui.theme().success},
            {"Phases sans dégât", std::to_string(phases), Color{150, 220, 255, 255}},
            {"Boss rencontrés", std::to_string(met), ui.theme().textDim},
        };
        float gap = ui.s(14);
        float cw = (r.w - gap * (chips.size() - 1)) / static_cast<float>(chips.size());
        for (size_t i = 0; i < chips.size(); ++i) {
            Rect c{r.x + i * (cw + gap), r.y, cw, r.h};
            ui.r().fillRoundedRect(c, ui.s(10), Color{10, 16, 34, 220});
            ui.r().drawRoundedRect(c, ui.s(10), chips[i].c.withAlphaF(0.5f), std::max(1.0f, ui.s(1.2f)));
            float lw = ui.fitFont(chips[i].label, FontFace::Body, 16, cw * 0.6f);
            ui.label(chips[i].label, {c.x + ui.s(14), c.center().y}, lw, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(chips[i].value, {c.right() - ui.s(14), c.center().y}, ui.fitFont(chips[i].value, FontFace::Bold, 24, cw * 0.35f), chips[i].c,
                     Align::Right, FontFace::Bold, VAlign::Middle);
        }
    }

    void drawList(const Rect& area) {
        UIContext& ui = g_.ui;
        float rh = ui.s(78), gap = ui.s(8);
        ui.beginScroll("list", area, static_cast<float>(bosses_.size()) * (rh + gap));
        float y = area.y - ui.scrollOffset();
        for (size_t i = 0; i < bosses_.size(); ++i) {
            const EnemyDefinition& d = *bosses_[i];
            Rect row{area.x, y, area.w - ui.s(14), rh};
            y += rh + gap;
            std::string id = "b" + std::to_string(i);
            UIContext::ButtonStyle st;
            st.selected = static_cast<int>(i) == selected_;
            if (ui.button(id, row, "", st)) selected_ = static_cast<int>(i);
            if (ui.focused(id)) selected_ = static_cast<int>(i);
            if (row.bottom() < area.y || row.y > area.bottom()) continue;
            const BossRecord* rec = g_.bossStats.find(d.id);
            bool met = rec && rec->fights > 0;
            BossBadge::draw(ui.r(), g_.app.resources(), d, {row.x + ui.s(42), row.center().y}, ui.s(26), false, t_);
            float tx = row.x + ui.s(82), room = row.right() - tx - ui.s(14);
            std::string name = d.name;
            float nf = ui.fitFont(name, FontFace::Bold, 19, room, 0.75f);
            ui.label(ui.elide(name, FontFace::Bold, nf, room), {tx, row.y + ui.s(26)}, nf, met ? ui.theme().text : ui.theme().textDim, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            std::string sub = !met ? "Jamais affronté"
                                   : std::to_string(rec->kills) + (rec->kills > 1 ? " victoires" : " victoire") + "  ·  " +
                                         std::to_string(rec->noHitKills) + " sans dégât  ·  " + std::to_string(rec->noHitPhases()) +
                                         (rec->noHitPhases() > 1 ? " phases parfaites" : " phase parfaite");
            ui.label(sub, {tx, row.y + ui.s(54)}, ui.fitFont(sub, FontFace::Body, 15, room), rec && rec->noHitKills > 0 ? ui.theme().success : ui.theme().textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
        }
        ui.endScroll();
    }

    void drawDetail(const Rect& p, const EnemyDefinition& d) {
        UIContext& ui = g_.ui;
        ui.panel(p, true, 16);
        const BossRecord* rec = g_.bossStats.find(d.id);
        BossRecord empty;
        const BossRecord& r = rec ? *rec : empty;
        float x = p.x + ui.s(32), w = p.w - ui.s(64), y = p.y + ui.s(28);
        BossBadge::draw(ui.r(), g_.app.resources(), d, {x + ui.s(44), y + ui.s(44)}, ui.s(40), false, t_);
        float tx = x + ui.s(104);
        float tf = ui.fitFont(d.name, FontFace::Title, 32, x + w - tx, 0.6f);
        ui.label(d.name, {tx, y + ui.s(26)}, tf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        std::string title = d.bossTitle.empty() ? d.description : d.bossTitle;
        if (!title.empty())
            ui.label(ui.elide(title, FontFace::Body, 17, x + w - tx), {tx, y + ui.s(64)}, 17, ui.theme().textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
        y += ui.s(112);

        // Counters
        struct Cell {
            std::string label, value;
            Color c;
        };
        std::vector<Cell> cells = {
            {"Combats", std::to_string(r.fights), ui.theme().text},
            {"Victoires", std::to_string(r.kills), ui.theme().warm},
            {"Défaites", std::to_string(r.defeats), ui.theme().danger},
            {"Victoires sans dégât", std::to_string(r.noHitKills), ui.theme().success},
            {"Meilleur combat", seconds(r.bestKill), ui.theme().accent},
            {"Dernière victoire", r.lastKill.empty() ? "—" : dateFr(r.lastKill), ui.theme().textDim},
        };
        int cols = 3;
        float gap = ui.s(14), ch = ui.s(88);
        float cw = (w - gap * (cols - 1)) / cols;
        for (size_t i = 0; i < cells.size(); ++i) {
            Rect c{x + (i % cols) * (cw + gap), y + (i / cols) * (ch + gap), cw, ch};
            ui.r().fillRoundedRect(c, ui.s(10), Color{8, 13, 28, 230});
            ui.r().drawRoundedRect(c, ui.s(10), cells[i].c.withAlphaF(0.35f), std::max(1.0f, ui.s(1.0f)));
            ui.label(cells[i].label, {c.x + ui.s(16), c.y + ui.s(24)}, ui.fitFont(cells[i].label, FontFace::Body, 16, c.w - ui.s(32)), ui.theme().textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(cells[i].value, {c.x + ui.s(16), c.y + ui.s(60)}, ui.fitFont(cells[i].value, FontFace::Bold, 30, c.w - ui.s(32)), cells[i].c,
                     Align::Left, FontFace::Bold, VAlign::Middle);
        }
        y += 2 * (ch + gap) + ui.s(10);

        // Phases
        ui.label("Phases", {x, y + ui.s(12)}, 22, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        const std::string hint = "battues · dont sans dégât";
        ui.label(hint, {x + w, y + ui.s(12)}, 15, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        y += ui.s(36);
        size_t phases = std::max<size_t>(1, d.phases.size());
        float rowH = std::min(ui.s(58), (p.bottom() - ui.s(24) - y) / static_cast<float>(phases));
        for (size_t i = 0; i < phases; ++i) {
            std::string name = i < d.phases.size() && !d.phases[i].name.empty() ? d.phases[i].name : "Phase " + std::to_string(i + 1);
            int clears = i < r.phaseClears.size() ? r.phaseClears[i] : 0;
            int perfect = i < r.phaseNoHit.size() ? r.phaseNoHit[i] : 0;
            Rect row{x, y, w, rowH - ui.s(6)};
            ui.r().fillRoundedRect(row, ui.s(8), Color{12, 18, 38, 220});
            float cy = row.center().y;
            std::string label = std::to_string(i + 1) + ". " + name;
            float countsW = ui.s(220);
            float barW = std::min(ui.s(260), w * 0.3f);
            float room = row.w - countsW - barW - ui.s(48);
            float lf = ui.fitFont(label, FontFace::Bold, 18, room, 0.75f);
            ui.label(ui.elide(label, FontFace::Bold, lf, room), {row.x + ui.s(14), cy}, lf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
            Rect bar{row.x + ui.s(14) + room + ui.s(14), cy - ui.s(6), barW, ui.s(12)};
            ui.r().fillRoundedRect(bar, ui.s(6), Color{26, 34, 60, 255});
            if (clears > 0) {
                float f = std::clamp(static_cast<float>(perfect) / static_cast<float>(clears), 0.0f, 1.0f);
                if (f > 0.0f) ui.r().fillRoundedRect({bar.x, bar.y, std::max(bar.h, bar.w * f), bar.h}, ui.s(6), ui.theme().success);
            }
            std::string counts = std::to_string(clears) + "  ·  " + std::to_string(perfect) + " sans dégât";
            ui.label(counts, {row.right() - ui.s(14), cy}, ui.fitFont(counts, FontFace::Bold, 17, countsW), perfect > 0 ? ui.theme().success : ui.theme().textDim,
                     Align::Right, FontFace::Bold, VAlign::Middle);
            y += rowH;
        }
        if (!rec || r.fights == 0)
            ui.label("Pas encore affronté : ses statistiques commenceront au premier combat.", {x, p.bottom() - ui.s(26)}, 16, ui.theme().textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
    }

    GameServices& g_;
    MenuBackground bg_;
    std::vector<const EnemyDefinition*> bosses_;
    int selected_ = 0;
    float t_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeBossStats(GameServices& g) { return std::make_unique<BossStatsScene>(g); }

}  // namespace cosmic
