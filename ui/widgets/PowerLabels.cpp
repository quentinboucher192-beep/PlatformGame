#include "ui/widgets/PowerLabels.h"

#include <algorithm>
#include <cmath>

#include "engine/physics/PhysicsWorld.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/powers/PowerDefinition.h"
#include "ui/online/RaidWidgets.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

std::vector<PowerLabel> powerLabels(UIContext& ui, const GameWorld& w, const WorldView& view, bool armoryOnly) {
    std::vector<PowerLabel> out;
    struct Tab {
        const PowerUnlockEntity* e;
        Rect box;
    };
    std::vector<Tab> tabs;
    for (const auto& e : w.entities()) {
        auto* t = dynamic_cast<const PowerUnlockEntity*>(e.get());
        if (!t || e->removed) continue;
        if (armoryOnly ? !t->object().prop<bool>("armory", false) : (t->power().empty() || t->disabledByChallenge(w))) continue;
        tabs.push_back({t, e->box});
    }
    if (tabs.empty()) return out;
    // What a name must never cover: the tablets and the ledges (screen).
    std::vector<Rect> solid;
    for (const Tab& t : tabs) solid.push_back(view.toScreen(t.box));
    for (const Collider& col : w.physics().colliders())
        if (col.enabled && !col.dynamic && col.kind == ColliderKind::OneWay) solid.push_back(view.toScreen(col.box));
    std::vector<Rect> taken;
    for (const Tab& t : tabs) {
        const PowerDefinition* def = w.content().power(t.e->power());
        if (!def) continue;
        // Its room: half the way to the nearest other tablet, less that one's ledge (world px).
        float half = 90.0f;
        for (const Tab& o : tabs)
            if (o.e != t.e) half = std::min(half, std::fabs(o.box.center().x - t.box.center().x) - 16.0f - 4.0f);
        half = std::max(half, 12.0f);
        const float room = half * 2.0f * view.scale - ui.s(8);
        PowerLabel l;
        l.power = def->id;
        l.size = ui.fitFont(def->name, FontFace::Bold, 13.0f, room, 0.7f);
        l.name = ui.text().measure(def->name, FontFace::Bold, ui.s(l.size)).x <= room ? def->name : ui.elide(def->name, FontFace::Bold, l.size, room);
        const float nameW = ui.text().measure(l.name, FontFace::Bold, ui.s(l.size)).x;
        const float nameH = ui.s(l.size) * 1.3f, costSize = 11.0f, costH = ui.s(costSize) * 1.35f;
        // Under the name, its icon and its cost when they fit (never an invented value: the definition's).
        const std::string cost = def->energyCost > 0.0f ? std::to_string(static_cast<int>(std::lround(def->energyCost))) + " d'énergie" : "sans énergie";
        const float costW = costH + ui.s(4) + ui.text().measure(cost, FontFace::Body, ui.s(costSize)).x;
        const Vec2 top = view.toScreen({t.box.center().x, t.box.y - 4.0f});
        auto boxOf = [&](bool second) {
            const float bw = std::max(nameW, second ? costW : 0.0f) + ui.s(8);
            const float bh = nameH + (second ? costH : 0.0f) + ui.s(2);
            return Rect{top.x - bw * 0.5f, top.y - bh, bw, bh};
        };
        auto clear = [&](const Rect& b) {
            for (const Rect& o : solid)
                if (o.intersects(b)) return false;
            for (const Rect& o : taken)
                if (o.intersects(b)) return false;
            return true;
        };
        Rect b = boxOf(true);
        const bool second = costW + ui.s(8) <= room && clear(b);
        if (second) {
            l.icon = def->icon;
            l.cost = cost;
        } else {
            b = boxOf(false);
            // Two tablets one above the other: a name never covers another one.
            if (std::any_of(taken.begin(), taken.end(), [&](const Rect& o) { return o.intersects(b); })) continue;
        }
        l.box = b;
        taken.push_back(b);
        out.push_back(std::move(l));
    }
    return out;
}

bool powerLabelsAllowed(UIContext& ui, GameWorld& w, std::vector<Rect>& keepOut) {
    for (const WorldMessage& m : w.messages()) {
        if (m.age >= m.duration) continue;
        if (m.style != WorldMessage::Style::Hint) return false;
        // The hints' line (Hud::drawMessages: centred, 150 px above the bottom, 210 with a boss).
        const float hintY = ui.screenH() - ui.s(w.activeBoss() ? 210 : 150);
        keepOut.push_back({0.0f, hintY - ui.s(45), ui.screenW(), ui.s(90)});
    }
    return true;
}

void drawPowerLabels(UIContext& ui, std::vector<PowerLabel> labels, const std::vector<Rect>& keepOut) {
    using raidui::fit;
    for (const PowerLabel& l : labels) {
        if (l.box.right() < 0.0f || l.box.x > ui.screenW() || l.box.bottom() < 0.0f || l.box.y > ui.screenH()) continue;
        if (std::any_of(keepOut.begin(), keepOut.end(), [&](const Rect& o) { return !o.empty() && o.intersects(l.box); })) continue;
        ui.r().fillRoundedRect(l.box, ui.s(5), Color{8, 10, 24, 175});
        const float nameH = ui.s(l.size) * 1.3f;
        fit(ui, l.name, {l.box.center().x, l.box.y + ui.s(1) + nameH * 0.5f}, l.size, l.box.w - ui.s(6), raidui::kGold, FontFace::Bold, Align::Center);
        if (l.cost.empty()) continue;
        const float costSize = 11.0f, costH = ui.s(costSize) * 1.35f;
        const float cw = costH + ui.s(4) + ui.text().measure(l.cost, FontFace::Body, ui.s(costSize)).x;
        const float x = l.box.center().x - cw * 0.5f, y = l.box.y + ui.s(1) + nameH;
        if (l.icon >= 0) ui.icon("ui/icons_powers.png", l.icon, 32, {x, y, costH, costH});
        fit(ui, l.cost, {x + costH + ui.s(4), y + costH * 0.5f}, costSize, cw - costH - ui.s(4) + ui.s(2), raidui::kDim, FontFace::Body, Align::Left);
    }
}

}  // namespace cosmic
