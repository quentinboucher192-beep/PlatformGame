#include "game/gameplay/EnemyArrowsDraw.h"

#include <algorithm>
#include <string>
#include <unordered_set>

#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::EnemyArrows {

Targets targets(GameWorld& w, const WorldView& view, const Rect& screen, Tracker& tracker) {
    Targets t;
    // 2.62.2: the enemies of the ambush being fought have arrows of their own (violet), met or
    // not; the others keep theirs (amber, those already met). The boss has its own arrow.
    std::unordered_set<uint32_t> inAmbush;
    if (const CombatZoneEntity* ambush = w.activeAmbush())
        for (const Enemy* e : ambush->fightingEnemies(w)) {
            inAmbush.insert(e->uid());
            if (Rect r = view.toScreen(e->box); !r.intersects(screen)) t.ambush.push_back(r);
        }
    // Who is on screen now (noted whatever the setting: turned on again, the arrows know them).
    std::vector<Tracker::Enemy> seen;
    seen.reserve(w.enemies().size());
    for (const auto& e : w.enemies())
        seen.push_back({e->uid(), view.toScreen(e->box), e->alive() && !e->dormant() && !e->def().isBoss && !inAmbush.count(e->uid())});
    t.usual = tracker.update(screen, seen);
    return t;
}

Layout standardLayout(UIContext& ui, const Rect& screen, size_t maxCount) {
    Layout l;
    l.screen = screen;
    l.arrowSize = ui.s(54);
    l.gap = ui.s(6);
    l.inset = l.arrowSize * 0.5f + ui.s(14);
    l.packGap = ui.s(120);
    l.packSpan = ui.s(360);
    // The count of a pack: its room is that of the largest count possible (every enemy away).
    const float fs = std::max(22.0f, 13.0f / ui.s(1.0f));
    const Vec2 widest = ui.text().measure("×" + std::to_string(std::max<size_t>(1, maxCount)), FontFace::Bold, ui.s(fs));
    l.badgeSize = {widest.x + ui.s(16), std::max(widest.y, ui.s(fs)) + ui.s(6)};
    return l;
}

void draw(UIContext& ui, ResourceManager& res, Layout l, const Targets& t, std::vector<Rect>* areas) {
    if (t.usual.empty() && t.ambush.empty()) return;
    const Look usual = look(false), fight = look(true);
    auto tex = res.texture(usual.texture);
    auto ambushTex = res.texture(fight.texture);
    if (!tex || !ambushTex) return;
    const float W = l.screen.w;
    const float fs = std::max(22.0f, 13.0f / ui.s(1.0f));
    auto set = [&](const std::vector<Rect>& targets, const Texture& arrow, Color badge) {
        for (const Arrow& a : layout(l, targets)) {
            // Steady (the boss arrow blinks), fainter for a pack far away.
            const float distK = std::clamp((a.target - a.pos).length() / (W * 1.2f), 0.0f, 1.0f);
            const float alpha = 1.0f - 0.45f * distK;
            ui.r().drawTexture(arrow, nullptr, a.box, Color::white().withAlphaF(alpha), BlendMode::Alpha, false, false, a.angle * 57.29578f);
            if (a.count > 1) {
                const std::string n = "×" + std::to_string(a.count);
                ui.r().fillRoundedRect(a.badge, a.badge.h * 0.5f, Color{8, 12, 26, 220});
                ui.r().drawRoundedRect(a.badge, a.badge.h * 0.5f, badge.withAlphaF(0.8f * alpha), std::max(1.0f, ui.s(1.5f)));
                ui.label(n, a.badge.center(), ui.fitFont(n, FontFace::Bold, fs, a.badge.w - ui.s(10), 0.6f), badge, Align::Center, FontFace::Bold,
                         VAlign::Middle);
            }
            if (areas) areas->push_back(a.area.expanded(ui.s(6)));  // with room for a frame's move
            l.keepOut.push_back(a.area);                             // the next set never covers it
        }
    };
    set(t.ambush, *ambushTex, fight.count);  // first: the fight under way
    set(t.usual, *tex, usual.count);
}

}  // namespace cosmic::EnemyArrows
