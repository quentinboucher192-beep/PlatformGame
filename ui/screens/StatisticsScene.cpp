// Main menu → Statistiques (2.54): the player's own statistics, computed from their real runs and
// kept on this computer (game/records/PlayStats.h, saves/stats.json). Six sections:
//  * Vue d'ensemble: time played, dodge rate, precision, deaths per hour over 7 / 30 / 90 days,
//    against the previous period of the same length; stars of the achievements; deaths per hour;
//  * Combat: dodge rate and precision day by day, precision per attack, where the blows come from;
//  * Boss: the best progress on each boss attempt after attempt, a sheet per boss, the defeats
//    per phase (and the boss statistics of before, « Fiches des boss », BossStatsScene);
//  * Niveaux: the classic levels (not EXTRÊME): PB and Sum of Best over time, the segments where
//    time is lost, their consistency, the regularity of the full runs;
//  * Passages difficiles: where the player falls in a level (40 slices over its real relief),
//    the 5 hardest passages of every level and boss phase, with their training;
//  * Données et sauvegarde: what is kept, where, how big, how it travels with the copy of the
//    progression and « Migrer vers un nouveau compte » (2.55), how to reset it.
// Every chart: tooltip under the mouse, a cursor with ← → when focused, a « Tableau » view.
// The screen starts empty and fills up with the runs: no example data anywhere.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <future>
#include <map>

#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/LevelProgress.h"
#include "game/levels/LevelData.h"
#include "game/powers/PowerDefinition.h"
#include "game/records/PlayStats.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Charts.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

using charts::Bar;
using charts::LineSpec;
using charts::Series;
using charts::Tip;
using playstats::kOrigins;
using playstats::kSlices;

const Color kBossColors[3] = {{57, 135, 229, 255}, {217, 89, 38, 255}, {25, 158, 112, 255}};
const Color kBlue{57, 135, 229, 255};
const Color kText2{179, 189, 214, 255};
const Color kCard{26, 35, 56, 235};
const Color kLine{39, 50, 77, 255};
constexpr int kTabCount = 6;
constexpr int kTabOverview = 0, kTabCombat = 1, kTabBoss = 2, kTabLevels = 3, kTabHard = 4, kTabData = 5;

float fz(const UIContext& ui, float size) { return charts::fz(ui, size); }

// ------------------------------------------------------------------ French numbers

std::string frNum(double v, int decimals) {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    std::string t = b;
    if (auto p = t.find('.'); p != std::string::npos) t[p] = ',';
    return t;
}
std::string pct(double v) { return std::to_string(static_cast<int>(std::lround(v * 100.0))) + " %"; }
std::string duration(double sec) {
    long s = std::lround(std::max(0.0, sec));
    if (s < 60) return std::to_string(s) + " s";
    if (s < 3600) return std::to_string(s / 60) + " min";
    char b[32];
    std::snprintf(b, sizeof b, "%ld h %02ld", s / 3600, (s / 60) % 60);
    return b;
}
/// « 12,35 s », « 1:02,35 ».
std::string clockTime(float sec) {
    if (sec < 0.0f) return "—";
    long cs = std::lround(static_cast<double>(sec) * 100.0);
    char b[32];
    if (cs < 6000) std::snprintf(b, sizeof b, "%ld,%02ld s", cs / 100, cs % 100);
    else std::snprintf(b, sizeof b, "%ld:%02ld,%02ld", cs / 6000, (cs / 100) % 60, cs % 100);
    return b;
}
const char* const kMonths[12] = {"janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."};
std::string dayShort(int day) {
    int y, m, d;
    civilFromDays(day, y, m, d);
    return std::to_string(d) + " " + kMonths[std::clamp(m, 1, 12) - 1];
}
std::string dayLong(int day) {
    int y, m, d;
    civilFromDays(day, y, m, d);
    return std::to_string(d) + " " + kMonths[std::clamp(m, 1, 12) - 1] + " " + std::to_string(y);
}

const char* originLabel(HurtOrigin o) {
    switch (o) {
        case HurtOrigin::EnemyShot: return "Tirs ennemis";
        case HurtOrigin::EnemyAttack: return "Attaques d'ennemis";
        case HurtOrigin::BossAttack: return "Attaques de boss";
        case HurtOrigin::Contact: return "Contact avec un ennemi";
        case HurtOrigin::Hazard: return "Pièges et dangers";
        case HurtOrigin::Fall: return "Chutes dans le vide";
        default: return "Autres (temps écoulé…)";
    }
}

// ------------------------------------------------------------------ a level, read off the main thread

struct LevelInfo {
    bool ok = false;
    std::string name;
    bool extreme = false;
    std::vector<float> relief;                                // heights 0..1 along the route
    std::vector<std::pair<float, std::string>> checkpoints;   // (position 0..1, id), in route order
    std::map<std::string, std::string> labels;                // checkpoint id → its label
};

LevelInfo readLevelInfo(const ContentDatabase& content, const std::string& id) {
    LevelInfo info;
    auto level = content.loadLevel(id);
    if (!level) return info;
    info.ok = true;
    info.name = level->name;
    info.extreme = level->extreme;
    LevelProgressTrack t = buildProgressTrack(*level, content);
    if (t.valid()) {
        // The height of the route, sampled along its length (higher ground: taller).
        const int n = 120;
        std::vector<float> ys;
        size_t j = 0;
        for (int k = 0; k < n; ++k) {
            float at = t.length * static_cast<float>(k) / static_cast<float>(n - 1);
            while (j + 1 < t.cumulative.size() && t.cumulative[j + 1] < at) ++j;
            size_t j2 = std::min(j + 1, t.route.size() - 1);
            float seg = t.cumulative[j2] - t.cumulative[j];
            float u = seg > 0.0f ? std::clamp((at - t.cumulative[j]) / seg, 0.0f, 1.0f) : 0.0f;
            ys.push_back(t.route[j].y + (t.route[j2].y - t.route[j].y) * u);
        }
        auto [lo, hi] = std::minmax_element(ys.begin(), ys.end());
        const float span = *hi - *lo;
        for (float y : ys) info.relief.push_back(span > 1.0f ? 0.15f + 0.85f * (*hi - y) / span : 0.5f);
    }
    for (const LevelObject& o : level->objects) {
        if (o.type != "checkpoint") continue;
        float at = t.valid() ? t.fractionAt(o.rect.bottomCenter()) : 0.0f;
        info.checkpoints.push_back({at, o.id});
        info.labels[o.id] = o.propStr("label");
    }
    std::sort(info.checkpoints.begin(), info.checkpoints.end());
    return info;
}

// ------------------------------------------------------------------ the scene

class StatisticsScene : public Scene {
public:
    StatisticsScene(GameServices& g, int tab) : g_(g), tab_(std::clamp(tab, 0, kTabCount - 1)) {}

    void onEnter() override {
        bg_.init(g_);
        // Automated screenshots (--stats <section>:<options>): level=<id>, runs=<id>, boss=<id>,
        // table=<card>, focus=<chart>, period=<7|30|90>.
        if (g_.onlineOpen.rfind("stats:", 0) == 0) {
            std::string opts = g_.onlineOpen.substr(6) + ",";
            g_.onlineOpen.clear();
            size_t start = 0;
            for (size_t c = opts.find(','); c != std::string::npos; start = c + 1, c = opts.find(',', start)) {
                std::string o = opts.substr(start, c - start);
                auto eq = o.find('=');
                std::string k = o.substr(0, eq), v = eq == std::string::npos ? std::string() : o.substr(eq + 1);
                if (k == "level") wantSlices_ = v;
                else if (k == "runs") wantRuns_ = v;
                else if (k == "boss") phaseBoss_ = v;
                else if (k == "table") tables_[v] = true;
                else if (k == "focus") focusChart_ = v;
                else if (k == "period") period_ = std::clamp(std::atoi(v.c_str()), 7, 90);
            }
        }
        if (!focusChart_.empty()) g_.ui.setFocus("stats/c/" + focusChart_);  // the charts are "c/<id>" under "stats"
        // « NOUVEAU » (2.54): this screen is seen once opened.
        if (g_.ui.featureSeen && g_.ui.isNew("stats.player")) g_.ui.featureSeen("stats.player");
    }

    void update(float dt) override { bg_.update(dt); }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        tip_ = Tip{};
        today_ = g_.playStats.today();
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(40);
        ui.pushId("stats");
        ui.inputEnabled = !confirmReset_;
        UIContext::ButtonStyle small;
        small.small = true;
        Rect back{W - m - ui.s(200), ui.s(26), ui.s(200), ui.s(46)};
        bool leave = ui.button("back", back, "Retour", small);
        float tf = ui.fitFont("STATISTIQUES", FontFace::Title, 40, back.x - m - ui.s(24), 0.6f);
        ui.label("STATISTIQUES", {m, ui.s(49)}, tf, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        Rect tabsR{m, ui.s(92), W - 2 * m, ui.s(48)};
        int before = tab_;
        ui.tabs("tabs", tabsR, {"Vue d'ensemble", "Combat", "Boss", "Niveaux", "Passages difficiles", "Données et sauvegarde"}, tab_);
        if (tab_ != before) tip_ = Tip{};
        // « NOUVEAU » (2.55) on « Données et sauvegarde » until it is opened; its new cards keep
        // theirs during that visit.
        if (ui.isNew("stats.tab.data")) {
            if (tab_ == kTabData && ui.featureSeen) {
                ui.featureSeen("stats.tab.data");
                dataFresh_ = true;
            } else {
                ui.drawNewPill(tabsR.right() - ui.s(10), tabsR.y);
            }
        }
        Rect view{m, tabsR.bottom() + ui.s(4), W - 2 * m, H - tabsR.bottom() - ui.s(4) - ui.s(56)};
        ui.beginScroll("page" + std::to_string(tab_), view, contentH_[tab_]);
        const float x = view.x, w = view.w - ui.s(16);
        float y = view.y - ui.scrollOffset() + ui.s(12);  // room for the « NOUVEAU » pill of a first row
        const float startY = y;
        if (tab_ == kTabData) y = drawData(x, y, w);
        else if (noData() && tab_ != kTabBoss) y = drawEmpty(x, y, w);
        else if (tab_ == kTabOverview) y = drawOverview(x, y, w);
        else if (tab_ == kTabCombat) y = drawCombat(x, y, w);
        else if (tab_ == kTabBoss) y = drawBoss(x, y, w);
        else if (tab_ == kTabLevels) y = drawLevels(x, y, w);
        else if (tab_ == kTabHard) y = drawHard(x, y, w);
        contentH_[tab_] = y - startY + ui.s(16);
        ui.endScroll();
        // The footer: how to move around (never under the content).
        float fy = H - ui.s(28);
        float hx = m;
        hx += Glyphs::drawHint(ui, Action::MenuTabPrev, "/", {hx, fy}, ui.s(28));
        hx += Glyphs::drawHint(ui, Action::MenuTabNext, "Sections", {hx + ui.s(6), fy}, ui.s(28)) + ui.s(36);
        Glyphs::drawHint(ui, Action::MenuBack, "Retour", {hx, fy}, ui.s(28));
        charts::drawTip(ui, tip_);
        ui.inputEnabled = true;
        if (confirmReset_) drawConfirm();
        else if (leave || (ui.backPressed() && !ui.editingText())) g_.app.switchScene(makeMainMenu(g_), 0.3f);
        ui.popId();
        ui.end();
    }

private:
    const PlayStatsData& data() const { return g_.playStats.data(); }
    bool noData() const { return data().empty(); }

    // ------------------------------------------------------------------ building blocks

    /// A card: its frame, its title, its subtitle; at its right the « Tableau / Graphique » switch
    /// when `table` is given. Returns the room under the header.
    Rect card(const Rect& r, const std::string& title, const std::string& sub, const std::string& table = {}, Color top = {0, 0, 0, 0},
              bool fresh = false) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(r, ui.s(12), kCard);
        ui.r().drawRoundedRect(r, ui.s(12), kLine, std::max(1.0f, ui.s(1.0f)));
        if (top.a) ui.r().fillRoundedRect({r.x + ui.s(10), r.y, r.w - ui.s(20), ui.s(3)}, ui.s(1.5f), top);
        const float pad = ui.s(16);
        float right = r.right() - pad;
        if (!table.empty()) {
            bool& on = tables_[table];
            const std::string t = on ? "Graphique" : "Tableau";
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 15;
            float bw = ui.text().measure(t, FontFace::Bold, ui.s(15)).x + ui.s(28);
            if (ui.button("tbl/" + table, {right - bw, r.y + ui.s(12), bw, ui.s(32)}, t, st)) on = !on;
            right -= bw + ui.s(12);
        }
        const float tf = fz(ui, 16), sf = fz(ui, 14);
        const float room = right - (r.x + pad);
        float y = r.y + ui.s(14);
        // `fresh` (2.55): a « NOUVEAU » pill right after the title, in room kept for it.
        const float pill = fresh ? ui.newPillWidth(true) + ui.s(10) : 0.0f;
        float f1 = ui.fitFont(title, FontFace::Bold, tf, room - pill, 0.8f);
        const std::string shownTitle = ui.elide(title, FontFace::Bold, f1, room - pill);
        ui.label(shownTitle, {r.x + pad, y}, f1, ui.theme().text, Align::Left, FontFace::Bold);
        const float th = ui.text().measure("Ag", FontFace::Bold, ui.s(f1)).y;
        if (fresh) ui.drawNewPill(r.x + pad + ui.text().measure(shownTitle, FontFace::Bold, ui.s(f1)).x + pill, y + th * 0.5f, true);
        y += th + ui.s(3);
        if (!sub.empty()) {
            ui.label(ui.elide(sub, FontFace::Body, sf, room), {r.x + pad, y}, sf, ui.theme().textDim, Align::Left, FontFace::Body);
            y += ui.text().measure("Ag", FontFace::Body, ui.s(sf)).y;
        }
        y = std::max(y, r.y + ui.s(50));
        return {r.x + pad, y + ui.s(10), r.w - 2 * pad, r.bottom() - y - ui.s(10) - pad};
    }

    /// A legend: a short line or a dot per series, then its name (measured; wraps to a new line).
    float legend(float x, float y, float w, const std::vector<std::pair<Color, std::string>>& items, bool dotsFirst = false, int dots = -1) {
        UIContext& ui = g_.ui;
        const float f = fz(ui, 14), h = ui.text().measure("Ag", FontFace::Body, ui.s(f)).y;
        float cx = x, cy = y;
        for (size_t i = 0; i < items.size(); ++i) {
            const bool dot = static_cast<int>(i) == dots || (dotsFirst && i == 0 && dots < 0);
            float tw = ui.text().measure(items[i].second, FontFace::Body, ui.s(f)).x;
            float iw = ui.s(dot ? 10 : 18) + ui.s(8) + tw;
            if (cx > x && cx + iw > x + w) {
                cx = x;
                cy += h + ui.s(4);
            }
            if (dot) ui.r().fillCircle({cx + ui.s(5), cy + h * 0.5f}, ui.s(4), items[i].first);
            else ui.r().fillRoundedRect({cx, cy + h * 0.5f - ui.s(1.5f), ui.s(18), ui.s(3)}, ui.s(1.5f), items[i].first);
            cx += ui.s(dot ? 10 : 18) + ui.s(8);
            ui.label(ui.elide(items[i].second, FontFace::Body, f, x + w - cx), {cx, cy}, f, kText2);
            cx += tw + ui.s(18);
        }
        return cy + h - y;
    }

    /// A note explaining how something is computed: wrapped, measured, with an accent bar.
    float note(float x, float y, float w, const std::string& text) {
        UIContext& ui = g_.ui;
        const float f = fz(ui, 14);
        const float tw = w - ui.s(14);
        Vec2 size = ui.text().measureWrapped(text, FontFace::Body, ui.s(f), tw);
        ui.r().fillRect({x, y, ui.s(2), size.y + ui.s(8)}, Color{29, 111, 102, 255});
        ui.labelWrapped(text, {x + ui.s(14), y + ui.s(4)}, tw, f, kText2);
        return size.y + ui.s(8);
    }

    /// 7 j / 30 j / 90 j.
    float periodRow(float x, float y) {
        UIContext& ui = g_.ui;
        const float f = fz(ui, 15);
        ui.label("Période", {x, y + ui.s(18)}, f, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        float bx = x + ui.text().measure("Période", FontFace::Body, ui.s(f)).x + ui.s(14);
        const int periods[3] = {7, 30, 90};
        for (int p : periods) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 16;
            st.selected = period_ == p;
            std::string t = std::to_string(p) + " j";
            float bw = ui.text().measure(t, FontFace::Bold, ui.s(16)).x + ui.s(30);
            if (ui.button("period" + std::to_string(p), {bx, y, bw, ui.s(36)}, t, st)) period_ = p;
            bx += bw + ui.s(8);
        }
        return ui.s(36);
    }

    struct TileData {
        std::string key, value, detail;
    };
    /// Up to four tiles in a row (two by two when narrow). Returns their height.
    float tiles(float x, float y, float w, const std::vector<TileData>& ts) {
        UIContext& ui = g_.ui;
        const int perRow = w >= ui.s(880) ? static_cast<int>(ts.size()) : 2;
        const float gap = ui.s(12), th = ui.s(104);
        const float tw = (w - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
        int rows = (static_cast<int>(ts.size()) + perRow - 1) / perRow;
        for (size_t i = 0; i < ts.size(); ++i) {
            Rect r{x + (tw + gap) * static_cast<float>(static_cast<int>(i) % perRow), y + (th + gap) * static_cast<float>(static_cast<int>(i) / perRow), tw, th};
            ui.r().fillRoundedRect(r, ui.s(12), kCard);
            ui.r().drawRoundedRect(r, ui.s(12), kLine, std::max(1.0f, ui.s(1.0f)));
            const float room = r.w - ui.s(28);
            const float kf = ui.fitFont(ts[i].key, FontFace::Body, fz(ui, 13), room, 0.8f);
            ui.label(ui.elide(ts[i].key, FontFace::Body, kf, room), {r.x + ui.s(14), r.y + ui.s(12)}, kf, ui.theme().textDim);
            const float vf = std::max(fz(ui, 16), ui.fitFont(ts[i].value, FontFace::Title, 26, room, 0.6f));
            ui.label(ui.elide(ts[i].value, FontFace::Title, vf, room), {r.x + ui.s(14), r.y + ui.s(50)}, vf, ui.theme().text, Align::Left, FontFace::Title,
                     VAlign::Middle);
            const float df = ui.fitFont(ts[i].detail, FontFace::Body, fz(ui, 14), room, 0.8f);
            ui.label(ui.elide(ts[i].detail, FontFace::Body, df, room), {r.x + ui.s(14), r.bottom() - ui.s(14)}, df, kText2, Align::Left, FontFace::Body,
                     VAlign::Bottom);
        }
        return th * static_cast<float>(rows) + gap * static_cast<float>(rows - 1);
    }

    /// The comparison line of a tile: ▲ / ▼, the gap, and in words what it means.
    std::string deltaText(const Comparison& c, bool percentPoints) {
        if (!c.available) return "Pas de période précédente";
        std::string gap = percentPoints ? std::to_string(static_cast<int>(std::lround(std::abs(c.delta) * 100.0))) + " pts" : frNum(std::abs(c.delta), 1);
        if (c.same || gap == "0 pts" || gap == "0,0") return "= stable";
        return std::string(c.delta > 0.0 ? "▲ " : "▼ ") + gap + (c.progress ? " — en progrès" : " — en recul");
    }

    /// A chart card that turns into its table: two columns in a row when the width allows.
    struct ChartCard {
        std::string id, title, sub;
        std::vector<std::pair<Color, std::string>> legend;
        int legendDot = -1;
        LineSpec spec;
        std::vector<std::string> head;
        std::vector<std::vector<std::string>> rows;
        std::string empty;  // shown instead of the chart when it has no data
    };
    void chartCard(const Rect& r, ChartCard& c) {
        UIContext& ui = g_.ui;
        Rect in = card(r, c.title, c.sub, c.id);
        if (!c.legend.empty()) {
            float lh = legend(in.x, in.y, in.w, c.legend, false, c.legendDot);
            in.y += lh + ui.s(8);
            in.h -= lh + ui.s(8);
        }
        bool any = false;
        for (const auto& s : c.spec.series) any = any || !s.pts.empty();
        if (tables_[c.id]) {
            charts::table(ui, "t/" + c.id, in, c.head, c.rows, pages_[c.id]);
        } else if (!any) {
            const std::string e = c.empty.empty() ? "Pas encore de données sur la période." : c.empty;
            ui.hotspot("c/" + c.id, in);
            ui.labelWrapped(e, {in.x, in.center().y - ui.s(10)}, in.w, fz(ui, 15), ui.theme().textDim);
        } else {
            charts::lineChart(ui, "c/" + c.id, in, c.spec, cursorOf(c.id), tip_);
        }
    }
    /// Cards two by two (stacked when narrow). Returns the height used.
    float cardRow(float x, float y, float w, std::vector<ChartCard>& cards, float h) {
        UIContext& ui = g_.ui;
        const bool two = w >= ui.s(1000) && cards.size() > 1;
        const float gap = ui.s(14);
        float used = 0.0f;
        for (size_t i = 0; i < cards.size(); ++i) {
            if (two) {
                float cw = (w - gap) / 2.0f;
                chartCard({x + (i % 2) * (cw + gap), y + (i / 2) * (h + gap), cw, h}, cards[i]);
                used = (h + gap) * static_cast<float>((i / 2) + 1) - gap;
            } else {
                chartCard({x, y + i * (h + gap), w, h}, cards[i]);
                used = (h + gap) * static_cast<float>(i + 1) - gap;
            }
        }
        return used;
    }

    float drawEmpty(float x, float y, float w) {
        UIContext& ui = g_.ui;
        std::string body = "Tout est calculé à partir de vos vraies parties (campagne, défis, niveaux des joueurs), sur cet ordinateur, sans compte ni Internet. "
                           "L'onglet se remplit au fil des parties.";
        if (!g_.playStats.enabled) body += " L'enregistrement est désactivé : Paramètres › Jeu › « Enregistrer mes statistiques ».";
        if (g_.playStats.damaged()) body += " Le fichier des statistiques était abîmé : il a été gardé dans stats.json.bak et l'onglet repart de zéro.";
        const float bf = fz(ui, 16), tw = w - ui.s(60);
        const float bh = ui.text().measureWrapped(body, FontFace::Body, ui.s(bf), tw).y;
        Rect r{x, y + ui.s(30), w, ui.s(90) + bh + ui.s(30)};  // as tall as its text
        ui.r().fillRoundedRect(r, ui.s(14), kCard);
        ui.r().drawRoundedRect(r, ui.s(14), kLine, std::max(1.0f, ui.s(1.0f)));
        const std::string head = "Jouez quelques parties pour voir vos statistiques";
        float hf = ui.fitFont(head, FontFace::Bold, 24, tw, 0.6f);
        ui.label(ui.elide(head, FontFace::Bold, hf, tw), {r.center().x, r.y + ui.s(50)}, hf, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        ui.labelWrapped(body, {r.x + ui.s(30), r.y + ui.s(90)}, tw, bf, kText2);
        return r.bottom() - y;
    }

    // ------------------------------------------------------------------ Vue d'ensemble

    float drawOverview(float x, float y0, float w) {
        UIContext& ui = g_.ui;
        float y = y0;
        y += periodRow(x, y) + ui.s(14);
        const int to = today_, from = today_ - period_ + 1;
        PeriodTotals cur = periodTotals(data(), from, to), prev = periodTotals(data(), from - period_, from - 1);
        std::vector<TileData> ts;
        ts.push_back({"TEMPS DE JEU", duration(cur.c.playSec),
                      std::to_string(cur.daysPlayed) + (cur.daysPlayed > 1 ? " jours joués" : " jour joué")});
        ts.push_back({"TAUX D'ESQUIVE", cur.dodgeRate() ? pct(*cur.dodgeRate()) : "—", deltaText(comparePeriods(cur.dodgeRate(), prev.dodgeRate(), true), true)});
        ts.push_back({"PRÉCISION", cur.accuracy() ? pct(*cur.accuracy()) : "—", deltaText(comparePeriods(cur.accuracy(), prev.accuracy(), true), true)});
        ts.push_back({"MORTS PAR HEURE", cur.deathsPerHour() ? frNum(*cur.deathsPerHour(), 1) : "—",
                      deltaText(comparePeriods(cur.deathsPerHour(), prev.deathsPerHour(), false), false)});
        y += tiles(x, y, w, ts) + ui.s(10);
        if (cur.c.trainSec >= 1.0) {
            y += note(x, y, w, "Entraînement (boss, segments, essais) : " + duration(cur.c.trainSec) +
                                   " sur la période, compté à part : il n'entre dans aucune courbe.") +
                 ui.s(10);
        }
        // Stars.
        std::vector<ChartCard> cards(2);
        {
            ChartCard& c = cards[0];
            c.id = "stars";
            c.title = "ÉTOILES DE SUCCÈS";
            c.sub = "Total cumulé, jour par jour";
            auto st = starsSeries(data(), from, to);
            Series s{"Étoiles", ui.theme().accent, Series::Kind::Line, true, {}};
            int lo = 1 << 30, hi = 0;
            for (auto [d, n] : st) {
                s.pts.push_back({static_cast<float>(d), static_cast<float>(n)});
                lo = std::min(lo, n);
                hi = std::max(hi, n);
                c.rows.push_back({dayLong(d), std::to_string(n)});
            }
            std::reverse(c.rows.begin(), c.rows.end());
            c.head = {"Jour", "Étoiles"};
            c.spec.series = {s};
            c.spec.yMin = st.empty() ? 0.0f : std::max(0.0f, std::floor((lo - 10) / 10.0f) * 10.0f);
            c.spec.yMax = st.empty() ? 10.0f : std::ceil((hi + 10) / 10.0f) * 10.0f;
            c.spec.yLabel = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
            c.spec.xLabel = [](float v) { return dayShort(static_cast<int>(v)); };
            c.spec.tip = [st](float xv, Tip& t) {
                t.title = dayLong(static_cast<int>(xv));
                for (auto [d, n] : st)
                    if (d == static_cast<int>(xv)) t.rows.push_back({{57, 240, 216, 255}, "Étoiles", std::to_string(n)});
            };
        }
        {
            ChartCard& c = cards[1];
            c.id = "dph";
            c.title = "MORTS PAR HEURE DE JEU";
            c.sub = "Plus la courbe descend, plus vous progressez";
            auto se = dailySeries(data(), DayMetric::DeathsPerHour, from, to);
            fillRateCard(c, se, ui.theme().warm, false);
        }
        y += cardRow(x, y, w, cards, ui.s(330)) + ui.s(14);
        y += note(x, y, w,
                  "Progression : la période choisie est comparée à la précédente de même durée (▲ / ▼ avec l'écart, en points pour les taux, "
                  "et « en progrès » ou « en recul » : jamais la couleur seule). Comptent les parties de la campagne, des défis et des niveaux "
                  "des joueurs ; l'entraînement est compté à part, le mode Invincible jamais.");
        return y - y0;
    }

    /// A day-by-day rate: the points of each day, the average of the last 7 days played (area).
    void fillRateCard(ChartCard& c, const std::vector<SeriesPoint>& se, Color color, bool percent) {
        Series dots{"Chaque jour", color, Series::Kind::Dots, false, {}};
        Series avg{"Moyenne sur 7 jours joués", color, Series::Kind::Line, true, {}};
        float hi = 0.0f;
        for (const auto& p : se) {
            dots.pts.push_back({static_cast<float>(p.day), static_cast<float>(p.value)});
            avg.pts.push_back({static_cast<float>(p.day), static_cast<float>(p.rolling)});
            hi = std::max(hi, static_cast<float>(std::max(p.value, p.rolling)));
        }
        auto fmt = [percent](double v) { return percent ? pct(v) : frNum(v, 1); };
        for (auto it = se.rbegin(); it != se.rend(); ++it) c.rows.push_back({dayLong(it->day), fmt(it->value), fmt(it->rolling)});
        c.head = {"Jour", "Ce jour-là", "Moyenne sur 7 jours joués"};
        c.legend = {{color, "Moyenne sur 7 jours joués"}, {color, "Chaque jour"}};
        c.legendDot = 1;
        c.spec.series = {dots, avg};
        c.spec.yMin = 0.0f;
        c.spec.yMax = percent ? 1.0f : std::max(5.0f, std::ceil(hi * 1.15f / 5.0f) * 5.0f);
        c.spec.yLabel = [percent](float v) { return percent ? pct(v) : std::to_string(static_cast<int>(std::lround(v))); };
        c.spec.xLabel = [](float v) { return dayShort(static_cast<int>(v)); };
        c.spec.tip = [se, color, fmt](float xv, Tip& t) {
            t.title = dayLong(static_cast<int>(xv));
            for (const auto& p : se)
                if (p.day == static_cast<int>(xv)) {
                    t.rows.push_back({color, "Moyenne sur 7 jours joués", fmt(p.rolling)});
                    t.rows.push_back({charts::kNoSwatch, "Ce jour-là", fmt(p.value)});
                }
        };
    }

    // ------------------------------------------------------------------ Combat

    float drawCombat(float x, float y0, float w) {
        UIContext& ui = g_.ui;
        float y = y0;
        y += periodRow(x, y) + ui.s(14);
        const int to = today_, from = today_ - period_ + 1;
        std::vector<ChartCard> cards(2);
        cards[0].id = "dodge";
        cards[0].title = "TAUX D'ESQUIVE";
        cards[0].sub = "Attaques évitées sur attaques qui vous menaçaient";
        fillRateCard(cards[0], dailySeries(data(), DayMetric::Dodge, from, to), ui.theme().accent, true);
        cards[1].id = "accuracy";
        cards[1].title = "PRÉCISION DES ATTAQUES";
        cards[1].sub = "Attaques qui touchent sur attaques lancées";
        fillRateCard(cards[1], dailySeries(data(), DayMetric::Accuracy, from, to), ui.theme().accent, true);
        y += cardRow(x, y, w, cards, ui.s(330)) + ui.s(14);
        // Precision per attack, where the blows come from.
        PeriodTotals cur = periodTotals(data(), from, to);
        std::vector<Bar> powers, blows;
        for (const auto& [id, v] : cur.c.powers) {
            if (v[0] <= 0) continue;
            std::string name = id == "attack" ? std::string("Attaque") : id;
            if (const PowerDefinition* p = g_.content.power(id)) name = p->name;
            float acc = static_cast<float>(v[1]) / static_cast<float>(v[0]);
            powers.push_back({name, acc, pct(acc), ui.theme().accent, name, {{charts::kNoSwatch, "Touchent", std::to_string(v[1]) + " sur " + std::to_string(v[0])}}});
        }
        std::stable_sort(powers.begin(), powers.end(), [](const Bar& a, const Bar& b) { return a.value > b.value; });
        int total = 0;
        for (int n : cur.c.hurt) total += n;
        std::vector<std::pair<int, int>> order;
        for (int i = 0; i < kOrigins; ++i)
            if (cur.c.hurt[static_cast<size_t>(i)] > 0) order.push_back({cur.c.hurt[static_cast<size_t>(i)], i});
        std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (auto [n, i] : order) {
            float share = total > 0 ? static_cast<float>(n) / static_cast<float>(total) : 0.0f;
            std::string label = originLabel(static_cast<HurtOrigin>(i));
            blows.push_back({label, share, pct(share), ui.theme().warm, label,
                             {{charts::kNoSwatch, "Part des coups reçus", pct(share)}, {charts::kNoSwatch, "Coups", std::to_string(n) + " sur " + std::to_string(total)}}});
        }
        const float gap = ui.s(14);
        const bool two = w >= ui.s(1000);
        const float cw = two ? (w - gap) / 2.0f : w;
        auto barsCard = [&](const Rect& r, const std::string& id, const std::string& title, const std::string& sub, const std::vector<Bar>& bars,
                            const std::vector<std::string>& head, const std::string& empty) {
            Rect in = card(r, title, sub, id);
            if (tables_[id]) {
                std::vector<std::vector<std::string>> rows;
                for (const auto& b : bars) rows.push_back({b.label, b.valueText, b.tipRows.empty() ? std::string() : b.tipRows.back().value});
                charts::table(ui, "t/" + id, in, head, rows, pages_[id]);
            } else if (bars.empty()) {
                ui.hotspot("c/" + id, in);
                ui.labelWrapped(empty, {in.x, in.y + ui.s(10)}, in.w, fz(ui, 15), ui.theme().textDim);
            } else {
                charts::hBars(ui, "c/" + id, {in.x, in.y, in.w, charts::hBarsHeight(ui, bars.size())}, bars, 1.0f, cursorOf(id), tip_);
            }
        };
        const size_t rows = std::max<size_t>(std::max(powers.size(), blows.size()), 4);
        const float bh = ui.s(80) + ui.s(14) + charts::hBarsHeight(ui, rows) + ui.s(16) + ui.s(36);
        barsCard({x, y, cw, bh}, "powers", "PRÉCISION PAR POUVOIR", "Sur la période choisie", powers, {"Attaque", "Précision", "Touchent"},
                 "Aucune attaque lancée sur la période.");
        barsCard({two ? x + cw + gap : x, two ? y : y + bh + gap, cw, bh}, "blows", "CE QUI VOUS TOUCHE", "Origine des coups reçus (et des chutes)", blows,
                 {"Origine", "Part", "Coups"}, "Aucun coup reçu sur la période.");
        y += (two ? bh : 2 * bh + gap) + ui.s(14);
        y += note(x, y, w,
                  "Esquive : un tir ennemi, ou une attaque portée par le corps d'un ennemi ou d'un boss (coup, charge, bond, piqué…), vous menace "
                  "quand sa zone de dégâts passe à moins d'une tuile et demie de vous pendant qu'elle est dangereuse ; elle est esquivée si elle "
                  "repart sans vous blesser. Chaque attaque compte une seule fois. Taux = esquivées ÷ menaces. Les attaques à distance sans tir "
                  "(lasers, pluies, marées…) comptent dans « Ce qui vous touche », pas dans l'esquive.") +
             ui.s(8);
        y += note(x, y, w,
                  "Précision : un coup de l'attaque, un tir ou une lame (Onde, Tir chargé, Bombe à plasma, Boomerangs, Frappe sismique) « touche » "
                  "s'il retire des points de vie à au moins un ennemi ; un tir qui en traverse plusieurs compte une seule fois. Les pouvoirs qui "
                  "n'infligent pas de dégâts (Nova de givre, Bouclier, Dash…) ne comptent pas. Le mode Invincible (admin) n'est jamais compté.");
        return y - y0;
    }

    // ------------------------------------------------------------------ Boss

    std::string bossName(const std::string& id) const {
        auto it = data().bosses.find(id);
        if (it != data().bosses.end() && !it->second.name.empty()) return it->second.name;
        if (const EnemyDefinition* d = g_.content.enemy(id)) return d->name;
        return id;
    }

    float drawBoss(float x, float y0, float w) {
        UIContext& ui = g_.ui;
        float y = y0;
        const std::vector<std::string> order = bossesByRecency(data());
        // The boss statistics of before (BossStatsScene): always reachable from here.
        {
            UIContext::ButtonStyle st;
            st.fontSize = 20;
            const std::string t = "Fiches des boss : lieux, animations, entraînement";
            float bw = std::min(w, ui.text().measure(t, FontFace::Bold, ui.s(20)).x + ui.s(48));
            Rect br{x, y, bw, ui.s(48)};
            if (ui.button("bossstats", br, t, st)) g_.app.switchScene(makeBossStats(g_, phaseBoss_), 0.3f);
            ui.newBadge("bossstats", br, "stats.bosses");
            y += br.h + ui.s(16);
        }
        if (order.empty()) {
            y += note(x, y, w,
                      "Aucune tentative contre un boss n'est encore enregistrée. Chaque combat compte dès maintenant : la part des points de vie "
                      "retirée, la phase atteinte et l'attaque fatale. Les statistiques de boss d'avant (victoires, phases sans dégât) restent dans "
                      "les fiches des boss.");
            return y - y0;
        }
        // Three bosses on the curve at a time, the most recent first.
        const int groups = (static_cast<int>(order.size()) + 2) / 3;
        group_ = std::clamp(group_, 0, groups - 1);
        std::vector<std::string> shown(order.begin() + group_ * 3, order.begin() + std::min<int>(static_cast<int>(order.size()), group_ * 3 + 3));
        if (groups > 1) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.enabled = group_ > 0;
            if (ui.button("grpprev", {x, y, ui.s(52), ui.s(36)}, "‹", st)) --group_;
            st.enabled = group_ + 1 < groups;
            if (ui.button("grpnext", {x + ui.s(60), y, ui.s(52), ui.s(36)}, "›", st)) ++group_;
            group_ = std::clamp(group_, 0, groups - 1);
            std::string t = "Boss " + std::to_string(group_ * 3 + 1) + " à " + std::to_string(group_ * 3 + static_cast<int>(shown.size())) + " sur " +
                            std::to_string(order.size()) + ", du plus récent au plus ancien";
            float room = w - ui.s(128);
            float f = ui.fitFont(t, FontFace::Body, fz(ui, 15), room, 0.8f);
            ui.label(ui.elide(t, FontFace::Body, f, room), {x + ui.s(128), y + ui.s(18)}, f, kText2, Align::Left, FontFace::Body, VAlign::Middle);
            y += ui.s(36) + ui.s(12);
        }
        ChartCard c;
        c.id = "bosscurve";
        c.title = "PROGRESSION SUR CHAQUE BOSS";
        c.sub = "Meilleure part des points de vie retirés, tentative après tentative";
        std::vector<std::vector<std::pair<int, float>>> curves;
        int maxN = 0;
        for (size_t i = 0; i < shown.size(); ++i) {
            const BossHistory& b = data().bosses.at(shown[i]);
            curves.push_back(bossBestCurve(b));
            Series s{bossName(shown[i]), kBossColors[i], Series::Kind::Step, false, {}};
            for (auto [n, best] : curves.back()) {
                s.pts.push_back({static_cast<float>(n), best});
                maxN = std::max(maxN, n);
            }
            c.spec.series.push_back(s);
            c.legend.push_back({kBossColors[i], s.name});
        }
        c.spec.yMin = 0.0f;
        c.spec.yMax = 1.0f;
        c.spec.integerX = true;
        c.spec.endLabels = true;
        c.spec.yLabel = [](float v) { return pct(v); };
        c.spec.xLabel = [](float v) { return "n° " + std::to_string(static_cast<int>(std::lround(v))); };
        std::vector<std::string> names;
        for (const auto& id : shown) names.push_back(bossName(id));
        c.spec.tip = [curves, names](float xv, Tip& t) {
            const int n = static_cast<int>(std::lround(xv));
            t.title = "Tentative " + std::to_string(n);
            for (size_t i = 0; i < curves.size(); ++i) {
                const std::pair<int, float>* p = nullptr;
                for (const auto& q : curves[i])
                    if (q.first <= n) p = &q;
                if (!p) continue;
                t.rows.push_back({kBossColors[i], names[i], p->second >= 0.999f ? "100 % (vaincu)" : pct(p->second)});
            }
        };
        c.head = {"Tentative"};
        for (const auto& n : names) c.head.push_back(n);
        for (int n = maxN; n >= 1; --n) {
            std::vector<std::string> row = {std::to_string(n)};
            bool any = false;
            for (const auto& cv : curves) {
                std::string v = "—";
                for (const auto& q : cv)
                    if (q.first == n) {
                        v = q.second >= 0.999f ? "100 % (vaincu)" : pct(q.second);
                        any = true;
                    }
                row.push_back(v);
            }
            if (any) c.rows.push_back(row);
        }
        chartCard({x, y, w, ui.s(380)}, c);
        y += ui.s(380) + ui.s(14);
        // A sheet per boss fought.
        {
            const float gap = ui.s(12);
            const int per = std::max(1, static_cast<int>((w + gap) / (ui.s(330) + gap)));
            const float cw = (w - gap * static_cast<float>(per - 1)) / static_cast<float>(per);
            const float rowH = ui.s(27), ch = ui.s(70) + rowH * 8.0f;
            for (size_t i = 0; i < order.size(); ++i) {
                Rect r{x + (cw + gap) * static_cast<float>(static_cast<int>(i) % per), y + (ch + gap) * static_cast<float>(static_cast<int>(i) / per), cw, ch};
                auto pos = std::find(shown.begin(), shown.end(), order[i]);
                Color top = pos != shown.end() ? kBossColors[pos - shown.begin()] : Color{0, 0, 0, 0};
                bossSheet(r, order[i], top);
            }
            int rows = (static_cast<int>(order.size()) + per - 1) / per;
            y += (ch + gap) * static_cast<float>(rows) - gap + ui.s(14);
        }
        // Defeats per phase, for the boss chosen.
        {
            if (phaseBoss_.empty() || !data().bosses.count(phaseBoss_)) phaseBoss_ = order.front();
            const BossHistory& b = data().bosses.at(phaseBoss_);
            // The bosses to choose from (wrapping): their rows counted first, the card grows with them.
            int chipRows = 1;
            {
                float cx = 0.0f, inner = w - ui.s(32);
                for (const auto& id : order) {
                    float bw = std::min(inner, ui.text().measure(bossName(id), FontFace::Bold, ui.s(16)).x + ui.s(30));
                    if (cx > 0.0f && cx + bw > inner) {
                        cx = 0.0f;
                        ++chipRows;
                    }
                    cx += bw + ui.s(8);
                }
            }
            const float ch = ui.s(330) + ui.s(40) * static_cast<float>(chipRows - 1);
            Rect in = card({x, y, w, ch}, "OÙ LE COMBAT S'ARRÊTE", "Défaites par phase — choisissez un boss", "phases");
            float cx = in.x, cy = in.y;
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = 16;
            for (size_t i = 0; i < order.size(); ++i) {
                std::string n = bossName(order[i]);
                float bw = std::min(in.w, ui.text().measure(n, FontFace::Bold, ui.s(16)).x + ui.s(30));
                if (cx > in.x && cx + bw > in.right()) {
                    cx = in.x;
                    cy += ui.s(40);
                }
                st.selected = order[i] == phaseBoss_;
                if (ui.button("pb" + std::to_string(i), {cx, cy, bw, ui.s(34)}, ui.elide(n, FontFace::Bold, 16, bw - ui.s(20)), st)) phaseBoss_ = order[i];
                cx += bw + ui.s(8);
            }
            cy += ui.s(46);
            Rect plot{in.x, cy, in.w, in.bottom() - cy};
            const EnemyDefinition* def = g_.content.enemy(phaseBoss_);
            std::vector<Bar> bars;
            std::vector<std::vector<std::string>> rows;
            for (int p = 0; p < b.phases; ++p) {
                const int d = p < static_cast<int>(b.defeats.size()) ? b.defeats[static_cast<size_t>(p)] : 0;
                const int re = p < static_cast<int>(b.reached.size()) ? b.reached[static_cast<size_t>(p)] : 0;
                std::string pname = def && p < static_cast<int>(def->phases.size()) ? def->phases[static_cast<size_t>(p)].name : std::string();
                std::string title = bossName(phaseBoss_) + " — phase " + std::to_string(p + 1) + (pname.empty() ? "" : " : " + pname);
                bars.push_back({"P" + std::to_string(p + 1), static_cast<float>(d), std::to_string(d), ui.theme().warm, title,
                                {{charts::kNoSwatch, "Défaites", std::to_string(d)}, {charts::kNoSwatch, "Tentatives arrivées là", std::to_string(re)}}});
                rows.push_back({"Phase " + std::to_string(p + 1) + (pname.empty() ? "" : " : " + pname), std::to_string(d), std::to_string(re)});
            }
            if (tables_["phases"]) charts::table(ui, "t/phases", plot, {"Phase", "Défaites", "Tentatives arrivées là"}, rows, pages_["phases"]);
            else charts::vBars(ui, "c/phases", plot, bars, cursorOf("phases"), tip_, [](float v) { return std::to_string(static_cast<int>(std::lround(v))); });
            y += ch + ui.s(14);
        }
        y += note(x, y, w,
                  "Progression : à chaque tentative est enregistrée la part des points de vie totaux du boss (toutes phases) retirée au moment de la "
                  "défaite ou de l'abandon ; la courbe montre le meilleur résultat obtenu jusque-là, une victoire vaut 100 %. Une tentative reprise "
                  "après une mort retrouve le boss avec ses blessures. Les 200 dernières tentatives sont gardées par boss. Les victoires et phases "
                  "sans dégât d'avant le suivi viennent des statistiques de boss existantes.");
        return y - y0;
    }

    void bossSheet(const Rect& r, const std::string& id, Color top) {
        UIContext& ui = g_.ui;
        const BossHistory& b = data().bosses.at(id);
        const BossRecord* rec = g_.bossStats.find(id);
        Rect in = card(r, bossName(id), {}, {}, top);
        const float best = b.best();
        const int hp = hardestPhase(b);
        std::string fatal = commonFatal(b);
        std::string winAfter = b.firstWin > 0 ? std::to_string(b.firstWin) + (b.firstWin > 1 ? " tentatives" : " tentative")
                               : rec && rec->kills > 0 ? std::string("avant le suivi") : std::string("pas encore");
        std::vector<std::pair<std::string, std::string>> rows = {
            {"Tentatives", std::to_string(b.attempts)},
            {"Victoires", std::to_string(b.wins)},
            {"Meilleur", best >= 0.999f ? std::string("vaincu") : pct(best)},
            {"Victoire après", winAfter},
            {"Phase la plus dure", hp >= 0 ? std::to_string(hp + 1) + " / " + std::to_string(b.phases) : std::string("—")},
            {"Attaque fatale", fatal.empty() ? std::string("—") : fatal},
            {"Victoires (au total)", rec ? std::to_string(rec->kills) : std::string("0")},
            {"Phases sans dégât", rec ? std::to_string(rec->noHitPhases()) : std::string("0")},
        };
        const float f = fz(ui, 14), rowH = ui.s(27);
        float y = in.y - ui.s(4);
        for (const auto& [k, v] : rows) {
            const float kw = ui.text().measure(k, FontFace::Body, ui.s(f)).x;
            const float room = in.w - kw - ui.s(14);
            ui.label(k, {in.x, y + rowH * 0.5f}, f, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(ui.elide(v, FontFace::Bold, f, room), {in.right(), y + rowH * 0.5f}, f, ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
            y += rowH;
        }
    }

    // ------------------------------------------------------------------ Niveaux (classic levels)

    const LevelInfo* info(const std::string& id) {
        auto it = infos_.find(id);
        if (it == infos_.end())
            it = infos_.emplace(id, std::async(std::launch::async, [&content = g_.content, id] { return readLevelInfo(content, id); }).share()).first;
        if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
        return &it->second.get();
    }

    std::string checkpointName(const LevelInfo* li, const std::string& id, bool end) const {
        if (id.empty()) return end ? "Arrivée" : "Départ";
        if (li) {
            auto it = li->labels.find(id);
            if (it != li->labels.end() && !it->second.empty()) return it->second;
            for (size_t i = 0; i < li->checkpoints.size(); ++i)
                if (li->checkpoints[i].second == id) return "Point de passage " + std::to_string(i + 1);
        }
        return "Point de passage";
    }

    /// The section training (2.30.2) of a level from a checkpoint ("" = its start).
    void trainSegment(const std::string& levelId, const std::string& fromCheckpoint) {
        GhostSetupRequest req;
        req.training = true;
        req.trainingCheckpoint = fromCheckpoint;
        g_.app.pushScene(makeGhostSetup(g_, levelId, std::move(req)));
    }

    float drawLevels(float x, float y0, float w) {
        UIContext& ui = g_.ui;
        float y = y0;
        std::vector<std::pair<int, std::string>> order;
        for (const auto& [id, r] : data().runs)
            if (r.attempts > 0 || !r.segments.empty()) order.push_back({r.lastDay, id});
        std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (order.empty()) {
            y += note(x, y, w,
                      "Aucune partie d'un niveau classique n'est encore enregistrée. Jouez un niveau de la campagne (hors EXTRÊME) depuis son début : "
                      "chaque segment entre deux points de passage est chronométré, ses temps forment votre « somme des meilleurs » (SOB).");
            return y - y0;
        }
        int idx = 0;
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i].second == wantRuns_) idx = static_cast<int>(i);
        levelPicker(x, y, w, "runs", order, idx);
        wantRuns_ = order[static_cast<size_t>(idx)].second;
        y += ui.s(40) + ui.s(14);
        const std::string& levelId = wantRuns_;
        const LevelRuns& r = data().runs.at(levelId);
        const LevelInfo* li = info(levelId);
        const float sob = r.sob();
        // Summary.
        {
            std::string pbDay;
            for (const auto& h : r.history)
                if (std::abs(h.pb - r.pb) < 0.005f) {
                    pbDay = dayShort(h.day);
                    break;
                }
            std::vector<TileData> ts;
            ts.push_back({"RECORD (PB)", clockTime(r.pb), pbDay.empty() ? std::string("meilleure partie complète") : "le " + pbDay});
            ts.push_back({"SOMME DES MEILLEURS (SOB)", clockTime(sob), "meilleurs segments, entraînement compris"});
            ts.push_back({"TEMPS GAGNABLE", r.pb > 0.0f && sob > 0.0f ? clockTime(std::max(0.0f, r.pb - sob)) : std::string("—"), "PB − SOB, en théorie"});
            ts.push_back({"ARRIVÉES", std::to_string(r.finished) + " / " + std::to_string(r.attempts),
                          r.attempts > 0 ? pct(static_cast<double>(r.finished) / r.attempts) + " des tentatives" : std::string("—")});
            y += tiles(x, y, w, ts) + ui.s(14);
        }
        // PB and SOB over time.
        {
            ChartCard c;
            c.id = "sob";
            c.title = "ÉVOLUTION DU RECORD ET DE LA SOMME DES MEILLEURS";
            c.sub = "Un point à chaque date où l'un des deux a changé";
            Series pb{"Record (PB)", ui.theme().accent, Series::Kind::Step, false, {}};
            Series so{"Somme des meilleurs (SOB)", kBlue, Series::Kind::Step, false, {}};
            Series gap{"Temps gagnable", ui.theme().warm, Series::Kind::Step, false, {}};
            float hi = 1.0f;
            for (const auto& h : r.history) {
                if (h.pb > 0.0f) pb.pts.push_back({static_cast<float>(h.day), h.pb});
                if (h.sob > 0.0f) so.pts.push_back({static_cast<float>(h.day), h.sob});
                if (h.pb > 0.0f && h.sob > 0.0f) gap.pts.push_back({static_cast<float>(h.day), std::max(0.0f, h.pb - h.sob)});
                hi = std::max(hi, h.pb);
            }
            for (auto it = r.history.rbegin(); it != r.history.rend(); ++it)
                c.rows.push_back({dayLong(it->day), clockTime(it->pb), clockTime(it->sob),
                                  it->pb > 0.0f && it->sob > 0.0f ? clockTime(std::max(0.0f, it->pb - it->sob)) : std::string("—")});
            c.head = {"Jour", "Record (PB)", "Somme des meilleurs", "Temps gagnable"};
            c.legend = {{ui.theme().accent, pb.name}, {kBlue, so.name}, {ui.theme().warm, gap.name}};
            c.spec.series = {pb, so, gap};
            c.spec.endLabels = true;
            c.spec.yMin = 0.0f;
            c.spec.yMax = charts::niceTicks(0.0f, hi * 1.08f, 4).back() < hi ? hi * 1.1f : charts::niceTicks(0.0f, hi * 1.08f, 4).back();
            c.spec.yLabel = [](float v) { return clockTime(v); };
            c.spec.xLabel = [](float v) { return dayShort(static_cast<int>(v)); };
            auto hist = r.history;
            c.spec.tip = [hist](float xv, Tip& t) {
                t.title = dayLong(static_cast<int>(xv));
                const SobPoint* p = nullptr;
                for (const auto& h : hist)
                    if (h.day <= static_cast<int>(xv)) p = &h;
                if (!p) return;
                t.rows.push_back({{57, 240, 216, 255}, "Record (PB)", clockTime(p->pb)});
                t.rows.push_back({kBlue, "Somme des meilleurs", clockTime(p->sob)});
                if (p->pb > 0.0f && p->sob > 0.0f) t.rows.push_back({{255, 179, 71, 255}, "Temps gagnable", clockTime(std::max(0.0f, p->pb - p->sob))});
            };
            c.empty = "Finissez le niveau depuis son début pour voir votre record et votre somme des meilleurs.";
            chartCard({x, y, w, ui.s(360)}, c);
            y += ui.s(360) + ui.s(14);
        }
        // Zones to work on.
        std::vector<SegmentReport> reps = segmentReports(r);
        {
            std::vector<SegmentReport> ranked = reps;
            std::stable_sort(ranked.begin(), ranked.end(), [](const SegmentReport& a, const SegmentReport& b) { return a.lost > b.lost; });
            const float rowH = ui.s(78), gapR = ui.s(8);
            const float ch = ui.s(80) + (rowH + gapR) * static_cast<float>(std::max<size_t>(1, ranked.size()));
            Rect in = card({x, y, w, ch}, "ZONES À TRAVAILLER", "Temps perdu en moyenne sur chaque segment : médiane des derniers temps − meilleur temps");
            if (ranked.empty()) ui.labelWrapped("Pas encore de segment chronométré.", {in.x, in.y}, in.w, fz(ui, 15), ui.theme().textDim);
            float ry = in.y;
            for (size_t i = 0; i < ranked.size(); ++i) {
                const SegmentReport& s = ranked[i];
                const bool top3 = i < 3 && s.lost > 0.0f;
                Rect row{in.x, ry, in.w, rowH};
                ui.r().fillRoundedRect(row, ui.s(10), top3 ? Color{24, 44, 58, 255} : Color{20, 28, 46, 255});
                if (top3) ui.r().drawRoundedRect(row, ui.s(10), ui.theme().accent.withAlpha(150), std::max(1.0f, ui.s(1.2f)));
                // Rank, then the button (measured), the text in what is left between them.
                const float rk = ui.s(30);
                Vec2 rc{row.x + ui.s(16) + rk * 0.5f, row.center().y};
                ui.r().fillCircle(rc, rk * 0.5f, top3 ? ui.theme().accent.withAlpha(60) : Color{40, 52, 80, 255});
                ui.label(std::to_string(i + 1), rc, fz(ui, 15), ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
                const std::string bt = "S'entraîner sur ce segment";
                UIContext::ButtonStyle st;
                st.small = true;
                st.fontSize = 16;
                st.enabled = g_.content.level(levelId) != nullptr;
                float bw = ui.text().measure(bt, FontFace::Bold, ui.s(16)).x + ui.s(30);
                Rect br{row.right() - ui.s(12) - bw, row.center().y - ui.s(19), bw, ui.s(38)};
                if (ui.button("train" + std::to_string(i), br, bt, st)) trainSegment(levelId, s.from);
                const float tx = rc.x + rk * 0.5f + ui.s(14), room = br.x - ui.s(14) - tx;
                std::string name = "Segment " + std::to_string(s.index + 1) + " : " + checkpointName(li, s.from, false) + " → " + checkpointName(li, s.to, true);
                std::string meta = "Meilleur " + clockTime(s.best) + " · médiane " + clockTime(s.median) + " · perdu en moyenne " + clockTime(s.lost) +
                                   " · " + std::to_string(s.count) + (s.count > 1 ? " passages" : " passage");
                float nf = ui.fitFont(name, FontFace::Bold, fz(ui, 16), room, 0.8f);
                ui.label(ui.elide(name, FontFace::Bold, nf, room), {tx, row.y + rowH * 0.34f}, nf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
                float mf = ui.fitFont(meta, FontFace::Body, fz(ui, 14), room, 0.8f);
                ui.label(ui.elide(meta, FontFace::Body, mf, room), {tx, row.y + rowH * 0.70f}, mf, kText2, Align::Left, FontFace::Body, VAlign::Middle);
                ry += rowH + gapR;
            }
            y += ch + ui.s(14);
        }
        // Consistency.
        {
            std::vector<Bar> spread, near;
            std::vector<std::vector<std::string>> rows;
            float maxSpread = 0.05f;
            for (const auto& s : reps) maxSpread = std::max(maxSpread, s.spread);
            for (const auto& s : reps) {
                std::string label = "Segment " + std::to_string(s.index + 1);
                std::string title = label + " : " + checkpointName(li, s.from, false) + " → " + checkpointName(li, s.to, true);
                spread.push_back({label, s.spread, pct(s.spread), ui.theme().accent, title,
                                  {{charts::kNoSwatch, "Écart interquartile ÷ médiane", pct(s.spread)}, {charts::kNoSwatch, "Temps gardés", std::to_string(s.count)}}});
                int n = static_cast<int>(std::lround(s.nearBest * static_cast<float>(s.count)));
                near.push_back({label, s.nearBest, pct(s.nearBest), kBlue, title,
                                {{charts::kNoSwatch, "À moins de 5 % du meilleur", std::to_string(n) + " sur " + std::to_string(s.count)}, {charts::kNoSwatch, "Meilleur", clockTime(s.best)}}});
                rows.push_back({title, pct(s.spread), pct(s.nearBest), std::to_string(s.count)});
            }
            const float bh = charts::hBarsHeight(ui, std::max<size_t>(1, reps.size()));
            const float ch = ui.s(80) + ui.s(28) + bh + ui.s(18) + ui.s(28) + bh + ui.s(20);
            Rect in = card({x, y, w, ch}, "CONSTANCE PAR SEGMENT", "Plus la dispersion est faible, plus vos temps se ressemblent", "consistency");
            if (tables_["consistency"]) {
                charts::table(ui, "t/consistency", in, {"Segment", "Dispersion", "À moins de 5 % du meilleur", "Temps"}, rows, pages_["consistency"]);
            } else if (reps.empty()) {
                ui.hotspot("c/spread", in);
                ui.labelWrapped("Pas encore de segment chronométré.", {in.x, in.y}, in.w, fz(ui, 15), ui.theme().textDim);
            } else {
                const float f = fz(ui, 14);
                const std::string a = "Dispersion des temps (écart interquartile ÷ médiane)", b = "Passages à moins de 5 % du meilleur temps";
                ui.label(ui.elide(a, FontFace::Body, f, in.w), {in.x, in.y}, f, ui.theme().textDim);
                charts::hBars(ui, "c/spread", {in.x, in.y + ui.s(28), in.w, bh}, spread, maxSpread, cursorOf("spread"), tip_);
                float yb = in.y + ui.s(28) + bh + ui.s(18);
                ui.label(ui.elide(b, FontFace::Body, f, in.w), {in.x, yb}, f, ui.theme().textDim);
                charts::hBars(ui, "c/near", {in.x, yb + ui.s(28), in.w, bh}, near, 1.0f, cursorOf("near"), tip_);
            }
            y += ch + ui.s(14);
        }
        // Regularity.
        {
            Regularity g = regularity(r, today_);
            std::string trend = !g.sd ? std::string("au moins 3 parties complètes nécessaires")
                                : !g.sdBefore ? std::string("pas assez de parties il y a 30 jours")
                                : g.trend > 0 ? std::string("▲ plus régulier qu'il y a 30 jours")
                                : g.trend < 0 ? std::string("▼ moins régulier qu'il y a 30 jours")
                                              : std::string("= aussi régulier qu'il y a 30 jours");
            std::vector<TileData> ts;
            ts.push_back({"PARTIES ARRIVÉES AU BOUT", g.attempts > 0 ? pct(static_cast<double>(g.finished) / g.attempts) : std::string("—"),
                          std::to_string(g.finished) + " sur " + std::to_string(g.attempts) + " tentatives"});
            ts.push_back({"ÉCART TYPE (20 DERNIÈRES)", g.sd ? clockTime(*g.sd) : std::string("—"), trend});
            ts.push_back({"MÉDIANE (20 DERNIÈRES)", g.rollingMedian.empty() ? std::string("—") : clockTime(medianOf([&] {
                                                                                               std::vector<float> v;
                                                                                               for (const auto& f : g.last) v.push_back(f.time);
                                                                                               return v;
                                                                                           }())),
                          "temps d'arrivée"});
            y += tiles(x, y, w, ts) + ui.s(14);
            ChartCard c;
            c.id = "regularity";
            c.title = "TEMPS DES 20 DERNIÈRES PARTIES COMPLÈTES";
            c.sub = "Chaque partie, et la médiane des 5 dernières";
            Series dots{"Chaque partie", ui.theme().accent, Series::Kind::Dots, false, {}};
            Series med{"Médiane des 5 dernières", ui.theme().accent, Series::Kind::Line, false, {}};
            float lo = 1e9f, hi = 0.0f;
            const int firstN = r.finished - static_cast<int>(g.last.size()) + 1;
            for (size_t i = 0; i < g.last.size(); ++i) {
                float n = static_cast<float>(firstN + static_cast<int>(i));
                dots.pts.push_back({n, g.last[i].time});
                med.pts.push_back({n, g.rollingMedian[i]});
                lo = std::min(lo, g.last[i].time);
                hi = std::max(hi, g.last[i].time);
            }
            for (size_t i = g.last.size(); i-- > 0;)
                c.rows.push_back({std::to_string(firstN + static_cast<int>(i)), dayLong(g.last[i].day), clockTime(g.last[i].time), clockTime(g.rollingMedian[i])});
            c.head = {"Partie", "Jour", "Temps", "Médiane des 5 dernières"};
            c.legend = {{ui.theme().accent, med.name}, {ui.theme().accent, dots.name}};
            c.legendDot = 1;
            c.spec.series = {dots, med};
            c.spec.integerX = true;
            const float pad = std::max(1.0f, (hi - lo) * 0.25f);
            c.spec.yMin = g.last.empty() ? 0.0f : std::max(0.0f, std::floor(lo - pad));
            c.spec.yMax = g.last.empty() ? 60.0f : std::ceil(hi + pad);
            c.spec.yLabel = [](float v) { return clockTime(v); };
            c.spec.xLabel = [](float v) { return "n° " + std::to_string(static_cast<int>(std::lround(v))); };
            auto last = g.last;
            auto medians = g.rollingMedian;
            c.spec.tip = [last, medians, firstN](float xv, Tip& t) {
                const int i = static_cast<int>(std::lround(xv)) - firstN;
                if (i < 0 || i >= static_cast<int>(last.size())) return;
                t.title = "Partie n° " + std::to_string(static_cast<int>(std::lround(xv))) + " — " + dayLong(last[static_cast<size_t>(i)].day);
                t.rows.push_back({{57, 240, 216, 255}, "Médiane des 5 dernières", clockTime(medians[static_cast<size_t>(i)])});
                t.rows.push_back({charts::kNoSwatch, "Temps", clockTime(last[static_cast<size_t>(i)].time)});
            };
            c.empty = "Aucune partie complète pour l'instant.";
            chartCard({x, y, w, ui.s(330)}, c);
            y += ui.s(330) + ui.s(14);
        }
        y += note(x, y, w,
                  "Comment c'est calculé : seuls comptent les segments complets (d'un point de passage au suivant, ou jusqu'à l'arrivée) des parties "
                  "de la campagne jouées depuis le début d'un niveau classique (hors EXTRÊME), ni entraînement, ni replay, ni mode Invincible. Les "
                  "temps viennent du chronomètre du niveau (les mêmes que la course aux fantômes). Somme des meilleurs (SOB) = meilleur temps de "
                  "chaque segment du parcours de votre record, parties complètes et entraînement par segment (même version du niveau) confondus, "
                  "additionnés ; l'entraînement ne compte que là : jamais dans le record, les temps des segments ni la régularité. Temps "
                  "gagnable = record − SOB. Temps perdu = médiane des 50 derniers "
                  "temps du segment − son meilleur temps. Dispersion = écart interquartile ÷ médiane. Régularité : écart type des 20 dernières "
                  "parties complètes, comparé à celui des 20 parties d'avant il y a 30 jours (plus de 10 % d'écart pour parler d'un changement). "
                  "Une nouvelle version du niveau repart de zéro.");
        return y - y0;
    }

    /// ‹ the level › — returns true when it changed. `idx` is its place in `order`.
    bool levelPicker(float x, float y, float w, const std::string& id, const std::vector<std::pair<int, std::string>>& order, int& idx) {
        UIContext& ui = g_.ui;
        UIContext::ButtonStyle st;
        st.small = true;
        const int n = static_cast<int>(order.size());
        bool changed = false;
        st.enabled = n > 1;
        if (ui.button(id + "prev", {x, y, ui.s(52), ui.s(40)}, "‹", st)) idx = (idx + n - 1) % n, changed = true;
        if (ui.button(id + "next", {x + w - ui.s(52), y, ui.s(52), ui.s(40)}, "›", st)) idx = (idx + 1) % n, changed = true;
        idx = std::clamp(idx, 0, n - 1);
        const std::string& lid = order[static_cast<size_t>(idx)].second;
        std::string name = lid;
        if (const LevelEntry* e = g_.content.level(lid); e && !e->name.empty()) name = e->name;
        else if (auto it = data().runs.find(lid); it != data().runs.end() && !it->second.name.empty()) name = it->second.name;
        else if (auto il = data().levels.find(lid); il != data().levels.end() && !il->second.name.empty()) name = il->second.name;
        std::string where = "Niveau " + std::to_string(idx + 1) + " sur " + std::to_string(n);
        const float room = w - 2 * ui.s(52) - ui.s(32);
        const float wf = fz(ui, 14);
        const float ww = ui.text().measure(where, FontFace::Body, ui.s(wf)).x;
        const float nroom = room - ww - ui.s(20);
        float nf = ui.fitFont(name, FontFace::Bold, 20, nroom, 0.7f);
        float nw = std::min(nroom, ui.text().measure(name, FontFace::Bold, ui.s(nf)).x);
        float cx = x + w * 0.5f - (nw + ui.s(20) + ww) * 0.5f;
        ui.label(ui.elide(name, FontFace::Bold, nf, nroom), {cx, y + ui.s(20)}, nf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(where, {cx + nw + ui.s(20), y + ui.s(20)}, wf, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        return changed;
    }

    // ------------------------------------------------------------------ Passages difficiles

    float drawHard(float x, float y0, float w) {
        UIContext& ui = g_.ui;
        float y = y0;
        std::vector<std::pair<int, std::string>> order;
        for (const auto& [id, l] : data().levels)
            if (l.any()) order.push_back({l.lastDay, id});
        std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (!order.empty()) {
            int idx = 0;
            for (size_t i = 0; i < order.size(); ++i)
                if (order[i].second == wantSlices_) idx = static_cast<int>(i);
            levelPicker(x, y, w, "slices", order, idx);
            wantSlices_ = order[static_cast<size_t>(idx)].second;
            y += ui.s(40) + ui.s(14);
            const LevelSlices& l = data().levels.at(wantSlices_);
            const LevelInfo* li = info(wantSlices_);
            charts::Heat h;
            auto ds = sliceDifficulties(l, today_);
            float mx = 0.0f;
            for (int i = 0; i < kSlices; ++i) {
                size_t k = static_cast<size_t>(i);
                h.values[k] = ds[k];
                h.passes[k] = l.passes[k];
                h.deaths[k] = l.deaths[k];
                mx = std::max(mx, ds[k]);
            }
            h.maxValue = std::max(0.05f, mx);
            if (li) {
                h.relief = li->relief;
                for (const auto& [at, id] : li->checkpoints) h.checkpoints.push_back(at);
            }
            const float ch = ui.s(330);
            Rect in = card({x, y, w, ch}, "OÙ VOUS TOMBEZ", "Le niveau découpé en 40 tranches : plus c'est clair, plus on y meurt souvent", "heat");
            const float legendH = ui.s(30);
            Rect band{in.x, in.y, in.w, in.h - legendH};
            if (tables_["heat"]) {
                std::vector<std::vector<std::string>> rows;
                for (int i = 0; i < kSlices; ++i) {
                    size_t k = static_cast<size_t>(i);
                    if (l.passes[k] <= 0) continue;
                    rows.push_back({std::to_string(static_cast<int>(std::lround(i * 2.5))) + "–" + std::to_string(static_cast<int>(std::lround((i + 1) * 2.5))) + " %",
                                    std::to_string(l.passes[k]), std::to_string(l.deaths[k]), pct(ds[k])});
                }
                charts::table(ui, "t/heat", in, {"Tranche", "Passages", "Morts", "Difficulté"}, rows, pages_["heat"]);
            } else {
                charts::heatBand(ui, "c/heat", band, h, cursorOf("heat"), tip_);
                // The scale: one hue from dark to light, its two ends written.
                const float f = fz(ui, 13);
                const std::string lo = "0 %", hi = pct(h.maxValue) + " de morts par passage";
                const float lw = ui.text().measure(lo, FontFace::Body, ui.s(f)).x, hw = ui.text().measure(hi, FontFace::Body, ui.s(f)).x;
                const float ly = in.bottom() - legendH * 0.5f;
                const float gx = in.x + lw + ui.s(10), gw = std::min(ui.s(220), in.w - lw - hw - ui.s(20));
                if (gw > ui.s(40)) {
                    ui.label(lo, {in.x, ly}, f, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
                    for (int i = 0; i < 20; ++i)
                        ui.r().fillRect({gx + gw * i / 20.0f, ly - ui.s(5), gw / 20.0f + 1.0f, ui.s(10)}, charts::heatColor(i / 19.0f));
                    ui.label(hi, {gx + gw + ui.s(10), ly}, f, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
                }
            }
            y += ch + ui.s(14);
        }
        // The 5 hardest passages, every level and boss phase together.
        auto top = hardestPassages(data(), today_, 5);
        {
            const float rowH = ui.s(96), gapR = ui.s(8);
            const float ch = ui.s(70) + (rowH + gapR) * static_cast<float>(std::max<size_t>(1, top.size()));
            Rect in = card({x, y, w, ch}, "VOS 5 PASSAGES LES PLUS DIFFICILES", "Tous les niveaux et toutes les phases de boss");
            if (top.empty())
                ui.labelWrapped("Aucun passage ne se détache encore (il en faut au moins 5 passages pour en juger).", {in.x, in.y}, in.w, fz(ui, 15),
                                ui.theme().textDim);
            float ry = in.y;
            for (size_t i = 0; i < top.size(); ++i) {
                const HardPassage& h = top[i];
                Rect row{in.x, ry, in.w, rowH};
                ui.r().fillRoundedRect(row, ui.s(10), Color{20, 28, 46, 255});
                const float rk = ui.s(30);
                Vec2 rc{row.x + ui.s(16) + rk * 0.5f, row.center().y};
                ui.r().fillCircle(rc, rk * 0.5f, Color{40, 52, 80, 255});
                ui.label(std::to_string(i + 1), rc, fz(ui, 15), ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
                const bool boss = h.kind == HardPassage::Kind::BossPhase;
                std::string bt;
                bool can = false;
                if (boss) {
                    bt = "S'entraîner sur cette phase";
                    can = g_.content.enemy(h.id) != nullptr;
                } else {
                    bt = "S'entraîner sur ce segment";
                    can = g_.content.level(h.id) != nullptr && info(h.id) != nullptr;
                }
                UIContext::ButtonStyle st;
                st.small = true;
                st.fontSize = 16;
                st.enabled = can;
                float bw = ui.text().measure(bt, FontFace::Bold, ui.s(16)).x + ui.s(30);
                Rect br{row.right() - ui.s(12) - bw, row.center().y - ui.s(19), bw, ui.s(38)};
                if (ui.button("hard" + std::to_string(i), br, bt, st)) {
                    if (boss) {
                        g_.app.switchScene(makeBossTraining(g_, h.id, h.phase), 0.3f);
                    } else if (const LevelInfo* li = info(h.id)) {
                        // The section training from the last checkpoint before the passage.
                        std::string from;
                        const float at = static_cast<float>(h.from) / static_cast<float>(kSlices);
                        for (const auto& [cpAt, id] : li->checkpoints)
                            if (cpAt <= at + 0.001f) from = id;
                        trainSegment(h.id, from);
                    }
                }
                const float tx = rc.x + rk * 0.5f + ui.s(14), room = br.x - ui.s(14) - tx;
                std::string name, meta;
                if (boss) {
                    name = h.name + " — phase " + std::to_string(h.phase + 1) + " / " + std::to_string(h.phases);
                    meta = (h.attack.empty() ? std::string() : "Attaque fatale la plus fréquente : " + h.attack + " · ") + std::to_string(h.deaths) +
                           (h.deaths > 1 ? " défaites" : " défaite") + " sur " + std::to_string(h.passes) + " tentatives arrivées là · difficulté " + pct(h.difficulty);
                } else {
                    std::string lname = h.name;
                    if (const LevelEntry* e = g_.content.level(h.id); e && !e->name.empty()) lname = e->name;
                    name = lname;
                    meta = "Tranche " + std::to_string(static_cast<int>(std::lround(h.from * 2.5))) + "–" +
                           std::to_string(static_cast<int>(std::lround((h.to + 1) * 2.5))) + " % · " + std::to_string(h.deaths) +
                           (h.deaths > 1 ? " morts" : " mort") + " pour " + std::to_string(h.passes) + " passages · difficulté " + pct(h.difficulty);
                }
                float nf = ui.fitFont(name, FontFace::Bold, fz(ui, 16), room, 0.8f);
                ui.label(ui.elide(name, FontFace::Bold, nf, room), {tx, row.y + ui.s(24)}, nf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
                float mf = ui.fitFont(meta, FontFace::Body, fz(ui, 14), room, 0.8f);
                ui.label(ui.elide(meta, FontFace::Body, mf, room), {tx, row.y + ui.s(50)}, mf, kText2, Align::Left, FontFace::Body, VAlign::Middle);
                Rect track{tx, row.y + ui.s(70), std::max(0.0f, room), ui.s(8)};
                ui.r().fillRoundedRect(track, ui.s(4), Color{14, 20, 36, 255});
                ui.r().fillRoundedRect({track.x, track.y, std::max(ui.s(6), track.w * std::clamp(h.difficulty, 0.0f, 1.0f)), track.h}, ui.s(4), ui.theme().warm);
                ry += rowH + gapR;
            }
            y += ch + ui.s(14);
        }
        y += note(x, y, w,
                  "Algorithme : pour chaque tranche, difficulté = (morts + 1) ÷ (passages + 2), un lissage qui évite qu'une seule mort rende une "
                  "tranche « impossible ». Les parties récentes pèsent plus (demi-vie de 14 jours) pour suivre vos progrès. Les tranches voisines "
                  "au-dessus de 2 × la médiane du niveau sont fusionnées en un passage, retenu seulement avec au moins 5 passages ; sa difficulté = "
                  "1 − le produit des (1 − difficulté de chaque tranche). Phase de boss : défaites dans la phase ÷ tentatives arrivées à la phase "
                  "(au moins 5). Le classement mélange tous les niveaux et les phases de boss. « S'entraîner sur ce segment » ouvre l'entraînement "
                  "par segment depuis le point de passage précédent ; « S'entraîner sur cette phase », l'entraînement contre le boss sur cette phase.");
        return y - y0;
    }

    // ------------------------------------------------------------------ Données et sauvegarde

    float drawData(float x, float y0, float w) {
        UIContext& ui = g_.ui;
        float y = y0;
        const std::string path = Paths::toUtf8(g_.playStats.file());
        struct Block {
            std::string title, body;
            bool fresh = false;  // new in 2.55: « NOUVEAU » during the first visit
        };
        std::vector<Block> blocks = {
            {"CE QUI EST ENREGISTRÉ",
             "Des compteurs par jour : temps joué, morts, menaces et esquives, attaques lancées et réussies (par attaque), origine des coups "
             "reçus, étoiles des succès. Par niveau : passages et morts dans chacune des 40 tranches. Par boss : chaque tentative (part des "
             "points de vie retirés, phase atteinte, attaque fatale). Par niveau classique : les temps de chaque segment, vos arrivées, votre "
             "record et votre somme des meilleurs. Aucune donnée personnelle, aucune position."},
            {"OÙ ET QUAND",
             "Dans le fichier stats.json, à côté de vos autres sauvegardes, avec un numéro de format pour les futures mises à jour. Il est écrit "
             "à la fin de chaque tentative (une chute, la fin ou l'abandon d'une partie) et quand le jeu se ferme, sur un fil secondaire (jamais "
             "pendant que vous jouez), par écriture atomique : fichier temporaire puis remplacement, comme les records."},
            {"TAILLE MAÎTRISÉE",
             "Un résumé par jour, gardé 365 jours (les jours plus anciens sont fusionnés par mois). Par boss, les 200 dernières tentatives ; par "
             "segment, les 50 derniers temps. Au total, moins de 300 Ko même après des années de jeu ou une migration."},
            {"HORS DE LA SIMULATION",
             "Les statistiques lisent ce qui se passe sans rien changer à la partie : replays, records et classements ne bougent pas. Les replays "
             "relus et les parties d'amis regardées ne sont pas comptés, ni les tests de l'éditeur ni le mode Invincible ; l'entraînement est "
             "compté à part."},
            {"AVEC VOTRE PROGRESSION",
             "Connecté à Cosmic Online, le jeu envoie vos statistiques dans la copie privée de votre progression, avec vos records et vos succès "
             "(aucun autre joueur ne la voit). Sans compte ni Internet, rien ne part et tout fonctionne ; pour les garder vous-même, copiez "
             "stats.json. Si le fichier est abîmé, l'onglet repart de zéro et l'ancien fichier est gardé en stats.json.bak.",
             true},
            {"MIGRER VERS UN NOUVEAU COMPTE",
             "La progression restaurée apporte ses statistiques, ajoutées aux vôtres sans double compte : chaque jour garde le plus grand des "
             "deux compteurs, chaque tentative de boss n'est comptée qu'une fois (les 200 plus récentes gardées), les temps des segments sont "
             "réunis, le meilleur record et la meilleure somme des meilleurs sont gardés. Restaurer deux fois la même copie ne change rien. "
             "Avant, vos sauvegardes, stats.json compris, sont copiées dans saves › avant_restauration_…",
             true},
        };
        const float gap = ui.s(14);
        const int per = w >= ui.s(1000) ? 2 : 1;
        const float cw = (w - gap * static_cast<float>(per - 1)) / static_cast<float>(per);
        const float f = fz(ui, 15);
        // Each card as tall as its text (measured); the cards of a row as tall as the tallest.
        for (size_t i = 0; i < blocks.size(); i += static_cast<size_t>(per)) {
            float rowH = 0.0f;
            for (size_t k = i; k < std::min(blocks.size(), i + static_cast<size_t>(per)); ++k)
                rowH = std::max(rowH, ui.text().measureWrapped(blocks[k].body, FontFace::Body, ui.s(f), cw - ui.s(32)).y);
            rowH += ui.s(70) + ui.s(16);
            for (size_t k = i; k < std::min(blocks.size(), i + static_cast<size_t>(per)); ++k) {
                Rect r{x + (cw + gap) * static_cast<float>(k - i), y, cw, rowH};
                Rect in = card(r, blocks[k].title, {}, {}, {0, 0, 0, 0}, blocks[k].fresh && dataFresh_);
                ui.labelWrapped(blocks[k].body, {in.x, in.y}, in.w, f, kText2);
            }
            y += rowH + gap;
        }
        // Where it is, whether it records, and the reset.
        {
            const float ch = ui.s(232);
            Rect in = card({x, y, w, ch}, "VOUS DÉCIDEZ", {});
            std::string state = g_.playStats.enabled ? "L'enregistrement est activé (Paramètres › Jeu › « Enregistrer mes statistiques »)."
                                                     : "L'enregistrement est désactivé (Paramètres › Jeu › « Enregistrer mes statistiques ») : rien de nouveau n'est compté.";
            const float sf = fz(ui, 15);
            float used = ui.labelWrapped(state, {in.x, in.y}, in.w, sf, kText2).y;
            const std::string file = "Fichier : " + path;
            {
                i18n::Verbatim pathAsIs;  // a path is shown as it is
                ui.label(ui.elide(file, FontFace::Body, fz(ui, 14), in.w), {in.x, in.y + used + ui.s(8)}, fz(ui, 14), ui.theme().textDim);
            }
            UIContext::ButtonStyle danger;
            danger.danger = true;
            danger.fontSize = 20;
            const std::string rt = "Réinitialiser les statistiques";
            float bw = ui.text().measure(rt, FontFace::Bold, ui.s(20)).x + ui.s(40);
            Rect br{in.x, in.bottom() - ui.s(50), bw, ui.s(50)};
            if (ui.button("reset", br, rt, danger)) confirmReset_ = true;
            UIContext::ButtonStyle st;
            st.fontSize = 20;
            const std::string ot = "Ouvrir les Paramètres";
            float ow = ui.text().measure(ot, FontFace::Bold, ui.s(20)).x + ui.s(40);
            if (br.right() + ui.s(14) + ow <= in.right() && ui.button("settings", {br.right() + ui.s(14), br.y, ow, br.h}, ot, st))
                g_.app.pushScene(makeSettingsScene(g_, true));
            if (!status_.empty() && br.right() + ui.s(14) + ow + ui.s(20) < in.right()) {
                float room = in.right() - (br.right() + ui.s(14) + ow + ui.s(20));
                ui.label(ui.elide(status_, FontFace::Body, fz(ui, 15), room), {in.right(), br.center().y}, fz(ui, 15), ui.theme().success, Align::Right,
                         FontFace::Body, VAlign::Middle);
            }
            y += ch + gap;
        }
        return y - y0;
    }

    void drawConfirm() {
        UIContext& ui = g_.ui;
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{0, 0, 0, 160});
        Rect m = Rect::fromCenter({ui.screenW() * 0.5f, ui.screenH() * 0.5f}, {std::min(ui.screenW() - ui.s(40), ui.s(760)), ui.s(250)});
        ui.panel(m, true, 16);
        const std::string head = "Réinitialiser les statistiques ?";
        float hf = ui.fitFont(head, FontFace::Bold, 26, m.w - ui.s(60), 0.6f);
        ui.label(head, {m.center().x, m.y + ui.s(42)}, hf, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        ui.labelWrapped("Toutes vos statistiques (jours, combats, boss, niveaux, passages) seront effacées de cet ordinateur. Vos records, succès et "
                        "statistiques de boss d'avant ne changent pas.",
                        {m.x + ui.s(30), m.y + ui.s(76)}, m.w - ui.s(60), fz(ui, 16), kText2);
        const float bw = (m.w - ui.s(90)) / 2.0f;
        UIContext::ButtonStyle st, danger;
        danger.danger = true;
        if (ui.button("no", {m.x + ui.s(30), m.bottom() - ui.s(76), bw, ui.s(52)}, "Annuler", st) || ui.backPressed()) confirmReset_ = false;
        else if (ui.button("yes", {m.right() - ui.s(30) - bw, m.bottom() - ui.s(76), bw, ui.s(52)}, "Tout effacer", danger)) {
            g_.playStats.reset();
            confirmReset_ = false;
            status_ = "Statistiques réinitialisées.";
        }
        if (!ui.hasFocus()) ui.setFocus("stats/no");
    }

    GameServices& g_;
    MenuBackground bg_;
    int tab_ = 0;
    int period_ = 30;
    int today_ = 0;
    float contentH_[kTabCount] = {};
    Tip tip_;
    std::map<std::string, bool> tables_;
    std::map<std::string, int> pages_, cursors_;
    /// The keyboard cursor of a chart: -1 until it moves (the chart then starts on its last point).
    int& cursorOf(const std::string& id) { return cursors_.try_emplace(id, -1).first->second; }
    std::map<std::string, std::shared_future<LevelInfo>> infos_;
    int group_ = 0;
    std::string phaseBoss_, wantSlices_, wantRuns_, focusChart_;
    bool confirmReset_ = false;
    bool dataFresh_ = false;  // « Données et sauvegarde » was new when opened (2.55)
    std::string status_;
};

}  // namespace

std::unique_ptr<Scene> makeStatistics(GameServices& g, int tab) { return std::make_unique<StatisticsScene>(g, tab); }

}  // namespace cosmic
