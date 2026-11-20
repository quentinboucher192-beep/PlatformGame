#include "ui/online/RaidBotsUi.h"

#include <algorithm>
#include <cmath>

#include "engine/input/InputManager.h"
#include "engine/rendering/Text.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::botsui {

namespace {

TextStyle style(UIContext& ui, float size, Color c, FontFace f, Align a = Align::Left, VAlign v = VAlign::Middle) {
    TextStyle s;
    s.size = ui.s(size);
    s.color = c;
    s.face = f;
    s.align = a;
    s.valign = v;
    s.shadow = true;
    s.shadowOffset = ui.s(1.0f);
    return s;
}

const Color kOrderGold{255, 210, 122, 255};

}  // namespace

// ------------------------------------------------------------------------------- the bubbles

std::vector<Bubble> layoutThoughts(UIContext& ui, const std::vector<ThoughtView>& in, const std::vector<Rect>& avoid, const std::vector<Rect>& bodies,
                                   const Rect& screen) {
    std::vector<Bubble> out;
    const float margin = ui.s(4), padX = ui.s(7), padY = ui.s(4), gap = ui.s(3), dot = ui.s(7);
    const float maxW = std::min(ui.s(270), screen.w - margin * 2.0f);
    // From left to right (the same order every frame), each one over its player's head, then higher, then beside.
    std::vector<const ThoughtView*> order;
    for (const ThoughtView& t : in) order.push_back(&t);
    std::stable_sort(order.begin(), order.end(), [](const ThoughtView* a, const ThoughtView* b) {
        return a->body.x < b->body.x || (a->body.x == b->body.x && a->seat < b->seat);
    });
    std::vector<Rect> placed;
    for (const ThoughtView* t : order) {
        if (t->intent.empty() && t->role.empty()) continue;
        Bubble b;
        b.seat = t->seat;
        b.color = t->color;
        b.order = t->order;
        // Its first line: the role (bold), the arrow, the intention — the intention shortened to fit.
        b.size1 = 13.0f;
        b.size2 = 12.0f;
        const float roleW = ui.text().measure(t->role, FontFace::Bold, ui.s(b.size1)).x;
        const float arrowW = ui.text().measure(kArrow, FontFace::Body, ui.s(b.size1)).x;
        const float room1 = maxW - padX * 2.0f - dot - ui.s(4) - roleW - arrowW;
        if (room1 < ui.s(30)) continue;
        b.role = t->role;
        b.intent = ui.elide(t->intent, FontFace::Body, b.size1, room1);
        const float intentW = ui.text().measure(b.intent, FontFace::Body, ui.s(b.size1)).x;
        const float h1 = std::max(ui.text().measure(t->role, FontFace::Bold, ui.s(b.size1)).y, ui.text().measure(b.intent, FontFace::Body, ui.s(b.size1)).y);
        float w = dot + ui.s(4) + roleW + arrowW + intentW;
        float h2 = 0.0f;
        if (!t->move.empty()) {
            b.move = ui.elide(t->move, FontFace::Body, b.size2, maxW - padX * 2.0f);
            const Vec2 m = ui.text().measure(b.move, FontFace::Body, ui.s(b.size2));
            w = std::max(w, m.x);
            h2 = m.y;
        }
        const float bw = w + padX * 2.0f, bh = padY * 2.0f + h1 + (h2 > 0.0f ? gap + h2 : 0.0f);
        // Its places, in order: over the head (and over its name), higher, then beside it.
        std::vector<Vec2> tries;
        const float headY = t->body.y - ui.s(20);
        for (int level = 0; level < 5; ++level) tries.push_back({t->body.center().x - bw * 0.5f, headY - bh - static_cast<float>(level) * (bh + gap)});
        for (int level = 0; level < 3; ++level) {
            const float y = headY - bh - static_cast<float>(level) * (bh + gap);
            tries.push_back({t->body.right() + ui.s(10), y + bh * 0.5f});
            tries.push_back({t->body.x - ui.s(10) - bw, y + bh * 0.5f});
        }
        tries.push_back({t->body.center().x - bw * 0.5f, t->body.bottom() + ui.s(8)});  // under it, last
        bool ok = false;
        for (const Vec2& p : tries) {
            Rect r{p.x, p.y, bw, bh};
            r.x = std::clamp(r.x, screen.x + margin, std::max(screen.x + margin, screen.right() - margin - bw));
            if (r.y < screen.y + margin || r.bottom() > screen.bottom() - margin || r.right() > screen.right() - margin + 0.5f) continue;
            bool hit = false;
            for (const Rect& o : placed) hit = hit || o.intersects(r.expanded(ui.s(2)));
            for (const Rect& o : avoid) hit = hit || o.intersects(r);
            for (const Rect& o : bodies) hit = hit || o.expanded(ui.s(2)).intersects(r);
            if (hit) continue;
            b.box = r;
            ok = true;
            break;
        }
        if (!ok) continue;  // no room for it: not drawn (never over anything)
        placed.push_back(b.box);
        out.push_back(std::move(b));
    }
    return out;
}

void drawThoughts(UIContext& ui, const std::vector<Bubble>& bubbles) {
    Renderer& r = ui.r();
    const float padX = ui.s(7), padY = ui.s(4), gap = ui.s(3), dot = ui.s(7);
    for (const Bubble& b : bubbles) {
        r.fillRoundedRect(b.box, ui.s(6), Color{7, 11, 26, 222});
        r.drawRoundedRect(b.box, ui.s(6), (b.order ? kOrderGold : b.color).withAlpha(210), ui.s(b.order ? 1.6f : 1.1f));
        const float h1 = ui.text().measure(b.role, FontFace::Bold, ui.s(b.size1)).y;
        const float cy = b.box.y + padY + h1 * 0.5f;
        float x = b.box.x + padX;
        r.fillRoundedRect({x, cy - dot * 0.5f, dot, dot}, dot * 0.5f, b.color);  // its colour: whose bubble
        x += dot + ui.s(4);
        x += ui.text().draw(b.role, {x, cy}, style(ui, b.size1, b.order ? kOrderGold : b.color, FontFace::Bold)).x;
        x += ui.text().draw(kArrow, {x, cy}, style(ui, b.size1, ui.theme().textDim, FontFace::Body)).x;
        ui.text().draw(b.intent, {x, cy}, style(ui, b.size1, ui.theme().text, FontFace::Body));
        if (!b.move.empty())
            ui.text().draw(b.move, {b.box.x + padX, b.box.y + padY + h1 + gap}, style(ui, b.size2, ui.theme().textDim, FontFace::Body, Align::Left, VAlign::Top));
    }
}

// ------------------------------------------------------------------------------- the order wheel

std::string wheelLabel(WheelItem i, bool thoughtsShown) {
    switch (i) {
        case WheelItem::Follow: return "Suivez-moi";
        case WheelItem::Wait: return "Attendez ici";
        case WheelItem::Objective: return "Tous sur l'objectif";
        case WheelItem::Revive: return "Réanimez-le";
        case WheelItem::Free: return "Libres";
        case WheelItem::Mark: return "Marquer un endroit";
        case WheelItem::Thoughts: return thoughtsShown ? "Masquer les pensées" : "Afficher les pensées";
        default: return "";
    }
}

const char* wheelHelp(WheelItem i) {
    switch (i) {
        case WheelItem::Follow: return "Ils vous suivent et combattent près de vous.";
        case WheelItem::Wait: return "Ils restent là où vous êtes et frappent ce qui passe à leur portée.";
        case WheelItem::Objective: return "Tous sur le mécanisme ou la cible du moment, sans garder de rôle.";
        case WheelItem::Revive: return "Les deux plus proches relèvent le joueur à terre le plus près de vous.";
        case WheelItem::Free: return "Ils reprennent leurs rôles et leur tactique.";
        case WheelItem::Mark: return "Un signal : ils vont frapper, tenir ou actionner ce qui s'y trouve.";
        case WheelItem::Thoughts: return "Les bulles au-dessus d'eux : leur rôle, ce qu'ils font, comment ils se déplacent.";
        default: return "";
    }
}

WheelLayout layoutWheel(UIContext& ui, bool thoughtsShown, int selected, const std::string& keysText) {
    WheelLayout l;
    const float W = ui.screenW(), H = ui.screenH();
    const float itemW = std::min(ui.s(200), W * 0.28f), itemH = ui.s(42);
    // Around an ellipse: wide enough that no two items meet (checked, and opened up until they do not).
    float rx = std::max(itemW * 1.25f, ui.s(240)), ry = std::max(itemH * 3.4f, ui.s(150));
    const Vec2 c{W * 0.5f, H * 0.46f};
    for (int guard = 0; guard < 12; ++guard) {
        l.items.clear();
        for (int k = 0; k < kWheelItems; ++k) {
            const float a = -1.5707963f + static_cast<float>(k) * 6.2831853f / static_cast<float>(kWheelItems);
            const Vec2 p{c.x + std::cos(a) * rx, c.y + std::sin(a) * ry};
            l.items.push_back({p.x - itemW * 0.5f, p.y - itemH * 0.5f, itemW, itemH});
        }
        bool overlap = false;
        for (size_t i = 0; i < l.items.size(); ++i)
            for (size_t j = i + 1; j < l.items.size(); ++j) overlap = overlap || l.items[i].expanded(ui.s(4)).intersects(l.items[j]);
        if (!overlap) break;
        rx *= 1.08f;
        ry *= 1.08f;
    }
    l.itemFont = 16.0f;
    for (int k = 0; k < kWheelItems; ++k)
        l.itemFont = std::min(l.itemFont, ui.fitFont(wheelLabel(static_cast<WheelItem>(k), thoughtsShown), FontFace::Bold, 16.0f, itemW - ui.s(16)));
    // The title over the top item, the help and the keys under the bottom ones, each measured.
    float top = c.y, bottom = c.y, left = c.x, right = c.x;
    for (const Rect& r : l.items) {
        top = std::min(top, r.y);
        bottom = std::max(bottom, r.bottom());
        left = std::min(left, r.x);
        right = std::max(right, r.right());
    }
    const float textW = std::max(right - left, ui.s(420));
    const std::string title = "ORDRES AUX JOUEURS SIMULÉS";
    l.titleFont = ui.fitFont(title, FontFace::Bold, 20.0f, textW);
    const float titleH = ui.text().measure(title, FontFace::Bold, ui.s(l.titleFont)).y;
    l.title = {c.x - textW * 0.5f, top - ui.s(12) - titleH, textW, titleH};
    const std::string help = wheelHelp(static_cast<WheelItem>(std::clamp(selected, 0, kWheelItems - 1)));
    const float helpH = ui.text().measureWrapped(help, FontFace::Body, ui.s(15), textW).y;
    l.help = {c.x - textW * 0.5f, bottom + ui.s(12), textW, helpH};
    const float keysH = ui.text().measureWrapped(keysText, FontFace::Body, ui.s(13), textW).y;
    l.keys = {c.x - textW * 0.5f, l.help.bottom() + ui.s(8), textW, keysH};
    const float x0 = std::min(left, l.title.x) - ui.s(16), x1 = std::max(right, l.title.right()) + ui.s(16);
    l.panel = {x0, l.title.y - ui.s(12), x1 - x0, l.keys.bottom() + ui.s(12) - (l.title.y - ui.s(12))};
    // Kept on the screen (everything moves together).
    const float dx = std::clamp(l.panel.x, ui.s(4), std::max(ui.s(4), W - l.panel.w - ui.s(4))) - l.panel.x;
    const float dy = std::clamp(l.panel.y, ui.s(4), std::max(ui.s(4), H - l.panel.h - ui.s(4))) - l.panel.y;
    for (Rect* r : {&l.panel, &l.title, &l.help, &l.keys}) {
        r->x += dx;
        r->y += dy;
    }
    for (Rect& r : l.items) {
        r.x += dx;
        r.y += dy;
    }
    return l;
}

WheelResult handleWheel(UIContext& ui, const InputManager& in, OrderWheel& w, bool allowed, bool thoughtsShown, const WorldView& view, Vec2 start,
                        float dt) {
    WheelResult res;
    if (!w.open) {
        if (allowed && in.menuPressed(Action::BotOrders)) {
            w.open = true;
            w.marking = false;
        }
        return res;
    }
    if (!allowed) {
        w.open = false;
        w.marking = false;
        res.close = true;
        return res;
    }
    if (w.marking) {
        // The place: the stick or the arrows move it (fast), the mouse puts it where it points.
        Vec2 move{0.0f, 0.0f};
        if (in.held(Action::MenuLeft) || in.held(Action::MoveLeft)) move.x -= 1.0f;
        if (in.held(Action::MenuRight) || in.held(Action::MoveRight)) move.x += 1.0f;
        if (in.held(Action::MenuUp) || in.held(Action::MoveUp)) move.y -= 1.0f;
        if (in.held(Action::MenuDown) || in.held(Action::MoveDown)) move.y += 1.0f;
        if (std::fabs(in.leftStick().x) > 0.25f || std::fabs(in.leftStick().y) > 0.25f) move = in.leftStick();
        w.cursor = w.cursor + move * (420.0f * dt);
        if (in.mouseMovedThisFrame()) w.cursor = view.toWorld(in.mousePos());
        if (in.menuPressed(Action::MenuConfirm) || in.mousePressed(1)) {
            res.order = true;
            res.kind = raid::bots::OrderKind::Mark;
            res.markAt = true;
            res.at = w.cursor;
            w.open = false;
            w.marking = false;
        } else if (in.menuPressed(Action::MenuBack) || in.menuPressed(Action::BotOrders)) {
            w.marking = false;  // back to the wheel
        }
        return res;
    }
    if (in.menuPressed(Action::MenuBack) || in.menuPressed(Action::BotOrders)) {
        w.open = false;
        res.close = true;
        return res;
    }
    // The choice: the stick toward an item, the arrows around the wheel, the mouse over one.
    const WheelLayout l = layoutWheel(ui, thoughtsShown, w.selected, "");
    const Vec2 st = in.leftStick();
    if (st.length() > 0.6f) {
        float best = 1e30f;
        const Vec2 c = l.panel.center();
        for (int k = 0; k < kWheelItems; ++k) {
            Vec2 d = l.items[static_cast<size_t>(k)].center() - c;
            const float len = d.length();
            if (len < 1.0f) continue;
            d = d * (1.0f / len);
            const float score = -(d.x * st.x + d.y * st.y);
            if (score < best) {
                best = score;
                w.selected = k;
            }
        }
    }
    if (in.menuPressed(Action::MenuRight) || in.menuPressed(Action::MenuDown)) w.selected = (w.selected + 1) % kWheelItems;
    if (in.menuPressed(Action::MenuLeft) || in.menuPressed(Action::MenuUp)) w.selected = (w.selected + kWheelItems - 1) % kWheelItems;
    bool clicked = false;
    for (int k = 0; k < kWheelItems; ++k)
        if (l.items[static_cast<size_t>(k)].contains(in.mousePos())) {
            if (in.mouseMovedThisFrame()) w.selected = k;
            if (in.mousePressed(1)) {
                w.selected = k;
                clicked = true;
            }
        }
    if (!clicked && !in.menuPressed(Action::MenuConfirm)) return res;
    const auto item = static_cast<WheelItem>(w.selected);
    switch (item) {
        case WheelItem::Thoughts:
            res.toggleThoughts = true;
            return res;  // the wheel stays open
        case WheelItem::Mark:
            w.marking = true;
            w.cursor = start;
            return res;
        default: break;
    }
    res.order = true;
    res.kind = item == WheelItem::Follow      ? raid::bots::OrderKind::Follow
               : item == WheelItem::Wait      ? raid::bots::OrderKind::Wait
               : item == WheelItem::Objective ? raid::bots::OrderKind::Objective
               : item == WheelItem::Revive    ? raid::bots::OrderKind::Revive
                                              : raid::bots::OrderKind::Free;
    w.open = false;
    return res;
}

void drawWheel(UIContext& ui, const OrderWheel& w, bool thoughtsShown, const WorldView& view, const std::string& keysText) {
    if (!w.open) return;
    Renderer& r = ui.r();
    if (w.marking) {
        // The cursor of the place being marked, and how to place it.
        const Vec2 p = view.toScreen(w.cursor);
        const float s = ui.s(16);
        r.drawLine({p.x - s, p.y}, {p.x + s, p.y}, kOrderGold, ui.s(2));
        r.drawLine({p.x, p.y - s}, {p.x, p.y + s}, kOrderGold, ui.s(2));
        r.drawRoundedRect({p.x - s * 0.6f, p.y - s * 0.6f, s * 1.2f, s * 1.2f}, s * 0.6f, kOrderGold, ui.s(1.5f));
        const std::string t = "Placez le signal : stick, flèches ou souris · Valider / clic : le poser · Retour : la roue";
        const float size = ui.fitFont(t, FontFace::Bold, 16.0f, ui.screenW() - ui.s(40));
        const Vec2 m = ui.text().measure(t, FontFace::Bold, ui.s(size));
        const Rect box{(ui.screenW() - m.x) * 0.5f - ui.s(10), ui.screenH() - m.y - ui.s(60), m.x + ui.s(20), m.y + ui.s(12)};
        r.fillRoundedRect(box, ui.s(6), Color{7, 11, 26, 225});
        ui.text().draw(t, box.center(), style(ui, size, kOrderGold, FontFace::Bold, Align::Center));
        return;
    }
    const WheelLayout l = layoutWheel(ui, thoughtsShown, w.selected, keysText);
    r.fillRoundedRect(l.panel, ui.s(14), Color{7, 11, 26, 215});
    r.drawRoundedRect(l.panel, ui.s(14), ui.theme().border, ui.s(1.2f));
    ui.text().draw("ORDRES AUX JOUEURS SIMULÉS", {l.title.center().x, l.title.y}, style(ui, l.titleFont, kOrderGold, FontFace::Bold, Align::Center, VAlign::Top));
    for (int k = 0; k < kWheelItems; ++k) {
        const Rect& it = l.items[static_cast<size_t>(k)];
        const bool sel = k == w.selected;
        r.fillRoundedRect(it, ui.s(8), sel ? Color{40, 60, 96, 240} : Color{20, 28, 52, 230});
        r.drawRoundedRect(it, ui.s(8), sel ? ui.theme().accent : ui.theme().border, ui.s(sel ? 2.0f : 1.0f));
        ui.text().draw(wheelLabel(static_cast<WheelItem>(k), thoughtsShown), it.center(),
                       style(ui, l.itemFont, sel ? ui.theme().text : ui.theme().textDim, FontFace::Bold, Align::Center));
    }
    ui.text().drawWrapped(wheelHelp(static_cast<WheelItem>(w.selected)), {l.help.x, l.help.y}, l.help.w, style(ui, 15.0f, ui.theme().text, FontFace::Body, Align::Center, VAlign::Top));
    ui.text().drawWrapped(keysText, {l.keys.x, l.keys.y}, l.keys.w, style(ui, 13.0f, ui.theme().textDim, FontFace::Body, Align::Center, VAlign::Top));
}

}  // namespace cosmic::botsui
