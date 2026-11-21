#include "ui/online/RaidHud.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/core/Application.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/entities/Entities.h"
#include "game/powers/PowerDefinition.h"
#include "game/levels/LevelTimer.h"
#include "saves/Settings.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::raidhud {

using namespace raidui;

Pill ambushPill(UIContext& ui, const Rect& over, int left, int total, float screenW) {
    Pill p;
    const float pad = ui.s(16), margin = ui.s(8);
    // Its whole text (wider than what it is over: it says why), smaller only when the screen is too narrow.
    p.text = "Embuscades restantes : " + std::to_string(left) + " sur " + std::to_string(total);
    const float room = std::max(ui.s(40), screenW - margin * 2.0f);
    p.font = ui.fitFont(p.text, FontFace::Bold, 13.0f, room - pad, 0.6f);
    const float w = std::min(room, textW(ui, p.text, p.font, FontFace::Bold) + pad);
    const float x = std::clamp(over.center().x - w * 0.5f, margin, std::max(margin, screenW - margin - w));
    p.box = {x, over.y - ui.s(30), w, ui.s(24)};
    return p;
}

Pill coopPill(UIContext& ui, const Rect& plate, int on, int need, int ambushLeft, int ambushTotal, float screenW) {
    // 2.81.2: an ambush before it still to beat: titan 12, how many of its own (« Embuscades restantes : 1 sur 2 »).
    if (ambushLeft > 0) return ambushPill(ui, plate, ambushLeft, std::max(ambushLeft, ambushTotal), screenW);
    Pill p;
    const float pad = ui.s(16);
    p.text = std::to_string(std::min(on, need)) + " / " + std::to_string(need);
    const float w = std::min(textW(ui, p.text, p.font, FontFace::Bold) + pad, std::max(ui.s(40), plate.w));
    p.box = {plate.center().x - w * 0.5f, plate.y - ui.s(30), w, ui.s(24)};
    return p;
}

namespace {

constexpr float kTexel = 3.0f;  // world pixels per pixel of the baked terrain
const Color kHudFill{11, 10, 20, 214};
const Color kDanger{255, 74, 74, 255};

/// The cells of ui/raids/minimap.json (read once).
struct Atlas {
    bool loaded = false;
    std::map<std::string, Rect> cells;
    float tile = 16.0f;
    const Rect* cell(const std::string& n) const {
        auto it = cells.find(n);
        return it == cells.end() ? nullptr : &it->second;
    }
};
Atlas& atlas(ResourceManager& res) {
    static Atlas a;
    if (a.loaded) return a;
    a.loaded = true;
    if (auto j = res.json("ui/raids/minimap.json"); j && j->is_object()) {
        a.tile = jget<float>(*j, "tile", 16.0f);
        if (j->contains("cells") && (*j)["cells"].is_object())
            for (const auto& [name, v] : (*j)["cells"].items())
                if (v.is_array() && v.size() == 4)
                    a.cells[name] = Rect{v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>()};
    }
    return a;
}

Rect clampToScreen(UIContext& ui, Rect r) {
    r.x = std::clamp(r.x, 0.0f, std::max(0.0f, ui.screenW() - r.w));
    r.y = std::clamp(r.y, 0.0f, std::max(0.0f, ui.screenH() - r.h));
    return r;
}

/// The default area of an element of the raid's own HUD at scale k (Vitals, Powers, Boss: `hud`).
Rect defaultArea(UIContext& ui, const RaidHudLayout& l, RaidHudElement e, const HudAreas& hud) {
    const float W = ui.screenW(), H = ui.screenH(), m = ui.s(16);
    const float k = scaleOf(l, e);
    auto objectives = [&] {
        const float ko = scaleOf(l, RaidHudElement::Objectives);
        const float w = std::min(ui.s(320), W * 0.24f) * ko;
        return Rect{W - m - w, ui.s(14), w, ui.s(236) * ko};
    };
    auto group = [&] {
        const float kg = scaleOf(l, RaidHudElement::Group);
        const float w = std::min(ui.s(270), W * 0.2f) * kg;
        float y = ui.s(14);
        // Under this player's vitals and powers when they are in its column (the HUD's places).
        for (const Rect& a : {hud.vitals, hud.powers})
            if (a.w > 0.0f && a.x < m + w && a.y < H * 0.5f) y = std::max(y, a.bottom() + ui.s(12));
        return Rect{m, y, w, ui.s(242) * kg};
    };
    switch (e) {
        case RaidHudElement::Vitals: return hud.vitals;
        case RaidHudElement::Powers: return hud.powers;
        case RaidHudElement::Boss: return hud.boss;
        case RaidHudElement::Objectives: return objectives();
        case RaidHudElement::Alerts: {
            const Rect o = objectives();
            const float w = std::min(ui.s(320), W * 0.24f) * k;
            return Rect{W - m - w, o.bottom() + ui.s(10), w, ui.s(150) * k};
        }
        case RaidHudElement::Group: return group();
        case RaidHudElement::Minimap: {
            const float h = ui.s(122) * k;
            const Rect band{0.0f, ui.s(14), W, h};
            float left = m;
            for (const Rect& a : {hud.vitals, hud.powers, group()})
                if (a.w > 0.0f && a.intersects(band) && a.x < W * 0.5f) left = std::max(left, a.right() + ui.s(14));
            const Rect o = objectives();
            const float right = o.x - ui.s(14);
            const float w = std::min(ui.s(680) * k, right - left);
            if (w >= ui.s(220)) return Rect{left + (right - left - w) * 0.5f, ui.s(14), w, h};
            const Rect g = group();  // no room at the top: under the group panel
            return Rect{m, g.bottom() + ui.s(10), std::min(ui.s(420) * k, W * 0.3f), h};
        }
        case RaidHudElement::Danger: {
            const float w = std::min(W * 0.6f, ui.s(760) * k), h = ui.s(44) * k;
            return Rect{(W - w) * 0.5f, H * 0.62f, w, h};
        }
        case RaidHudElement::Contributions: {
            const float w = std::min(W * 0.42f, ui.s(600) * k), h = ui.s(34) * k;
            return Rect{W - m - w, H - m - h, w, h};
        }
        case RaidHudElement::Status: {
            const float w = std::min(W * 0.42f, ui.s(560) * k), h = ui.s(34) * k;
            return Rect{m, H - m - h, w, h};
        }
        case RaidHudElement::Timer: {
            // Under the group panel (and under the minimap when it had no room at the top).
            const float w = std::min(ui.s(270), W * 0.2f) * k;
            float y = group().bottom() + ui.s(10);
            const Rect mm = defaultArea(ui, l, RaidHudElement::Minimap, hud);
            if (mm.x <= m + 0.5f && mm.y > ui.s(20)) y = std::max(y, mm.bottom() + ui.s(10));
            return Rect{m, y, w, ui.s(86) * k};
        }
        case RaidHudElement::Heart: {
            // 2.74: under the minimap when it is at the top; else in the left column, under the chrono.
            const Rect mm = defaultArea(ui, l, RaidHudElement::Minimap, hud);
            const float h = ui.s(58) * k;
            if (mm.w > 0.0f && mm.y < ui.s(20)) {
                const float w = std::min(mm.w, ui.s(640) * k);
                return Rect{mm.x + (mm.w - w) * 0.5f, mm.bottom() + ui.s(8), w, h};
            }
            const Rect t = defaultArea(ui, l, RaidHudElement::Timer, hud);
            return Rect{m, t.bottom() + ui.s(10), std::min(ui.s(420) * k, W * 0.3f), h};
        }
        case RaidHudElement::HeartEffects: {
            // 2.74: in the left column under the chrono (and the heart's bar when it is there too),
            // above the network line; never as wide as the gigaboss's bar's place (bottom centre).
            float y = defaultArea(ui, l, RaidHudElement::Timer, hud).bottom() + ui.s(10);
            const Rect hb = defaultArea(ui, l, RaidHudElement::Heart, hud);
            if (hb.x <= m + 0.5f) y = std::max(y, hb.bottom() + ui.s(10));
            const float bottom = defaultArea(ui, l, RaidHudElement::Status, hud).y - ui.s(10);
            // Never under the danger band (centred), on the small screens too.
            const float w =
                std::max(ui.s(120), std::min({ui.s(500) * k, W * 0.28f, defaultArea(ui, l, RaidHudElement::Danger, hud).x - ui.s(10) - m}));
            return Rect{m, y, w, std::max(ui.s(80), std::min(ui.s(320) * k, bottom - y))};
        }
        case RaidHudElement::LairPassage: {
            // 2.78: the right column, under the alerts (« Ce qu'il reste à faire » goes under it).
            const Rect a = defaultArea(ui, l, RaidHudElement::Alerts, hud);
            const float w = std::min(ui.s(320), W * 0.24f) * k;
            return Rect{W - m - w, a.bottom() + ui.s(10), w, ui.s(56) * k};
        }
        case RaidHudElement::Remaining: {
            // 2.75: the right column under the alerts, never down to the danger band (centred) nor to the
            // contributions' line (bottom right). 2.78: under the distance to the lair's passage.
            const Rect a = defaultArea(ui, l, RaidHudElement::LairPassage, hud);
            const float w = std::min(ui.s(320), W * 0.24f) * k;
            const float x = W - m - w;
            float y = a.bottom() + ui.s(10);
            float bottom = defaultArea(ui, l, RaidHudElement::Contributions, hud).y - ui.s(10);
            const Rect d = defaultArea(ui, l, RaidHudElement::Danger, hud);
            if (d.right() > x && d.x < x + w && d.bottom() > y) {
                if (d.y - ui.s(10) - y >= ui.s(120)) bottom = std::min(bottom, d.y - ui.s(10));
                else y = d.bottom() + ui.s(10);  // a short screen: under the band
            }
            return Rect{x, y, w, std::max(ui.s(40), std::min(ui.s(300) * k, bottom - y))};
        }
        case RaidHudElement::Notices: {
            // 2.75.5: between the two columns, under the minimap and the heart's bar when they are at the
            // top, above the danger band: away from the middle of the screen, where the players are.
            const Rect g = group(), o = objectives();
            float left = g.right() + ui.s(14);
            const float right = o.x - ui.s(14);
            for (const Rect& a : {hud.vitals, hud.powers})
                if (a.w > 0.0f && a.x < W * 0.5f && a.y < H * 0.4f) left = std::max(left, a.right() + ui.s(14));
            const float w = std::max(ui.s(160), std::min(ui.s(520) * k, right - left));
            const float x = std::clamp(left + (right - left - w) * 0.5f, m, std::max(m, W - m - w));
            float y = ui.s(14);
            for (const RaidHudElement above : {RaidHudElement::Minimap, RaidHudElement::Heart}) {
                const Rect a = defaultArea(ui, l, above, hud);
                if (a.w > 0.0f && a.y < H * 0.3f && a.x < x + w && a.right() > x) y = std::max(y, a.bottom() + ui.s(10));
            }
            const float bottom = defaultArea(ui, l, RaidHudElement::Danger, hud).y - ui.s(10);
            return Rect{x, y, w, std::max(ui.s(60), std::min(ui.s(200) * k, bottom - y))};
        }
        default: return {};
    }
}

}  // namespace

float scaleOf(const RaidHudLayout& l, RaidHudElement e) { return std::clamp(l[e].scale, HudLayout::kMinScale, HudLayout::kMaxScale); }

Rect area(UIContext& ui, const RaidHudLayout& l, RaidHudElement e, const HudAreas& hud) {
    Rect r = defaultArea(ui, l, e, hud);
    if (RaidHudLayout::hudElement(e) != HudElement::Count) return r;  // the game's HUD places it
    const HudPlace& p = l[e];
    if (p.custom) {
        r.x = p.x * ui.screenW();
        r.y = p.y * ui.screenH();
    }
    return clampToScreen(ui, r);
}

GameplaySettings hudSettings(const GameplaySettings& base) {
    GameplaySettings s = base;
    s.focusMode = false;  // the raids have their own layout
    for (RaidHudElement e : {RaidHudElement::Vitals, RaidHudElement::Powers, RaidHudElement::Boss}) {
        HudPlace p = base.raidHud[e];
        p.focus = true;
        s.hud[RaidHudLayout::hudElement(e)] = p;
    }
    return s;
}

LairPassageLayout layoutLairPassage(UIContext& ui, const Rect& r, float k) {
    LairPassageLayout L;
    const float pad = ui.s(8) * k, a = std::min(r.h - pad * 2, ui.s(30) * k);
    L.arrow = {r.x + pad, r.center().y - a * 0.5f, a, a};
    const float tx = L.arrow.right() + ui.s(8) * k, tw = std::max(0.0f, r.right() - pad - tx);
    const float half = (r.h - pad * 2) * 0.5f;
    L.title = {tx, r.y + pad, tw, half};
    L.state = {tx, r.y + pad + half, tw, half};
    return L;
}

void drawLairPassage(UIContext& ui, const Rect& r, float k, const LairPassage& p) {
    hudBox(ui, r);
    const LairPassageLayout L = layoutLairPassage(ui, r, k);
    // The arrow (ui/raids/contrib.png, its last icon), turned toward where to go, in the state's colour.
    if (auto tex = ui.app().resources().texture("ui/raids/contrib.png")) {
        const Rect src{9.0f * 32.0f, 0.0f, 32.0f, 32.0f};
        const float deg = std::atan2(p.dir.y, p.dir.x) * 57.29578f;
        ui.r().drawTexture(*tex, &src, L.arrow, p.open ? kGreen : kGold, BlendMode::Alpha, false, false, deg);
    }
    fit(ui, p.title, {L.title.x, L.title.center().y}, 14 * k, L.title.w, kText, FontFace::Bold);
    fit(ui, p.state, {L.state.x, L.state.center().y}, 13 * k, L.state.w, p.open ? kGreen : kOrange);
}

void hudBox(UIContext& ui, const Rect& r) {
    ui.r().fillRoundedRect(r, ui.s(12), kHudFill);
    ui.r().drawRoundedRect(r, ui.s(12), Color{255, 255, 255, 50}, ui.s(1));
}

// ------------------------------------------------------------------------------------ panels

std::vector<float> zoneShares(const ZoneTable& t) {
    int64_t boss = 0, all = 0;
    for (const ZoneRow& r : t.rows) {
        boss += std::max(0, r.bossDamage);
        all += std::max(0, r.damage);
    }
    std::vector<float> out;
    for (const ZoneRow& r : t.rows)
        out.push_back(boss > 0  ? static_cast<float>(std::max(0, r.bossDamage)) / static_cast<float>(boss)
                      : all > 0 ? static_cast<float>(std::max(0, r.damage)) / static_cast<float>(all)
                                : 0.0f);
    return out;
}

ZoneColumns zoneColumns(const ZoneTable& t, float width) {
    ZoneColumns c;
    bool boss = false;
    for (const ZoneRow& r : t.rows) boss = boss || r.bossDamage > 0;
    static const float kShare[6] = {0.27f, 0.27f, 0.12f, 0.11f, 0.13f, 0.10f};
    const char* heads[6] = {"Joueur", boss ? "Part des dégâts au boss" : "Part des dégâts", "Dégâts", "Soutien", "Mécanismes", "Objectifs"};
    float x = 0.0f;
    for (int i = 0; i < 6; ++i) {
        c.x[i] = x;
        c.w[i] = width * kShare[i];
        c.header[i] = heads[i];
        x += c.w[i];
    }
    return c;
}

void drawZoneTable(UIContext& ui, const Rect& r, float k, const ZoneTable& t) {
    const float pad = ui.s(14) * k, iw = r.w - pad * 2, x = r.x + pad;
    float y = r.y + pad;
    fit(ui, t.title, {x, y + ui.s(11) * k}, 18 * k, iw, kGold, FontFace::Bold);  // 2.75.5: « · » is not in the titles' font
    y += ui.s(26) * k;
    if (!t.sub.empty()) {
        const auto lines = wrap(ui, t.sub, FontFace::Body, 13 * k, iw, 2);
        y += drawLines(ui, lines, {x, y}, 13 * k, ui.s(18) * k, kDim) + ui.s(4) * k;
    }
    const ZoneColumns c = zoneColumns(t, iw);
    const float gap = ui.s(6) * k;  // inside each column, at its right
    for (int i = 0; i < 6; ++i)
        fit(ui, c.header[i], {x + c.x[i] + (i >= 2 ? c.w[i] - gap : 0.0f), y + ui.s(9) * k}, 12 * k, c.w[i] - gap, kDim, FontFace::Bold,
            i >= 2 ? Align::Right : Align::Left);
    y += ui.s(20) * k;
    if (t.rows.empty()) {
        drawLines(ui, wrap(ui, "Personne n'a encore rien compté dans cette zone.", FontFace::Body, 14 * k, iw, 2), {x, y + ui.s(4) * k}, 14 * k,
                  ui.s(20) * k, kDim);
        return;
    }
    const std::vector<float> share = zoneShares(t);
    const float rowH = ui.s(28) * k, bottom = r.bottom() - pad;
    for (size_t i = 0; i < t.rows.size(); ++i) {
        const size_t left = t.rows.size() - i;
        if (y + rowH > bottom - (left > 1 ? ui.s(20) * k : 0.0f)) {
            fit(ui, "… et " + std::to_string(left) + (left > 1 ? " joueurs de plus" : " joueur de plus"), {x, y + ui.s(9) * k}, 13 * k, iw, kDim);
            return;
        }
        const ZoneRow& row = t.rows[i];
        const Rect rr{x, y, iw, rowH - ui.s(3) * k};
        ui.r().fillRoundedRect(rr, ui.s(6) * k, row.me ? Color{42, 42, 90, 240} : Color{22, 29, 66, 230});
        const float cy = rr.center().y;
        fit(ui, row.name, {x + c.x[0] + ui.s(6) * k, cy}, 14 * k, c.w[0] - gap - ui.s(6) * k, row.me ? kGold : row.color, FontFace::Bold, Align::Left, true);
        // The share: a bar and its percentage (measured first, at the bar's right).
        const std::string pct = std::to_string(static_cast<int>(std::lround(share[i] * 100.0f))) + " %";
        const float pw = std::min(textW(ui, "100 %", 13 * k, FontFace::Bold), c.w[1] * 0.45f);
        const float bw = std::max(0.0f, c.w[1] - gap - pw - ui.s(6) * k), bh = ui.s(8) * k;
        ui.r().fillRoundedRect({x + c.x[1], cy - bh * 0.5f, bw, bh}, bh * 0.5f, Color{58, 42, 58, 255});
        if (share[i] > 0.0f) ui.r().fillRoundedRect({x + c.x[1], cy - bh * 0.5f, bw * std::clamp(share[i], 0.0f, 1.0f), bh}, bh * 0.5f, kOrange);
        fit(ui, pct, {x + c.x[1] + c.w[1] - gap, cy}, 13 * k, pw, kText, FontFace::Bold, Align::Right, true);
        const int values[4] = {row.damage, row.support, row.mechanisms, row.objectives};
        for (int v = 0; v < 4; ++v)
            fit(ui, std::to_string(values[v]), {x + c.x[2 + v] + c.w[2 + v] - gap, cy}, 14 * k, c.w[2 + v] - gap, kText, FontFace::Body, Align::Right, true);
        y += rowH;
    }
}

float zoneTableHeight(UIContext& ui, const ZoneTable& t, float width, float k) {
    // The same steps as drawZoneTable: padding, title, sub-title (2 lines at most), header, rows.
    const float pad = ui.s(14) * k, iw = width - pad * 2;
    float h = pad + ui.s(26) * k;
    if (!t.sub.empty()) h += static_cast<float>(wrap(ui, t.sub, FontFace::Body, 13 * k, iw, 2).size()) * ui.s(18) * k + ui.s(4) * k;
    h += ui.s(20) * k;
    h += t.rows.empty() ? ui.s(4) * k + 2.0f * ui.s(20) * k : static_cast<float>(t.rows.size()) * ui.s(28) * k;
    return h + pad + ui.s(2);
}

void drawGroup(UIContext& ui, const Rect& r, float k, const Group& g) {
    Renderer& rr = ui.r();
    hudBox(ui, r);
    const float pad = ui.s(10) * k, iw = r.w - pad * 2, x = r.x + pad;
    fit(ui, g.title, {x, r.y + pad + ui.s(9) * k}, 14 * k, iw, kText, FontFace::Bold, Align::Left, true);
    const float rowsTop = r.y + pad + ui.s(24) * k, footerH = ui.s(22) * k;
    const float rowH = std::max(1.0f, (r.bottom() - pad - footerH - rowsTop) / static_cast<float>(std::max<size_t>(1, g.rows.size())));
    const float nameW = iw * 0.36f, stateW = iw * 0.3f, barX = x + nameW + ui.s(6) * k, barW = std::max(0.0f, iw - nameW - stateW - ui.s(12) * k);
    float y = rowsTop;
    const float fs = std::min(13.0f * k, rowH / ui.s(1.0f) * 0.62f);
    for (const GroupRow& row : g.rows) {
        const float cy = y + rowH * 0.5f;
        fit(ui, row.vacant ? "—" : row.name, {x, cy}, fs, nameW, row.vacant ? kDim : row.color, FontFace::Bold, Align::Left, true);
        const float hp = std::clamp(row.health, 0.0f, 1.0f);
        const Color hc = hp > 0.6f ? kGreen : hp > 0.3f ? kGold : kOrange;
        const float bh = std::min(ui.s(8) * k, rowH * 0.4f);
        rr.fillRoundedRect({barX, cy - bh * 0.5f, barW, bh}, bh * 0.5f, Color{58, 42, 58, 255});
        if (row.playing && hp > 0.0f) rr.fillRoundedRect({barX, cy - bh * 0.5f, barW * hp, bh}, bh * 0.5f, hc);
        fit(ui, row.state, {r.right() - pad, cy}, fs * 0.94f, stateW, row.stateColor, FontFace::Body, Align::Right);
        y += rowH;
    }
    fit(ui, g.footer, {x, r.bottom() - pad - footerH * 0.5f}, 12 * k, iw, kGold);
}

void drawObjectives(UIContext& ui, const Rect& r, float k, const std::string& title, const std::vector<Line>& lines) {
    hudBox(ui, r);
    const float pad = ui.s(10) * k, iw = r.w - pad * 2, markW = ui.s(44) * k;
    const float fs = 13.0f * k, lineH = ui.s(18) * k;
    fit(ui, title, {r.x + pad, r.y + pad + ui.s(8) * k}, fs, iw, kText, FontFace::Bold);
    float y = r.y + pad + ui.s(22) * k;
    const float bottom = r.bottom() - pad;
    for (size_t i = 0; i < lines.size(); ++i) {
        const Line& l = lines[i];
        const size_t left = lines.size() - i;
        // Room for this line and, if others follow, the count of those that would not fit.
        const float reserve = left > 1 ? lineH + ui.s(4) * k : 0.0f;
        const int maxLines = std::max(1, std::min(3, static_cast<int>((bottom - reserve - y) / lineH)));
        if (y + lineH > bottom - reserve) {
            const std::string more = "… et " + std::to_string(left) + (left > 1 ? " lignes de plus" : " ligne de plus");
            fit(ui, more, {r.x + pad, y + lineH * 0.5f}, fs, iw, kDim);
            return;
        }
        const auto wrapped = wrap(ui, l.text, FontFace::Body, fs, iw - markW, maxLines);
        fit(ui, l.mark, {r.x + pad, y + lineH * 0.5f}, fs, markW - ui.s(6) * k, l.color, FontFace::Bold);
        y += drawLines(ui, wrapped, {r.x + pad + markW, y}, fs, lineH, kText) + ui.s(4) * k;
    }
}

std::vector<Rect> drawAlerts(UIContext& ui, const Rect& r, float k, const std::vector<Line>& alerts) {
    std::vector<Rect> out;
    float y = r.y;
    const float pad = ui.s(10) * k, fs = 13.0f * k, lineH = ui.s(18) * k;
    for (const Line& a : alerts) {
        const auto lines = wrap(ui, a.text, FontFace::Body, fs, r.w - pad * 2 - ui.s(4) * k, 2);
        const float h = static_cast<float>(lines.size()) * lineH + pad;
        if (y + h > r.bottom()) break;  // never past its box
        ui.r().fillRoundedRect({r.x, y, r.w, h}, ui.s(8) * k, Color{20, 14, 18, 220});
        ui.r().fillRect({r.x, y, ui.s(4) * k, h}, a.color);
        drawLines(ui, lines, {r.x + pad + ui.s(4) * k, y + pad * 0.5f}, fs, lineH, kText);
        out.push_back({r.x, y, r.w, h});
        y += h + ui.s(6) * k;
    }
    return out;
}

void drawDanger(UIContext& ui, const Rect& r, float k, const std::string& text) {
    // The band takes the width its text needs, centred in its area (never wider).
    const float tw = std::min(textW(ui, text, 20 * k, FontFace::Bold) + ui.s(40) * k, r.w);
    const Rect band{r.center().x - tw * 0.5f, r.y, tw, r.h};
    ui.r().fillRoundedRect(band, ui.s(8) * k, Color{120, 20, 20, 210});
    ui.r().drawRoundedRect(band, ui.s(8) * k, kDanger, ui.s(2) * k);
    fit(ui, text, band.center(), 20 * k, band.w - ui.s(24) * k, Color{255, 200, 200, 255}, FontFace::Bold, Align::Center);
}

std::string RaidClock::at(float raidSeconds) const {
    if (!deadline) return LevelTimer::format(std::max(0.0f, raidSeconds), false);
    return "reste " + LevelTimer::format(std::max(0.0f, end - raidSeconds), false);
}

Color clockColor(const RaidClock& c, Color normal, float time) {
    if (!c.urgent()) return normal;
    // Its last minute: red, its brightness beating twice a second.
    const float beat = 0.5f + 0.5f * std::sin(time * 6.2831853f * 2.0f);
    return Color{255, static_cast<uint8_t>(60 + 90 * beat), static_cast<uint8_t>(60 + 90 * beat), 255};
}

void drawTimer(UIContext& ui, const Rect& r, float k, const RaidClock& raid, float zoneSeconds) {
    hudBox(ui, r);
    const float pad = ui.s(10) * k, iconS = ui.s(28) * k;
    const float bigH = ui.s(34) * k, smallH = ui.s(16) * k;
    const float top = r.y + pad, rowY = top + bigH * 0.5f;
    // The clock of the levels' timer, then the time in its font (fitted to what is left).
    const Color col = clockColor(raid, ui.theme().text, ui.time());
    if (raid.urgent()) ui.r().drawRoundedRect(r, ui.s(10) * k, col.withAlpha(200), ui.s(2) * k);  // its last minute
    ui.icon("ui/icons_hud.png", 6, 16, {r.x + pad, rowY - iconS * 0.5f, iconS, iconS}, Color{255, 255, 255, 200});
    const float tx = r.x + pad + iconS + ui.s(8) * k, room = r.right() - pad - tx;
    const std::string big = LevelTimer::format(raid.deadline ? raid.left() : std::max(0.0f, raid.now), false);
    const float fs = ui.fitFont(big, FontFace::Title, 34.0f * k, room, 0.5f);
    TextStyle st;
    st.face = FontFace::Title;
    st.size = ui.s(fs);
    st.color = col;
    st.align = Align::Left;
    st.valign = VAlign::Middle;
    st.shadow = true;
    st.shadowOffset = ui.s(2) * k;
    st.glow = ui.s(raid.urgent() ? 16 : 10) * k;
    st.glowColor = col.withAlpha(raid.urgent() ? 90 : 40);
    if (room > ui.s(10)) ui.text().draw(big, {tx, rowY}, st);
    // What the big time is, then the zone's (elapsed: a zone has no limit), fitted to the box.
    const std::string zone = LevelTimer::format(std::max(0.0f, zoneSeconds), false);
    const std::string line = !raid.deadline       ? "Écoulé depuis le début · zone : " + zone
                             : raid.left() <= 0.0f ? "Temps du raid écoulé · zone : " + zone
                                                   : "Reste du raid · zone : " + zone + " écoulées";
    fit(ui, line, {r.x + pad, top + bigH + ui.s(6) * k + smallH * 0.5f}, 15 * k, r.w - pad * 2, raid.urgent() ? col : kDim);
}

void drawLineBox(UIContext& ui, const Rect& r, float k, const std::string& text, Color textColor, bool test) {
    const float w = std::min(textW(ui, text, 13 * k, test ? FontFace::Bold : FontFace::Body) + ui.s(24) * k, r.w);
    // Anchored on the side of the screen it is nearest to.
    const bool right = r.center().x > ui.screenW() * 0.5f;
    const Rect box{right ? r.right() - w : r.x, r.y, w, r.h};
    ui.r().fillRoundedRect(box, ui.s(10) * k, test ? Color{120, 20, 40, 230} : kHudFill);
    if (test) ui.r().drawRoundedRect(box, ui.s(10) * k, kGold, ui.s(1.5f) * k);
    fit(ui, text, {box.x + ui.s(12) * k, box.center().y}, 13 * k, box.w - ui.s(24) * k, textColor, test ? FontFace::Bold : FontFace::Body);
}

void drawHeartBar(UIContext& ui, const Rect& r, float k, const HeartBar& h) {
    Renderer& rr = ui.r();
    hudBox(ui, r);
    const float pad = ui.s(10) * k, x = r.x + pad, w = r.w - pad * 2;
    // The top row: its phase at the right (measured first), the title in what is left.
    const float rw = std::min(textW(ui, h.phase, 13 * k, FontFace::Bold), w * 0.3f);
    const float top = r.y + pad + ui.s(5) * k;
    fit(ui, h.phase, {r.right() - pad, top}, 13 * k, rw, h.fallen ? kGreen : kGold, FontFace::Bold, Align::Right);
    fit(ui, h.title, {x, top}, 13 * k, std::max(0.0f, w - rw - ui.s(10) * k), kText, FontFace::Bold);
    // The bar: what is left (gold), this group's part (green) and the others' (violet) at its right.
    const Rect bar{x, r.y + pad + ui.s(14) * k, w, ui.s(9) * k};
    const float rad = bar.h * 0.5f;
    rr.fillRoundedRect(bar, rad, Color{50, 36, 60, 255});
    const float left = std::clamp(h.left, 0.0f, 1.0f);
    if (left > 0.0f) rr.fillRoundedRect({bar.x, bar.y, bar.w * left, bar.h}, rad, kGold);
    const float ox = bar.x + bar.w * left;
    const float ow = std::clamp(bar.w * h.own, 0.0f, std::max(0.0f, bar.right() - ox));
    if (ow > 0.0f) rr.fillRect({ox, bar.y, ow, bar.h}, Color{124, 240, 168, 200});
    const float vw = std::clamp(bar.w * h.others, 0.0f, std::max(0.0f, bar.right() - ox - ow));
    if (vw > 0.0f) rr.fillRect({ox + ow, bar.y, vw, bar.h}, Color{180, 108, 255, 170});
    for (float t : {0.25f, 0.5f, 0.75f})
        rr.fillRect({bar.x + bar.w * t - ui.s(1) * k, bar.y - ui.s(2) * k, ui.s(2) * k, bar.h + ui.s(4) * k}, Color{255, 255, 255, 90});
    // The line under it, in the room left at the bottom of the box.
    const float lineY = (bar.bottom() + r.bottom()) * 0.5f;
    fit(ui, h.sub, {x, lineY}, 12 * k, w, kDim);
}

// -------------------------------------------------------------------------------------- maps

const char* iconCell(Icon i) {
    switch (i) {
        case Icon::Me: return "me";
        case Icon::Ally: return "ally";
        case Icon::Enemy: return "enemy";
        case Icon::Boss: return "boss";
        case Icon::Objective: return "objective";
        case Icon::ObjectiveDone: return "objective_done";
        case Icon::ObjectiveOptional: return "objective_optional";
        case Icon::Exit: return "exit";
        case Icon::ExitLocked: return "exit_locked";
        case Icon::Door: return "door";
        case Icon::Checkpoint: return "checkpoint";
        case Icon::CheckpointOn: return "checkpoint_on";
        case Icon::Plate: return "plate";
        case Icon::PlateDone: return "plate_done";
        case Icon::Vent: return "vent";
        case Icon::VentClosed: return "vent_closed";
        case Icon::Magma: return "magma";
        default: return "";
    }
}

const char* iconLabel(Icon i) {
    switch (i) {
        case Icon::Me: return "Vous";
        case Icon::Ally: return "Groupe";
        case Icon::Enemy: return "Ennemis";
        case Icon::Boss: return "Boss";
        case Icon::Objective: return "Objectif";
        case Icon::ObjectiveDone: return "Fait";
        case Icon::ObjectiveOptional: return "Facultatif";
        case Icon::Exit: return "Passage";
        case Icon::ExitLocked: return "Verrouillé";
        case Icon::Door: return "Porte";
        case Icon::Checkpoint: return "Point de contrôle";
        case Icon::CheckpointOn: return "Point atteint";
        case Icon::Plate: return "Plaque de groupe";
        case Icon::PlateDone: return "Plaque faite";
        case Icon::Vent: return "Évent";
        case Icon::VentClosed: return "Évent fermé";
        case Icon::Magma: return "Magma";
        default: return "";
    }
}

bool Minimap::ensure(UIContext& ui, ResourceManager& res, const GameWorld& w) {
    Atlas& a = atlas(res);
    if (!atlas_) atlas_ = res.texture("ui/raids/minimap.png", true);
    if (!atlas_ || a.cells.empty()) return false;
    if (cells_.empty()) {
        for (int i = 0; i < static_cast<int>(Icon::Count); ++i) {
            const Rect* c = a.cell(iconCell(static_cast<Icon>(i)));
            cells_.push_back(c ? *c : Rect{});
        }
        if (const Rect* m = a.cell("magma_zone")) magmaCell_ = *m;
    }
    if (world_ == &w && terrain_) return true;
    Renderer& r = ui.r();
    bounds_ = w.level().bounds;
    const int tw = std::max(1, static_cast<int>(std::ceil(bounds_.w / kTexel))), th = std::max(1, static_cast<int>(std::ceil(bounds_.h / kTexel)));
    terrain_ = r.createTarget(tw, th, true);
    if (!terrain_) return false;
    r.setTarget(terrain_);
    SDL_SetRenderDrawColor(r.sdl(), 0, 0, 0, 0);
    SDL_RenderClear(r.sdl());
    const float t = a.tile;
    // A rectangle (texture pixels) filled with a tile, the grid anchored on the zone (seamless).
    auto tiled = [&](const Rect& dst, const Rect& cell) {
        for (float y = std::floor(dst.y / t) * t; y < dst.bottom(); y += t)
            for (float x = std::floor(dst.x / t) * t; x < dst.right(); x += t) {
                const float x0 = std::max(x, dst.x), y0 = std::max(y, dst.y), x1 = std::min(x + t, dst.right()), y1 = std::min(y + t, dst.bottom());
                if (x1 <= x0 || y1 <= y0) continue;
                const Rect src{cell.x + (x0 - x), cell.y + (y0 - y), x1 - x0, y1 - y0};
                r.drawTexture(*atlas_, &src, {x0, y0, x1 - x0, y1 - y0});
            }
    };
    auto toTex = [&](const Rect& wr) {
        return Rect{std::round((wr.x - bounds_.x) / kTexel), std::round((wr.y - bounds_.y) / kTexel), std::max(1.0f, std::round(wr.w / kTexel)),
                    std::max(1.0f, std::round(wr.h / kTexel))};
    };
    if (const Rect* back = a.cell("back")) tiled({0, 0, static_cast<float>(tw), static_cast<float>(th)}, *back);
    const Rect* rock = a.cell("rock");
    const Rect* edge = a.cell("rock_edge");
    const Rect* ledge = a.cell("ledge");
    const Rect* lava = a.cell("lava");
    const uint32_t movable = ColliderTag::Door | ColliderTag::Teammate | ColliderTag::Moving | ColliderTag::Gate;
    for (const Collider& c : w.physics().colliders()) {
        if (c.dynamic || (c.tags & movable)) continue;  // doors, lifts, players: drawn as they are, live
        const Rect d = toTex(c.box);
        if (c.kind == ColliderKind::Solid && rock) {
            tiled(d, *rock);
            if (edge) tiled({d.x, d.y, d.w, std::min(d.h, t * 0.25f)}, Rect{edge->x, edge->y, edge->w, t * 0.25f});
        } else if (c.kind == ColliderKind::OneWay && ledge) {
            tiled({d.x, d.y, d.w, std::min(t * 0.25f, std::max(2.0f, d.h))}, Rect{ledge->x, ledge->y, ledge->w, t * 0.25f});
        }
    }
    if (lava)
        for (const LevelObject& o : w.level().objects)
            if (o.type == "liquid") tiled(toTex(o.rect), *lava);
    r.setTarget(nullptr);
    world_ = &w;
    return true;
}

void Minimap::drawTerrain(UIContext& ui, const GameWorld& w, const Rect& dst, const Rect& win, const std::vector<MagmaArea>& magma) {
    Renderer& r = ui.r();
    const Rect src{(win.x - bounds_.x) / kTexel, (win.y - bounds_.y) / kTexel, win.w / kTexel, win.h / kTexel};
    r.drawTexture(*terrain_, &src, dst);
    const float sx = dst.w / win.w, sy = dst.h / win.h;
    auto at = [&](const Rect& wr) { return Rect{dst.x + (wr.x - win.x) * sx, dst.y + (wr.y - win.y) * sy, wr.w * sx, wr.h * sy}; };
    r.pushClip(dst);
    // The lifts and platforms that move, where they are now (thin ledges).
    for (const Collider& c : w.physics().colliders())
        if (c.dynamic && (c.tags & ColliderTag::Moving) && c.enabled) {
            const Rect m = at(c.box);
            r.fillRect({m.x, m.y, std::max(2.0f, m.w), std::max(2.0f, std::min(m.h, ui.s(3)))}, Color{230, 200, 150, 255});
        }
    // The magma's area: hatched faintly, pulsing before it rises, bright while it is up.
    for (const MagmaArea& m : magma) {
        const Rect d = at(m.rect);
        if (d.w <= 0.0f) continue;
        const float pulse = 0.5f + 0.5f * std::sin(ui.time() * 8.0f);
        const uint8_t alpha = m.active ? 230 : m.warning ? static_cast<uint8_t>(120 + 100 * pulse) : 70;
        if (const Rect* cell = magmaCell_.w > 0.0f ? &magmaCell_ : nullptr) {
            for (float y = d.y; y < d.bottom(); y += cell->h)
                for (float x = d.x; x < d.right(); x += cell->w) {
                    const float ww = std::min(cell->w, d.right() - x), hh = std::min(cell->h, d.bottom() - y);
                    const Rect src{cell->x, cell->y, ww, hh};
                    r.drawTexture(*atlas_, &src, {x, y, ww, hh}, Color{255, 255, 255, alpha});
                }
        } else {
            r.fillRect(d, Color{255, 74, 42, alpha});
        }
    }
    r.popClip();
}

void Minimap::drawMarks(UIContext& ui, const Rect& dst, const Rect& win, const std::vector<Mark>& marks, float size) {
    Renderer& r = ui.r();
    const float sx = dst.w / win.w, sy = dst.h / win.h;
    r.pushClip(dst);
    // Behind first (objects of the zone), the enemies, the group, this player on top.
    for (int pass = 0; pass < 3; ++pass)
        for (const Mark& m : marks) {
            const int layer = m.icon == Icon::Me ? 2 : (m.icon == Icon::Ally || m.icon == Icon::Enemy || m.icon == Icon::Boss) ? 1 : 0;
            if (layer != pass) continue;
            const Rect* cell = static_cast<size_t>(m.icon) < cells_.size() ? &cells_[static_cast<size_t>(m.icon)] : nullptr;
            if (!cell || cell->w <= 0.0f) continue;
            const float s = m.icon == Icon::Me ? size * 1.25f : m.icon == Icon::Enemy ? size * 0.8f : m.icon == Icon::Boss ? size * 1.2f : size;
            const Vec2 c{dst.x + (m.at.x - win.x) * sx, dst.y + (m.at.y - win.y) * sy};
            r.drawTexture(*atlas_, cell, Rect::fromCenter(c, {s, s}), m.tint);
        }
    r.popClip();
}

float Minimap::legendRow(UIContext& ui, float x, float cy, float right, float k, const std::vector<Icon>& icons) {
    const float is = ui.s(16) * k, gap = ui.s(10) * k;
    const float x0 = x;
    for (Icon i : icons) {
        const std::string label = iconLabel(i);
        const float w = is + ui.s(4) * k + textW(ui, label, 12 * k);
        if (x + w > right) break;
        if (static_cast<size_t>(i) < cells_.size() && cells_[static_cast<size_t>(i)].w > 0.0f)
            ui.r().drawTexture(*atlas_, &cells_[static_cast<size_t>(i)], Rect::fromCenter({x + is * 0.5f, cy}, {is, is}),
                               i == Icon::Ally ? kSky : Color::white());
        ui.label(label, {x + is + ui.s(4) * k, cy}, 12 * k, kDim, Align::Left, FontFace::Body, VAlign::Middle);
        x += w + gap;
    }
    return x - x0;
}

void Minimap::drawSmall(UIContext& ui, ResourceManager& res, const GameWorld& w, const Rect& r, float k, const std::string& title,
                        const std::vector<Mark>& marks, const std::vector<MagmaArea>& magma, Vec2 focus) {
    const bool ready = ensure(ui, res, w);  // before any clip: it draws into its own texture
    hudBox(ui, r);
    const float pad = ui.s(10) * k, rowY = r.y + ui.s(13) * k;
    // The legend at the right (the items that fit in 60 % of the row), the title in the room left.
    const float room = r.w - pad * 2;
    float legendW = 0.0f;
    if (ready) {
        static const std::vector<Icon> kItems = {Icon::Me, Icon::Ally, Icon::Enemy, Icon::Objective, Icon::Exit, Icon::Door};
        // Measured first, then drawn right-aligned.
        float need = 0.0f;
        size_t n = 0;
        for (Icon i : kItems) {
            const float iw = ui.s(16) * k + ui.s(4) * k + textW(ui, iconLabel(i), 12 * k) + (n ? ui.s(10) * k : 0.0f);
            if (need + iw > room * 0.62f) break;
            need += iw;
            ++n;
        }
        std::vector<Icon> shown(kItems.begin(), kItems.begin() + static_cast<long>(n));
        legendW = legendRow(ui, r.right() - pad - need, rowY, r.right() - pad + 1.0f, k, shown);
    }
    fit(ui, title, {r.x + pad, rowY}, 12 * k, room - (legendW > 0.0f ? legendW + ui.s(12) * k : 0.0f), Color{200, 192, 208, 255});
    const Rect strip{r.x + pad, r.y + ui.s(26) * k, r.w - pad * 2, r.bottom() - pad * 0.8f - (r.y + ui.s(26) * k)};
    ui.r().fillRoundedRect(strip, ui.s(6) * k, Color{20, 12, 18, 255});
    if (!ready || strip.w <= 0.0f || strip.h <= 0.0f) return;
    // The whole zone when it fits the strip's height, else a window around the player.
    const Rect b = bounds_;
    float scale = strip.h / std::max(1.0f, b.h);
    Rect win{b.x, b.y, strip.w / scale, b.h};
    Rect dst = strip;
    if (win.w >= b.w) {
        // Narrower than the strip: centred, the bounds kept.
        scale = std::min(strip.w / b.w, strip.h / b.h);
        dst = Rect{strip.center().x - b.w * scale * 0.5f, strip.center().y - b.h * scale * 0.5f, b.w * scale, b.h * scale};
        win = b;
    } else {
        win.x = std::clamp(focus.x - win.w * 0.5f, b.x, b.right() - win.w);
    }
    drawTerrain(ui, w, dst, win, magma);
    drawMarks(ui, dst, win, marks, ui.s(17) * k);
    // When it shows a part of the zone: where that part is along it (a thin bar under the strip).
    if (win.w < b.w - 1.0f) {
        const float bx = strip.x, bw = strip.w, by = strip.bottom() - ui.s(3) * k;
        ui.r().fillRect({bx, by, bw, ui.s(3) * k}, Color{60, 50, 70, 200});
        ui.r().fillRect({bx + (win.x - b.x) / b.w * bw, by, std::max(ui.s(6), win.w / b.w * bw), ui.s(3) * k}, kGold);
    }
}

void Minimap::drawLarge(UIContext& ui, ResourceManager& res, const GameWorld& w, const Rect& r, const std::string& title, const std::string& header,
                        Color headerColor, const std::vector<Mark>& marks, const std::vector<MagmaArea>& magma, Vec2 focus, float pan,
                        const std::vector<ZoneChip>& zones, const std::string& closeHint) {
    const bool ready = ensure(ui, res, w);
    Renderer& rr = ui.r();
    rr.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 10, 150});
    ui.panel(r, true, 16);
    const float pad = ui.s(24), x = r.x + pad, iw = r.w - pad * 2;
    // Title at the left, the danger now at the right (each in its measured share of the row).
    const float headerW = header.empty() ? 0.0f : std::min(textW(ui, header, 18, FontFace::Bold), iw * 0.45f);
    fit(ui, title, {x, r.y + ui.s(30)}, 24, iw - (headerW > 0.0f ? headerW + ui.s(20) : 0.0f), kText, FontFace::Bold);
    if (!header.empty()) fit(ui, header, {r.right() - pad, r.y + ui.s(30)}, 18, headerW, headerColor, FontFace::Bold, Align::Right);
    // From the bottom up: how to close it, the legend, the branch's zones; the map takes the rest.
    float bottom = r.bottom() - pad;
    fit(ui, closeHint, {r.center().x, bottom - ui.s(10)}, 15, iw, kDim, FontFace::Body, Align::Center);
    bottom -= ui.s(30);
    // The legend: every icon, in rows measured to the panel's width.
    std::vector<std::vector<Icon>> legendRows(1);
    {
        float lx = 0.0f;
        for (int i = 0; i < static_cast<int>(Icon::Count); ++i) {
            const Icon ic = static_cast<Icon>(i);
            const float wdt = ui.s(18) + ui.s(4) + textW(ui, iconLabel(ic), 14) + ui.s(14);
            if (lx + wdt > iw && !legendRows.back().empty()) {
                legendRows.emplace_back();
                lx = 0.0f;
            }
            legendRows.back().push_back(ic);
            lx += wdt;
        }
    }
    const float legendLineH = ui.s(24);
    bottom -= legendLineH * static_cast<float>(legendRows.size());
    if (ready)
        for (size_t i = 0; i < legendRows.size(); ++i) {
            float lx = x;
            for (Icon ic : legendRows[i]) {
                const float cy = bottom + legendLineH * (static_cast<float>(i) + 0.5f);
                const Rect& cell = cells_[static_cast<size_t>(ic)];
                if (cell.w > 0.0f) rr.drawTexture(*atlas_, &cell, Rect::fromCenter({lx + ui.s(9), cy}, {ui.s(18), ui.s(18)}), ic == Icon::Ally ? kSky : Color::white());
                const float tw = textW(ui, iconLabel(ic), 14);
                ui.label(iconLabel(ic), {lx + ui.s(22), cy}, 14, kDim, Align::Left, FontFace::Body, VAlign::Middle);
                lx += ui.s(22) + tw + ui.s(14);
            }
        }
    bottom -= ui.s(10);
    // The branch's zones: chips measured to their names, wrapping.
    if (!zones.empty()) {
        std::vector<std::pair<Rect, const ZoneChip*>> chips;
        float cx = x, cy = 0.0f;
        // 2.75: a zone still to finish says what it misses on a second line (measured, at most a third of the row).
        bool twoLines = false;
        for (const ZoneChip& z : zones) twoLines = twoLines || !z.missing.empty();
        const float chipH = ui.s(twoLines ? 52.0f : 34.0f), gap = ui.s(8);
        for (const ZoneChip& z : zones) {
            const std::string t = z.name + " · " + z.state;
            const float mw = z.missing.empty() ? 0.0f : std::min(textW(ui, z.missing, 12), iw / 3.0f);
            const float cw = std::min(iw, std::max(textW(ui, t, 14, FontFace::Bold), mw) + ui.s(24));
            if (cx + cw > x + iw && cx > x) {
                cx = x;
                cy += chipH + gap;
            }
            chips.push_back({Rect{cx, cy, cw, chipH}, &z});
            cx += cw + gap;
        }
        const float h = cy + chipH;
        bottom -= h;
        for (auto& [c, z] : chips) {
            const Rect cr{c.x, bottom + c.y, c.w, c.h};
            rr.fillRoundedRect(cr, ui.s(10), z->here ? Color{60, 44, 20, 240} : Color{26, 33, 72, 245});
            rr.drawRoundedRect(cr, ui.s(10), z->here ? kGold : z->color.withAlpha(140), ui.s(z->here ? 2.0f : 1.0f));
            if (twoLines) {
                fit(ui, z->name + " · " + z->state, {cr.center().x, cr.y + ui.s(16)}, 14, cr.w - ui.s(16), z->here ? kGold : z->color, FontFace::Bold,
                    Align::Center);
                if (!z->missing.empty()) fit(ui, z->missing, {cr.center().x, cr.y + ui.s(37)}, 12, cr.w - ui.s(16), kDim, FontFace::Body, Align::Center);
            } else {
                fit(ui, z->name + " · " + z->state, cr.center(), 14, cr.w - ui.s(16), z->here ? kGold : z->color, FontFace::Bold, Align::Center);
            }
        }
        bottom -= ui.s(12);
    }
    const float top = r.y + ui.s(56);
    Rect area{x, top, iw, bottom - top};
    if (!ready || area.h < ui.s(40)) return;
    // The zone: its whole width when that keeps it at least 35 % of the room's height, else a window
    // around the player (moved by `pan`) and an overview of the whole zone under it.
    const Rect b = bounds_;
    const float overviewH = ui.s(46);
    float scale = std::max(area.w / b.w, area.h / b.h * 0.35f);
    scale = std::min(scale, area.h / b.h);
    const bool windowed = b.w * scale > area.w + 1.0f;
    Rect mapArea = area;
    if (windowed) mapArea.h -= overviewH + ui.s(10);
    scale = std::min(scale, mapArea.h / b.h);
    largeScale_ = 1.0f / std::max(1e-4f, scale);
    Rect win{b.x, b.y, std::min(b.w, mapArea.w / scale), b.h};
    if (win.w < b.w) win.x = std::clamp(focus.x - win.w * 0.5f + pan, b.x, b.right() - win.w);
    const Rect dst{mapArea.center().x - win.w * scale * 0.5f, mapArea.center().y - b.h * scale * 0.5f, win.w * scale, b.h * scale};
    rr.fillRect(dst.expanded(ui.s(2)), Color{40, 30, 40, 255});
    drawTerrain(ui, w, dst, win, magma);
    drawMarks(ui, dst, win, marks, ui.s(24));
    if (windowed) {
        const float ok = std::min(area.w / b.w, overviewH / b.h);
        const Rect ov{area.center().x - b.w * ok * 0.5f, area.bottom() - overviewH, b.w * ok, b.h * ok};
        drawTerrain(ui, w, ov, b, magma);
        rr.drawRect({ov.x + (win.x - b.x) * ok, ov.y, win.w * ok, ov.h}, kGold, ui.s(2));
        for (const Mark& m : marks)
            if (m.icon == Icon::Me) rr.fillCircle({ov.x + (m.at.x - b.x) * ok, ov.y + (m.at.y - b.y) * ok}, ui.s(3), kGold);
    }
}

}  // namespace cosmic::raidhud
