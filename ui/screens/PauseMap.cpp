#include "ui/screens/PauseMap.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/levels/ObjectTypes.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

void ExploredMap::reset(const Rect& levelBounds) {
    bounds = levelBounds;
    cols = std::max(1, static_cast<int>(std::ceil(bounds.w / cell)));
    rows = std::max(1, static_cast<int>(std::ceil(bounds.h / cell)));
    seen.assign(static_cast<size_t>(cols) * static_cast<size_t>(rows), 0);
}

void ExploredMap::mark(const Rect& area) {
    if (cols == 0) return;
    int c0 = std::max(0, static_cast<int>(std::floor((area.x - bounds.x) / cell)));
    int r0 = std::max(0, static_cast<int>(std::floor((area.y - bounds.y) / cell)));
    int c1 = std::min(cols - 1, static_cast<int>(std::floor((area.right() - bounds.x) / cell)));
    int r1 = std::min(rows - 1, static_cast<int>(std::floor((area.bottom() - bounds.y) / cell)));
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) seen[static_cast<size_t>(r * cols + c)] = 1;
}

bool ExploredMap::seenPoint(Vec2 p) const {
    return at(static_cast<int>(std::floor((p.x - bounds.x) / cell)), static_cast<int>(std::floor((p.y - bounds.y) / cell)));
}

bool ExploredMap::empty() const { return std::none_of(seen.begin(), seen.end(), [](uint8_t v) { return v != 0; }); }

Rect ExploredMap::seenBox() const {
    int c0 = cols, r0 = rows, c1 = -1, r1 = -1;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            if (at(c, r)) {
                c0 = std::min(c0, c);
                r0 = std::min(r0, r);
                c1 = std::max(c1, c);
                r1 = std::max(r1, r);
            }
    if (c1 < 0) return {};
    return {bounds.x + c0 * cell, bounds.y + r0 * cell, (c1 - c0 + 1) * cell, (r1 - r0 + 1) * cell};
}

PauseMapView drawPauseMap(UIContext& ui, GameWorld& world, const ExploredMap& seen, const Rect& area,
                          const std::function<void(const Rect& dst, const Rect& win)>& drawLevel, float pan) {
    Renderer& r = ui.r();
    PauseMapView out;
    if (seen.cols == 0 || seen.empty()) {
        ui.labelWrapped("Rien d'exploré pour l'instant.", {area.x, area.y}, area.w, 16, ui.theme().textDim);
        return out;
    }
    const Player& player = world.player();
    // The window shown: what was explored, at the frame's proportions. Wider than the frame:
    // the part around the player, which can be scrolled.
    const Rect box = seen.seenBox().expanded(seen.cell * 0.5f);
    const float aspect = area.w / area.h;
    Rect win = box;
    const float fitW = std::max(box.h * aspect, 960.0f);
    if (box.w > fitW) {
        float centred = player.center().x - fitW * 0.5f;
        out.minPan = box.x - centred;
        out.maxPan = box.right() - fitW - centred;
        float x = std::clamp(centred + pan, box.x, box.right() - fitW);
        win = {x, box.y, fitW, box.h};
    }
    const float k = std::min(area.w / win.w, area.h / win.h);
    Rect dst{std::floor(area.center().x - win.w * k * 0.5f), std::floor(area.center().y - win.h * k * 0.5f), std::floor(win.w * k), std::floor(win.h * k)};
    out.dst = dst;
    out.scale = k;
    auto toMap = [&](Vec2 p) { return Vec2{dst.x + (p.x - win.x) * k, dst.y + (p.y - win.y) * k}; };
    auto rectToMap = [&](Rect w) -> Rect {
        float x0 = std::max(w.x, win.x), y0 = std::max(w.y, win.y), x1 = std::min(w.right(), win.right()), y1 = std::min(w.bottom(), win.bottom());
        if (x1 <= x0 || y1 <= y0) return {};
        Vec2 a = toMap({x0, y0}), b = toMap({x1, y1});
        return {a.x, a.y, b.x - a.x, b.y - a.y};
    };
    // The real level, small.
    drawLevel(dst, win);
    // What was never seen stays hidden, with soft edges.
    r.pushClip(dst);
    const Color fog{7, 10, 22, 255};
    const float feather = seen.cell * k * 0.35f;
    int c0 = std::max(0, static_cast<int>((win.x - seen.bounds.x) / seen.cell) - 1), c1 = std::min(seen.cols - 1, static_cast<int>((win.right() - seen.bounds.x) / seen.cell) + 1);
    int r0 = std::max(0, static_cast<int>((win.y - seen.bounds.y) / seen.cell) - 1), r1 = std::min(seen.rows - 1, static_cast<int>((win.bottom() - seen.bounds.y) / seen.cell) + 1);
    for (int pass = 0; pass < 2; ++pass)
        for (int row = r0; row <= r1; ++row) {
            int c = c0;
            while (c <= c1) {
                if (seen.at(c, row)) {
                    ++c;
                    continue;
                }
                int start = c;
                while (c <= c1 && !seen.at(c, row)) ++c;
                Rect cells{seen.bounds.x + start * seen.cell, seen.bounds.y + row * seen.cell, (c - start) * seen.cell, seen.cell};
                Rect m = rectToMap(cells);
                if (m.w <= 0.0f) continue;
                if (pass == 0) r.fillRect(m.expanded(feather), fog.withAlphaF(0.45f));  // the soft edge
                else r.fillRect(m.expanded(0.5f), fog);
            }
        }
    // Outside the level itself: hidden too.
    Rect lv = rectToMap(seen.bounds);
    if (lv.w > 0.0f) {
        if (lv.x > dst.x) r.fillRect({dst.x, dst.y, lv.x - dst.x, dst.h}, fog);
        if (lv.right() < dst.right()) r.fillRect({lv.right(), dst.y, dst.right() - lv.right(), dst.h}, fog);
        if (lv.y > dst.y) r.fillRect({dst.x, dst.y, dst.w, lv.y - dst.y}, fog);
        if (lv.bottom() < dst.bottom()) r.fillRect({dst.x, lv.bottom(), dst.w, dst.bottom() - lv.bottom()}, fog);
    }
    // Markers over the drawing, only where the player has been (outlined, to read on any art).
    const float u = std::max(ui.s(4), 3.0f);
    const Color ink{6, 8, 16, 255};
    for (const auto& e : world.entities()) {
        if (e->removed || !e->active) continue;
        Vec2 p = e->box.center();
        if (!seen.seenPoint(p) || !win.contains(p)) continue;
        Vec2 m = toMap(p);
        if (auto* cp = dynamic_cast<const CheckpointEntity*>(e.get())) {
            Rect f{m.x - u, m.y - u * 1.5f, u * 2, u * 3};
            r.fillRect(f.expanded(1.5f), ink);
            r.fillRect(f, cp->activated() ? ui.theme().success : Color{150, 160, 180, 255});
        } else if (dynamic_cast<const ExitEntity*>(e.get())) {
            Rect f{m.x - u * 1.4f, m.y - u * 1.8f, u * 2.8f, u * 3.6f};
            r.fillRect(f.expanded(1.5f), ink);
            r.fillRect(f, Color{255, 206, 84, 255});
        } else if (auto* pu = dynamic_cast<const PowerUnlockEntity*>(e.get())) {
            if (pu->power().empty() || player.hasPower(pu->power())) continue;
            float d = u * 1.9f, o = d + 2.0f;
            r.fillTriangle({m.x, m.y - o}, {m.x + o, m.y}, {m.x - o, m.y}, ink);
            r.fillTriangle({m.x - o, m.y}, {m.x + o, m.y}, {m.x, m.y + o}, ink);
            Color c{190, 110, 255, 255};
            r.fillTriangle({m.x, m.y - d}, {m.x + d, m.y}, {m.x - d, m.y}, c);
            r.fillTriangle({m.x - d, m.y}, {m.x + d, m.y}, {m.x, m.y + d}, c);
        } else if (dynamic_cast<const CollectibleEntity*>(e.get()) && e->visible) {
            r.fillCircle(m, u * 0.9f + 1.5f, ink);
            r.fillCircle(m, u * 0.9f, Color{255, 230, 120, 255});
        }
    }
    Vec2 pm = toMap(player.center());
    if (dst.contains(pm)) {
        r.drawRing(pm, u * 2.6f, 2.0f, ui.theme().accent);
        r.drawRing(pm, u * 2.6f + 2.0f, 1.0f, ink);
    }
    r.popClip();
    r.drawRect(dst, ui.theme().border, 1.0f);
    return out;
}

}  // namespace cosmic
