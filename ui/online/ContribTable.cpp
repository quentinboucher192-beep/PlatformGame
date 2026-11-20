#include "ui/online/ContribTable.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "engine/input/InputManager.h"
#include "game/GameServices.h"
#include "game/online/Raids.h"
#include "game/raid/RaidBranch.h"
#include "ui/online/RaidMenu.h"
#include "ui/online/RaidWidgets.h"
#include "ui/screens/AsterionFilm.h"
#include "ui/screens/CreatureArt.h"
#include "ui/widgets/AsterionFigure.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::contrib {

using namespace raidui;

namespace {

constexpr const char* kIcons = "ui/raids/contrib.png";  // tools/assetgen/environment/raid_contrib_icons.py
constexpr const char* kRelic = "sprites/objects/relic.png";  // the relic of the levels (8 frames of 16 px)
constexpr int kIconCell = 32;
// The icons' order in the sheet (ui/raids/contrib.json): boss, mini, support, mechanisms, objectives,
// deaths, revives, revived, crown, arrow.
constexpr int kCrownIcon = 8;
const Color kPanelFill{18, 26, 54, 255};  // opaque: nothing of the game shows through
const Color kHeadFill{24, 34, 68, 255};
const Color kRowA{22, 32, 64, 255};
const Color kRowB{19, 28, 58, 255};
const Color kSection{30, 30, 70, 255};
const Color kMeEdge{110, 190, 255, 255};
const Color kTrack{34, 44, 82, 255};
const Color kMuted{140, 152, 186, 255};
const Color kOn{40, 80, 120, 255};
const Color kHover{30, 44, 80, 255};
const Color kFallen{150, 40, 48, 255};

int iconOf(Col c) {
    switch (c) {
        case Col::Boss: return 0;
        case Col::Mini: return 1;
        case Col::Support: return 2;
        case Col::Mechanisms: return 3;
        case Col::Objectives: return 4;
        case Col::Deaths: return 5;
        case Col::Revives: return 6;
        case Col::Revived: return 7;
        default: return -1;
    }
}

/// « 1 234 » (the hearts of tenths, rounded).
std::string hearts(int64_t tenths) {
    const int64_t v = (tenths + 5) / 10;
    std::string s = std::to_string(v), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i > 0 && (s.size() - i) % 3 == 0) out += ' ';
        out += s[i];
    }
    return out;
}

std::string pct(double share) { return std::to_string(static_cast<int>(std::lround(share * 100.0))) + " %"; }

/// The column weights (their share of the width; the player's is the widest).
constexpr float kWeight[kCols] = {1.75f, 1.9f, 1.5f, 0.86f, 0.98f, 0.86f, 0.7f, 0.9f, 0.9f};

bool crowned(Col c) { return c == Col::Boss || c == Col::Mini || c == Col::Support || c == Col::Mechanisms || c == Col::Objectives || c == Col::Revives; }

const Part* partOf(const Model& m, const View& v) { return v.part >= 0 && v.part < static_cast<int>(m.parts.size()) ? &m.parts[static_cast<size_t>(v.part)] : nullptr; }

const Table& tableOf(const Model& m, const View& v) {
    static const Table none;
    const Part* part = partOf(m, v);
    if (v.tab == 1) return part ? part->whole : m.raid;
    const std::vector<Table>& zones = zonesOf(m, v);
    if (zones.empty()) return none;
    return zones[static_cast<size_t>(std::clamp(v.zone, 0, static_cast<int>(zones.size()) - 1))];
}

/// The name of a tab: « Raid entier », or, a part chosen, the whole of that part.
const char* tabName(const Model& m, const View& v, int i) {
    if (i == 0) return "Zone";
    const Part* part = partOf(m, v);
    if (!part) return "Raid entier";
    return part->id == "coeur" ? "Tout le Cœur" : "Toute la branche";
}

void triangleDown(UIContext& ui, Vec2 c, float s, Color col) { ui.r().fillTriangle({c.x - s, c.y - s * 0.55f}, {c.x + s, c.y - s * 0.55f}, {c.x, c.y + s * 0.65f}, col); }

std::string keysLine(const Model& m, bool zones, bool parts) {
    const std::string tab = m.tabKey.empty() ? std::string("LB RB") : parts ? m.tabKey : m.tabKey + " / LB RB";
    std::string s = tab + " : onglets · flèches gauche / droite : trier · survol ou flèches haut / bas : détails";
    if (parts) s += " · " + m.partKey + " : partie du titan";
    s += " · molette ou barre : défiler";
    if (zones) s += " · Entrée / A : zone suivante";
    if (!m.extraKeys.empty()) s += " · " + m.extraKeys;
    return s + " · Échap / B : fermer";
}

/// The words of a part's state (its pill on its callout).
const char* stateWord(const Part& p) {
    if (p.fallen) return "TOMBÉ";
    switch (p.state) {
        case asterion::State::Attacked: return "COMBAT EN COURS";
        case asterion::State::Weakened: return "AFFAIBLI";
        case asterion::State::Destroyed: return "TOMBÉ";
        case asterion::State::Unknown: return "PAS DÉCOUVERT";
        default: return "INTACT";
    }
}

Color stateFill(const Part& p) {
    if (p.fallen || p.state == asterion::State::Destroyed) return kFallen;
    switch (p.state) {
        case asterion::State::Attacked: return {122, 74, 26, 255};
        case asterion::State::Weakened: return {122, 26, 26, 255};
        case asterion::State::Unknown: return {42, 58, 122, 255};
        default: return {31, 94, 58, 255};
    }
}

std::string groupsLine(const Part& p) {
    if (p.groups.empty()) return "Aucun groupe";
    if (p.groups.size() == 1) return "Groupe " + std::to_string(p.groups[0]);
    std::string s = "Groupes ";
    for (size_t i = 0; i < p.groups.size(); ++i) s += (i == 0 ? "" : ", ") + std::to_string(p.groups[i]);
    return s;
}

/// The line under the titan: what is shown.
std::string captionText(const Model& m, const View& v) {
    const Part* p = partOf(m, v);
    if (!p) return "Raid entier : chaque groupe et ses joueurs. Une partie du titan (clic, ou " + m.partKey + ") : ses groupes et ses zones.";
    std::string s = p->name + (p->gigaboss.empty() ? std::string() : " · " + p->gigaboss) + " · " + groupsLine(*p);
    if (p->relic) s += " · relique rapportée";
    if (!p->open && !p->closed.empty()) s += " · " + p->closed;
    return s;
}

/// The titan's manifest (its canvas, each part's point), read once: the layout places the callouts without GameServices.
const asterion::Sprite& titanSprite() {
    static asterion::Sprite s;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        if (auto j = FileUtil::loadJson(Paths::assetsDir() / "ui" / "raids" / "asterion.json")) s = asterion::parseSprite(*j);
    }
    return s;
}

/// The titan's column: « Raid entier », the drawing between the callouts (half the parts at its left, half at
/// its right, each side from the top down as they are on the titan), the caption. False: no room for it.
bool layoutTitan(UIContext& ui, Layout& L, const Model& m, const View& v) {
    const Rect c = L.titan;
    L.overview = {c.x, c.y, c.w, ui.s(42)};
    L.captionLines = wrap(ui, captionText(m, v), FontFace::Body, 14, c.w, 3);
    const float capH = static_cast<float>(L.captionLines.size()) * ui.s(19);
    L.caption = {c.x, c.bottom() - capH, c.w, capH};
    const float top = L.overview.bottom() + ui.s(12);
    const Rect area{c.x, top, c.w, L.caption.y - ui.s(10) - top};
    const size_t n = m.parts.size();
    const asterion::Sprite& sp = titanSprite();
    const float cw = sp.valid ? static_cast<float>(sp.canvasW) : 160.0f, ch = sp.valid ? static_cast<float>(sp.canvasH) : 128.0f;
    std::vector<Vec2> at(n);
    for (size_t i = 0; i < n; ++i) {
        at[i] = {cw * 0.5f, ch * static_cast<float>(i + 1) / static_cast<float>(n + 1)};
        for (const asterion::Layer& l : sp.layers)
            if (l.id == m.parts[i].id) at[i] = {static_cast<float>(l.anchorX) + 0.5f, static_cast<float>(l.anchorY) + 0.5f};
    }
    std::vector<int> byX(n);
    for (size_t i = 0; i < n; ++i) byX[i] = static_cast<int>(i);
    std::stable_sort(byX.begin(), byX.end(), [&](int a, int b) { return at[static_cast<size_t>(a)].x < at[static_cast<size_t>(b)].x; });
    std::vector<int> left(byX.begin(), byX.begin() + static_cast<long>((n + 1) / 2)), right(byX.begin() + static_cast<long>((n + 1) / 2), byX.end());
    auto byY = [&](int a, int b) { return at[static_cast<size_t>(a)].y < at[static_cast<size_t>(b)].y; };
    std::stable_sort(left.begin(), left.end(), byY);
    std::stable_sort(right.begin(), right.end(), byY);
    const float calW = std::clamp(c.w * 0.3f, ui.s(150), ui.s(200)), cgap = ui.s(10);
    const size_t perSide = std::max<size_t>(1, std::max(left.size(), right.size()));
    const float calH = std::min(ui.s(104), (area.h - cgap * static_cast<float>(perSide - 1)) / static_cast<float>(perSide));
    L.figure = {area.x + calW + ui.s(14), area.y, area.w - calW * 2.0f - ui.s(28), area.h};
    if (calH < ui.s(48) || L.figure.w < ui.s(80) || area.h <= 0.0f) return false;
    float k = std::min(L.figure.w / cw, L.figure.h / ch);
    if (k >= 1.0f) k = std::floor(k);  // as AsterionFigure: whole multiples once enlarged
    L.titanScale = k;
    const float ox = std::round(L.figure.x + (L.figure.w - cw * k) * 0.5f), oy = std::round(L.figure.y + (L.figure.h - ch * k) * 0.5f);
    L.anchors.assign(n, {});
    L.callouts.assign(n, {});
    for (size_t i = 0; i < n; ++i) L.anchors[i] = {ox + at[i].x * k, oy + at[i].y * k};
    auto stack = [&](const std::vector<int>& side, float x) {
        const float cnt = static_cast<float>(side.size());
        if (side.empty()) return;
        const float spread = side.size() > 1 ? std::min(ui.s(60), (area.h - cnt * calH) / (cnt - 1.0f)) : 0.0f;
        const float total = cnt * calH + (cnt - 1.0f) * spread;
        float y = area.y + (area.h - total) * 0.5f;
        for (int i : side) {
            L.callouts[static_cast<size_t>(i)] = {x, y, calW, calH};
            y += calH + spread;
        }
    };
    stack(left, area.x);
    stack(right, area.right() - calW);
    return true;
}

}  // namespace

const char* colName(Col c) {
    switch (c) {
        case Col::Player: return "Joueur";
        case Col::Boss: return "Dégâts au boss";
        case Col::Mini: return "Mini-boss";
        case Col::Support: return "Soutien";
        case Col::Mechanisms: return "Mécanismes";
        case Col::Objectives: return "Objectifs";
        case Col::Deaths: return "Morts";
        case Col::Revives: return "Réa. faites";
        case Col::Revived: return "Réa. reçues";
        default: return "";
    }
}

Color colColor(Col c) {
    switch (c) {
        case Col::Boss: return {255, 96, 96, 255};
        case Col::Mini: return {255, 160, 80, 255};
        case Col::Support: return {96, 220, 150, 255};
        case Col::Mechanisms: return {255, 205, 110, 255};
        case Col::Objectives: return {110, 170, 255, 255};
        case Col::Deaths: return {226, 232, 248, 255};
        case Col::Revives: return {120, 235, 170, 255};
        case Col::Revived: return {180, 240, 210, 255};
        default: return kText;
    }
}

const std::vector<Table>& zonesOf(const Model& m, const View& v) {
    const Part* part = partOf(m, v);
    return part ? part->zones : m.zones;
}

std::vector<Part> titanParts() {
    static std::vector<Part> parts;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::map<std::string, std::string> enemies;  // part → its gigaboss's enemy
        if (auto j = FileUtil::loadJson(Paths::dataDir() / "raids" / "asterion_sheets.json"))
            if (j->contains("gigaboss") && (*j)["gigaboss"].is_array())
                for (const Json& e : (*j)["gigaboss"]) enemies[jstr(e, "part")] = jstr(e, "enemy");
        if (auto j = FileUtil::loadJson(Paths::dataDir() / "raids" / "asterion.json"))
            for (const asterion::Part& a : asterion::parseParts(*j)) {
                Part p;
                p.id = a.id;
                p.name = a.name;
                p.gigaboss = a.boss;
                if (auto it = enemies.find(a.id); it != enemies.end()) p.enemy = it->second;
                parts.push_back(std::move(p));
            }
    }
    return parts;
}

// ------------------------------------------------------------------------------------- the numbers

Prepared prepare(const Table& t, Col sort) {
    Prepared p;
    p.table = &t;
    const size_t n = t.rows.size();
    int64_t bossAll = 0, miniAll = 0, damageAll = 0;
    for (const Row& r : t.rows) {
        bossAll += std::max<int64_t>(0, r.bossTenths - (r.v2 ? r.miniTenths : 0));
        miniAll += r.v2 ? std::max<int64_t>(0, r.miniTenths) : 0;
        damageAll += std::max<int64_t>(0, r.damageTenths);
    }
    p.bossShare = bossAll > 0;
    for (int c = 0; c < kCols; ++c) p.headers[static_cast<size_t>(c)] = colName(static_cast<Col>(c));
    if (!p.bossShare) p.headers[static_cast<size_t>(Col::Boss)] = "Dégâts";
    p.cells.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Row& r = t.rows[i];
        auto& c = p.cells[i];
        c[static_cast<size_t>(Col::Player)].text = r.name;
        const int64_t boss = std::max<int64_t>(0, r.bossTenths - (r.v2 ? r.miniTenths : 0));
        const double share = p.bossShare ? static_cast<double>(boss) / static_cast<double>(bossAll)
                             : damageAll > 0 ? static_cast<double>(std::max<int64_t>(0, r.damageTenths)) / static_cast<double>(damageAll)
                                             : 0.0;
        c[static_cast<size_t>(Col::Boss)] = {true, share, pct(share), 0.0f, false};
        if (r.v2) {
            const double ms = miniAll > 0 ? static_cast<double>(std::max<int64_t>(0, r.miniTenths)) / static_cast<double>(miniAll) : 0.0;
            c[static_cast<size_t>(Col::Mini)] = {true, ms, pct(ms), 0.0f, false};
        } else {
            c[static_cast<size_t>(Col::Mini)] = {false, -1.0, "—", -1.0f, false};
        }
        auto num = [&](Col col, int v, bool known) {
            c[static_cast<size_t>(col)] = known ? Cell{true, static_cast<double>(v), std::to_string(v), -1.0f, false} : Cell{false, -1.0, "—", -1.0f, false};
        };
        num(Col::Support, r.support, true);
        num(Col::Mechanisms, r.mechanisms, true);
        num(Col::Objectives, r.objectives, true);
        num(Col::Deaths, r.deaths, r.v2);
        num(Col::Revives, r.revives, r.v2);
        num(Col::Revived, r.revived, r.v2);
    }
    // The bars: proportional to the best of the column; the crowns: the best of each category (ties: all of them).
    for (int col = 1; col < kCols; ++col) {
        double best = 0.0;
        for (size_t i = 0; i < n; ++i)
            if (p.cells[i][static_cast<size_t>(col)].known) best = std::max(best, p.cells[i][static_cast<size_t>(col)].value);
        for (size_t i = 0; i < n; ++i) {
            Cell& cell = p.cells[i][static_cast<size_t>(col)];
            if (col == static_cast<int>(Col::Boss) || col == static_cast<int>(Col::Mini))
                cell.bar = cell.known ? (best > 0.0 ? static_cast<float>(cell.value / best) : 0.0f) : -1.0f;
            cell.best = crowned(static_cast<Col>(col)) && cell.known && best > 0.0 && cell.value >= best;
        }
    }
    p.order.resize(n);
    for (size_t i = 0; i < n; ++i) p.order[i] = static_cast<int>(i);
    std::stable_sort(p.order.begin(), p.order.end(), [&](int a, int b) {
        const auto& ca = p.cells[static_cast<size_t>(a)];
        const auto& cb = p.cells[static_cast<size_t>(b)];
        if (sort == Col::Player) return t.rows[static_cast<size_t>(a)].name < t.rows[static_cast<size_t>(b)].name;
        const Cell& x = ca[static_cast<size_t>(sort)];
        const Cell& y = cb[static_cast<size_t>(sort)];
        if (x.value != y.value) return x.value > y.value;  // the most first (« — » last)
        return t.rows[static_cast<size_t>(a)].name < t.rows[static_cast<size_t>(b)].name;
    });
    return p;
}

// ------------------------------------------------------------------------------------- the keys

void View::choose(int p) {
    part = p;
    zone = 0;
    selected = -1;
}

bool View::handleKeys(const InputManager& in, int zones, int parts) {
    if (in.menuPressed(Action::MenuBack)) return true;
    const bool prev = in.menuPressed(Action::MenuTabPrev), next = in.menuPressed(Action::MenuTabNext);
    if (parts > 0 && (prev || next)) {
        // -1 (« Raid entier ») then each part, around.
        const int n = parts + 1;
        choose(((part + 1 + (next ? 1 : -1)) % n + n) % n - 1);
    } else if (prev || next || in.menuPressed(Action::RaidMenu)) {
        tab = 1 - tab;
        selected = -1;
    }
    int step = 0;
    if (in.menuPressed(Action::MenuLeft)) step = -1;
    if (in.menuPressed(Action::MenuRight)) step = 1;
    if (step != 0) sort = static_cast<Col>((static_cast<int>(sort) + step + kCols) % kCols);
    if (in.menuPressed(Action::MenuUp)) selected = std::max(-1, selected - 1);
    if (in.menuPressed(Action::MenuDown)) selected = selected + 1;
    if (in.menuPressed(Action::MenuConfirm) && tab == 0 && zones > 1) {
        zone = (zone + 1) % zones;
        selected = -1;
    }
    return false;
}

// ------------------------------------------------------------------------------------- the layout

Layout layout(UIContext& ui, const Rect& r, const Model& m, const View& v) {
    Layout L;
    L.panel = r;
    const Table& t = tableOf(m, v);
    const Prepared p = prepare(t, v.sort);
    const bool emb = m.embedded;
    const float pad = emb ? 0.0f : ui.s(28), x = r.x + pad, iw = r.w - pad * 2;
    float y = r.y + (emb ? 0.0f : ui.s(18));
    if (!emb) {
        // The heading and « Fermer » (measured first, at the right).
        const float closeW = std::min(iw * 0.3f, textW(ui, "Fermer", 16, FontFace::Bold) + ui.s(36));
        L.close = {x + iw - closeW, y, closeW, ui.s(40)};
        L.heading = {x, y, std::max(0.0f, iw - closeW - ui.s(16)), ui.s(40)};
        L.texts.push_back(L.heading);
        y += ui.s(46);
    }
    L.sub = {x, y, iw, ui.s(26)};
    L.texts.push_back(L.sub);
    y += ui.s(34);
    // The keys, the whole width at the bottom.
    const bool parts = !m.parts.empty();
    L.keyLines = wrap(ui, keysLine(m, v.tab == 0 && zonesOf(m, v).size() > 1, parts), FontFace::Body, 15, iw, 2);
    const float keysH = static_cast<float>(L.keyLines.size()) * ui.s(20);
    L.keys = {x, r.bottom() - (emb ? 0.0f : ui.s(14)) - keysH, iw, keysH};
    L.texts.push_back(L.keys);
    const float bottom = L.keys.y - ui.s(10);
    // The titan's column (the table keeps at least its readable width; without room, no titan).
    float tx0 = x, tw = iw;
    if (parts) {
        float colW = std::clamp(iw * 0.34f, ui.s(380), ui.s(640));
        if (iw - colW - ui.s(20) < ui.s(820)) colW = iw - ui.s(20) - ui.s(820);
        if (colW >= ui.s(380)) {
            L.titan = {x, y, colW, bottom - y};
            if (layoutTitan(ui, L, m, v)) {
                tx0 = x + colW + ui.s(20);
                tw = iw - colW - ui.s(20);
                L.texts.push_back(L.overview);
                if (!L.captionLines.empty()) L.texts.push_back(L.caption);
                for (const Rect& c : L.callouts) L.texts.push_back(c);
            } else {
                L.titan = L.overview = L.figure = L.caption = Rect{};
                L.callouts.clear();
                L.anchors.clear();
                L.captionLines.clear();
                L.titanScale = 0.0f;
            }
        }
    }
    L.table = {tx0, y, tw, bottom - y};
    // The tabs, each as wide as its name, then (the « Zone » tab with several zones) the zone buttons, then the sort.
    float tx = tx0;
    for (int i = 0; i < 2; ++i) {
        const float w = textW(ui, tabName(m, v, i), 18, FontFace::Bold) + ui.s(40);
        L.tabs[static_cast<size_t>(i)] = {tx, y, w, ui.s(42)};
        tx += w + ui.s(10);
    }
    if (v.tab == 0 && zonesOf(m, v).size() > 1) {
        const float pw = textW(ui, "Zone précédente", 14, FontFace::Bold) + ui.s(26), nw = textW(ui, "Zone suivante", 14, FontFace::Bold) + ui.s(26);
        L.prevZone = {tx + ui.s(8), y + ui.s(4), pw, ui.s(34)};
        L.nextZone = {L.prevZone.right() + ui.s(8), y + ui.s(4), nw, ui.s(34)};
        tx = L.nextZone.right() + ui.s(10);
    }
    L.sortLine = {tx + ui.s(10), y, std::max(0.0f, tx0 + tw - tx - ui.s(10)), ui.s(42)};
    L.texts.push_back(L.sortLine);
    y += ui.s(54);
    // The bottom of the table's side: the best of the zone (its legend inside).
    L.legendLines = wrap(ui, m.legend, FontFace::Body, 14, tw - ui.s(40), 2);
    const float bestH = ui.s(76);
    L.best = {tx0, bottom - bestH, tw, bestH};
    // The rows in the order shown: by group (Table::sections) when it has some, each group sorted.
    struct Item {
        int section;  // its header (row < 0), or the group of the row
        int row;
    };
    std::vector<Item> items;
    const int ns = static_cast<int>(t.sections.size());
    for (int ri : p.order)
        if (t.rows[static_cast<size_t>(ri)].section < 0 || t.rows[static_cast<size_t>(ri)].section >= ns) items.push_back({-1, ri});
    for (int s = 0; s < ns; ++s) {
        items.push_back({s, -1});
        for (int ri : p.order)
            if (t.rows[static_cast<size_t>(ri)].section == s) items.push_back({s, ri});
    }
    const float headH = ui.s(48), gap = ui.s(6), secH = ui.s(36);
    const float listTop = y + headH + ui.s(10);
    L.list = {tx0, listTop, tw, std::max(0.0f, L.best.y - ui.s(12) - listTop)};
    size_t nRows = 0, nSecs = 0;
    for (const Item& it : items) (it.row < 0 ? nSecs : nRows) += 1;
    // As high as the room lets, never lower than readable (the rows scroll past that).
    float rowH = nRows > 0 ? (L.list.h - static_cast<float>(nSecs) * (secH + gap)) / static_cast<float>(nRows) - gap : ui.s(40);
    rowH = std::clamp(rowH, ui.s(44), ui.s(66));
    L.contentH = static_cast<float>(nSecs) * (secH + gap) + static_cast<float>(nRows) * (rowH + gap);
    if (!items.empty()) L.contentH -= gap;
    const float lane = L.contentH > L.list.h + 0.5f ? ui.s(16) : 0.0f;  // the scroll bar's lane, kept free
    const float rw = tw - lane;
    // The header and its columns.
    L.header = {tx0, y, rw, headH};
    float total = 0.0f;
    for (float w : kWeight) total += w;
    const float cgap = ui.s(10), room = rw - ui.s(20) - cgap * static_cast<float>(kCols - 1);
    float cx = tx0 + ui.s(14);
    for (int c = 0; c < kCols; ++c) {
        const float w = room * kWeight[c] / total;
        L.cols[static_cast<size_t>(c)] = {cx, y, w, headH};
        cx += w + cgap;
    }
    // The headers' font: the same for all, as large as the narrowest lets (an icon before each name, ▼ after the sorted one).
    const float iconW = ui.s(22);
    L.headFont = 17.0f;
    for (int c = 0; c < kCols; ++c) {
        const float room2 = L.cols[static_cast<size_t>(c)].w - (c == 0 ? 0.0f : iconW + ui.s(6)) - ui.s(18);
        L.headFont = std::min(L.headFont, ui.fitFont(p.headers[static_cast<size_t>(c)], FontFace::Bold, 17, std::max(ui.s(10), room2), 0.62f));
    }
    for (int c = 0; c < kCols; ++c) L.texts.push_back(L.cols[static_cast<size_t>(c)]);
    L.rowFont = std::clamp(rowH / ui.s(1.0f) * 0.36f, 13.0f, 22.0f);
    float ry = L.list.y;
    for (const Item& it : items) {
        if (it.row < 0) {
            L.sections.push_back({tx0, ry, rw, secH});
            L.sectionOf.push_back(it.section);
            ry += secH + gap;
        } else {
            L.rows.push_back({tx0, ry, rw, rowH});
            L.rowOf.push_back(it.row);
            ry += rowH + gap;
        }
    }
    return L;
}

// ------------------------------------------------------------------------------------- the tooltip

Tooltip tooltip(UIContext& ui, const Model& m, const View& v, const Prepared& p, int row, Col col, const Rect& anchor) {
    Tooltip tt;
    tt.col = col;
    if (!p.table || row < 0 || row >= static_cast<int>(p.table->rows.size())) return tt;
    const Row& r = p.table->rows[static_cast<size_t>(row)];
    const Cell& c = p.cells[static_cast<size_t>(row)][static_cast<size_t>(col)];
    const std::string head = p.headers[static_cast<size_t>(col)];
    tt.title = head + " : " + c.text;
    std::string why;
    switch (col) {
        case Col::Player: why = "Un joueur du groupe (son siège et le moment où il l'a pris). Votre ligne est encadrée."; break;
        case Col::Boss:
            why = p.bossShare ? "Part des dégâts infligés au boss de la zone (gigaboss, boss du bestiaire, points faibles), hors mini-boss : " +
                                    hearts(std::max<int64_t>(0, r.bossTenths - (r.v2 ? r.miniTenths : 0))) + " PV."
                              : "Aucun boss dans cette zone : part des dégâts infligés à tous ses ennemis (" + hearts(r.damageTenths) + " PV).";
            break;
        case Col::Mini:
            why = c.known ? "Part des dégâts infligés aux mini-boss de la zone (les ennemis dont la chute est un objectif) : " + hearts(r.miniTenths) + " PV."
                          : "Tableau d'avant la 2.78 : les mini-boss n'y étaient pas comptés à part.";
            break;
        case Col::Support: why = "Coups bloqués au bouclier et alliés relevés."; break;
        case Col::Mechanisms: why = "Plaques tenues quand elles ont fonctionné, évents fermés, points faibles brisés."; break;
        case Col::Objectives: why = "Objectifs de la branche accomplis : une action à soi, ou un combat gagné avec le groupe présent."; break;
        case Col::Deaths: why = c.known ? "Fois à terre dans la zone." : "Tableau d'avant la 2.78 : les morts n'y étaient pas comptées."; break;
        case Col::Revives:
            why = c.known ? "Alliés relevés en maintenant « Interagir » 2 s près de leur âme." : "Tableau d'avant la 2.78 : les réanimations n'y étaient pas comptées.";
            break;
        case Col::Revived: why = c.known ? "Fois relevé par un allié." : "Tableau d'avant la 2.78 : les réanimations n'y étaient pas comptées."; break;
        default: break;
    }
    // The best of the column (its crown).
    std::string best;
    if (col != Col::Player && crowned(col)) {
        int bi = -1;
        for (size_t i = 0; i < p.cells.size(); ++i)
            if (p.cells[i][static_cast<size_t>(col)].best && bi < 0) bi = static_cast<int>(i);
        if (bi >= 0) best = (v.tab == 1 ? "Le plus du raid : " : "Le plus de la zone : ") + p.cells[static_cast<size_t>(bi)][static_cast<size_t>(col)].text + " (couronne).";
    }
    const float w = std::min(ui.s(560), ui.screenW() - ui.s(24));
    const float inner = w - ui.s(40);
    tt.lines = wrap(ui, why, FontFace::Body, 16, inner, 3);
    if (!best.empty())
        for (const std::string& l : wrap(ui, best, FontFace::Body, 16, inner, 1)) tt.lines.push_back(l);
    if (!m.tooltipNote.empty())
        for (const std::string& l : wrap(ui, m.tooltipNote, FontFace::Body, 14, inner, 2)) tt.lines.push_back(l);
    const float h = ui.s(54) + static_cast<float>(tt.lines.size()) * ui.s(24) + ui.s(10);
    // Under the cell when there is room, else above it; always inside the screen.
    float bx = std::clamp(anchor.x, ui.s(12), ui.screenW() - w - ui.s(12));
    float by = anchor.bottom() + ui.s(4);
    if (by + h > ui.screenH() - ui.s(12)) by = anchor.y - ui.s(4) - h;
    by = std::clamp(by, ui.s(12), std::max(ui.s(12), ui.screenH() - h - ui.s(12)));
    tt.box = {bx, by, w, h};
    return tt;
}

// ------------------------------------------------------------------------------------- drawing

namespace {

/// A part's callout: its gigaboss's portrait (the heart's light), its name, its groups, its state, its relic.
void drawCallout(UIContext& ui, GameServices* g, const Rect& c, const Part& p, bool on, bool hover) {
    Renderer& rr = ui.r();
    rr.fillRoundedRect(c, ui.s(10), on ? kOn : hover ? kHover : kHeadFill);
    rr.drawRoundedRect(c, ui.s(10), on ? kMeEdge : hover ? kGold : kBorder, ui.s(2));
    const float pad = ui.s(8);
    const bool twoRows = c.h >= ui.s(84);
    const float ps = twoRows ? std::min(ui.s(46), c.h - ui.s(44)) : std::min(ui.s(40), c.h - pad * 2);
    const Rect por{c.x + pad, c.y + pad, ps, ps};
    rr.fillRoundedRect(por, ui.s(6), Color{10, 14, 32, 255});
    const float t = ui.time();
    if (p.enemy.empty()) {  // the heart: its light, beating
        const float beat = 0.5f + 0.5f * std::sin(t * 3.2f);
        rr.drawGlow(por.center(), por.w * (0.42f + 0.08f * beat), Color{255, 140, 60, static_cast<uint8_t>(120 + 80 * beat)});
        rr.fillCircle(por.center(), por.w * 0.16f, Color{255, 190, 120, 255});
        rr.drawRing(por.center(), por.w * 0.34f, std::max(1.0f, ui.s(2)), kOrange.withAlpha(180));
    } else if (g) {
        if (const EnemyDefinition* def = g->content.enemy(p.enemy)) {
            rr.pushClip(por);
            drawCreature(*g, *def, por.expanded(-ui.s(3)), t, p.fallen ? Color{120, 120, 140, 255} : Color::white(), "idle");
            rr.popClip();
        }
    }
    if (p.fallen) {  // a red line across the portrait: fallen
        rr.drawLine({por.x + ui.s(4), por.bottom() - ui.s(4)}, {por.right() - ui.s(4), por.y + ui.s(4)}, kFallen.withAlpha(230), ui.s(3));
    }
    const float tx = por.right() + ui.s(8), room = c.right() - pad - tx;
    const Color nameC = p.open ? kText : kMuted;
    if (twoRows) {
        fit(ui, p.name, {tx, por.y + ps * 0.3f}, 15, room, nameC, FontFace::Bold);
        fit(ui, groupsLine(p), {tx, por.y + ps * 0.78f}, 13, room, p.groups.empty() ? kMuted : kSky);
        // The state's pill, the relic after it.
        const float cy = c.bottom() - pad - ui.s(14);
        const std::string word = stateWord(p);
        const float relicS = ui.s(22);
        const float maxRight = c.right() - pad - (p.relic ? relicS + ui.s(6) : 0.0f);
        float pw = pill(ui, c.x + pad, cy, word, stateFill(p), kText, maxRight, 12);
        if (pw == 0.0f) pw = fit(ui, word, {c.x + pad, cy}, 12, maxRight - c.x - pad, kText, FontFace::Bold).x;
        if (p.relic) ui.icon(kRelic, static_cast<int>(t * 8.0f) % 8, 16, {c.x + pad + pw + ui.s(6), cy - relicS * 0.5f, relicS, relicS});
    } else {
        const float relicS = ui.s(18);
        const float nameRoom = room - (p.relic ? relicS + ui.s(4) : 0.0f);
        fit(ui, p.name, {tx, c.y + c.h * 0.32f}, 14, nameRoom, nameC, FontFace::Bold);
        fit(ui, std::string(stateWord(p)) + " · " + groupsLine(p), {tx, c.y + c.h * 0.72f}, 12, room, p.fallen ? Color{255, 140, 140, 255} : kSky);
        if (p.relic) ui.icon(kRelic, static_cast<int>(t * 8.0f) % 8, 16, {c.right() - pad - relicS, c.y + c.h * 0.32f - relicS * 0.5f, relicS, relicS});
    }
}

/// The titan's column: « Raid entier », the drawing (GameServices: the sprite by parts), the lines to the callouts, the callouts.
void drawTitan(UIContext& ui, GameServices* g, AsterionFigure* fig, const Layout& L, const Model& m, View& v) {
    if (L.titan.empty()) return;
    Renderer& rr = ui.r();
    {
        bool hov = false;
        const bool on = v.part < 0;
        if (ui.mouseClick("overview", L.overview, &hov)) v.choose(-1);
        rr.fillRoundedRect(L.overview, ui.s(10), on ? kOn : hov ? kHover : kHeadFill);
        rr.drawRoundedRect(L.overview, ui.s(10), on ? kMeEdge : hov ? kGold : kBorder, ui.s(2));
        fit(ui, "Raid entier : tous les groupes", L.overview.center(), 17, L.overview.w - ui.s(24), on ? kText : kMuted, FontFace::Bold, Align::Center);
    }
    int hoverCard = -1;
    for (size_t i = 0; i < m.parts.size() && i < L.callouts.size(); ++i) {
        bool h = false;
        if (ui.mouseClick("part" + std::to_string(i), L.callouts[i], &h) && m.parts[i].open) v.choose(static_cast<int>(i));
        if (h) hoverCard = static_cast<int>(i);
    }
    std::string figHover;
    const bool drawn = g && fig && L.titanScale > 0.0f && !asterionfilm::hold(*g);
    if (drawn) {
        std::map<std::string, asterion::State> states;
        std::vector<std::string> openable;
        for (const Part& p : m.parts) {
            states[p.id] = p.fallen ? asterion::State::Destroyed : p.state;
            if (p.open) openable.push_back(p.id);
        }
        fig->setOpenHint("Clic ou validation : ses contributions");
        fig->mark(v.part >= 0 ? m.parts[static_cast<size_t>(v.part)].id : std::string(), hoverCard >= 0 ? m.parts[static_cast<size_t>(hoverCard)].id : std::string());
        const std::string act = fig->draw(*g, "titan", L.figure, states, openable);
        for (size_t i = 0; i < m.parts.size(); ++i)
            if (!act.empty() && m.parts[i].id == act) v.choose(static_cast<int>(i));
        figHover = fig->shownPart();
        // Each callout joined to its part (from the callout's edge facing the titan).
        for (size_t i = 0; i < m.parts.size() && i < L.callouts.size(); ++i) {
            const Rect& c = L.callouts[i];
            const bool leftSide = c.center().x < L.figure.center().x;
            const Vec2 from{leftSide ? c.right() : c.x, c.center().y};
            const bool lit = static_cast<int>(i) == hoverCard || m.parts[i].id == figHover;
            const Color lc = static_cast<int>(i) == v.part ? kSky : lit ? kGold : Color{96, 108, 160, 170};
            rr.drawLine(from, L.anchors[i], lc, std::max(1.0f, ui.s(static_cast<int>(i) == v.part || lit ? 2.0f : 1.2f)));
            rr.fillCircle(L.anchors[i], ui.s(4), lc);
        }
    }
    for (size_t i = 0; i < m.parts.size() && i < L.callouts.size(); ++i)
        drawCallout(ui, g, L.callouts[i], m.parts[i], static_cast<int>(i) == v.part, static_cast<int>(i) == hoverCard || (!figHover.empty() && m.parts[i].id == figHover));
    drawLines(ui, L.captionLines, {L.caption.x, L.caption.y}, 14, ui.s(19), kMuted);
}

bool drawImpl(UIContext& ui, GameServices* g, AsterionFigure* fig, const Rect& r, const Model& m, View& v) {
    bool closed = false;
    ui.pushId("contribtable");
    ui.consumeNavigation(true, true);  // its keys sort and choose rows: the focus never travels to its buttons
    if (v.part >= static_cast<int>(m.parts.size())) v.choose(-1);
    const Layout L = layout(ui, r, m, v);
    const Table& t = tableOf(m, v);
    const Prepared p = prepare(t, v.sort);
    Renderer& rr = ui.r();
    if (!m.embedded) {
        rr.fillRect({0.0f, 0.0f, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 150});
        rr.fillRoundedRect(r, ui.s(18), kPanelFill);
        rr.drawRoundedRect(r, ui.s(18), kBorder, ui.s(2));
        fit(ui, m.heading, {L.heading.x, L.heading.center().y}, 32, L.heading.w, kGold, FontFace::Title);
        UIContext::ButtonStyle cs;
        cs.small = true;
        if (ui.button("close", L.close, "Fermer", cs)) closed = true;
    } else {
        rr.fillRect(r, kPanelFill);  // opaque inside its panel too
    }
    const std::string sub = t.title.empty() ? t.sub : t.sub.empty() ? t.title : t.title + " · " + t.sub;
    fit(ui, sub.empty() ? std::string("Aucune zone terminée pour l'instant") : sub, {L.sub.x, L.sub.center().y}, 20, L.sub.w, kMuted);
    drawTitan(ui, g, fig, L, m, v);
    // The tabs.
    for (int i = 0; i < 2; ++i) {
        const Rect& tr = L.tabs[static_cast<size_t>(i)];
        const bool on = v.tab == i;
        bool hover = false;
        if (ui.mouseClick("tab" + std::to_string(i), tr, &hover)) {
            v.tab = i;
            v.selected = -1;
        }
        rr.fillRoundedRect(tr, ui.s(10), on ? kOn : hover ? kHover : kHeadFill);
        rr.drawRoundedRect(tr, ui.s(10), on ? kMeEdge : kBorder, ui.s(2));
        fit(ui, tabName(m, v, i), tr.center(), 18, tr.w - ui.s(16), on ? kText : kMuted, FontFace::Bold, Align::Center);
    }
    if (!L.prevZone.empty()) {
        UIContext::ButtonStyle zs;
        zs.small = true;
        zs.fontSize = 14;
        const int n = static_cast<int>(zonesOf(m, v).size());
        if (ui.button("prevzone", L.prevZone, "Zone précédente", zs)) v.zone = (v.zone + n - 1) % n;
        if (ui.button("nextzone", L.nextZone, "Zone suivante", zs)) v.zone = (v.zone + 1) % n;
    }
    {
        const std::string s = "Tri : " + p.headers[static_cast<size_t>(v.sort)] + " (clic sur un en-tête, ou flèches gauche / droite, LB RB)";
        fit(ui, !m.parts.empty() ? "Tri : " + p.headers[static_cast<size_t>(v.sort)] + " (clic sur un en-tête, ou flèches gauche / droite)" : s,
            {L.sortLine.x, L.sortLine.center().y}, 16, L.sortLine.w, kMuted);
    }
    // The header: an icon and its name per column, ▼ on the sorted one; a click sorts by it.
    rr.fillRoundedRect(L.header, ui.s(10), kHeadFill);
    const float iconS = ui.s(22);
    for (int c = 0; c < kCols; ++c) {
        const Rect& cr = L.cols[static_cast<size_t>(c)];
        const Col col = static_cast<Col>(c);
        bool hover = false;
        if (ui.mouseClick("head" + std::to_string(c), cr, &hover)) v.sort = col;
        float lx = cr.x;
        if (iconOf(col) >= 0) {
            ui.icon(kIcons, iconOf(col), kIconCell, {lx, cr.center().y - iconS * 0.5f, iconS, iconS});
            lx += iconS + ui.s(6);
        }
        const bool on = v.sort == col;
        const std::string& head = p.headers[static_cast<size_t>(c)];
        const float room = cr.right() - lx - ui.s(18);
        const std::string shown = ui.elide(head, FontFace::Bold, L.headFont, room);
        ui.label(shown, {lx, cr.center().y}, L.headFont, on ? kText : hover ? kText : kMuted, Align::Left, FontFace::Bold, VAlign::Middle);
        if (on) triangleDown(ui, {lx + textW(ui, shown, L.headFont, FontFace::Bold) + ui.s(10), cr.center().y}, ui.s(5), kText);
    }
    // The rows (and the groups' headers), scrolled: the wheel, the bar, and the row chosen with the keys kept in view.
    const Vec2 mouse = ui.input().mousePos();
    const bool overList = L.list.contains(mouse);
    int hoverRow = -1;
    Col hoverCol = Col::Count;
    Rect hoverRect;
    const float f = L.rowFont;
    if (p.order.empty()) fit(ui, "Personne n'a encore rien compté ici.", {L.header.x + ui.s(14), L.header.bottom() + ui.s(30)}, 18, L.header.w, kMuted);
    if (v.selected >= static_cast<int>(L.rows.size())) v.selected = static_cast<int>(L.rows.size()) - 1;
    const std::string scrollId = "rows" + std::to_string(v.part) + "/" + std::to_string(v.tab) + "/" + std::to_string(v.zone);
    ui.beginScroll(scrollId, L.list, L.contentH);
    const float off = ui.scrollOffset();
    if (v.selected >= 0) {  // the row chosen with the keys: brought into view (the next image)
        const Rect& sr = L.rows[static_cast<size_t>(v.selected)];
        if (sr.y - off < L.list.y) ui.setScroll(scrollId, sr.y - L.list.y - ui.s(4));
        else if (sr.bottom() - off > L.list.bottom()) ui.setScroll(scrollId, sr.bottom() - L.list.bottom() + ui.s(4));
    }
    // The groups' totals (their share of the damage to the boss, of all the damage without one).
    int64_t allDamage = 0;
    std::vector<int64_t> secDamage(t.sections.size(), 0);
    std::vector<int> secPlayers(t.sections.size(), 0);
    for (const Row& row : t.rows) {
        const int64_t d = p.bossShare ? std::max<int64_t>(0, row.bossTenths - (row.v2 ? row.miniTenths : 0)) : std::max<int64_t>(0, row.damageTenths);
        allDamage += d;
        if (row.section >= 0 && row.section < static_cast<int>(secDamage.size())) {
            secDamage[static_cast<size_t>(row.section)] += d;
            ++secPlayers[static_cast<size_t>(row.section)];
        }
    }
    for (size_t k = 0; k < L.sections.size(); ++k) {
        const Rect sr{L.sections[k].x, L.sections[k].y - off, L.sections[k].w, L.sections[k].h};
        if (sr.bottom() < L.list.y || sr.y > L.list.bottom()) continue;
        const int s = L.sectionOf[k];
        rr.fillRoundedRect(sr, ui.s(8), kSection);
        rr.fillRect({sr.x, sr.y + ui.s(6), ui.s(4), sr.h - ui.s(12)}, kGold);
        // Its share measured first, at the right; the group's name in what is left.
        std::string share;
        if (secPlayers[static_cast<size_t>(s)] == 0) share = "rien de compté pour l'instant";
        else if (allDamage > 0)
            share = pct(static_cast<double>(secDamage[static_cast<size_t>(s)]) / static_cast<double>(allDamage)) +
                    (p.bossShare ? " des dégâts au boss" : " des dégâts");
        const float shareW = share.empty() ? 0.0f : std::min(sr.w * 0.4f, textW(ui, share, 15, FontFace::Bold));
        if (!share.empty()) fit(ui, share, {sr.right() - ui.s(14), sr.center().y}, 15, shareW + ui.s(2), kMuted, FontFace::Bold, Align::Right);
        fit(ui, t.sections[static_cast<size_t>(s)], {sr.x + ui.s(16), sr.center().y}, 16, sr.w - ui.s(46) - shareW, kGold, FontFace::Bold);
    }
    for (size_t k = 0; k < L.rows.size(); ++k) {
        const int ri = L.rowOf[k];
        const Row& row = t.rows[static_cast<size_t>(ri)];
        const Rect rect{L.rows[k].x, L.rows[k].y - off, L.rows[k].w, L.rows[k].h};
        if (rect.bottom() < L.list.y || rect.y > L.list.bottom()) continue;
        rr.fillRoundedRect(rect, ui.s(10), k % 2 == 0 ? kRowA : kRowB);
        if (row.me) rr.drawRoundedRect(rect, ui.s(10), kMeEdge, ui.s(2));
        if (static_cast<int>(k) == v.selected) rr.drawRoundedRect(rect, ui.s(10), kGold, ui.s(2));
        const float cy = rect.center().y;
        // Rank (in its group), colour, name.
        const Rect& pc = L.cols[0];
        int rank = 1;
        for (size_t j = 0; j < k; ++j) rank += t.rows[static_cast<size_t>(L.rowOf[j])].section == row.section ? 1 : 0;
        const float rankW = textW(ui, "88", f, FontFace::Bold);
        fit(ui, std::to_string(rank), {pc.x + rankW * 0.5f, cy}, f, rankW, kMuted, FontFace::Bold, Align::Center, true);
        const float dot = std::min(rect.h * 0.5f, ui.s(30));
        rr.fillCircle({pc.x + rankW + ui.s(8) + dot * 0.5f, cy}, dot * 0.5f, row.color);
        const float nx = pc.x + rankW + ui.s(16) + dot;
        fit(ui, row.me ? std::string("Vous") : row.name, {nx, cy}, f, pc.right() - nx, kText, FontFace::Bold, Align::Left, !row.me);
        for (int c = 1; c < kCols; ++c) {
            const Col col = static_cast<Col>(c);
            const Rect& cr = L.cols[static_cast<size_t>(c)];
            const Cell& cell = p.cells[static_cast<size_t>(ri)][static_cast<size_t>(c)];
            const Rect hit{cr.x, rect.y, cr.w, rect.h};
            if (overList && hit.contains(mouse)) {
                hoverRow = ri;
                hoverCol = col;
                hoverRect = hit;
            }
            const float crown = ui.s(22);
            if (cell.bar >= 0.0f) {
                // A bar proportional to the best, its share measured after it, the crown after the share.
                const float pctW = textW(ui, "100 %", f * 0.92f, FontFace::Bold);
                const float bw = std::max(ui.s(20), cr.w - pctW - crown - ui.s(18));
                const float bh = std::min(ui.s(24), rect.h * 0.4f);
                rr.fillRoundedRect({cr.x, cy - bh * 0.5f, bw, bh}, bh * 0.4f, kTrack);
                if (cell.bar > 0.0f) rr.fillRoundedRect({cr.x, cy - bh * 0.5f, std::max(bh, bw * cell.bar), bh}, bh * 0.4f, colColor(col));
                const Vec2 end = fit(ui, cell.text, {cr.x + bw + ui.s(8), cy}, f * 0.92f, pctW, kText, FontFace::Bold, Align::Left, true);
                if (cell.best) {
                    const float tw = end.x > 0.0f ? end.x : textW(ui, cell.text, f * 0.92f, FontFace::Bold);
                    ui.icon(kIcons, kCrownIcon, kIconCell, {cr.x + bw + ui.s(8) + tw + ui.s(6), cy - crown * 0.5f, crown, crown});
                }
            } else {
                const Color col2 = !cell.known || cell.value == 0.0 ? kMuted : colColor(col);
                const Vec2 end = fit(ui, cell.text, {cr.x + ui.s(4), cy}, f * 1.05f, cr.w - crown - ui.s(14), col2, FontFace::Bold, Align::Left, true);
                if (cell.best) {
                    const float tw = end.x > 0.0f ? end.x : textW(ui, cell.text, f * 1.05f, FontFace::Bold);
                    ui.icon(kIcons, kCrownIcon, kIconCell, {cr.x + ui.s(4) + tw + ui.s(8), cy - crown * 0.5f, crown, crown});
                }
            }
        }
    }
    ui.endScroll();
    // The best of the zone (or of the raid), each with its icon, one after the other.
    rr.fillRoundedRect(L.best, ui.s(12), kHeadFill);
    {
        const float bx = L.best.x + ui.s(22);
        float by = L.best.y + ui.s(20);
        fit(ui, v.tab == 1 ? "Meilleurs du raid" : "Meilleurs de la zone", {bx, by}, 18, L.best.w * 0.3f, kGold, FontFace::Bold);
        if (!L.legendLines.empty()) {
            const float lw = L.best.w * 0.62f;
            const auto lines = wrap(ui, m.legend, FontFace::Body, 13, lw, 2);
            for (size_t i = 0; i < lines.size(); ++i)
                fit(ui, lines[i], {L.best.right() - ui.s(22), by - ui.s(6) + static_cast<float>(i) * ui.s(16)}, 13, lw, kMuted, FontFace::Body, Align::Right);
        }
        by = L.best.y + L.best.h - ui.s(22);
        float ix = bx;
        const Col cats[4] = {Col::Boss, Col::Support, Col::Revives, Col::Mechanisms};
        for (Col col : cats) {
            int bi = -1;
            for (size_t i = 0; i < p.cells.size() && bi < 0; ++i)
                if (p.cells[i][static_cast<size_t>(col)].best) bi = static_cast<int>(i);
            if (bi < 0) continue;
            const Row& row = t.rows[static_cast<size_t>(bi)];
            const std::string who = row.me ? i18n::tr("Vous") : row.name;
            const std::string text = i18n::keep(i18n::tr(p.headers[static_cast<size_t>(col)]) + (i18n::language() == Language::English ? ": " : " : ") + who + " (" +
                                                p.cells[static_cast<size_t>(bi)][static_cast<size_t>(col)].text + ")");
            const float w = ui.s(26) + textW(ui, text, 16, FontFace::Body, true);
            if (ix + w > L.best.right() - ui.s(16)) break;  // what does not fit waits (never over the edge)
            ui.icon(kIcons, iconOf(col), kIconCell, {ix, by - ui.s(10), ui.s(20), ui.s(20)});
            {
                i18n::Verbatim keep(true);
                ui.label(text, {ix + ui.s(26), by}, 16, kText, Align::Left, FontFace::Body, VAlign::Middle);
            }
            ix += w + ui.s(36);
        }
    }
    drawLines(ui, L.keyLines, {L.keys.x + (L.keys.w - (L.keyLines.empty() ? 0.0f : textW(ui, L.keyLines[0], 15))) * 0.5f, L.keys.y}, 15, ui.s(20), kMuted);
    // The tooltip: the cell under the pointer, else the row chosen with the keys on the sorted column.
    int tipRow = hoverRow;
    Col tipCol = hoverCol;
    Rect tipAt = hoverRect;
    if (tipRow < 0 && v.selected >= 0 && v.selected < static_cast<int>(L.rows.size())) {
        const Rect sr{L.rows[static_cast<size_t>(v.selected)].x, L.rows[static_cast<size_t>(v.selected)].y - off, L.rows[static_cast<size_t>(v.selected)].w,
                      L.rows[static_cast<size_t>(v.selected)].h};
        if (sr.y >= L.list.y - 0.5f && sr.bottom() <= L.list.bottom() + 0.5f) {
            tipRow = L.rowOf[static_cast<size_t>(v.selected)];
            tipCol = v.sort == Col::Player ? Col::Boss : v.sort;
            const Rect& cr = L.cols[static_cast<size_t>(tipCol)];
            tipAt = {cr.x, sr.y, cr.w, sr.h};
        }
    }
    if (tipRow >= 0 && tipCol != Col::Count) {
        const Tooltip tt = tooltip(ui, m, v, p, tipRow, tipCol, tipAt);
        rr.fillRoundedRect(tt.box, ui.s(12), Color{12, 18, 40, 255});
        rr.drawRoundedRect(tt.box, ui.s(12), colColor(tipCol), ui.s(2));
        float ty = tt.box.y + ui.s(28);
        float tx = tt.box.x + ui.s(18);
        if (iconOf(tipCol) >= 0) {
            ui.icon(kIcons, iconOf(tipCol), kIconCell, {tx, ty - ui.s(12), ui.s(24), ui.s(24)});
            tx += ui.s(32);
        }
        fit(ui, tt.title, {tx, ty}, 21, tt.box.right() - tx - ui.s(16), kText, FontFace::Bold, Align::Left, true);
        ty += ui.s(26);
        for (const std::string& l : tt.lines) {
            i18n::Verbatim done(true);  // wrap() translated them already
            ui.label(l, {tt.box.x + ui.s(20), ty}, 16, kMuted, Align::Left, FontFace::Body, VAlign::Top);
            ty += ui.s(24);
        }
    } else if (fig && g) {
        fig->drawTooltip(*g);  // the titan's part hovered (its name, its gigaboss, its state), over everything
    }
    ui.popId();
    return closed;
}

}  // namespace

bool draw(UIContext& ui, const Rect& r, const Model& m, View& v) { return drawImpl(ui, nullptr, nullptr, r, m, v); }

bool draw(GameServices& g, AsterionFigure& fig, const Rect& r, const Model& m, View& v) { return drawImpl(g.ui, &g, &fig, r, m, v); }

namespace {

Row rowOf(const raids::ZoneResultRow& r) {
    Row row;
    row.name = r.username.empty() ? std::string("Joueur parti") : r.username;
    row.color = r.me ? Color{110, 190, 255, 255} : Color{200, 206, 230, 255};
    row.me = r.me;
    row.bossTenths = static_cast<int64_t>(r.bossDamage) * 10;
    row.miniTenths = static_cast<int64_t>(r.miniDamage) * 10;
    row.damageTenths = static_cast<int64_t>(r.damage) * 10;
    row.support = r.support;
    row.mechanisms = r.mechanisms;
    row.objectives = r.objectives;
    row.deaths = r.deaths;
    row.revives = r.revives;
    row.revived = r.revived;
    row.v2 = r.v2;
    return row;
}

}  // namespace

Model modelOfResults(const raids::Results& res, const std::string& raidSub) {
    Model m;
    m.legend = "Chiffres comptés par la simulation de chaque groupe, validés par le serveur (la majorité des joueurs du groupe).";
    m.tooltipNote = "Compté par la simulation du groupe, validé par le serveur.";
    m.tabKey = "Q / E";
    for (const raids::ZoneResultTable& z : res.zoneTables) {
        Table t;
        std::string zoneName = "Zone " + std::to_string(z.zone), branchName;
        for (const raid::Branch* b : raid::Branch::all())
            if (b->id == z.branch) {
                branchName = b->name;
                if (const raid::ZoneInfo* zi = b->zone(z.zone)) zoneName = zi->name;
            }
        t.title = zoneName + (branchName.empty() ? "" : " · " + branchName);
        t.sub = "Groupe " + z.group + " · " + raidmenu::raidTime(static_cast<float>(z.tick) / 120.0f);
        for (const raids::ZoneResultRow& r : z.rows) t.rows.push_back(rowOf(r));
        m.zones.push_back(std::move(t));
    }
    m.raid.title = "Raid entier";
    m.raid.sub = raidSub;
    for (const raids::ZoneResultRow& r : res.raidTotals) m.raid.rows.push_back(rowOf(r));
    return m;
}

}  // namespace cosmic::contrib
