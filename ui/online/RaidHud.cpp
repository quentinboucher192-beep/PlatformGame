#include "ui/online/RaidHud.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/physics/PhysicsWorld.h"
#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/levels/LevelTimer.h"
#include "saves/Settings.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::raidhud {

using namespace raidui;

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

void hudBox(UIContext& ui, const Rect& r) {
    ui.r().fillRoundedRect(r, ui.s(12), kHudFill);
    ui.r().drawRoundedRect(r, ui.s(12), Color{255, 255, 255, 50}, ui.s(1));
}

// ------------------------------------------------------------------------------------ panels

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

void drawTimer(UIContext& ui, const Rect& r, float k, float instanceSeconds, float zoneSeconds) {
    hudBox(ui, r);
    const float pad = ui.s(10) * k, iconS = ui.s(28) * k;
    const float bigH = ui.s(34) * k, smallH = ui.s(16) * k;
    const float top = r.y + pad, rowY = top + bigH * 0.5f;
    // The clock of the levels' timer, then the time in its font (fitted to what is left).
    ui.icon("ui/icons_hud.png", 6, 16, {r.x + pad, rowY - iconS * 0.5f, iconS, iconS}, Color{255, 255, 255, 200});
    const float tx = r.x + pad + iconS + ui.s(8) * k, room = r.right() - pad - tx;
    const std::string big = LevelTimer::format(std::max(0.0f, instanceSeconds));
    const float fs = ui.fitFont(big, FontFace::Title, 34.0f * k, room, 0.5f);
    TextStyle st;
    st.face = FontFace::Title;
    st.size = ui.s(fs);
    st.color = ui.theme().text;
    st.align = Align::Left;
    st.valign = VAlign::Middle;
    st.shadow = true;
    st.shadowOffset = ui.s(2) * k;
    st.glow = ui.s(10) * k;
    st.glowColor = ui.theme().text.withAlpha(40);
    if (room > ui.s(10)) ui.text().draw(big, {tx, rowY}, st);
    fit(ui, "Zone : " + LevelTimer::format(std::max(0.0f, zoneSeconds), false), {r.x + pad, top + bigH + ui.s(6) * k + smallH * 0.5f}, 15 * k,
        r.w - pad * 2, kDim);
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
        const float chipH = ui.s(34), gap = ui.s(8);
        for (const ZoneChip& z : zones) {
            const std::string t = z.name + " · " + z.state;
            const float cw = std::min(iw, textW(ui, t, 14, FontFace::Bold) + ui.s(24));
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
            fit(ui, z->name + " · " + z->state, cr.center(), 14, cr.w - ui.s(16), z->here ? kGold : z->color, FontFace::Bold, Align::Center);
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
