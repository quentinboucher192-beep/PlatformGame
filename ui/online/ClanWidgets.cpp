#include "ui/online/ClanWidgets.h"

#include <algorithm>

#include "engine/rendering/Renderer.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

void drawClanEmblem(UIContext& ui, const Rect& r, const clans::Emblem& e) {
    const float side = std::min(r.w, r.h);
    const Rect tile{r.x + (r.w - side) * 0.5f, r.y + (r.h - side) * 0.5f, side, side};
    const float radius = side * 0.23f;
    // The background: the second colour, darkened; its border: the second colour.
    const Color bg = Color::lerp(Color{8, 11, 24, 255}, e.secondary, 0.28f);
    ui.r().fillRoundedRect(tile, radius, bg);
    ui.r().drawRoundedRect(tile, radius, e.secondary, std::max(1.0f, side * 0.035f));
    const Vec2 c = tile.center();
    const float R = side * 0.31f;
    // A soft glow of the first colour behind the shape.
    ui.r().drawGlow(c, R * 1.5f, e.primary.withAlphaF(0.22f));
    const Color light = Color::lerp(e.primary, Color::white(), 0.35f);
    const std::vector<Vec2> pts = clans::shapeOutline(e.shape, c, R);
    if (e.shape == "star") {
        // The star is not convex: one triangle from the centre per edge.
        for (size_t i = 0; i < pts.size(); ++i) ui.r().fillTriangle(c, pts[i], pts[(i + 1) % pts.size()], e.primary);
        ui.r().fillCircle(c, R * 0.18f, light);
        return;
    }
    if (e.shape == "orb") {
        ui.r().fillCircle(c, R, e.primary);
        ui.r().fillCircle({c.x - R * 0.3f, c.y - R * 0.3f}, R * 0.32f, light.withAlphaF(0.75f));
        return;
    }
    if (e.shape == "crescent") {
        // A disc cut by a disc of the background colour.
        ui.r().fillCircle(c, R, e.primary);
        ui.r().fillCircle({c.x + R * 0.42f, c.y - R * 0.18f}, R * 0.82f, bg);
        return;
    }
    ui.r().fillConvexPolygon(pts, e.primary);
    // A lighter inner copy, smaller: some depth on the flat shapes.
    std::vector<Vec2> inner = clans::shapeOutline(e.shape, {c.x, c.y - R * 0.04f}, R * 0.5f);
    ui.r().fillConvexPolygon(inner, light.withAlphaF(0.55f));
}

Color clanRoleColor(UIContext& ui, const std::string& role) {
    if (role == "leader") return Color{255, 210, 122, 255};
    if (role == "officer") return Color{201, 179, 255, 255};
    return ui.theme().textDim;
}

}  // namespace cosmic
