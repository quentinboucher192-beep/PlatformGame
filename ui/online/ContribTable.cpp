#include "ui/online/ContribTable.h"

#include <algorithm>
#include <cmath>

#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::contrib {

using namespace raidui;

namespace {

constexpr const char* kIcons = "ui/raids/contrib.png";  // tools/assetgen/environment/raid_contrib_icons.py
constexpr int kIconCell = 32;
// The icons' order in the sheet (ui/raids/contrib.json): boss, mini, support, mechanisms, objectives,
// deaths, revives, revived, crown, arrow.
constexpr int kCrownIcon = 8;
const Color kPanelFill{18, 26, 54, 248};
const Color kHeadFill{24, 34, 68, 255};
const Color kRowA{22, 32, 64, 255};
const Color kRowB{19, 28, 58, 255};
const Color kMeEdge{110, 190, 255, 255};
const Color kTrack{34, 44, 82, 255};
const Color kMuted{140, 152, 186, 255};

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

const Table& tableOf(const Model& m, const View& v) {
    static const Table none;
    if (v.tab == 1) return m.raid;
    if (m.zones.empty()) return none;
    return m.zones[static_cast<size_t>(std::clamp(v.zone, 0, static_cast<int>(m.zones.size()) - 1))];
}

void triangleDown(UIContext& ui, Vec2 c, float s, Color col) { ui.r().fillTriangle({c.x - s, c.y - s * 0.55f}, {c.x + s, c.y - s * 0.55f}, {c.x, c.y + s * 0.65f}, col); }

std::string keysLine(const Model& m, bool zones) {
    const std::string tab = m.tabKey.empty() ? std::string("LB RB") : m.tabKey + " / LB RB";
    std::string s = tab + " : onglets · flèches gauche / droite : trier · survol ou flèches haut / bas : détails";
    if (zones) s += " · Entrée / A : zone suivante";
    return s + " · Échap / B : fermer";
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

// ------------------------------------------------------------------------------------- the numbers

Prepared prepare(const Table& t, Col sort) {
    Prepared p;
    p.table = &t;
    const size_t n = t.rows.size();
    int64_t bossAll = 0, miniAll = 0, damageAll = 0;
    bool anyV2 = false;
    for (const Row& r : t.rows) {
        bossAll += std::max<int64_t>(0, r.bossTenths - (r.v2 ? r.miniTenths : 0));
        miniAll += r.v2 ? std::max<int64_t>(0, r.miniTenths) : 0;
        damageAll += std::max<int64_t>(0, r.damageTenths);
        anyV2 = anyV2 || r.v2;
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
    (void)anyV2;
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

bool View::handleKeys(const InputManager& in, int zones) {
    if (in.menuPressed(Action::MenuBack)) return true;
    if (in.menuPressed(Action::MenuTabPrev) || in.menuPressed(Action::MenuTabNext) || in.menuPressed(Action::RaidMenu)) {
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
    const float pad = ui.s(28), x = r.x + pad, iw = r.w - pad * 2;
    float y = r.y + ui.s(18);
    // The heading and « Fermer » (measured first, at the right).
    const float closeW = std::min(iw * 0.3f, textW(ui, "Fermer", 16, FontFace::Bold) + ui.s(36));
    L.close = {x + iw - closeW, y, closeW, ui.s(40)};
    L.heading = {x, y, std::max(0.0f, iw - closeW - ui.s(16)), ui.s(40)};
    L.texts.push_back(L.heading);
    y += ui.s(46);
    L.sub = {x, y, iw, ui.s(26)};
    L.texts.push_back(L.sub);
    y += ui.s(34);
    // The tabs, each as wide as its name, then (the « Zone » tab with several zones) the zone buttons, then the sort.
    float tx = x;
    const char* names[2] = {"Zone", "Raid entier"};
    for (int i = 0; i < 2; ++i) {
        const float w = textW(ui, names[i], 18, FontFace::Bold) + ui.s(40);
        L.tabs[static_cast<size_t>(i)] = {tx, y, w, ui.s(42)};
        tx += w + ui.s(10);
    }
    if (v.tab == 0 && m.zones.size() > 1) {
        const float pw = textW(ui, "Zone précédente", 14, FontFace::Bold) + ui.s(26), nw = textW(ui, "Zone suivante", 14, FontFace::Bold) + ui.s(26);
        L.prevZone = {tx + ui.s(8), y + ui.s(4), pw, ui.s(34)};
        L.nextZone = {L.prevZone.right() + ui.s(8), y + ui.s(4), nw, ui.s(34)};
        tx = L.nextZone.right() + ui.s(10);
    }
    L.sortLine = {tx + ui.s(10), y, std::max(0.0f, x + iw - tx - ui.s(10)), ui.s(42)};
    L.texts.push_back(L.sortLine);
    y += ui.s(54);
    // The bottom, from the bottom up: the keys, the legend, the best of the zone.
    L.keyLines = wrap(ui, keysLine(m, v.tab == 0 && m.zones.size() > 1), FontFace::Body, 15, iw, 2);
    const float keysH = static_cast<float>(L.keyLines.size()) * ui.s(20);
    L.keys = {x, r.bottom() - ui.s(14) - keysH, iw, keysH};
    L.texts.push_back(L.keys);
    L.legendLines = wrap(ui, m.legend, FontFace::Body, 14, iw - ui.s(40), 2);
    const float bestH = ui.s(76);
    L.best = {x, L.keys.y - ui.s(10) - bestH, iw, bestH};
    // The header and the rows between.
    const float headH = ui.s(48);
    L.header = {x, y, iw, headH};
    float total = 0.0f;
    for (float w : kWeight) total += w;
    const float gap = ui.s(10), room = iw - ui.s(20) - gap * static_cast<float>(kCols - 1);
    float cx = x + ui.s(14);
    for (int c = 0; c < kCols; ++c) {
        const float w = room * kWeight[c] / total;
        L.cols[static_cast<size_t>(c)] = {cx, y, w, headH};
        cx += w + gap;
    }
    // The headers' font: the same for all, as large as the narrowest lets (an icon before each name, ▼ after the sorted one).
    const float iconW = ui.s(22);
    L.headFont = 17.0f;
    for (int c = 0; c < kCols; ++c) {
        const float room2 = L.cols[static_cast<size_t>(c)].w - (c == 0 ? 0.0f : iconW + ui.s(6)) - ui.s(18);
        L.headFont = std::min(L.headFont, ui.fitFont(p.headers[static_cast<size_t>(c)], FontFace::Bold, 17, std::max(ui.s(10), room2), 0.62f));
    }
    for (int c = 0; c < kCols; ++c) L.texts.push_back(L.cols[static_cast<size_t>(c)]);
    y += headH + ui.s(10);
    const float bottom = L.best.y - ui.s(12);
    const size_t n = p.order.size();
    float rowH = n > 0 ? (bottom - y) / static_cast<float>(n) - ui.s(6) : ui.s(40);
    rowH = std::min(rowH, ui.s(66));
    size_t shown = n;
    if (rowH < ui.s(30)) {
        rowH = ui.s(30);
        shown = static_cast<size_t>(std::max(0.0f, (bottom - y - ui.s(26)) / (rowH + ui.s(6))));
    }
    L.rowFont = std::clamp(rowH / ui.s(1.0f) * 0.36f, 13.0f, 22.0f);
    for (size_t i = 0; i < shown && i < n; ++i) {
        L.rows.push_back({x, y, iw, rowH});
        y += rowH + ui.s(6);
    }
    L.hidden = static_cast<int>(n - std::min(shown, n));
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

bool draw(UIContext& ui, const Rect& r, const Model& m, View& v) {
    bool closed = false;
    ui.pushId("contribtable");
    ui.consumeNavigation(true, true);  // its keys sort and choose rows: the focus never travels to its buttons
    const Layout L = layout(ui, r, m, v);
    const Table& t = tableOf(m, v);
    const Prepared p = prepare(t, v.sort);
    Renderer& rr = ui.r();
    rr.fillRect({0.0f, 0.0f, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 150});
    rr.fillRoundedRect(r, ui.s(18), kPanelFill);
    rr.drawRoundedRect(r, ui.s(18), kBorder, ui.s(2));
    fit(ui, m.heading, {L.heading.x, L.heading.center().y}, 32, L.heading.w, kGold, FontFace::Title);
    UIContext::ButtonStyle cs;
    cs.small = true;
    if (ui.button("close", L.close, "Fermer", cs)) closed = true;
    const std::string sub = t.title.empty() ? t.sub : t.sub.empty() ? t.title : t.title + " · " + t.sub;
    fit(ui, sub.empty() ? std::string("Aucune zone terminée pour l'instant") : sub, {L.sub.x, L.sub.center().y}, 20, L.sub.w, kMuted);
    // The tabs.
    const char* names[2] = {"Zone", "Raid entier"};
    for (int i = 0; i < 2; ++i) {
        const Rect& tr = L.tabs[static_cast<size_t>(i)];
        const bool on = v.tab == i;
        bool hover = false;
        if (ui.mouseClick("tab" + std::to_string(i), tr, &hover)) {
            v.tab = i;
            v.selected = -1;
        }
        rr.fillRoundedRect(tr, ui.s(10), on ? Color{40, 80, 120, 255} : hover ? Color{30, 44, 80, 255} : kHeadFill);
        rr.drawRoundedRect(tr, ui.s(10), on ? kMeEdge : kBorder, ui.s(2));
        fit(ui, names[i], tr.center(), 18, tr.w - ui.s(16), on ? kText : kMuted, FontFace::Bold, Align::Center);
    }
    if (!L.prevZone.empty()) {
        UIContext::ButtonStyle zs;
        zs.small = true;
        zs.fontSize = 14;
        const int n = static_cast<int>(m.zones.size());
        if (ui.button("prevzone", L.prevZone, "Zone précédente", zs)) v.zone = (v.zone + n - 1) % n;
        if (ui.button("nextzone", L.nextZone, "Zone suivante", zs)) v.zone = (v.zone + 1) % n;
    }
    {
        const std::string s = "Tri : " + p.headers[static_cast<size_t>(v.sort)] + " (clic sur un en-tête, ou flèches gauche / droite, LB RB)";
        fit(ui, s, {L.sortLine.x, L.sortLine.center().y}, 16, L.sortLine.w, kMuted);
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
    // The rows.
    const Vec2 mouse = ui.input().mousePos();
    int hoverRow = -1;
    Col hoverCol = Col::Count;
    Rect hoverRect;
    const float f = L.rowFont;
    if (p.order.empty()) fit(ui, "Personne n'a encore rien compté ici.", {L.header.x + ui.s(14), L.header.bottom() + ui.s(30)}, 18, L.header.w, kMuted);
    if (v.selected >= static_cast<int>(L.rows.size())) v.selected = static_cast<int>(L.rows.size()) - 1;
    for (size_t k = 0; k < L.rows.size(); ++k) {
        const int ri = p.order[k];
        const Row& row = t.rows[static_cast<size_t>(ri)];
        const Rect& rect = L.rows[k];
        rr.fillRoundedRect(rect, ui.s(10), k % 2 == 0 ? kRowA : kRowB);
        if (row.me) rr.drawRoundedRect(rect, ui.s(10), kMeEdge, ui.s(2));
        const float cy = rect.center().y;
        // Rank, colour, name.
        const Rect& pc = L.cols[0];
        const std::string rank = std::to_string(k + 1);
        const float rankW = textW(ui, "88", f, FontFace::Bold);
        fit(ui, rank, {pc.x + rankW * 0.5f, cy}, f, rankW, kMuted, FontFace::Bold, Align::Center, true);
        const float dot = std::min(rect.h * 0.5f, ui.s(30));
        rr.fillCircle({pc.x + rankW + ui.s(8) + dot * 0.5f, cy}, dot * 0.5f, row.color);
        const float nx = pc.x + rankW + ui.s(16) + dot;
        fit(ui, row.me ? std::string("Vous") : row.name, {nx, cy}, f, pc.right() - nx, kText, FontFace::Bold, Align::Left, !row.me);
        for (int c = 1; c < kCols; ++c) {
            const Col col = static_cast<Col>(c);
            const Rect& cr = L.cols[static_cast<size_t>(c)];
            const Cell& cell = p.cells[static_cast<size_t>(ri)][static_cast<size_t>(c)];
            const Rect hit{cr.x, rect.y, cr.w, rect.h};
            if (hit.contains(mouse)) {
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
    if (L.hidden > 0 && !L.rows.empty())
        fit(ui, "… et " + std::to_string(L.hidden) + (L.hidden > 1 ? " joueurs de plus" : " joueur de plus"), {L.header.x + ui.s(14), L.rows.back().bottom() + ui.s(14)}, 15,
            L.header.w, kMuted);
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
        tipRow = p.order[static_cast<size_t>(v.selected)];
        tipCol = v.sort == Col::Player ? Col::Boss : v.sort;
        const Rect& cr = L.cols[static_cast<size_t>(tipCol)];
        tipAt = {cr.x, L.rows[static_cast<size_t>(v.selected)].y, cr.w, L.rows[static_cast<size_t>(v.selected)].h};
        rr.drawRoundedRect(L.rows[static_cast<size_t>(v.selected)], ui.s(10), kGold, ui.s(2));
    }
    if (tipRow >= 0 && tipCol != Col::Count) {
        const Tooltip tt = tooltip(ui, m, v, p, tipRow, tipCol, tipAt);
        rr.fillRoundedRect(tt.box, ui.s(12), Color{12, 18, 40, 250});
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
    }
    ui.popId();
    return closed;
}

}  // namespace cosmic::contrib
