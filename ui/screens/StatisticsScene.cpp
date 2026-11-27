// Main menu → Statistiques (2.54; 2.91: redesigned, livelier, with the game's own pictures): the
// player's own statistics, computed from their real runs and kept on this computer
// (game/records/PlayStats.h, saves/stats.json). Six sections:
//  * Vue d'ensemble: the period chosen (7 / 30 / 90 days) — its time played and its calendar, four
//    tiles against the previous period of the same length, each with its small curve; the stars of
//    the achievements; deaths per hour; « Récemment joué » (2.91: the levels and bosses played last);
//  * Combat: dodge rate and precision day by day, precision per attack, where the blows come from;
//  * Boss (StatisticsBoss.cpp): every boss, three at a time on their planet's banner, the best
//    progress attempt after attempt, the sheet and the defeats per phase of the boss chosen;
//  * Niveaux (StatisticsLevels.cpp): a classic level's Sum of Best and its segments, each with its
//    picture and what is met there, the record and the Sum of Best over time, the zones to work on,
//    their consistency, the regularity of the full runs;
//  * Passages difficiles (StatisticsLevels.cpp): where the player falls in a level, over the level's
//    own picture, and the 5 hardest passages of every level and boss phase, with their training;
//  * Données et sauvegarde: what is kept, where, how big (2.91: the share of each part of the file),
//    how it travels with the copy of the progression, recording on or off, the reset.
// Every chart: tooltip under the mouse, a cursor with ← → when focused, a « Tableau » view. The
// screen starts empty and fills up with the runs: no example data anywhere.

#include "ui/screens/StatisticsScene.h"

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShapeProbe.h"
#include "game/GameServices.h"
#include "game/content/CreaturePlaces.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/PersonalBests.h"
#include "game/powers/PowerDefinition.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/CosmicStyle.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

using charts::Bar;
using charts::LineSpec;
using charts::Series;
using playstats::kOrigins;
using namespace statsui;
using statsart::drawEnemy;
using statsart::drawSprite;
using statsart::drawSpriteAt;

// ------------------------------------------------------------------ French numbers and dates

namespace statsui {

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
std::string clockTime(float sec) {
    if (sec < 0.0f) return "—";
    long cs = std::lround(static_cast<double>(sec) * 100.0);
    char b[32];
    if (cs < 6000) std::snprintf(b, sizeof b, "%ld,%02ld s", cs / 100, cs % 100);
    else std::snprintf(b, sizeof b, "%ld:%02ld,%02ld", cs / 6000, (cs / 100) % 60, cs % 100);
    return b;
}
std::string clockLong(float sec) {
    if (sec < 0.0f) return "—";
    long cs = std::lround(static_cast<double>(sec) * 100.0);
    char b[32];
    std::snprintf(b, sizeof b, "%ld:%02ld,%02ld", cs / 6000, (cs / 100) % 60, cs % 100);
    return b;
}
namespace {
const char* const kMonths[12] = {"janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."};
const char* const kWeekdays[7] = {"Jeudi", "Vendredi", "Samedi", "Dimanche", "Lundi", "Mardi", "Mercredi"};  // 1970-01-01 was a Thursday
}  // namespace
std::string dayShort(int day) {
    int y, m, d;
    civilFromDays(day, y, m, d);
    char b[16];
    std::snprintf(b, sizeof b, "%02d/%02d", d, m);
    return b;
}
std::string dayLong(int day) {
    int y, m, d;
    civilFromDays(day, y, m, d);
    return std::to_string(d) + " " + kMonths[std::clamp(m, 1, 12) - 1] + " " + std::to_string(y);
}
std::string dayNamed(int day) { return std::string(kWeekdays[((day % 7) + 7) % 7]) + " " + dayShort(day); }
std::string dayAgo(int day, int today) {
    if (day <= 0) return {};
    if (day == today) return "AUJOURD'HUI";
    if (day == today - 1) return "HIER";
    return dayShort(day);
}
float lineHeight(UIContext& ui, FontFace f, float px) {
    i18n::Verbatim probe;  // a line's height, not a text (never translated, never judged)
    return ui.text().measure("Ag", f, px).y;
}

}  // namespace statsui

namespace {

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

/// Characters the title face lacks: drawn with the body's bold face.
bool titleLacks(uint32_t cp) { return cp == 0x00B1 || cp == 0x00B7 || cp == 0x25B2 || cp == 0x25BC || cp == 0x2192 || cp == 0x2190; }

/// 2.92.1: what the page draws while it is set (Renderer::setListener, the previous listener given back by
/// stop()): `shown`, each shape cut by the clip in force (the page's scroll region `view`, its cards' own
/// clips) — what is seen —; `reach`, the shapes not cut by the scroll region — where the page goes (a shape
/// inside a card's own clip counts with its card, drawn outside it). Shown::page and Shown::reach (tests).
class PageProbe final : public ShapeListener {
public:
    PageProbe(Renderer& r, const Rect& view) : r_(r), before_(r.listener()), view_(view) { r_.setListener(this); }
    ~PageProbe() override { stop(); }
    PageProbe(const PageProbe&) = delete;
    PageProbe& operator=(const PageProbe&) = delete;
    void stop() {
        if (on_) r_.setListener(before_);
        on_ = false;
    }
    Rect shown() const { return shown_; }
    Rect reach() const { return reach_; }

    void triangle(Vec2 a, Vec2 b, Vec2 c) override {
        add(std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}));
    }
    void segment(Vec2 a, Vec2 b) override { add(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)); }
    void rect(const Rect& rc) override { add(rc.x, rc.y, rc.right(), rc.bottom()); }
    void glow(Vec2 center, float radius) override {
        const float k = radius * ShapeListener::kGlowCore;
        add(center.x - k, center.y - k, center.x + k, center.y + k);
    }

private:
    static void unite(Rect& into, const Rect& r) { into = into.empty() ? r : into.united(r); }
    void add(float x0, float y0, float x1, float y1) {
        // A line one pixel thick at least (a horizontal rule has no height).
        const Rect b{x0 - 0.5f, y0 - 0.5f, x1 - x0 + 1.0f, y1 - y0 + 1.0f};
        Rect clip = view_;
        if (SDL_Renderer* sr = r_.sdl(); sr && SDL_RenderClipEnabled(sr)) {
            SDL_Rect c{};
            if (SDL_GetRenderClipRect(sr, &c)) clip = {static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.w), static_cast<float>(c.h)};
        }
        // The scroll region alone (its clip in whole pixels, rounded or cut once a card's own clip is
        // given back): the shape counts as drawn.
        const float px = 3.0f;
        const bool regionOnly = std::abs(clip.x - view_.x) <= px && std::abs(clip.y - view_.y) <= px && std::abs(clip.right() - view_.right()) <= px &&
                                std::abs(clip.bottom() - view_.bottom()) <= px;
        if (regionOnly) unite(reach_, b);
        if (const Rect seen = b.intersection(clip); !seen.empty()) unite(shown_, seen);
    }
    Renderer& r_;
    ShapeListener* before_;
    Rect view_;
    bool on_ = true;
    Rect shown_, reach_;
};

}  // namespace

std::vector<StatsLevelGroup> statisticsLevelGroups(const GameServices& g, const std::function<bool(const std::string&)>& hasData) {
    // 2.84: from the content, never a list written here: a planet or a level added to the campaign is in.
    std::vector<StatsLevelGroup> out;
    std::set<std::string> seen;
    for (const PlanetData* p : g.content.campaignPlanets()) {
        StatsLevelGroup grp{p->id, p->name, {}};
        for (const LevelEntry* l : g.content.levelsForPlanet(p->id))
            if (seen.insert(l->id).second) grp.levels.push_back(l->id);
        if (!grp.levels.empty()) out.push_back(std::move(grp));
    }
    // The other levels with statistics (a creation, an online level, a level no longer in the campaign).
    StatsLevelGroup other{"", "Autres niveaux", {}};
    for (const auto& [id, r] : g.playStats.data().runs)
        if (!seen.count(id) && hasData(id) && seen.insert(id).second) other.levels.push_back(id);
    for (const auto& [id, l] : g.playStats.data().levels)
        if (!seen.count(id) && hasData(id) && seen.insert(id).second) other.levels.push_back(id);
    if (!other.levels.empty()) out.push_back(std::move(other));
    return out;
}

std::vector<std::string> statisticsBosses(const GameServices& g) {
    // 2.84: the bosses fought first (the most recent first), then every other boss of the content (guardians,
    // giga-bosses, the raids' gigaboss and their heart) and any boss of the older statistics.
    std::vector<std::string> out = bossesByRecency(g.playStats.data());
    std::set<std::string> seen(out.begin(), out.end());
    for (const EnemyDefinition* d : g.content.enemies())
        if (d->isBoss && !d->tutorial && seen.insert(d->id).second) out.push_back(d->id);
    for (const auto& [id, rec] : g.bossStats.all())
        if (seen.insert(id).second) out.push_back(id);
    return out;
}

// ------------------------------------------------------------------ the scene

StatisticsScene::StatisticsScene(GameServices& g, int tab) : g_(g), tab_(std::clamp(tab, 0, kTabCount - 1)), art_(g) {}

StatisticsScene::~StatisticsScene() = default;

const PlayStatsData& StatisticsScene::data() const { return g_.playStats.data(); }

float StatisticsScene::fz(float size) const { return charts::fz(g_.ui, size); }

void StatisticsScene::onEnter() {
    bg_.init(g_);
    atlas_ = officialCreatureAtlas(g_.content);  // where the bosses are fought (worker thread)
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
            else if (k == "boss") selBoss_ = v;
            else if (k == "table") tables_[v] = true;
            else if (k == "focus") focusChart_ = v;
            else if (k == "period") period_ = std::clamp(std::atoi(v.c_str()), 7, 90);
        }
    }
    if (!selBoss_.empty()) group_ = -1;  // Boss opens on the page of the boss asked
    if (!focusChart_.empty()) {
        g_.ui.setFocus("stats/c/" + focusChart_);  // the charts are "c/<id>" under "stats"
        g_.ui.setUsingPointer(false);              // 2.92.1: as reached with the keys: its cursor and tooltip show
    }
    // « NOUVEAU » (2.54): this screen is seen once opened.
    if (g_.ui.featureSeen && g_.ui.isNew("stats.player")) g_.ui.featureSeen("stats.player");
}

void StatisticsScene::update(float dt) {
    bg_.update(dt);
    anim_ += dt;
    if (!atlas_) atlas_ = officialCreatureAtlas(g_.content);
    art_.update();  // the levels' pictures: a few milliseconds per frame, before the frame is drawn
    if (breakdownJob_ && breakdownJob_->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        breakdown_ = breakdownJob_->get();
        breakdownJob_.reset();
    }
}

void StatisticsScene::render(float) {
    UIContext& ui = g_.ui;
    ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
    bg_.render(g_, 0.6f);
    tip_ = Tip{};
    today_ = g_.playStats.today();
    const float W = ui.screenW(), m = ui.s(44);
    ui.pushId("stats");
    ui.inputEnabled = !confirmReset_;
    Rect back{W - m - ui.s(200), ui.s(26), ui.s(200), ui.s(46)};
    bool leave = cframe::back(ui, "back", back);
    // The title, then the tabs on the same row when every label fits there (measured), else under it.
    const std::vector<std::string> labels = {"Vue d'ensemble", "Combat", "Boss", "Niveaux", "Passages difficiles", "Données et sauvegarde"};
    const float titleW = cframe::measureTitle(ui, "STATISTIQUES", 40).x;
    const float tabsX = m + titleW + ui.s(24), tabsRoom = back.x - ui.s(20) - tabsX;
    float widest = 0.0f;
    for (const auto& l : labels) widest = std::max(widest, ui.text().measure(l, FontFace::Bold, ui.s(fz(17))).x);
    const bool oneRow = tabsRoom > 0.0f && widest + ui.s(30) <= tabsRoom / static_cast<float>(labels.size());
    cframe::title(ui, "STATISTIQUES", {m, ui.s(49)}, 40, Align::Left, VAlign::Middle, (oneRow ? tabsX - ui.s(24) : back.x - ui.s(24)) - m);
    const Rect tabsR = oneRow ? Rect{tabsX, ui.s(26), tabsRoom, ui.s(46)} : Rect{m, ui.s(92), W - 2 * m, ui.s(48)};
    const int before = tab_;
    cframe::tabs(ui, "tabs", tabsR, labels, tab_);
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
    } else if (tab_ != kTabData && ui.isNew("stats.file_parts")) {
        ui.drawNewPill(tabsR.right() - ui.s(10), tabsR.y);  // 2.91: « Répartition du fichier », until seen there
    }
    // 2.92.1: the footer first, measured (what its hints really cover): the page ends above its band,
    // never under it.
    const Rect footer = drawFooter();
    const float viewTop = tabsR.bottom() + ui.s(10);
    const Rect view{m, viewTop, W - 2 * m, std::max(ui.s(40), footer.y - ui.s(10) - viewTop)};
    const int shownTab = tab_;  // a card of « Récemment joué » may open another section while the page is drawn
    ui.beginScroll("page" + std::to_string(shownTab), view, contentH_[shownTab]);
    const float x = view.x, w = view.w - ui.s(16);
    const float top = ui.s(12);  // room for the « NOUVEAU » pill of a first row
    const float offset = ui.scrollOffset();
    const float y = view.y - offset + top;
    float h = 0.0f;  // the height each tab's page takes (2.92.1: a height, never a position)
    PageProbe probe(ui.r(), view);
    if (tab_ == kTabData) h = drawData(x, y, w);
    else if (noData() && tab_ != kTabBoss && tab_ != kTabLevels && tab_ != kTabHard) h = drawEmpty(x, y, w);  // 2.84: the levels are listed even before any run
    else if (tab_ == kTabOverview) h = drawOverview(x, y, w);
    else if (tab_ == kTabCombat) h = drawCombat(x, y, w);
    else if (tab_ == kTabBoss) h = drawBoss(x, y, w);
    else if (tab_ == kTabLevels) h = drawLevels(x, y, w);
    else if (tab_ == kTabHard) h = drawHard(x, y, w);
    probe.stop();  // the scroll bar is not the page's
    // Its room at the top, what it drew, a margin under its last row: scrolled to its end, the last row
    // shows whole above the footer (2.92.1: the height was counted from the top of the screen).
    contentH_[shownTab] = top + h + ui.s(16);
    ui.endScroll();
    tip_.bounds = {0.0f, 0.0f, W, footer.y};  // over the page, never over the hints
    const Rect tipBox = charts::drawTip(ui, tip_);
    shown_ = Shown{shownTab, view, footer, probe.shown(), probe.reach(), contentH_[shownTab], offset, tip_.show, tipBox, tip_.item, tip_.title};
    ui.inputEnabled = true;
    if (confirmReset_) drawConfirm();
    else if (leave || (ui.backPressed() && !ui.editingText())) g_.app.switchScene(makeMainMenu(g_), 0.3f);
    ui.popId();
    ui.end();
}

Rect StatisticsScene::drawFooter() {
    UIContext& ui = g_.ui;
    const float W = ui.screenW(), H = ui.screenH(), m = ui.s(44);
    const float fy = H - ui.s(28), hh = ui.s(28);
    ShapeBounds drawn;  // what the hints really cover
    ShapeListener* const before = ui.r().listener();
    ui.r().setListener(&drawn);
    float hx = m;
    hx += Glyphs::drawHint(ui, Action::MenuTabPrev, "/", {hx, fy}, hh);
    hx += Glyphs::drawHint(ui, Action::MenuTabNext, "Sections", {hx + ui.s(6), fy}, hh) + ui.s(36);
    hx += Glyphs::drawHint(ui, Action::MenuBack, "Retour", {hx, fy}, hh) + ui.s(36);
    {
        // What the charts answer, at the right when it fits after the others (measured, never over them).
        const std::string what = tab_ == kTabHard ? "Tranche" : tab_ == kTabData ? std::string() : "Curseur dans la courbe";
        if (!what.empty()) {
            auto hintW = [&](Action a, const std::string& t) {
                return Glyphs::measureAction(ui, a, hh) + hh * 0.3f + ui.text().measure(t, FontFace::Body, hh * 0.62f).x;
            };
            const float need = hintW(Action::MenuLeft, "/") + ui.s(6) + hintW(Action::MenuRight, what);
            if (W - m - need > hx) {
                float rx = W - m - need;
                rx += Glyphs::drawHint(ui, Action::MenuLeft, "/", {rx, fy}, hh) + ui.s(6);
                Glyphs::drawHint(ui, Action::MenuRight, what, {rx, fy}, hh);
            }
        }
    }
    ui.r().setListener(before);
    // The band: from the top of what was drawn (at least the row of the hints) to the bottom of the screen.
    float bandTop = fy - hh * 0.5f;
    if (const Rect a = drawn.area(); !a.empty()) bandTop = std::min(bandTop, a.y);
    bandTop = std::clamp(bandTop, 0.0f, H);
    return {0.0f, bandTop, W, H - bandTop};
}

// ------------------------------------------------------------------ building blocks

void StatisticsScene::cardFrame(const Rect& r, Color top, bool selected) {
    UIContext& ui = g_.ui;
    ui.r().fillRoundedRect(r, ui.s(12), kCard);
    if (selected) {
        ui.r().drawRoundedRect(r.expanded(ui.s(2)), ui.s(14), ui.theme().accent.withAlpha(70), std::max(1.0f, ui.s(3.0f)));
        ui.r().drawRoundedRect(r, ui.s(12), ui.theme().accent, std::max(1.0f, ui.s(1.5f)));
    } else {
        ui.r().drawRoundedRect(r, ui.s(12), kCardLine, std::max(1.0f, ui.s(1.0f)));
    }
    if (top.a) ui.r().fillRoundedRect({r.x + ui.s(12), r.y, r.w - ui.s(24), ui.s(3)}, ui.s(1.5f), top);
}

float StatisticsScene::tableSwitch(const std::string& id, float right, float y, bool draw, const std::string& on, const std::string& off) {
    UIContext& ui = g_.ui;
    const float f = fz(14);
    const float w1 = ui.text().measure(off, FontFace::Bold, ui.s(f)).x + ui.s(22), w2 = ui.text().measure(on, FontFace::Bold, ui.s(f)).x + ui.s(22);
    const float total = w1 + ui.s(6) + w2;
    if (!draw) return total;
    bool& state = tables_[id];
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = f;
    st.selected = !state;
    if (cframe::button(ui, "tbl/" + id + "/c", {right - total, y, w1, ui.s(30)}, off, st)) state = false;
    st.selected = state;
    if (cframe::button(ui, "tbl/" + id + "/t", {right - w2, y, w2, ui.s(30)}, on, st)) state = true;
    return total;
}

Rect StatisticsScene::card(const Rect& r, const CardHead& h) {
    UIContext& ui = g_.ui;
    cardFrame(r, h.top);
    const float pad = ui.s(18);
    float right = r.right() - pad;
    const float topY = r.y + ui.s(12);
    if (!h.table.empty()) right -= tableSwitch(h.table, right, topY, true, h.tableOn, h.tableOff) + ui.s(12);
    if (h.extra) {
        const float ew = h.extra(right, topY, false);
        if (right - ew - ui.s(12) > r.x + pad + ui.s(160)) {  // it goes only when the title keeps room
            h.extra(right, topY, true);
            right -= ew + ui.s(12);
        }
    }
    bool legendInHead = false;
    if (!h.legend.empty()) {
        const float lw = legendWidth(h.legend);
        if (right - lw - ui.s(12) > r.x + pad + ui.s(200)) {
            legend(right - lw, topY + ui.s(6), lw + ui.s(2), h.legend);
            right -= lw + ui.s(12);
            legendInHead = true;
        }
    }
    const float tf = fz(15), sf = fz(14);
    const float room = right - (r.x + pad);
    float y = r.y + ui.s(14);
    const float pillW = h.fresh ? ui.newPillWidth(true) + ui.s(10) : 0.0f;
    const float f1 = ui.fitFont(h.title, FontFace::Bold, tf, room - pillW, 0.8f);
    const std::string shown = ui.elide(h.title, FontFace::Bold, f1, room - pillW);
    ui.label(shown, {r.x + pad, y}, f1, ui.theme().text, Align::Left, FontFace::Bold);
    const float th = lineHeight(ui, FontFace::Bold, ui.s(f1));
    if (h.fresh) ui.drawNewPill(r.x + pad + ui.text().measure(shown, FontFace::Bold, ui.s(f1)).x + pillW, y + th * 0.5f, true);
    y += th + ui.s(2);
    if (!h.sub.empty()) {
        ui.label(ui.elide(h.sub, FontFace::Body, sf, room), {r.x + pad, y}, sf, ui.theme().textDim, Align::Left, FontFace::Body);
        y += lineHeight(ui, FontFace::Body, ui.s(sf));
    }
    y = std::max(y, r.y + ui.s(46));
    Rect in{r.x + pad, y + ui.s(10), r.w - 2 * pad, r.bottom() - y - ui.s(10) - ui.s(14)};
    if (!h.legend.empty() && !legendInHead) {
        const float lh = legend(in.x, in.y, in.w, h.legend);
        in.y += lh + ui.s(8);
        in.h -= lh + ui.s(8);
    }
    return in;
}

float StatisticsScene::legendWidth(const std::vector<LegendItem>& items) {
    UIContext& ui = g_.ui;
    const float f = fz(13);
    float w = 0.0f;
    for (size_t i = 0; i < items.size(); ++i) {
        const float sw = items[i].kind == 1 ? ui.s(10) : items[i].kind == 3 ? ui.s(18) : items[i].kind == 4 ? 0.0f : ui.s(16);
        w += sw + (items[i].kind == 4 ? 0.0f : ui.s(7)) + ui.text().measure(items[i].text, FontFace::Body, ui.s(f)).x + (i + 1 < items.size() ? ui.s(16) : 0.0f);
    }
    return w;
}

float StatisticsScene::legend(float x, float y, float w, const std::vector<LegendItem>& items, bool draw) {
    UIContext& ui = g_.ui;
    const float f = fz(13), h = lineHeight(ui, FontFace::Body, ui.s(f));
    float cx = x, cy = y;
    for (const auto& it : items) {
        const float sw = it.kind == 1 ? ui.s(10) : it.kind == 3 ? ui.s(18) : it.kind == 4 ? 0.0f : ui.s(16);  // 5: a hatched band
        const float sg = it.kind == 4 ? 0.0f : ui.s(7);  // 4: a remark, no swatch
        const float tw = ui.text().measure(it.text, FontFace::Body, ui.s(f)).x;
        const float iw = sw + sg + tw;
        if (cx > x && cx + iw > x + w) {
            cx = x;
            cy += h + ui.s(4);
        }
        if (draw) {
            const float mid = cy + h * 0.5f;
            if (it.kind == 1) ui.r().fillCircle({cx + sw * 0.5f, mid}, ui.s(4), it.color);
            else if (it.kind == 2) ui.r().fillRoundedRect({cx, mid - ui.s(5), sw, ui.s(10)}, ui.s(2), it.color);
            else if (it.kind == 5) {
                ui.r().fillRect({cx, mid - ui.s(5), sw, ui.s(10)}, it.color.withAlpha(static_cast<uint8_t>(it.color.a / 5)));
                charts::hatch(ui, {cx, mid - ui.s(5), sw, ui.s(10)}, it.color, ui.s(4));
            }
            else if (it.kind == 3 && it.icon) it.icon({cx, mid - sw * 0.5f, sw, sw});
            else if (it.kind == 0) ui.r().fillRoundedRect({cx, mid - ui.s(1.5f), sw, ui.s(3)}, ui.s(1.5f), it.color);
            ui.label(ui.elide(it.text, FontFace::Body, f, x + w - (cx + sw + sg)), {cx + sw + sg, cy}, f, it.kind == 4 ? ui.theme().textDim : kText2);
        }
        cx += iw + ui.s(16);
    }
    return cy + h - y;
}

float StatisticsScene::note(float x, float y, float w, const std::string& text) {
    UIContext& ui = g_.ui;
    const float f = fz(14);
    const float tw = w - ui.s(14);
    Vec2 size = ui.text().measureWrapped(text, FontFace::Body, ui.s(f), tw);
    ui.r().fillRect({x, y, ui.s(2), size.y + ui.s(8)}, Color{29, 111, 102, 255});
    ui.labelWrapped(text, {x + ui.s(14), y + ui.s(4)}, tw, f, kText2);
    return size.y + ui.s(8);
}

float StatisticsScene::kicker(const std::string& t0, Vec2 pos, Color c, float size, float room, bool upperCase) {
    UIContext& ui = g_.ui;
    i18n::Verbatim asIs(upperCase);  // a name in capitals: translated first, then drawn as it is
    const std::string t = upperCase ? cstyle::upper(i18n::tr(t0)) : t0;
    const float f = room > 0.0f ? ui.fitFont(t, FontFace::Bold, fz(size), room, 0.8f) : fz(size);
    const std::string shown = room > 0.0f ? ui.elide(t, FontFace::Bold, f, room) : t;
    ui.label(shown, pos, f, c, Align::Left, FontFace::Bold);
    return ui.text().measure(shown, FontFace::Bold, ui.s(f)).x;
}

float StatisticsScene::periodRow(float x, float y, float w) {
    UIContext& ui = g_.ui;
    const float f = fz(15), h = ui.s(38);
    ui.label("Période", {x, y + h * 0.5f}, f, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    float bx = x + ui.text().measure("Période", FontFace::Body, ui.s(f)).x + ui.s(14);
    for (int p : {7, 30, 90}) {
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = fz(16);
        st.selected = period_ == p;
        const std::string t = std::to_string(p) + " j";
        const float bw = ui.text().measure(t, FontFace::Bold, ui.s(fz(16))).x + ui.s(30);
        if (cframe::button(ui, "period" + std::to_string(p), {bx, y, bw, h}, t, st)) period_ = p;
        bx += bw + ui.s(8);
    }
    // What the period is: its days, the period it is compared to.
    const std::string remark = "du " + dayShort(today_ - period_ + 1) + " au " + dayShort(today_) + ", comparée aux " + std::to_string(period_) +
                               " jours d'avant · ▲ ▼ avec l'écart, jamais la couleur seule";
    const float room = x + w - (bx + ui.s(12));
    if (room > ui.s(120)) {
        const float rf = ui.fitFont(remark, FontFace::Body, fz(14), room, 0.85f);
        ui.label(ui.elide(remark, FontFace::Body, rf, room), {bx + ui.s(12), y + h * 0.5f}, rf, ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
    }
    return h;
}

float StatisticsScene::richText(const std::string& text, float x, float y, float w, float size, Color c, bool draw) {
    UIContext& ui = g_.ui;
    // Translated whole (the dictionary has the sentence with its marks), then cut in words.
    const std::string shown = i18n::tr(text);
    const float f = fz(size), px = ui.s(f);
    const float lh = lineHeight(ui, FontFace::Body, px) * 1.12f;
    struct Word {
        std::string t;
        bool bold;
    };
    std::vector<Word> words;
    bool bold = false;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty()) words.push_back({cur, bold});
        cur.clear();
    };
    for (size_t i = 0; i < shown.size(); ++i) {
        if (shown.compare(i, 2, "**") == 0) {
            flush();
            bold = !bold;
            ++i;
            continue;
        }
        if (shown[i] == ' ') {
            flush();
            words.push_back({" ", bold});
            continue;
        }
        cur += shown[i];
    }
    flush();
    float space;
    {
        i18n::Verbatim probe;
        space = ui.text().measure("a a", FontFace::Body, px).x - ui.text().measure("aa", FontFace::Body, px).x;
    }
    float cx = x, cy = y;
    bool lineStart = true;
    for (size_t i = 0; i < words.size(); ++i) {
        const Word& wd = words[i];
        if (wd.t == " ") {
            if (!lineStart) cx += space;
            continue;
        }
        const FontFace face = wd.bold ? FontFace::Bold : FontFace::Body;
        float ww;
        {
            i18n::Verbatim asIs;  // already translated
            ww = ui.text().measure(wd.t, face, px).x;
        }
        if (!lineStart && cx + ww > x + w) {
            cx = x;
            cy += lh;
        }
        if (draw) {
            i18n::Verbatim asIs;
            ui.label(wd.t, {cx, cy}, f, wd.bold ? ui.theme().text : c, Align::Left, face);
        }
        cx += ww;
        lineStart = false;
    }
    return cy + lh - y;
}

float StatisticsScene::bigValue(const std::string& v, Vec2 pos, float size, float room, Color c, VAlign va) {
    UIContext& ui = g_.ui;
    // Runs of the title face and of the bold face (the signs the title face lacks).
    struct Run {
        std::string t;
        FontFace face;
    };
    std::vector<Run> runs;
    for (size_t i = 0; i < v.size();) {
        const auto ch = static_cast<unsigned char>(v[i]);
        const int n = ch < 0x80 ? 1 : (ch >> 5) == 6 ? 2 : (ch >> 4) == 14 ? 3 : 4;
        uint32_t cp = n == 1 ? ch : n == 2 ? (ch & 0x1Fu) : n == 3 ? (ch & 0x0Fu) : (ch & 0x07u);
        for (int k = 1; k < n && i + static_cast<size_t>(k) < v.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(v[i + static_cast<size_t>(k)]) & 0x3Fu);
        const FontFace face = titleLacks(cp) ? FontFace::Bold : FontFace::Title;
        if (runs.empty() || runs.back().face != face) runs.push_back({"", face});
        runs.back().t += v.substr(i, static_cast<size_t>(n));
        i += static_cast<size_t>(n);
    }
    float total = 0.0f;
    auto widthAt = [&](float f) {
        float t = 0.0f;
        for (const auto& r : runs) t += ui.text().measure(r.t, r.face, ui.s(f)).x;
        return t;
    };
    float f = size;
    total = widthAt(f);
    if (room > 0.0f && total > room) {
        f = std::max(fz(12), f * room / total);
        total = widthAt(f);
    }
    float cx = pos.x;
    for (const auto& r : runs) {
        ui.label(r.t, {cx, pos.y}, f, c, Align::Left, r.face, va);
        cx += ui.text().measure(r.t, r.face, ui.s(f)).x;
    }
    return total;
}

float StatisticsScene::miniTileHeight(bool value, bool detail) {
    UIContext& ui = g_.ui;
    float h = ui.s(9) + lineHeight(ui, FontFace::Bold, ui.s(fz(12))) + ui.s(5);
    if (value) h += lineHeight(ui, FontFace::Title, ui.s(22)) + ui.s(4);
    if (detail) h += lineHeight(ui, FontFace::Body, ui.s(fz(12)));
    return h + ui.s(9);
}

void StatisticsScene::miniTile(const Rect& r, const std::string& key, const std::string& value, const std::string& detail, Color detailColor,
                               Color keySwatch) {
    UIContext& ui = g_.ui;
    ui.r().fillRoundedRect(r, ui.s(10), Color{10, 16, 34, 230});
    ui.r().drawRoundedRect(r, ui.s(10), kCardLine, std::max(1.0f, ui.s(1.0f)));
    const float pad = ui.s(12), room = r.w - 2 * pad;
    float y = r.y + ui.s(9);
    float kx = r.x + pad;
    if (keySwatch.a) {
        ui.r().fillRoundedRect({kx, y + ui.s(2), ui.s(11), ui.s(11)}, ui.s(2), keySwatch);
        kx += ui.s(17);
    }
    kicker(key, {kx, y}, ui.theme().textDim, 12, r.x + r.w - pad - kx);
    y += lineHeight(ui, FontFace::Bold, ui.s(fz(12))) + ui.s(5);
    if (!value.empty()) {
        bigValue(value, {r.x + pad, y}, 22, room, ui.theme().text);
        y += lineHeight(ui, FontFace::Title, ui.s(22)) + ui.s(4);
    }
    if (!detail.empty() && y + lineHeight(ui, FontFace::Body, ui.s(fz(12))) <= r.bottom() - ui.s(4)) {
        const float df = ui.fitFont(detail, FontFace::Body, fz(12), room, 0.85f);
        ui.label(ui.elide(detail, FontFace::Body, df, room), {r.x + pad, y}, df, detailColor.a ? detailColor : ui.theme().textDim);
    }
}

StatisticsScene::Delta StatisticsScene::deltaText(const Comparison& c, bool percentPoints) {
    if (!c.available) return {"Pas de période précédente", 0};
    const double a = std::abs(c.delta);
    std::string gap = percentPoints ? std::to_string(static_cast<int>(std::lround(a * 100.0))) + " pts" : frNum(a, 1);
    if (c.same || gap == "0 pts" || gap == "0,0") return {"= stable", 0};
    // Whole models (« ▲ +{0} · en progrès »…): translated as such.
    std::string t;
    if (c.delta > 0.0) t = c.progress ? "▲ +" + gap + " · en progrès" : "▲ +" + gap + " · en recul";
    else t = c.progress ? "▼ −" + gap + " · en progrès" : "▼ −" + gap + " · en recul";
    return {t, c.progress ? 1 : -1};
}

float StatisticsScene::pill(const std::string& t, Vec2 pos, int mood, float size, float room) {
    UIContext& ui = g_.ui;
    const float f = ui.fitFont(t, FontFace::Bold, fz(size), room - ui.s(18), 0.8f);
    const std::string shown = ui.elide(t, FontFace::Bold, f, room - ui.s(18));
    const Vec2 sz = ui.text().measure(shown, FontFace::Bold, ui.s(f));
    const Rect r{pos.x, pos.y, sz.x + ui.s(18), sz.y + ui.s(6)};
    const Color fg = mood > 0 ? ui.theme().success : mood < 0 ? ui.theme().warm : ui.theme().textDim;
    ui.r().fillRoundedRect(r, r.h * 0.5f, fg.withAlpha(30));
    ui.label(shown, r.center(), f, fg, Align::Center, FontFace::Bold, VAlign::Middle);
    return r.w;
}

void StatisticsScene::tableIn(const std::string& id, const Rect& r, const std::vector<std::string>& head, const std::vector<std::vector<std::string>>& rows) {
    charts::table(g_.ui, "t/" + id, r, head, rows, pages_[id]);
}

void StatisticsScene::emptyIn(const std::string& id, const Rect& r, const std::string& text) {
    UIContext& ui = g_.ui;
    ui.hotspot("c/" + id, r);
    const Vec2 sz = ui.text().measureWrapped(text, FontFace::Body, ui.s(fz(15)), r.w);
    ui.labelWrapped(text, {r.x, std::max(r.y, r.center().y - sz.y * 0.5f)}, r.w, fz(15), ui.theme().textDim);
}

void StatisticsScene::picturePending(const Rect& r, bool failed) {
    UIContext& ui = g_.ui;
    ui.r().fillRoundedRect(r, ui.s(6), Color{11, 16, 32, 255});
    // A light sweeping the frame while the picture is being made; nothing once it cannot be.
    if (!failed) {
        const float k = std::fmod(anim_ * 0.8f, 1.0f);
        const float bw = r.w * 0.3f;
        const float x0 = r.x - bw + (r.w + bw) * k;
        ui.r().pushClip(r);
        ui.r().fillGradientH({x0, r.y, bw * 0.5f, r.h}, Color{57, 240, 216, 0}, Color{57, 240, 216, 22});
        ui.r().fillGradientH({x0 + bw * 0.5f, r.y, bw * 0.5f, r.h}, Color{57, 240, 216, 22}, Color{57, 240, 216, 0});
        ui.r().popClip();
    }
}

float StatisticsScene::sectionTitle(float x, float y, float w, const std::string& title, const std::string& remark, const std::string& feature,
                                    const std::string& badgeWidget) {
    UIContext& ui = g_.ui;
    const float tf = fz(15);
    const float th = lineHeight(ui, FontFace::Bold, ui.s(tf));
    float cx = x + kicker(title, {x, y}, ui.theme().text, 15, w) + ui.s(12);
    if (!feature.empty() && ui.isNew(feature)) {
        const float pw = ui.newPillWidth(true);
        if (cx + pw <= x + w) {
            ui.drawNewPill(cx + pw, y + th * 0.5f, true);
            cx += pw + ui.s(12);
        }
    }
    const float room = x + w - cx;
    if (!remark.empty() && room > ui.s(80)) {
        const float rf = ui.fitFont(remark, FontFace::Body, fz(13), room, 0.85f);
        ui.label(ui.elide(remark, FontFace::Body, rf, room), {cx, y + th * 0.5f}, rf, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
    return th;
}

float StatisticsScene::drawEmpty(float x, float y, float w) {
    UIContext& ui = g_.ui;
    std::string body = "Tout est calculé à partir de vos vraies parties (campagne, défis, niveaux des joueurs), sur cet ordinateur, sans compte ni Internet. "
                       "L'onglet se remplit au fil des parties.";
    if (!g_.playStats.enabled) body += " L'enregistrement est désactivé : Paramètres › Jeu › « Enregistrer mes statistiques ».";
    if (g_.playStats.damaged()) body += " Le fichier des statistiques était abîmé : il a été gardé dans stats.json.bak et l'onglet repart de zéro.";
    const float bf = fz(16), tw = w - ui.s(60) - ui.s(140);
    const float bh = ui.text().measureWrapped(body, FontFace::Body, ui.s(bf), tw).y;
    Rect r{x, y + ui.s(30), w, std::max(ui.s(160), ui.s(90) + bh + ui.s(30))};  // as tall as its text
    cardFrame(r);
    // Nova, waiting for the first runs (at the right of the text, never over it).
    drawSpriteAt(g_, "animations/nova.json", "idle", anim_, {r.right() - ui.s(150), r.y + ui.s(20), ui.s(120), r.h - ui.s(40)});
    const std::string head = "Jouez quelques parties pour voir vos statistiques";
    float hf = ui.fitFont(head, FontFace::Bold, 24, tw, 0.6f);
    ui.label(ui.elide(head, FontFace::Bold, hf, tw), {r.x + ui.s(30), r.y + ui.s(50)}, hf, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
    ui.labelWrapped(body, {r.x + ui.s(30), r.y + ui.s(90)}, tw, bf, kText2);
    return r.bottom() - y;
}

// ------------------------------------------------------------------ Vue d'ensemble

void StatisticsScene::fillRateCard(LineSpec& spec, std::vector<std::vector<std::string>>& rows, std::vector<std::string>& head,
                                   const std::vector<SeriesPoint>& se, Color color, bool percent) {
    Series dots{"Chaque jour", color, Series::Kind::Dots, false, {}};
    Series avg{"Moyenne sur 7 jours joués", color, Series::Kind::Line, true, {}};
    float hi = 0.0f;
    for (const auto& p : se) {
        dots.pts.push_back({static_cast<float>(p.day), static_cast<float>(p.value)});
        avg.pts.push_back({static_cast<float>(p.day), static_cast<float>(p.rolling)});
        hi = std::max(hi, static_cast<float>(std::max(p.value, p.rolling)));
    }
    auto fmt = [percent](double v) { return percent ? pct(v) : frNum(v, 1); };
    if (!se.empty()) avg.endLabel = fmt(se.back().rolling);  // its last value at its end
    for (auto it = se.rbegin(); it != se.rend(); ++it) rows.push_back({dayLong(it->day), fmt(it->value), fmt(it->rolling)});
    head = {"Jour", "Ce jour-là", "Moyenne sur 7 jours joués"};
    spec.series = {dots, avg};
    spec.endLabels = true;
    spec.yMin = 0.0f;
    spec.yMax = percent ? 1.0f : std::max(5.0f, std::ceil(hi * 1.15f / 5.0f) * 5.0f);
    spec.yLabel = [percent](float v) { return percent ? pct(v) : std::to_string(static_cast<int>(std::lround(v))); };
    spec.xLabel = [](float v) { return dayShort(static_cast<int>(v)); };
    spec.tip = [se, color, fmt](float xv, Tip& t) {
        t.title = dayNamed(static_cast<int>(xv));
        for (const auto& p : se)
            if (p.day == static_cast<int>(xv)) {
                t.rows.push_back({color, "Moyenne sur 7 jours joués", fmt(p.rolling)});
                t.rows.push_back({charts::kNoSwatch, "Ce jour-là", fmt(p.value)});
            }
    };
}

void StatisticsScene::heroCard(const Rect& r, const PeriodTotals& cur) {
    UIContext& ui = g_.ui;
    cardFrame(r);
    ui.r().fillRoundedRectGradient(r.expanded(-ui.s(1)), ui.s(11), Color{57, 240, 216, 26}, Color{14, 20, 40, 0});
    const float pad = ui.s(18);
    // Nova running at the top right; the texts at the left of her.
    const float novaW = std::min(ui.s(110), r.w * 0.3f);
    const Rect nova{r.right() - pad - novaW, r.y + ui.s(12), novaW, ui.s(100)};
    drawSpriteAt(g_, "animations/nova.json", "run", anim_, nova);
    const float room = nova.x - ui.s(10) - (r.x + pad);
    float y = r.y + ui.s(14);
    kicker(std::to_string(period_) + " DERNIERS JOURS", {r.x + pad, y}, ui.theme().accent, 13, room);
    y += lineHeight(ui, FontFace::Bold, ui.s(fz(13))) + ui.s(4);
    bigValue(duration(cur.c.playSec), {r.x + pad, y}, 34, room, ui.theme().text);
    y += lineHeight(ui, FontFace::Title, ui.s(34)) + ui.s(2);
    const float lf = fz(14), lh = lineHeight(ui, FontFace::Body, ui.s(lf));
    const std::string days = "de jeu sur " + std::to_string(cur.daysPlayed) + (cur.daysPlayed > 1 ? " jours" : " jour");
    ui.label(ui.elide(days, FontFace::Body, lf, room), {r.x + pad, y}, lf, ui.theme().textDim);
    y += lh;
    if (cur.c.trainSec >= 1.0) {
        const std::string tr = "entraînement, compté à part : " + duration(cur.c.trainSec);
        ui.label(ui.elide(tr, FontFace::Body, lf, r.w - 2 * pad), {r.x + pad, y}, lf, ui.theme().textDim);
        y += lh;
    }
    y = std::max(y, nova.bottom()) + ui.s(8);
    // The calendar: a square per day of the period, lighter for more time played (one hue).
    const std::string cap = "un carré par jour · plus clair = plus de temps de jeu";
    const float cf = fz(12), ch = lineHeight(ui, FontFace::Body, ui.s(cf));
    const Rect area{r.x + pad, y, r.w - 2 * pad, r.bottom() - pad - ch - ui.s(6) - y};
    if (area.h < ui.s(12)) return;
    const int from = today_ - period_ + 1;
    const std::vector<double> secs = playstats::dailyPlaySeconds(data(), from, today_);
    const int n = static_cast<int>(secs.size());
    const int cols = period_ <= 7 ? 7 : period_ <= 30 ? 10 : 15;
    const int rows = (n + cols - 1) / cols;
    const float gap = ui.s(4);
    const float cell = std::max(ui.s(4), std::min({ui.s(26), (area.w - gap * (cols - 1)) / cols, (area.h - gap * (rows - 1)) / std::max(1, rows)}));
    const double most = std::max(1.0, *std::max_element(secs.begin(), secs.end()));
    const Rect calR{area.x, area.y, cell * cols + gap * (cols - 1), cell * rows + gap * (rows - 1)};
    // 2.92.1: its tooltip while the mouse is over a day, or while it has the focus (keyboard, gamepad).
    const charts::Pointing pt = charts::pointing(ui, "c/cal", calR);
    int& cursor = cursorOf("cal");
    if (pt.keys) {
        if (cursor < 0 || cursor >= n) cursor = n - 1;
        if (ui.input().menuPressed(Action::MenuLeft)) cursor = std::max(0, cursor - 1);
        if (ui.input().menuPressed(Action::MenuRight)) cursor = std::min(n - 1, cursor + 1);
        ui.consumeNavigation(true, false);
    }
    int at = -1;
    for (int i = 0; i < n; ++i) {
        const Rect c{calR.x + (cell + gap) * static_cast<float>(i % cols), calR.y + (cell + gap) * static_cast<float>(i / cols), cell, cell};
        const double s = secs[static_cast<size_t>(i)];
        const Color col = s >= 1.0 ? ui.theme().accent.withAlphaF(0.2f + 0.8f * static_cast<float>(s / most)) : Color{22, 32, 60, 255};
        ui.r().fillRoundedRect(c, std::min(ui.s(4), cell * 0.25f), col);
        if (pt.mouse && c.contains(ui.input().mousePos())) at = i, cursor = i;
    }
    if (at < 0 && pt.keys) at = cursor;
    if (pt.any()) ui.r().drawRoundedRect(calR.expanded(ui.s(4)), ui.s(6), ui.theme().accent.withAlpha(170), std::max(1.0f, ui.s(1.5f)));
    if (at >= 0 && at < n) {
        const Rect c{calR.x + (cell + gap) * static_cast<float>(at % cols), calR.y + (cell + gap) * static_cast<float>(at / cols), cell, cell};
        ui.r().drawRoundedRect(c.expanded(ui.s(1.5f)), ui.s(4), ui.theme().text, std::max(1.0f, ui.s(1.5f)));
        if (charts::showTip(ui, tip_, {c.center().x, c.y}, c.expanded(ui.s(1.5f)))) {
            tip_.title = dayNamed(from + at);
            const double s = secs[static_cast<size_t>(at)];
            tip_.rows.push_back({charts::kNoSwatch, "Temps de jeu", s >= 1.0 ? duration(s) : std::string("pas joué")});
        }
    }
    ui.label(ui.elide(cap, FontFace::Body, cf, r.w - 2 * pad), {r.x + pad, std::min(calR.bottom() + ui.s(6), r.bottom() - pad - ch)}, cf, ui.theme().textDim);
}

void StatisticsScene::kpiTile(const Rect& r, const std::string& key, const std::string& value, const Delta& d, const std::vector<float>& spark, Color color) {
    UIContext& ui = g_.ui;
    cardFrame(r);
    const float pad = ui.s(16), room = r.w - 2 * pad;
    float y = r.y + ui.s(12);
    kicker(key, {r.x + pad, y}, ui.theme().textDim, 13, room);
    y += lineHeight(ui, FontFace::Bold, ui.s(fz(13))) + ui.s(2);
    // The pill at the bottom left; the value between the kicker and the pill (smaller when the tile is
    // short); the small curve in what is left at the right (never under the value).
    const float ph = lineHeight(ui, FontFace::Bold, ui.s(fz(13))) + ui.s(6);
    const float py = r.bottom() - ui.s(10) - ph;
    float vs = 32.0f, vh = lineHeight(ui, FontFace::Title, ui.s(vs));
    if (const float avail = py - ui.s(3) - y; vh > avail && avail > 0.0f) {
        vs *= avail / vh;
        vh = avail;
    }
    bigValue(value, {r.x + pad, y}, vs, room, ui.theme().text);
    const float valueBottom = y + vh;
    const float pw = pill(d.text, {r.x + pad, py}, d.mood, 13, room * 0.62f);
    const float sx = std::max(r.x + pad + pw + ui.s(10), r.x + r.w * 0.45f);
    const Rect sp{sx, std::max(valueBottom, r.y + r.h * 0.45f), r.right() - pad * 0.6f - sx, r.bottom() - ui.s(8) - std::max(valueBottom, r.y + r.h * 0.45f)};
    if (sp.w >= ui.s(50) && sp.h >= ui.s(18) && spark.size() >= 2) charts::sparkline(ui, sp, spark, color);
}

float StatisticsScene::drawOverview(float x, float y0, float w) {
    UIContext& ui = g_.ui;
    float y = y0;
    y += periodRow(x, y, w) + ui.s(16);
    const int to = today_, from = today_ - period_ + 1;
    const PeriodTotals cur = periodTotals(data(), from, to), prev = periodTotals(data(), from - period_, from - 1);
    // The tiles' small curves: the time played over the last 7 days, the 7-day averages of the rates.
    std::vector<float> sTime, sDodge, sAcc, sDph;
    for (double v : playstats::trailingSums(playstats::dailyPlaySeconds(data(), from, to), 7)) sTime.push_back(static_cast<float>(v));
    const auto dodge = dailySeries(data(), DayMetric::Dodge, from, to), acc = dailySeries(data(), DayMetric::Accuracy, from, to),
               dph = dailySeries(data(), DayMetric::DeathsPerHour, from, to);
    for (const auto& p : dodge) sDodge.push_back(static_cast<float>(p.rolling));
    for (const auto& p : acc) sAcc.push_back(static_cast<float>(p.rolling));
    for (const auto& p : dph) sDph.push_back(static_cast<float>(p.rolling));
    // Stars.
    LineSpec stars;
    std::vector<std::vector<std::string>> starRows;
    auto st = starsSeries(data(), from, to);
    int gained = 0;
    {
        Series s{"Étoiles", ui.theme().accent, Series::Kind::Step, false, {}};
        int lo = 1 << 30, hi = 0;
        for (auto [d, n] : st) {
            s.pts.push_back({static_cast<float>(d), static_cast<float>(n)});
            lo = std::min(lo, n);
            hi = std::max(hi, n);
            starRows.push_back({dayLong(d), std::to_string(n)});
        }
        std::reverse(starRows.begin(), starRows.end());
        if (!st.empty()) {
            s.endLabel = std::to_string(st.back().second);
            const auto before = starsSeries(data(), from - 400, from - 1);
            gained = st.back().second - (before.empty() ? st.front().second : before.back().second);
        }
        stars.series = {s};
        stars.endLabels = true;
        stars.yMin = st.empty() ? 0.0f : std::max(0.0f, std::floor((lo - 10) / 10.0f) * 10.0f);
        stars.yMax = st.empty() ? 10.0f : std::ceil((hi + 10) / 10.0f) * 10.0f;
        stars.yLabel = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
        stars.xLabel = [](float v) { return dayShort(static_cast<int>(v)); };
        stars.tip = [st](float xv, Tip& t) {
            t.title = dayNamed(static_cast<int>(xv));
            for (auto [d, n] : st)
                if (d == static_cast<int>(xv)) t.rows.push_back({{57, 240, 216, 255}, "Étoiles", std::to_string(n)});
        };
    }
    auto starsCard = [&](const Rect& r) {
        CardHead h;
        h.title = "ÉTOILES DE SUCCÈS";
        h.sub = "Total cumulé, jour par jour" +
                (st.empty() ? std::string() : " · " + std::to_string(st.back().second) + " étoiles (+" + std::to_string(std::max(0, gained)) + " sur la période)");
        h.table = "stars";
        h.extra = [&](float right, float yy, bool draw) {
            const float s = ui.s(30);
            if (draw) ui.icon("ui/icons_hud.png", 8, 16, {right - s, yy, s, s});
            return s;
        };
        Rect in = card(r, h);
        if (tables_["stars"]) tableIn("stars", in, {"Jour", "Étoiles"}, starRows);
        else if (st.empty()) emptyIn("stars", in, "Pas encore de données sur la période.");
        else charts::lineChart(ui, "c/stars", in, stars, cursorOf("stars"), tip_);
    };
    // The first row: the period (time and calendar), the four tiles, the stars.
    const float gap = ui.s(14);
    const Delta dtDays{std::to_string(cur.daysPlayed) + (cur.daysPlayed > 1 ? " jours joués" : " jour joué"), 0};
    auto tiles = [&](const Rect& area) {
        const float tw = (area.w - gap) / 2.0f, th = (area.h - gap) / 2.0f;
        kpiTile({area.x, area.y, tw, th}, "TEMPS DE JEU", duration(cur.c.playSec), dtDays, sTime, ui.theme().accent);
        kpiTile({area.x + tw + gap, area.y, tw, th}, "TAUX D'ESQUIVE", cur.dodgeRate() ? pct(*cur.dodgeRate()) : "—",
                deltaText(comparePeriods(cur.dodgeRate(), prev.dodgeRate(), true), true), sDodge, ui.theme().accent);
        kpiTile({area.x, area.y + th + gap, tw, th}, "PRÉCISION", cur.accuracy() ? pct(*cur.accuracy()) : "—",
                deltaText(comparePeriods(cur.accuracy(), prev.accuracy(), true), true), sAcc, ui.theme().accent);
        kpiTile({area.x + tw + gap, area.y + th + gap, tw, th}, "MORTS PAR HEURE", cur.deathsPerHour() ? frNum(*cur.deathsPerHour(), 1) : "—",
                deltaText(comparePeriods(cur.deathsPerHour(), prev.deathsPerHour(), false), false), sDph, ui.theme().warm);
    };
    const float rowH = ui.s(262);
    if (w >= ui.s(1500)) {
        const float heroW = w * 0.236f, tilesW = w * 0.381f;
        heroCard({x, y, heroW, rowH}, cur);
        tiles({x + heroW + gap, y, tilesW, rowH});
        starsCard({x + heroW + tilesW + 2 * gap, y, w - heroW - tilesW - 2 * gap, rowH});
        y += rowH + gap;
    } else {
        const float half = (w - gap) / 2.0f;
        heroCard({x, y, half, rowH}, cur);
        starsCard({x + half + gap, y, half, rowH});
        y += rowH + gap;
        tiles({x, y, w, rowH});
        y += rowH + gap;
    }
    // Deaths per hour, the width of the screen.
    {
        LineSpec spec;
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> head;
        fillRateCard(spec, rows, head, dph, ui.theme().warm, false);
        const std::vector<LegendItem> leg = {{ui.theme().warm, "Moyenne sur 7 jours joués", 0, {}}, {ui.theme().warm.withAlpha(150), "Chaque jour", 1, {}}};
        CardHead h;
        h.title = "MORTS PAR HEURE DE JEU";
        h.sub = "Plus la courbe descend, plus vous progressez";
        h.table = "dph";
        h.legend = leg;
        const float ch = ui.s(300);
        Rect in = card({x, y, w, ch}, h);
        if (tables_["dph"]) tableIn("dph", in, head, rows);
        else if (dph.empty()) emptyIn("dph", in, "Pas encore de données sur la période.");
        else charts::lineChart(ui, "c/dph", in, spec, cursorOf("dph"), tip_);
        y += ch + gap;
    }
    y += recentRow(x, y, w);
    y += note(x, y, w,
              "Progression : la période choisie est comparée à la précédente de même durée (▲ / ▼ avec l'écart, en points pour les taux, "
              "et « en progrès » ou « en recul » : jamais la couleur seule). Comptent les parties de la campagne, des défis et des niveaux "
              "des joueurs ; l'entraînement est compté à part, le mode Invincible jamais.");
    return y - y0;
}

float StatisticsScene::recentRow(float x, float y0, float w) {
    UIContext& ui = g_.ui;
    const auto recent = playstats::recentlyPlayed(data(), 4);
    if (recent.empty()) return 0.0f;
    float y = y0 + ui.s(4);
    y += sectionTitle(x, y, w, "RÉCEMMENT JOUÉ", "d'après le dernier jour de chaque niveau et l'ordre des combats de boss, déjà enregistrés", "stats.recent",
                      "recent0") +
         ui.s(12);
    const float gap = ui.s(16);
    const int per = w >= ui.s(1300) ? 4 : 2;
    const float cw = (w - gap * static_cast<float>(per - 1)) / static_cast<float>(per), ch = ui.s(200);
    for (size_t i = 0; i < recent.size(); ++i) {
        const auto& it = recent[i];
        const Rect r{x + (cw + gap) * static_cast<float>(static_cast<int>(i) % per), y + (ch + gap) * static_cast<float>(static_cast<int>(i) / per), cw, ch};
        const std::string wid = "recent" + std::to_string(i);
        if (i == 0) ui.newBadge(wid, r, "stats.recent", false);  // the pill is after the section's title; seen with the first card
        bool hovered = false;
        const bool open = ui.hotspot(wid, r, &hovered);
        const bool lit = hovered || ui.focused(wid);
        // The planet's banner (the level's, or where the boss is fought), darkened at the left for the texts.
        ui.r().fillRoundedRect(r, ui.s(12), Color{11, 16, 32, 255});
        std::string planet, kick;
        const std::string when = dayAgo(it.day, today_);
        if (it.kind == playstats::RecentPlay::Kind::Level) {
            auto [pl, n] = statsart::levelPlace(g_, it.id);
            planet = pl;
            kick = levelShort(it.id);
        } else {
            planet = statsart::bossPlace(g_, atlas_.get(), it.id).planetId;
            kick = "BOSS";
        }
        if (!when.empty()) kick += " · " + when;
        ui.r().pushClip(r.expanded(-ui.s(1)));
        statsart::drawPlanetBanner(g_, planet, r, Color{255, 255, 255, 140});
        ui.r().fillGradientH({r.x, r.y, r.w * 0.75f, r.h}, Color{5, 7, 15, 238}, Color{5, 7, 15, 40});
        ui.r().popClip();
        ui.r().drawRoundedRect(r, ui.s(12), lit ? ui.theme().accent : kCardLine, std::max(1.0f, ui.s(lit ? 1.5f : 1.0f)));
        const float pad = ui.s(16);
        float portraitW = 0.0f;
        if (it.kind == playstats::RecentPlay::Kind::Boss) {
            portraitW = std::min(r.w * 0.4f, ui.s(170));
            ui.r().pushClip(r.expanded(-ui.s(1)));
            bossPortrait(it.id, {r.right() - ui.s(8) - portraitW, r.y + ui.s(30), portraitW, r.h - ui.s(36)}, anim_ + static_cast<float>(i) * 0.37f);
            ui.r().popClip();
        }
        const float room = r.w - 2 * pad - (portraitW > 0.0f ? portraitW + ui.s(4) : 0.0f);
        float ty = r.y + ui.s(14);
        kicker(kick, {r.x + pad, ty}, ui.theme().accent, 12, room, true);
        ty += lineHeight(ui, FontFace::Bold, ui.s(fz(12))) + ui.s(4);
        std::string name, m1, m2;
        if (it.kind == playstats::RecentPlay::Kind::Level) {
            name = levelName(it.id);
            auto ir = data().runs.find(it.id);
            if (ir != data().runs.end() && ir->second.attempts > 0) {
                const LevelRuns& lr = ir->second;
                // 2.91: the one Sum of Best of the level version (game/ghost/PersonalBests.h) and its record.
                const std::string key = lr.version.empty() ? std::string() : GhostStore::levelKey(it.id, lr.version);
                const PersonalBests::Run* pbRun = key.empty() ? nullptr : g_.bests.pb(key);
                const float pb = pbRun ? pbRun->time : lr.pb, sob = key.empty() ? -1.0f : g_.bests.sob(key);
                m1 = pb > 0.0f ? "Record " + clockTime(pb) + (sob > 0.0f ? " · SOB " + clockTime(sob) : std::string()) : "Pas encore d'arrivée";
                m2 = std::to_string(lr.attempts) + (lr.attempts > 1 ? " tentatives · " : " tentative · ") + std::to_string(lr.finished) +
                     (lr.finished > 1 ? " arrivées" : " arrivée");
            }
            if (auto il = data().levels.find(it.id); il != data().levels.end()) {
                int deaths = 0;
                for (int dd : il->second.deaths) deaths += dd;
                const std::string dl = std::to_string(deaths) + (deaths > 1 ? " morts dans ce niveau" : " mort dans ce niveau");
                if (m1.empty()) m1 = dl;
                else if (m2.empty()) m2 = dl;
                if (m2.empty()) {
                    const auto ps = levelPassages(it.id, il->second, today_);
                    if (!ps.empty())
                        m2 = "passage le plus dur : " + std::to_string(static_cast<int>(std::lround(ps.front().from * 2.5))) + "–" +
                             std::to_string(static_cast<int>(std::lround((ps.front().to + 1) * 2.5))) + " %";
                }
            }
        } else {
            name = bossName(it.id);
            const BossHistory& b = data().bosses.at(it.id);
            const int hp = hardestPhase(b);
            if (b.wins > 0) {
                m1 = "Vaincu · " + std::to_string(b.attempts) + (b.attempts > 1 ? " tentatives" : " tentative");
                if (hp >= 0) m2 = "phase la plus dure : " + std::to_string(hp + 1) + " / " + std::to_string(b.phases);
            } else {
                m1 = "Meilleur : " + pct(b.best()) + " retirés";
                m2 = std::to_string(b.attempts) + (b.attempts > 1 ? " tentatives" : " tentative") + " · pas encore vaincu";
            }
        }
        {
            i18n::Verbatim asIs(it.kind == playstats::RecentPlay::Kind::Level && !g_.content.level(it.id));  // a player's level: its name as written
            const float nf = ui.fitFont(name, FontFace::Bold, fz(19), room, 0.8f);
            ui.label(ui.elide(name, FontFace::Bold, nf, room), {r.x + pad, ty}, nf, ui.theme().text, Align::Left, FontFace::Bold);
            ty += lineHeight(ui, FontFace::Bold, ui.s(nf)) + ui.s(6);
        }
        if (!m1.empty()) {
            const float f1 = ui.fitFont(m1, FontFace::Body, fz(14), room, 0.85f);
            ui.label(ui.elide(m1, FontFace::Body, f1, room), {r.x + pad, ty}, f1, kText2);
            ty += lineHeight(ui, FontFace::Body, ui.s(f1)) + ui.s(3);
        }
        if (!m2.empty()) {
            const float f2 = ui.fitFont(m2, FontFace::Body, fz(13), room, 0.85f);
            ui.label(ui.elide(m2, FontFace::Body, f2, room), {r.x + pad, ty}, f2, ui.theme().textDim);
        }
        if (lit && !ui.usingPointer()) {
            // What Entrée does here.
            const std::string t = "Entrée : voir";
            const float tf = fz(12);
            const float tw = ui.text().measure(t, FontFace::Bold, ui.s(tf)).x;
            if (tw + 2 * pad < r.w - portraitW) ui.label(t, {r.x + pad, r.bottom() - ui.s(12)}, tf, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Bottom);
        }
        if (open) {
            // To its section: the level in Niveaux (a classic level with times) or in Passages difficiles,
            // the boss in Boss.
            if (it.kind == playstats::RecentPlay::Kind::Level) {
                auto ir = data().runs.find(it.id);
                if (ir != data().runs.end() && ir->second.attempts > 0) {
                    wantRuns_ = it.id;
                    tab_ = kTabLevels;
                } else {
                    wantSlices_ = it.id;
                    tab_ = kTabHard;
                }
            } else {
                selBoss_ = it.id;
                group_ = -1;  // the page of the boss chosen
                tab_ = kTabBoss;
            }
            ui.setScroll("page" + std::to_string(tab_), 0.0f);
        }
    }
    const int rows = (static_cast<int>(recent.size()) + per - 1) / per;
    y += (ch + gap) * static_cast<float>(rows);
    return y - y0;
}

// ------------------------------------------------------------------ Combat

void StatisticsScene::drawPowerIcon(const std::string& power, const Rect& box) {
    if (power == "attack") {
        drawSprite(g_, "animations/nova.json", "attack", 2, box);
        return;
    }
    if (const PowerDefinition* p = g_.content.power(power); p && p->icon >= 0) {
        const float s = std::min(box.w, box.h);
        g_.ui.icon("ui/icons_powers.png", p->icon, 32, Rect::fromCenter(box.center(), {s, s}));
    }
}

void StatisticsScene::drawHurtIcon(int origin, const Rect& box) {
    auto enemyFrame = [&](const char* id, const char* clip, int frame) {
        if (const EnemyDefinition* d = g_.content.enemy(id)) drawSprite(g_, d->animation, clip, frame, box, d->tint);
    };
    auto region = [&](const char* tex, Rect src) {
        if (!g_.app.resources().exists(tex)) return;
        if (TexturePtr t = g_.app.resources().texture(tex)) {
            const float s = std::floor(std::min(box.w / src.w, box.h / src.h));
            g_.app.renderer().drawTexture(*t, &src, Rect::fromCenter(box.center(), {src.w * std::max(1.0f, s), src.h * std::max(1.0f, s)}));
        }
    };
    switch (static_cast<HurtOrigin>(origin)) {
        case HurtOrigin::EnemyShot: drawSprite(g_, "animations/projectiles.json", "wisp_orb", 0, box); break;
        case HurtOrigin::EnemyAttack: enemyFrame("stalker", "attack", 2); break;
        case HurtOrigin::BossAttack: enemyFrame("mycelia", "attack", 2); break;
        case HurtOrigin::Contact: enemyFrame("gloop", "idle", 0); break;
        case HurtOrigin::Hazard: region("sprites/objects/spikes.png", {0, 0, 16, 16}); break;
        case HurtOrigin::Fall: region("sprites/objects/liquid_acid.png", {0, 0, 32, 32}); break;
        default: {
            const float s = std::min(box.w, box.h);
            g_.ui.icon("ui/icons_hud.png", 6, 16, Rect::fromCenter(box.center(), {s, s}));
        }
    }
}

float StatisticsScene::drawCombat(float x, float y0, float w) {
    UIContext& ui = g_.ui;
    float y = y0;
    y += periodRow(x, y, w) + ui.s(16);
    const int to = today_, from = today_ - period_ + 1;
    const PeriodTotals cur = periodTotals(data(), from, to);
    const float gap = ui.s(16);
    const bool two = w >= ui.s(1100);
    const float cw = two ? (w - gap) / 2.0f : w;
    // The two rates, side by side on the same scale (0 to 100 %): one legend for both, under them.
    struct Rate {
        const char* id;
        const char* title;
        const char* sub;
        DayMetric metric;
        std::optional<double> total;
    };
    const Rate rates[2] = {{"dodge", "TAUX D'ESQUIVE", "Attaques évitées sur attaques qui vous menaçaient", DayMetric::Dodge, cur.dodgeRate()},
                           {"accuracy", "PRÉCISION DES ATTAQUES", "Attaques qui touchent sur attaques lancées", DayMetric::Accuracy, cur.accuracy()}};
    const float rh = ui.s(336);
    for (int k = 0; k < 2; ++k) {
        const Rate& rt = rates[k];
        const Rect r{two ? x + (cw + gap) * static_cast<float>(k) : x, two ? y : y + (rh + gap) * static_cast<float>(k), cw, rh};
        LineSpec spec;
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> head;
        const auto se = dailySeries(data(), rt.metric, from, to);
        fillRateCard(spec, rows, head, se, ui.theme().accent, true);
        CardHead h;
        h.title = rt.title;
        h.sub = rt.sub;
        h.table = rt.id;
        const std::string total = rt.total ? pct(*rt.total) : "—";
        h.extra = [&](float right, float yy, bool draw) {
            const float vf = 26.0f, lf = fz(13);
            const float vw = ui.text().measure(total, FontFace::Title, ui.s(vf)).x, lw = ui.text().measure("sur la période", FontFace::Body, ui.s(lf)).x;
            if (draw) {
                bigValue(total, {right - vw, yy + ui.s(15)}, vf, 0.0f, ui.theme().text, VAlign::Middle);
                ui.label("sur la période", {right - vw - ui.s(10), yy + ui.s(17)}, lf, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
            }
            return vw + ui.s(10) + lw;
        };
        Rect in = card(r, h);
        if (tables_[rt.id]) tableIn(rt.id, in, head, rows);
        else if (se.empty()) emptyIn(rt.id, in, "Pas encore de données sur la période.");
        else charts::lineChart(ui, std::string("c/") + rt.id, in, spec, cursorOf(rt.id), tip_);
    }
    y += two ? rh : 2 * rh + gap;
    y += ui.s(10);
    y += legend(x, y, w,
                {{ui.theme().accent, "Moyenne sur 7 jours joués", 0, {}},
                 {ui.theme().accent.withAlpha(150), "Chaque jour", 1, {}},
                 {Color{0, 0, 0, 0}, "· même échelle (0 à 100 %) pour les deux courbes", 4, {}}}) +
         ui.s(16);
    // Precision per attack, where the blows come from: a picture per row.
    std::vector<charts::IconBar> powers, blows;
    for (const auto& [id, v] : cur.c.powers) {
        if (v[0] <= 0) continue;
        std::string name = id == "attack" ? std::string("Attaque") : id;
        if (const PowerDefinition* p = g_.content.power(id)) name = p->name;
        const float a = static_cast<float>(v[1]) / static_cast<float>(v[0]);
        const std::string pid = id;
        powers.push_back({name, a, pct(a), std::to_string(v[1]) + " sur " + std::to_string(v[0]), ui.theme().accent,
                          [this, pid](const Rect& b) { drawPowerIcon(pid, b); }, name,
                          {{charts::kNoSwatch, "Touchent", std::to_string(v[1]) + " sur " + std::to_string(v[0])}}});
    }
    std::stable_sort(powers.begin(), powers.end(), [](const auto& a, const auto& b) { return a.value > b.value; });
    int total = 0;
    for (int n : cur.c.hurt) total += n;
    std::vector<std::pair<int, int>> order;
    for (int i = 0; i < kOrigins; ++i)
        if (cur.c.hurt[static_cast<size_t>(i)] > 0) order.push_back({cur.c.hurt[static_cast<size_t>(i)], i});
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    float maxShare = 0.0f;
    for (auto [n, i] : order) {
        const float share = total > 0 ? static_cast<float>(n) / static_cast<float>(total) : 0.0f;
        maxShare = std::max(maxShare, share);
        const std::string label = originLabel(static_cast<HurtOrigin>(i));
        blows.push_back({label, share, pct(share), std::to_string(n) + (n > 1 ? " coups" : " coup"), ui.theme().warm,
                         [this, i](const Rect& b) { drawHurtIcon(i, b); }, label,
                         {{charts::kNoSwatch, "Part des coups reçus", pct(share)}, {charts::kNoSwatch, "Coups", std::to_string(n) + " sur " + std::to_string(total)}}});
    }
    const size_t rows = std::max<size_t>(std::max(powers.size(), blows.size()), 4);
    const float bh = ui.s(80) + ui.s(12) + charts::iconBarsHeight(ui, rows) + ui.s(20);
    auto barsCard = [&](const Rect& r, const std::string& id, const std::string& title, const std::string& sub, const std::vector<charts::IconBar>& bars,
                        float max, const std::vector<std::string>& head, const std::string& empty) {
        CardHead h;
        h.title = title;
        h.sub = sub;
        h.table = id;
        Rect in = card(r, h);
        if (tables_[id]) {
            std::vector<std::vector<std::string>> trows;
            for (const auto& b : bars) trows.push_back({b.label, b.valueText, b.countText});
            tableIn(id, in, head, trows);
        } else if (bars.empty()) {
            emptyIn(id, in, empty);
        } else {
            charts::iconBars(ui, "c/" + id, {in.x, in.y + ui.s(4), in.w, charts::iconBarsHeight(ui, bars.size())}, bars, max, cursorOf(id), tip_);
        }
    };
    barsCard({x, y, cw, bh}, "powers", "PRÉCISION PAR POUVOIR", "Sur la période choisie · touchent / lancées", powers, 1.0f, {"Attaque", "Précision", "Touchent"},
             "Aucune attaque lancée sur la période.");
    barsCard({two ? x + cw + gap : x, two ? y : y + bh + gap, cw, bh}, "blows", "CE QUI VOUS TOUCHE",
             "Origine des coups reçus (et des chutes) · " + std::to_string(total) + (total > 1 ? " coups sur la période" : " coup sur la période"), blows,
             std::min(1.0f, std::max(0.05f, maxShare * 1.15f)), {"Origine", "Part", "Coups"}, "Aucun coup reçu sur la période.");
    y += (two ? bh : 2 * bh + gap) + ui.s(16);
    y += note(x, y, w,
              "Esquive : un tir ennemi, ou une attaque portée par le corps d'un ennemi ou d'un boss (coup, charge, bond, piqué…), vous menace "
              "quand sa zone de dégâts passe à moins d'une tuile et demie de vous pendant qu'elle est dangereuse ; elle est esquivée si elle "
              "repart sans vous blesser. Chaque attaque compte une seule fois. Taux = esquivées ÷ menaces. Les attaques à distance sans tir "
              "(lasers, pluies, marées…) comptent dans « Ce qui vous touche », pas dans l'esquive.") +
         ui.s(8);
    y += note(x, y, w,
              "Précision : un coup de l'attaque, un tir ou une lame (Onde, Tir chargé, Bombe à plasma, Boomerangs, Frappe sismique) « touche » "
              "s'il retire des points de vie à au moins un ennemi ; un tir qui en traverse plusieurs compte une seule fois. Les pouvoirs qui "
              "n'infligent pas de dégâts (Nova de givre, Bouclier, Dash…) ne comptent pas. Le mode Invincible (admin) n'est jamais compté. "
              "Les images des origines montrent un exemple de chacune : le jeu garde l'origine d'un coup, jamais l'ennemi qui l'a porté.");
    return y - y0;
}

// ------------------------------------------------------------------ Données et sauvegarde

float StatisticsScene::drawData(float x, float y0, float w) {
    UIContext& ui = g_.ui;
    float y = y0;
    const std::string path = Paths::toUtf8(g_.playStats.file());
    if (!breakdown_ && !breakdownJob_) {
        // The share of each part of the file: stats.json read off the main thread, once per visit.
        const fs::path file = g_.playStats.file();
        breakdownJob_ = std::async(std::launch::async, [file] {
            playstats::FileBreakdown b;
            std::error_code ec;
            const auto size = fs::file_size(file, ec);
            if (ec) return b;
            if (auto text = FileUtil::readText(file)) {
                try {
                    b = playstats::fileBreakdown(Json::parse(*text), static_cast<size_t>(size));
                } catch (const Json::exception&) {
                }
            }
            return b;
        });
    }
    const float gap = ui.s(16);
    const float pad = ui.s(18), f = fz(15);
    // A card's head: an icon of the game, its title in the accent colour.
    auto head = [&](const Rect& r, const std::function<void(const Rect&)>& icon, const std::string& title, float yy, bool fresh = false) {
        const float is = ui.s(28);
        if (icon) icon({r.x + pad, yy, is, is});
        const float tx = r.x + pad + is + ui.s(10);
        const float pw = fresh ? ui.newPillWidth(true) + ui.s(10) : 0.0f;
        const float room = r.right() - pad - tx - pw;
        const float tf = ui.fitFont(title, FontFace::Bold, fz(15), room, 0.8f);
        const std::string shown = ui.elide(title, FontFace::Bold, tf, room);
        ui.label(shown, {tx, yy + is * 0.5f}, tf, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        if (fresh) ui.drawNewPill(tx + ui.text().measure(shown, FontFace::Bold, ui.s(tf)).x + pw, yy + is * 0.5f, true);
        return is + ui.s(10);
    };
    auto hud = [&](int index) { return [this, index](const Rect& b) { g_.ui.icon("ui/icons_hud.png", index, 16, b); }; };
    auto paragraph = [&](const std::string& t, float px, float py, float pw, bool draw) {
        const float h = ui.text().measureWrapped(t, FontFace::Body, ui.s(f), pw).y;
        if (draw) ui.labelWrapped(t, {px, py}, pw, f, kText2);
        return h;
    };
    // « Ce qui est enregistré »: a line per part, with its picture.
    struct Item {
        std::function<void(const Rect&)> icon;
        std::string text;
    };
    const std::vector<Item> items = {
        {hud(6), "**Par jour** : temps joué, morts, menaces et esquives, attaques lancées et réussies (par attaque), origine des coups reçus, "
                 "étoiles des succès."},
        {[this](const Rect& b) {
             if (TexturePtr t = g_.app.resources().texture("textures/tiles/sanctum_stone.png")) {
                 const Rect src{0, 16, 16, 16};
                 g_.app.renderer().drawTexture(*t, &src, b.expanded(-g_.ui.s(4)));
             }
         },
         "**Par niveau** : passages et morts dans chacune des 40 tranches."},
        {[this](const Rect& b) { drawEnemy(g_, "mycelia", b); }, "**Par boss** : chaque tentative (part des points de vie retirés, phase atteinte, "
                                                                  "attaque fatale), les 200 dernières."},
        {[this](const Rect& b) { drawSprite(g_, "animations/checkpoint.json", "active", 0, b); },
         "**Par niveau classique** : les temps de chaque segment (les 50 derniers), vos arrivées, votre record. La somme des meilleurs, une "
         "seule par version du niveau et toutes sources confondues, est gardée avec vos meilleurs segments (bests.json)."},
        {[this](const Rect& b) { drawSprite(g_, "animations/nova.json", "idle", 0, b); }, "**Jamais** : aucune donnée personnelle, aucune position."},
    };
    auto recordedCard = [&](const Rect& r, bool draw) {
        float yy = r.y + ui.s(16);
        if (draw) head(r, hud(3), "CE QUI EST ENREGISTRÉ", yy);
        yy += ui.s(28) + ui.s(14);
        const float iw = ui.s(40), tx = r.x + pad + iw + ui.s(12), tw = r.right() - pad - tx;
        for (const auto& it : items) {
            const float th = richText(it.text, tx, yy, tw, 14, kText2, false);
            const float rowH = std::max(ui.s(38), th);
            if (draw) {
                const Rect ib{r.x + pad, yy + (rowH - ui.s(36)) * 0.5f, iw, ui.s(36)};
                ui.r().fillRoundedRect(ib, ui.s(7), Color{5, 8, 18, 180});
                ui.r().drawRoundedRect(ib, ui.s(7), Color{38, 53, 90, 255}, std::max(1.0f, ui.s(1.0f)));
                ui.r().pushClip(ib);
                it.icon(ib.expanded(-ui.s(4)));
                ui.r().popClip();
                richText(it.text, tx, yy + (rowH - th) * 0.5f, tw, 14, kText2, true);
            }
            yy += rowH + ui.s(8);
        }
        return yy - r.y + ui.s(8);
    };
    // « Où et comment »: where, when, how big.
    std::error_code ec;
    const auto fileBytes = fs::file_size(g_.playStats.file(), ec);
    const std::string where = "Dans stats.json, à côté de vos autres sauvegardes. Écrit à la fin de chaque tentative (une chute, la fin ou l'abandon "
                              "d'une partie) et à la fermeture du jeu, sur un fil secondaire (jamais pendant que vous jouez), par écriture atomique "
                              "(fichier temporaire puis remplacement), comme les records.";
    const std::string damaged = "Fichier abîmé : l'onglet repart de zéro, l'ancien est gardé en stats.json.bak.";
    auto whereCard = [&](const Rect& r, bool draw) {
        float yy = r.y + ui.s(16);
        if (draw) head(r, hud(4), "OÙ ET COMMENT", yy);
        yy += ui.s(28) + ui.s(12);
        const float tw = r.w - 2 * pad;
        yy += paragraph(where, r.x + pad, yy, tw, draw) + ui.s(16);
        // The size of the file against its ceiling.
        const float lf = fz(14), lh = lineHeight(ui, FontFace::Body, ui.s(lf));
        if (draw) {
            ui.label("Taille actuelle", {r.x + pad, yy}, lf, ui.theme().textDim);
            const std::string sz = ec ? std::string("pas encore de fichier") : std::to_string((fileBytes + 1023) / 1024) + " Ko";
            const std::string cap = "moins de 300 Ko même après des années";
            const float sw = ui.text().measure(sz, FontFace::Bold, ui.s(lf)).x;
            const float labelW = ui.text().measure("Taille actuelle", FontFace::Body, ui.s(lf)).x;
            const float capRoom = r.right() - pad - sw - ui.s(10) - (r.x + pad + labelW + ui.s(16));
            if (capRoom > ui.s(60)) {
                const float cf = ui.fitFont(cap, FontFace::Body, fz(13), capRoom, 0.85f);
                ui.label(ui.elide(cap, FontFace::Body, cf, capRoom), {r.right() - pad, yy + lh * 0.5f}, cf, ui.theme().textDim, Align::Right, FontFace::Body,
                         VAlign::Middle);
            }
            ui.label(sz, {r.right() - pad - (capRoom > ui.s(60) ? capRoom + ui.s(10) : 0.0f), yy}, lf, ui.theme().text, Align::Right, FontFace::Bold);
            const Rect gauge{r.x + pad, yy + lh + ui.s(8), tw, ui.s(12)};
            ui.r().fillRoundedRect(gauge, ui.s(6), kTrack);
            const float k = ec ? 0.0f : std::clamp(static_cast<float>(fileBytes) / static_cast<float>(playstats::kMaxFileBytes), 0.0f, 1.0f);
            if (k > 0.0f) {
                const Rect fill{gauge.x, gauge.y, std::max(ui.s(8), gauge.w * k), gauge.h};
                ui.r().fillRoundedRect(fill, ui.s(6), ui.theme().accent);
                ui.r().fillGradientH({fill.x + fill.w * 0.5f, fill.y + ui.s(2), fill.w * 0.5f - ui.s(4), fill.h - ui.s(4)}, ui.theme().accent, kSeries[0]);
            }
        }
        yy += lh + ui.s(8) + ui.s(12) + ui.s(16);
        // What keeps it small.
        const float tg = ui.s(10), mw = (tw - 2 * tg) / 3.0f, mh = miniTileHeight(true, true);
        if (draw) {
            miniTile({r.x + pad, yy, mw, mh}, "JOURS GARDÉS", std::to_string(playstats::kDaysKept), "puis fusion par mois");
            miniTile({r.x + pad + mw + tg, yy, mw, mh}, "PAR BOSS", std::to_string(playstats::kBossAttemptsKept), "dernières tentatives");
            miniTile({r.x + pad + 2 * (mw + tg), yy, mw, mh}, "PAR SEGMENT", std::to_string(playstats::kSegmentTimesKept), "derniers temps");
        }
        yy += mh + ui.s(14);
        yy += paragraph(damaged, r.x + pad, yy, tw, draw) + ui.s(16);
        return yy - r.y;
    };
    const std::string outside = "Les statistiques lisent ce qui se passe sans rien changer à la partie : replays, records et classements ne bougent "
                                "pas. Les replays relus (mesures A → B comprises) et les parties d'amis regardées ne sont pas comptés, ni les tests de "
                                "l'éditeur ni le mode Invincible ; l'entraînement est compté à part.";
    const std::string travels = "Connecté à Cosmic Online, le jeu envoie vos statistiques dans la copie privée de votre progression, avec vos records "
                                "et vos succès (aucun autre joueur ne la voit). Sans compte ni Internet, rien ne part et tout fonctionne ; pour les "
                                "garder vous-même, copiez stats.json.";
    auto outsideCard = [&](const Rect& r, bool draw) {
        float yy = r.y + ui.s(16);
        const float tw = r.w - 2 * pad;
        if (draw) head(r, hud(10), "HORS DE LA SIMULATION", yy);
        yy += ui.s(28) + ui.s(12);
        yy += paragraph(outside, r.x + pad, yy, tw, draw) + ui.s(20);
        if (draw) head(r, hud(9), "AVEC VOTRE PROGRESSION", yy, dataFresh_);
        yy += ui.s(28) + ui.s(12);
        yy += paragraph(travels, r.x + pad, yy, tw, draw) + ui.s(16);
        return yy - r.y;
    };
    const std::string migrate = "La progression restaurée apporte ses statistiques, ajoutées aux vôtres sans double compte : chaque jour garde le plus "
                                "grand des deux compteurs, chaque tentative de boss n'est comptée qu'une fois (les 200 plus récentes gardées), les temps "
                                "des segments sont réunis, le meilleur record et la meilleure somme des meilleurs sont gardés. Restaurer deux fois la "
                                "même copie ne change rien. Avant, vos sauvegardes, stats.json compris, sont copiées dans saves › avant_restauration_…";
    auto migrateCard = [&](const Rect& r, bool draw) {
        float yy = r.y + ui.s(16);
        if (draw) head(r, hud(2), "MIGRER VERS UN NOUVEAU COMPTE", yy, dataFresh_);
        yy += ui.s(28) + ui.s(12);
        yy += paragraph(migrate, r.x + pad, yy, r.w - 2 * pad, draw) + ui.s(16);
        return yy - r.y;
    };
    // « Vous décidez »: recording on or off (the setting of Paramètres › Jeu), the settings, the reset.
    auto decideCard = [&](const Rect& r, bool draw) {
        float yy = r.y + ui.s(16);
        if (draw) head(r, hud(5), "VOUS DÉCIDEZ", yy);
        yy += ui.s(28) + ui.s(12);
        const float rowH = ui.s(44);
        if (draw) {
            bool on = g_.settings.gameplay.recordStats;
            if (ui.toggle("record", {r.x + pad, yy, r.w - 2 * pad, rowH}, "Enregistrer mes statistiques", on)) {
                g_.settings.gameplay.recordStats = on;
                g_.playStats.enabled = on;
                g_.saveSettings();
                status_ = on ? "Enregistrement activé." : "Enregistrement désactivé : rien de nouveau n'est compté.";
            }
        }
        yy += rowH + ui.s(12);
        UIContext::ButtonStyle st, danger;
        st.fontSize = fz(17);
        danger.danger = true;
        danger.fontSize = fz(17);
        const std::string ot = "Ouvrir les Paramètres", rt = "Réinitialiser les statistiques…";
        const float ow = ui.text().measure(ot, FontFace::Bold, ui.s(fz(17))).x + ui.s(36);
        const float rw = ui.text().measure(rt, FontFace::Bold, ui.s(fz(17))).x + ui.s(36);
        const bool sideBySide = ow + ui.s(12) + rw <= r.w - 2 * pad;
        const float bh = ui.s(44);
        if (draw) {
            if (cframe::button(ui, "settings", {r.x + pad, yy, std::min(ow, r.w - 2 * pad), bh}, ot, st)) g_.app.pushScene(makeSettingsScene(g_, true));
            const Rect rb = sideBySide ? Rect{r.x + pad + ow + ui.s(12), yy, rw, bh} : Rect{r.x + pad, yy + bh + ui.s(10), std::min(rw, r.w - 2 * pad), bh};
            if (cframe::button(ui, "reset", rb, rt, danger)) confirmReset_ = true;
        }
        yy += (sideBySide ? bh : 2 * bh + ui.s(10)) + ui.s(12);
        const float sf = fz(13), sh = lineHeight(ui, FontFace::Body, ui.s(sf));
        const std::string after = status_.empty() ? std::string("Avec confirmation ; records et succès ne changent pas.") : status_;
        if (draw) ui.label(ui.elide(after, FontFace::Body, sf, r.w - 2 * pad), {r.x + pad, yy}, sf, status_.empty() ? ui.theme().textDim : ui.theme().success);
        yy += sh + ui.s(8);
        if (draw) {
            i18n::Verbatim pathAsIs;  // a path is shown as it is
            const std::string file = path;
            ui.label(ui.elide(file, FontFace::Body, sf, r.w - 2 * pad), {r.x + pad, yy}, sf, ui.theme().textDim);
        }
        yy += sh + ui.s(16);
        return yy - r.y;
    };
    // Laid out by rows, each card as tall as its contents (measured), the cards of a row as tall as the tallest.
    using CardFn = std::function<float(const Rect&, bool)>;
    auto row = [&](const std::vector<std::pair<float, CardFn>>& cards) {
        float rowH = 0.0f, cx = x;
        const float total = w - gap * static_cast<float>(cards.size() - 1);
        for (const auto& [share, fn] : cards) {
            rowH = std::max(rowH, fn({cx, y, total * share, 10000.0f}, false));
            cx += total * share + gap;
        }
        cx = x;
        for (const auto& [share, fn] : cards) {
            const Rect r{cx, y, total * share, rowH};
            cardFrame(r);
            fn(r, true);
            cx += total * share + gap;
        }
        y += rowH + gap;
    };
    if (w >= ui.s(1500)) {
        row({{1.0f / 3.0f, recordedCard}, {1.0f / 3.0f, whereCard}, {1.0f / 3.0f, outsideCard}});
        row({{2.0f / 3.0f, migrateCard}, {1.0f / 3.0f, decideCard}});
    } else if (w >= ui.s(1000)) {
        row({{0.5f, recordedCard}, {0.5f, whereCard}});
        row({{0.5f, outsideCard}, {0.5f, migrateCard}});
        row({{1.0f, decideCard}});
    } else {
        for (const CardFn& fn : std::vector<CardFn>{recordedCard, whereCard, outsideCard, migrateCard, decideCard}) row({{1.0f, fn}});
    }
    // « Répartition du fichier » (2.91): the share of each part, read from the file itself.
    {
        const char* names[4] = {"Jours et mois", "Boss", "Niveaux", "Segments"};
        auto details = [](const playstats::FilePart& p) -> std::string {
            switch (p.kind) {
                case playstats::FilePart::Kind::Days:
                    return std::to_string(p.count) + (p.count > 1 ? " jours, " : " jour, ") + std::to_string(p.count2) +
                           (p.count2 > 1 ? " mois fusionnés" : " mois fusionné");
                case playstats::FilePart::Kind::Bosses:
                    return std::to_string(p.count) + " boss, " + std::to_string(p.count2) + (p.count2 > 1 ? " tentatives" : " tentative");
                case playstats::FilePart::Kind::Levels:
                    return std::to_string(p.count) + (p.count > 1 ? " niveaux" : " niveau") + " × " + std::to_string(p.count2) + " tranches";
                default:
                    return std::to_string(p.count) + (p.count > 1 ? " niveaux classiques, " : " niveau classique, ") + std::to_string(p.count2) +
                           (p.count2 > 1 ? " segments" : " segment");
            }
        };
        const float tilesH = miniTileHeight(false, true);
        const float ch = ui.s(16) + ui.s(28) + ui.s(10) + lineHeight(ui, FontFace::Body, ui.s(fz(14))) + ui.s(16) + ui.s(40) + ui.s(16) + tilesH + ui.s(18);
        const Rect r{x, y, w, ch};
        cardFrame(r);
        float yy = r.y + ui.s(16);
        head(r, hud(4), "RÉPARTITION DU FICHIER", yy, ui.isNew("stats.file_parts"));
        yy += ui.s(28) + ui.s(10);
        const std::string sub = "Calculée en lisant stats.json : la taille de chaque partie, rien de plus n'est enregistré.";
        ui.label(ui.elide(sub, FontFace::Body, fz(14), r.w - 2 * pad), {r.x + pad, yy}, fz(14), ui.theme().textDim);
        yy += lineHeight(ui, FontFace::Body, ui.s(fz(14))) + ui.s(16);
        const Rect bar{r.x + pad, yy, r.w - 2 * pad, ui.s(40)};
        ui.newBadge("c/fileparts", bar, "stats.file_parts", false);  // the pill is after the title; seen with the bar
        if (!breakdown_) {
            picturePending(bar, false);
        } else if (!breakdown_->ok) {
            ui.hotspot("c/fileparts", bar);
            ui.label("Pas encore de fichier : il est écrit à la fin de la première tentative.", {bar.x, bar.center().y}, fz(14), ui.theme().textDim, Align::Left,
                     FontFace::Body, VAlign::Middle);
        } else {
            std::vector<charts::Part> parts;
            for (size_t i = 0; i < breakdown_->parts.size() && i < 4; ++i) {
                const auto& p = breakdown_->parts[i];
                const std::string kb = std::to_string((p.bytes + 1023) / 1024) + " Ko";
                parts.push_back({static_cast<float>(p.bytes), kSeries[i], std::string(names[i]) + " · " + kb, names[i],
                                 {{charts::kNoSwatch, "Taille", kb}, {charts::kNoSwatch, "Contenu", details(p)},
                                  {charts::kNoSwatch, "Part du fichier", pct(breakdown_->total ? static_cast<double>(p.bytes) / breakdown_->total : 0.0)}}});
            }
            charts::stackedBar(ui, "c/fileparts", bar, parts, cursorOf("fileparts"), tip_);
        }
        yy += bar.h + ui.s(16);
        const float tg = ui.s(12), tw = (r.w - 2 * pad - 3 * tg) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            std::string d = "—";
            if (breakdown_ && breakdown_->ok && i < static_cast<int>(breakdown_->parts.size())) d = details(breakdown_->parts[static_cast<size_t>(i)]);
            std::string upper = names[i];
            for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            miniTile({r.x + pad + (tw + tg) * static_cast<float>(i), yy, tw, tilesH}, upper, "", d, ui.theme().textDim, kSeries[i]);
        }
        y += ch + gap;
    }
    return y - y0;
}

void StatisticsScene::drawConfirm() {
    UIContext& ui = g_.ui;
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{0, 0, 0, 160});
    Rect m = Rect::fromCenter({ui.screenW() * 0.5f, ui.screenH() * 0.5f}, {std::min(ui.screenW() - ui.s(40), ui.s(760)), ui.s(250)});
    cframe::panel(ui, m, true, 16);
    const std::string head = "Réinitialiser les statistiques ?";
    float hf = ui.fitFont(head, FontFace::Bold, 26, m.w - ui.s(60), 0.6f);
    ui.label(head, {m.center().x, m.y + ui.s(42)}, hf, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
    ui.labelWrapped("Toutes vos statistiques (jours, combats, boss, niveaux, passages) seront effacées de cet ordinateur. Vos records, succès et "
                    "statistiques de boss d'avant ne changent pas.",
                    {m.x + ui.s(30), m.y + ui.s(76)}, m.w - ui.s(60), fz(16), kText2);
    const float bw = (m.w - ui.s(90)) / 2.0f;
    UIContext::ButtonStyle st, danger;
    danger.danger = true;
    if (cframe::button(ui, "no", {m.x + ui.s(30), m.bottom() - ui.s(76), bw, ui.s(52)}, "Annuler", st) || ui.backPressed()) confirmReset_ = false;
    else if (cframe::button(ui, "yes", {m.right() - ui.s(30) - bw, m.bottom() - ui.s(76), bw, ui.s(52)}, "Tout effacer", danger)) {
        g_.playStats.reset();
        confirmReset_ = false;
        breakdown_.reset();  // read again
        status_ = "Statistiques réinitialisées.";
    }
    if (!ui.hasFocus()) ui.setFocus("stats/no");
}

std::unique_ptr<Scene> makeStatistics(GameServices& g, int tab) { return std::make_unique<StatisticsScene>(g, tab); }

}  // namespace cosmic
