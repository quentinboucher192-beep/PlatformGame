// Main menu → Succès: every achievement of the game on a spider web. Each category is a
// sector of the web; the rings are the difficulty (1 star outside, 5 stars at the heart);
// threads link an achievement to the ones it opens. A list view shows them all as text.
// Everything shown comes from the player's own saves (game/achievements/Achievements.h).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game/GameServices.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

constexpr float kPi = 3.14159265f;

/// Never below 11.5 pixels (small windows): below, the font loses its spaces.
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

std::string dateFr(const std::string& d) {
    // FileUtil::nowString(): "YYYY-MM-DD HH:MM".
    if (d.size() < 16) return d;
    return d.substr(8, 2) + "/" + d.substr(5, 2) + "/" + d.substr(0, 4) + " à " + d.substr(11, 5);
}

std::string thousands(int64_t v) {
    std::string s = std::to_string(v), out;
    int n = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (n && n % 3 == 0) out.insert(out.begin(), ' ');
        out.insert(out.begin(), *it);
        ++n;
    }
    return out;
}

/// A five-pointed star (filled, or its outline).
void drawStar(Renderer& r, Vec2 c, float radius, Color col, bool filled) {
    Vec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        float a = -kPi / 2.0f + static_cast<float>(i) * kPi / 5.0f;
        float rr = i % 2 == 0 ? radius : radius * 0.45f;
        pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    for (int i = 0; i < 10; ++i) {
        if (filled) r.fillTriangle(c, pts[i], pts[(i + 1) % 10], col);
        else r.drawLine(pts[i], pts[(i + 1) % 10], col, std::max(1.0f, radius * 0.14f));
    }
}

/// `stars` filled stars out of 5, from `left` (returns the width used).
float drawStarRow(Renderer& r, Vec2 left, float size, int stars, Color on, Color off) {
    float step = size * 1.15f;
    for (int i = 0; i < 5; ++i) drawStar(r, {left.x + size * 0.5f + i * step, left.y}, size * 0.5f, i < stars ? on : off, i < stars);
    return step * 4.0f + size;
}

class AchievementsScene : public Scene {
public:
    explicit AchievementsScene(GameServices& g) : g_(g) {}

    void onEnter() override {
        bg_.init(g_);
        g_.checkAchievements();
        inputs_ = g_.achievementInputs();
        const auto& all = g_.achievementCatalog.achievements();
        // First focus: the first achievement still to get (or the first one).
        for (size_t i = 0; i < all.size(); ++i)
            if (!g_.achievements.unlocked(all[i].id) && g_.achievements.available(all[i])) {
                selected_ = static_cast<int>(i);
                break;
            }
        // Automated screenshots: "ach:<id>" selects it, "ach-list" opens the list.
        if (g_.onlineOpen.rfind("ach:", 0) == 0) {
            std::string id = g_.onlineOpen.substr(4);
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].id == id) selected_ = static_cast<int>(i);
            g_.onlineOpen.clear();
        } else if (g_.onlineOpen.rfind("ach-zoom:", 0) == 0) {
            std::string id = g_.onlineOpen.substr(9);
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].id == id) selected_ = static_cast<int>(i);
            zoom_ = 2.4f;
            centerOn_ = selected_;
            g_.onlineOpen.clear();
        } else if (g_.onlineOpen == "ach-list") {
            view_ = 1;
            g_.onlineOpen.clear();
        }
        g_.ui.setFocus("ach/n" + std::to_string(selected_));
    }

    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
        refresh_ -= dt;
        if (refresh_ <= 0.0f) {  // the counters move only between runs; a slow refresh is enough
            inputs_ = g_.achievementInputs();
            refresh_ = 2.0f;
        }
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.7f);
        ui.pushId("ach");
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(48);
        const AchievementCatalog& cat = g_.achievementCatalog;
        ui.label("SUCCÈS", {m, ui.s(52)}, 40, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        UIContext::ButtonStyle bs;
        bs.small = true;
        Rect back{W - m - ui.s(180), ui.s(30), ui.s(180), ui.s(46)};
        bool leave = ui.button("back", back, "Retour", bs);
        Rect tabs{back.x - ui.s(20) - ui.s(300), ui.s(30), ui.s(300), ui.s(46)};
        int view = view_;
        if (ui.tabs("view", tabs, {"Toile", "Liste"}, view)) view_ = view;
        drawSummary({m, ui.s(96), W - 2 * m, ui.s(50)});
        if (cat.achievements().empty()) {
            ui.label("Le catalogue des succès est introuvable (data/achievements).", {W * 0.5f, H * 0.5f}, 22, ui.theme().danger,
                     Align::Center, FontFace::Body, VAlign::Middle);
        } else {
            float top = ui.s(166), bottom = H - ui.s(62);
            if (view_ == 0) {
                float side = std::min(bottom - top, W * 0.56f);
                Rect web{m, top, side, bottom - top};
                drawWeb(web);
                drawDetail({web.right() + ui.s(24), top, W - m - web.right() - ui.s(24), bottom - top});
            } else {
                drawList({m, top, W - 2 * m, bottom - top});
            }
            // Category jumps (LB / RB, Q / E).
            InputManager& in = g_.app.input();
            if (ui.acceptsInput() && view_ == 0 && (in.menuPressed(Action::MenuTabPrev) || in.menuPressed(Action::MenuTabNext)))
                jumpCategory(in.menuPressed(Action::MenuTabNext) ? 1 : -1);
        }
        // Footer hints.
        float fy = H - ui.s(30), fx = m;
        fx += Glyphs::drawHint(ui, Action::MenuConfirm, "Choisir", {fx, fy}, ui.s(30)) + ui.s(36);
        if (view_ == 0) fx += Glyphs::drawHint(ui, Action::MenuTabNext, "Catégorie suivante", {fx, fy}, ui.s(30)) + ui.s(36);
        fx += Glyphs::drawHint(ui, Action::MenuBack, "Retour", {fx, fy}, ui.s(30)) + ui.s(36);
        if (view_ == 0 && !cat.achievements().empty()) drawZoomControls(fx, fy);
        if (leave || ui.backPressed()) g_.app.switchScene(makeMainMenu(g_), 0.3f);
        ui.popId();
        ui.end();
    }

private:
    const AchievementDef& sel() const {
        const auto& all = g_.achievementCatalog.achievements();
        return all[static_cast<size_t>(std::clamp(selected_, 0, static_cast<int>(all.size()) - 1))];
    }
    Color colorOf(const std::string& category) const {
        const AchievementCategory* c = g_.achievementCatalog.category(category);
        return c ? c->color : g_.ui.theme().accent;
    }

    void drawSummary(const Rect& r) {
        UIContext& ui = g_.ui;
        const AchievementCatalog& cat = g_.achievementCatalog;
        int stars = g_.achievements.stars(cat), total = cat.totalStars();
        int got = 0, full = 0;
        for (const auto& a : cat.achievements()) got += g_.achievements.unlocked(a.id) ? 1 : 0;
        for (const auto& c : cat.categories()) {
            bool all = true;
            for (const auto& a : cat.achievements())
                if (a.category == c.id && !g_.achievements.unlocked(a.id)) all = false;
            full += all ? 1 : 0;
        }
        struct Chip {
            std::string label, value;
            Color c;
        };
        Chip chips[3] = {{"Étoiles", std::to_string(stars) + " / " + std::to_string(total), Color{255, 214, 90, 255}},
                         {"Succès obtenus", std::to_string(got) + " / " + std::to_string(cat.achievements().size()), ui.theme().accent},
                         {"Catégories complètes", std::to_string(full) + " / " + std::to_string(cat.categories().size()), ui.theme().success}};
        float gap = ui.s(14), cw = (r.w - gap * 2) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            Rect c{r.x + i * (cw + gap), r.y, cw, r.h};
            ui.r().fillRoundedRect(c, ui.s(10), Color{10, 16, 34, 220});
            ui.r().drawRoundedRect(c, ui.s(10), chips[i].c.withAlphaF(0.5f), std::max(1.0f, ui.s(1.2f)));
            float x = c.x + ui.s(14);
            if (i == 0) {
                drawStar(ui.r(), {x + ui.s(11), c.center().y}, ui.s(11), chips[i].c, true);
                x += ui.s(30);
            }
            float lw = ui.fitFont(chips[i].label, FontFace::Body, 16, cw * 0.5f);
            ui.label(chips[i].label, {x, c.center().y}, lw, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(chips[i].value, {c.right() - ui.s(14), c.center().y}, ui.fitFont(chips[i].value, FontFace::Bold, 24, cw * 0.38f),
                     chips[i].c, Align::Right, FontFace::Bold, VAlign::Middle);
        }
    }

    // ------------------------------------------------------------ the web

    struct Node {
        Vec2 p;
        float r = 8.0f;
    };

    /// Places every achievement: its category's sector (width by its number of achievements),
    /// its ring by its stars (1 star outside, 5 stars next to the heart).
    void layout(Vec2 c, float R, float hub) {
        const AchievementCatalog& cat = g_.achievementCatalog;
        const auto& all = cat.achievements();
        nodes_.assign(all.size(), Node{});
        sectors_.clear();
        const float legend = 0.20f;  // radians kept free at the top for the rings' legend
        float total = 0.0f;
        std::vector<int> counts;
        for (const auto& k : cat.categories()) {
            int n = 0;
            for (const auto& a : all) n += a.category == k.id ? 1 : 0;
            counts.push_back(n);
            total += static_cast<float>(n) + 1.2f;  // + a gap between sectors
        }
        float a = -kPi / 2.0f + legend * 0.5f;
        float span = 2.0f * kPi - legend;
        float band = (R - hub) / 5.0f;
        for (size_t ci = 0; ci < cat.categories().size(); ++ci) {
            float w = span * (static_cast<float>(counts[ci]) + 1.2f) / total;
            Sector s{a + w * 0.6f / (static_cast<float>(counts[ci]) + 1.2f), a + w - w * 0.6f / (static_cast<float>(counts[ci]) + 1.2f), a, a + w};
            sectors_.push_back(s);
            const std::string& id = cat.categories()[ci].id;
            for (int st = 1; st <= 5; ++st) {
                std::vector<size_t> ring;
                for (size_t i = 0; i < all.size(); ++i)
                    if (all[i].category == id && all[i].stars == st) ring.push_back(i);
                if (ring.empty()) continue;
                // Ring of `st` stars: the outermost for 1 star.
                float mid = hub + band * (5.0f - static_cast<float>(st)) + band * 0.5f;
                float arc = (s.a1 - s.a0) * mid / static_cast<float>(ring.size());
                bool zig = arc < g_.ui.s(30);
                for (size_t k = 0; k < ring.size(); ++k) {
                    float ang = s.a0 + (s.a1 - s.a0) * (static_cast<float>(k) + 0.5f) / static_cast<float>(ring.size());
                    float rr = mid + (zig ? (k % 2 ? band * 0.24f : -band * 0.24f) : 0.0f);
                    Node& n = nodes_[ring[k]];
                    n.p = {c.x + std::cos(ang) * rr, c.y + std::sin(ang) * rr};
                    n.r = std::clamp(std::min(arc * (zig ? 0.62f : 0.4f), band * 0.3f), g_.ui.s(6), g_.ui.s(12) * std::min(zoom_, 2.2f));
                }
            }
            a += w;
        }
    }

    void thread(Vec2 c, float r0, float a0, float a1, Color col, float thick) {
        // A web thread between two spokes, sagging a little towards the centre.
        Vec2 prev{c.x + std::cos(a0) * r0, c.y + std::sin(a0) * r0};
        for (int i = 1; i <= 8; ++i) {
            float t = static_cast<float>(i) / 8.0f;
            float ang = a0 + (a1 - a0) * t;
            float sag = 1.0f - 0.07f * std::sin(t * kPi);
            Vec2 p{c.x + std::cos(ang) * r0 * sag, c.y + std::sin(ang) * r0 * sag};
            g_.ui.r().drawLine(prev, p, col, thick);
            prev = p;
        }
    }

    /// Zoom around a screen point (the cursor, or the web's centre): that point stays put.
    void zoomAt(float newZoom, Vec2 at, const Rect& area) {
        newZoom = std::clamp(newZoom, kMinZoom, kMaxZoom);
        Vec2 c0 = area.center();
        Vec2 q = (at - c0 - pan_) / zoom_;
        zoom_ = newZoom;
        pan_ = at - c0 - q * zoom_;
    }

    void clampPan(float R0) {
        float lim = R0 * zoom_;
        if (zoom_ <= 1.0f) lim = 0.0f;
        pan_.x = std::clamp(pan_.x, -lim, lim);
        pan_.y = std::clamp(pan_.y, -lim, lim);
    }

    void webInput(const Rect& area, float R0) {
        UIContext& ui = g_.ui;
        InputManager& in = g_.app.input();
        if (!ui.acceptsInput()) return;
        Vec2 m = in.mousePos();
        bool over = area.contains(m);
        if (over && in.wheel() != 0.0f) zoomAt(zoom_ * (in.wheel() > 0.0f ? 1.18f : 1.0f / 1.18f), m, area);
        if (in.keyPressed(SDL_SCANCODE_EQUALS) || in.keyPressed(SDL_SCANCODE_KP_PLUS)) zoomAt(zoom_ * 1.25f, area.center(), area);
        if (in.keyPressed(SDL_SCANCODE_MINUS) || in.keyPressed(SDL_SCANCODE_KP_MINUS)) zoomAt(zoom_ / 1.25f, area.center(), area);
        // Drag to move the web (from anywhere on it).
        if (over && in.mousePressed(SDL_BUTTON_LEFT)) {
            dragging_ = true;
            dragMoved_ = 0.0f;
            dragLast_ = m;
        }
        if (dragging_) {
            if (!in.mouseDown(SDL_BUTTON_LEFT)) {
                dragging_ = false;
            } else {
                Vec2 d = m - dragLast_;
                dragMoved_ += std::abs(d.x) + std::abs(d.y);
                if (dragMoved_ > ui.s(6)) pan_ = pan_ + d;
                dragLast_ = m;
            }
        }
        clampPan(R0);
    }

    void drawWeb(const Rect& area) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        const AchievementCatalog& cat = g_.achievementCatalog;
        const auto& all = cat.achievements();
        const float labelRoom = ui.s(88);  // the category names around the web
        const float R0 = std::min(area.w, area.h) * 0.5f - labelRoom;
        webInput(area, R0);
        float R = R0 * zoom_;
        Vec2 c = area.center() + pan_;
        float hub = R * 0.17f;
        layout(c, R, hub);
        // A node chosen with the keyboard, the gamepad or the list: brought into view.
        if (centerOn_ >= 0 && centerOn_ < static_cast<int>(nodes_.size())) {
            Vec2 p = nodes_[static_cast<size_t>(centerOn_)].p;
            if (!area.expanded(-ui.s(40)).contains(p)) {
                pan_ = pan_ + (area.center() - p);
                clampPan(R0);
                c = area.center() + pan_;
                layout(c, R, hub);
            }
            centerOn_ = -1;
        }
        r.pushClip(area);
        float band = (R - hub) / 5.0f;
        r.drawGlow(c, R * 1.1f, Color{40, 70, 140, 40}, BlendMode::Additive);
        r.fillCircle(c, R + ui.s(6), Color{6, 10, 24, 150});
        // Spokes (sector edges and centres) and the rings' threads.
        std::vector<float> spokes;
        for (const Sector& s : sectors_) {
            spokes.push_back(s.e0);
            spokes.push_back((s.a0 + s.a1) * 0.5f);
        }
        spokes.push_back(-kPi / 2.0f + 2.0f * kPi);  // closes at the top (legend gap)
        Color dim{170, 190, 230, 40};
        for (float a : spokes) r.drawLine({c.x + std::cos(a) * hub, c.y + std::sin(a) * hub}, {c.x + std::cos(a) * R, c.y + std::sin(a) * R}, dim, ui.s(1));
        for (int k = 0; k <= 5; ++k) {
            float rr = hub + band * static_cast<float>(k);
            for (size_t i = 0; i + 1 < spokes.size(); ++i) thread(c, rr, spokes[i], spokes[i + 1], dim, ui.s(1));
        }
        // Each category's spoke in its colour, brighter as it fills up.
        for (size_t ci = 0; ci < sectors_.size(); ++ci) {
            const AchievementCategory& k = cat.categories()[ci];
            int have = g_.achievements.stars(cat, k.id), tot = 0;
            for (const auto& a : all) tot += a.category == k.id ? a.stars : 0;
            float f = tot > 0 ? static_cast<float>(have) / static_cast<float>(tot) : 0.0f;
            float a = (sectors_[ci].a0 + sectors_[ci].a1) * 0.5f;
            r.drawLine({c.x + std::cos(a) * hub, c.y + std::sin(a) * hub}, {c.x + std::cos(a) * R, c.y + std::sin(a) * R},
                       k.color.withAlpha(static_cast<uint8_t>(50 + 150 * f)), ui.s(1.5f));
        }
        // The rings' legend in the free space at the top: 5 stars at the heart, 1 outside.
        for (int st = 1; st <= 5; ++st) {
            float mid = hub + band * (5.0f - static_cast<float>(st)) + band * 0.5f;
            Vec2 p{c.x, c.y - mid};
            float sz = std::min(ui.s(9), band * 0.22f);
            for (int i = 0; i < st; ++i) drawStar(r, {p.x + (static_cast<float>(i) - (st - 1) * 0.5f) * sz * 0.95f, p.y}, sz * 0.5f, Color{255, 214, 90, 150}, true);
        }
        // Threads between an achievement and the ones it opens.
        for (size_t i = 0; i < all.size(); ++i)
            for (const std::string& req : all[i].after) {
                const AchievementDef* d = cat.find(req);
                if (!d) continue;
                size_t j = static_cast<size_t>(d - all.data());
                bool lit = g_.achievements.unlocked(all[i].id) && g_.achievements.unlocked(req);
                Color col = colorOf(all[i].category);
                r.drawLine(nodes_[j].p, nodes_[i].p, lit ? col.withAlpha(200) : col.withAlpha(55), lit ? ui.s(2.2f) : ui.s(1.1f));
            }
        // Nodes.
        for (size_t i = 0; i < all.size(); ++i) {
            const AchievementDef& a = all[i];
            const Node& n = nodes_[i];
            Color col = colorOf(a.category);
            std::string id = "n" + std::to_string(i);
            Rect hit = Rect::fromCenter(n.p, {std::max(n.r * 2.4f, ui.s(18)), std::max(n.r * 2.4f, ui.s(18))});
            // Only the nodes in view can be pointed at or reached (a drag never selects).
            if (area.contains(n.p) && !(dragging_ && dragMoved_ > ui.s(6))) ui.hotspot(id, hit);
            if (ui.focused(id) && selected_ != static_cast<int>(i)) {
                selected_ = static_cast<int>(i);
                if (!g_.ui.usingPointer()) centerOn_ = selected_;
            }
            bool done = g_.achievements.unlocked(a.id);
            bool open = g_.achievements.available(a);
            if (done) {
                r.drawGlow(n.p, n.r * 2.8f, col.withAlpha(90), BlendMode::Additive);
                r.fillCircle(n.p, n.r, col);
                r.fillCircle(n.p - Vec2{n.r * 0.3f, n.r * 0.3f}, n.r * 0.35f, Color{255, 255, 255, 110});
            } else if (open) {
                r.fillCircle(n.p, n.r, Color{12, 18, 38, 255});
                r.drawCircle(n.p, n.r, col.withAlpha(200), ui.s(1.6f));
                AchievementProgress p = AchievementBook::progress(a, inputs_);
                float f = std::clamp(static_cast<float>(p.value) / static_cast<float>(std::max<int64_t>(1, p.target)), 0.0f, 1.0f);
                if (f > 0.0f) r.drawArc(n.p, n.r - ui.s(2.5f), ui.s(2.2f), -kPi / 2.0f, -kPi / 2.0f + 2.0f * kPi * f, col);
            } else {
                r.fillCircle(n.p, n.r, Color{10, 12, 24, 255});
                r.drawCircle(n.p, n.r, Color{120, 130, 160, 90}, ui.s(1.2f));
            }
            if (static_cast<int>(i) == selected_) {
                float pulse = 1.0f + 0.12f * std::sin(t_ * 6.0f);
                r.drawCircle(n.p, (n.r + ui.s(5)) * pulse, Color::white(), ui.s(2));
            }
        }
        // The heart: the stars obtained.
        r.fillCircle(c, hub * 0.92f, Color{14, 20, 44, 240});
        r.drawCircle(c, hub * 0.92f, Color{255, 214, 90, 160}, ui.s(2));
        int stars = g_.achievements.stars(cat);
        std::string sv = std::to_string(stars);
        ui.label(sv, {c.x, c.y - hub * 0.12f}, ui.fitFont(sv, FontFace::Title, 30, hub * 1.4f), Color{255, 214, 90, 255}, Align::Center,
                 FontFace::Title, VAlign::Middle);
        std::string of = "sur " + std::to_string(cat.totalStars());
        ui.label(of, {c.x, c.y + hub * 0.42f}, ui.fitFont(of, FontFace::Body, 13, hub * 1.4f), ui.theme().textDim, Align::Center, FontFace::Body,
                 VAlign::Middle);
        // Zoomed in: each achievement's name next to its node (when there is room for it).
        std::vector<Rect> placed;
        if (zoom_ >= 1.7f) {
            for (const Node& n : nodes_) placed.push_back(Rect::fromCenter(n.p, {n.r * 2.0f, n.r * 2.0f}));
            placed.push_back(Rect::fromCenter(c, {hub * 2.0f, hub * 2.0f}));
            for (size_t i = 0; i < all.size(); ++i) {
                const Node& n = nodes_[i];
                if (!area.contains(n.p)) continue;
                const float fs = std::max(13.0f, 11.5f / ui.s(1.0f));  // never below 11.5 px
                Vec2 sz = ui.text().measure(all[i].name, FontFace::Body, ui.s(fs));
                Rect cand[4] = {{n.p.x - sz.x * 0.5f, n.p.y + n.r + ui.s(3), sz.x, sz.y},
                                {n.p.x - sz.x * 0.5f, n.p.y - n.r - ui.s(3) - sz.y, sz.x, sz.y},
                                {n.p.x + n.r + ui.s(4), n.p.y - sz.y * 0.5f, sz.x, sz.y},
                                {n.p.x - n.r - ui.s(4) - sz.x, n.p.y - sz.y * 0.5f, sz.x, sz.y}};
                for (const Rect& b : cand) {
                    if (!area.containsRect(b)) continue;
                    bool hit = false;
                    for (const Rect& o : placed)
                        if (o.intersects(b)) {
                            hit = true;
                            break;
                        }
                    if (hit) continue;
                    placed.push_back(b);
                    r.fillRoundedRect(b.expanded(ui.s(2)), ui.s(3), Color{6, 10, 24, 190});
                    bool done = g_.achievements.unlocked(all[i].id);
                    ui.label(all[i].name, b.pos(), fs, done ? colorOf(all[i].category) : ui.theme().textDim, Align::Left, FontFace::Body);
                    break;
                }
            }
        }
        // Category names around the web, kept apart.
        for (size_t ci = 0; ci < sectors_.size(); ++ci) {
            const AchievementCategory& k = cat.categories()[ci];
            float a = (sectors_[ci].a0 + sectors_[ci].a1) * 0.5f;
            int have = g_.achievements.stars(cat, k.id), tot = 0;
            for (const auto& x : all) tot += x.category == k.id ? x.stars : 0;
            std::string sub = std::to_string(have) + " / " + std::to_string(tot);
            float fs = 16.0f;
            Vec2 sz = ui.text().measure(k.name, FontFace::Bold, ui.s(fs));
            Vec2 sz2 = ui.text().measure(sub, FontFace::Body, ui.s(fz(ui, 13)));
            float bw = std::max(sz.x, sz2.x + ui.s(16)), bh = sz.y + sz2.y;
            float rr = R + ui.s(12);
            Rect box;
            for (int tries = 0; tries < 6; ++tries, rr += ui.s(10)) {
                Vec2 p{c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
                float cx = std::cos(a);
                float x = cx > 0.3f ? p.x : cx < -0.3f ? p.x - bw : p.x - bw * 0.5f;
                float y = std::sin(a) < -0.3f ? p.y - bh : std::sin(a) > 0.3f ? p.y : p.y - bh * 0.5f;
                // Inside the web's area (at 100 %: never cut by its edges).
                if (zoom_ <= 1.0f) {
                    x = std::clamp(x, area.x, std::max(area.x, area.right() - bw));
                    y = std::clamp(y, area.y, std::max(area.y, area.bottom() - bh));
                }
                box = {x, y, bw, bh};
                bool hit = false;
                for (const Rect& o : placed) hit = hit || o.intersects(box);
                if (!hit) break;
            }
            placed.push_back(box);
            bool on = ci == static_cast<size_t>(categoryIndex(sel().category));
            ui.label(k.name, {box.x + box.w * 0.5f, box.y}, fs, on ? k.color : k.color.withAlpha(200), Align::Center, FontFace::Bold);
            drawStar(ui.r(), {box.x + (box.w - sz2.x - ui.s(16)) * 0.5f + ui.s(6), box.y + sz.y + sz2.y * 0.5f}, ui.s(6), Color{255, 214, 90, 200}, true);
            ui.label(sub, {box.x + (box.w - sz2.x - ui.s(16)) * 0.5f + ui.s(16), box.y + sz.y}, fz(ui, 13), ui.theme().textDim, Align::Left);
        }
        r.popClip();
        webArea_ = area;
    }

    /// Zoom controls under the web (mouse, keyboard and gamepad), from `x` on the footer line.
    void drawZoomControls(float x, float y) {
        UIContext& ui = g_.ui;
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = 22;
        float h = ui.s(40), bw = ui.s(44);
        if (ui.button("zoomout", {x, y - h * 0.5f, bw, h}, "−", st)) zoomAt(zoom_ / 1.25f, webArea_.center(), webArea_);
        x += bw + ui.s(8);
        char pct[16];
        std::snprintf(pct, sizeof pct, "%d %%", static_cast<int>(std::lround(zoom_ * 100.0f)));
        ui.label(pct, {x + ui.s(36), y}, fz(ui, 17), ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        x += ui.s(72) + ui.s(8);
        if (ui.button("zoomin", {x, y - h * 0.5f, bw, h}, "+", st)) zoomAt(zoom_ * 1.25f, webArea_.center(), webArea_);
        x += bw + ui.s(10);
        UIContext::ButtonStyle rs;
        rs.small = true;
        rs.fontSize = 17;
        rs.enabled = zoom_ != 1.0f || pan_.x != 0.0f || pan_.y != 0.0f;
        if (ui.button("zoomreset", {x, y - h * 0.5f, ui.s(130), h}, "Recentrer", rs)) {
            zoom_ = 1.0f;
            pan_ = {};
        }
    }

    int categoryIndex(const std::string& id) const {
        const auto& cs = g_.achievementCatalog.categories();
        for (size_t i = 0; i < cs.size(); ++i)
            if (cs[i].id == id) return static_cast<int>(i);
        return 0;
    }

    void jumpCategory(int dir) {
        const auto& cs = g_.achievementCatalog.categories();
        const auto& all = g_.achievementCatalog.achievements();
        int ci = (categoryIndex(sel().category) + dir + static_cast<int>(cs.size())) % static_cast<int>(cs.size());
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].category == cs[static_cast<size_t>(ci)].id) {
                selected_ = static_cast<int>(i);
                centerOn_ = selected_;
                g_.ui.setFocus("ach/n" + std::to_string(i));
                break;
            }
    }

    std::string statusText(const AchievementDef& a, Color& col) const {
        const UITheme& th = g_.ui.theme();
        if (g_.achievements.unlocked(a.id)) {
            col = th.success;
            std::string d = g_.achievements.unlockDate(a.id);
            return d.empty() ? "Obtenu" : "Obtenu le " + dateFr(d);
        }
        if (!g_.achievements.available(a)) {
            col = th.textDim;
            std::string need;
            for (const auto& r : a.after)
                if (!g_.achievements.unlocked(r))
                    if (const AchievementDef* d = g_.achievementCatalog.find(r)) need += (need.empty() ? "« " : ", « ") + d->name + " »";
            return "Obtenir d'abord " + need;
        }
        col = th.text;
        AchievementProgress p = AchievementBook::progress(a, inputs_);
        if (a.condition.type == "playtime")
            return playTimeText(static_cast<double>(p.value)) + " / " + playTimeText(static_cast<double>(p.target));
        return thousands(std::min(p.value, p.target)) + " / " + thousands(p.target);
    }

    void drawDetail(const Rect& p) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const AchievementCatalog& cat = g_.achievementCatalog;
        const AchievementDef& a = sel();
        const AchievementCategory* k = cat.category(a.category);
        Color col = k ? k->color : th.accent;
        ui.panel(p, true, 16);
        float x = p.x + ui.s(22), w = p.w - ui.s(44), y = p.y + ui.s(18);
        // The category.
        int have = g_.achievements.stars(cat, a.category), tot = 0, got = 0, n = 0;
        for (const auto& x2 : cat.achievements())
            if (x2.category == a.category) {
                tot += x2.stars;
                ++n;
                got += g_.achievements.unlocked(x2.id) ? 1 : 0;
            }
        std::string right = std::to_string(have) + " / " + std::to_string(tot) + " étoiles  ·  " + std::to_string(got) + " / " + std::to_string(n);
        float rw = ui.text().measure(right, FontFace::Body, ui.s(16)).x;
        r().fillCircle({x + ui.s(7), y + ui.s(13)}, ui.s(7), col);
        std::string cname = k ? k->name : a.category;
        ui.label(ui.elide(cname, FontFace::Bold, 22, w - rw - ui.s(40)), {x + ui.s(22), y}, 22, col, Align::Left, FontFace::Bold);
        ui.label(right, {x + w, y + ui.s(3)}, fz(ui, 16), th.textDim, Align::Right);
        y += ui.s(34);
        if (k) y += ui.labelWrapped(k->description, {x, y}, w, std::max(15.0f, 11.5f / ui.s(1.0f)), th.textDim).y + ui.s(10);
        ui.separator({x, y}, w);
        y += ui.s(14);
        // The achievement.
        float nf = ui.fitFont(a.name, FontFace::Title, 30, w, 0.6f);
        ui.label(a.name, {x, y}, nf, th.text, Align::Left, FontFace::Title);
        y += ui.s(44);
        drawStarRow(ui.r(), {x, y + ui.s(10)}, ui.s(20), a.stars, Color{255, 214, 90, 255}, Color{120, 130, 160, 160});
        std::string diff = a.stars == 1 ? "Facile" : a.stars == 2 ? "Moyen" : a.stars == 3 ? "Difficile" : a.stars == 4 ? "Très difficile" : "Extrême";
        ui.label(diff, {x + ui.s(132), y + ui.s(10)}, fz(ui, 16), th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        y += ui.s(32);
        y += ui.labelWrapped(a.description, {x, y}, w, 19, th.text).y + ui.s(12);
        Color sc;
        std::string status = statusText(a, sc);
        bool showBar = !g_.achievements.unlocked(a.id) && g_.achievements.available(a);
        if (showBar) {
            AchievementProgress pr = AchievementBook::progress(a, inputs_);
            float f = std::clamp(static_cast<float>(pr.value) / static_cast<float>(std::max<int64_t>(1, pr.target)), 0.0f, 1.0f);
            Rect bar{x, y, w * 0.55f, ui.s(12)};
            ui.r().fillRoundedRect(bar, ui.s(6), Color{20, 28, 52, 255});
            if (f > 0.0f) ui.r().fillRoundedRect({bar.x, bar.y, std::max(bar.h, bar.w * f), bar.h}, ui.s(6), col);
            ui.label(ui.elide(status, FontFace::Bold, fz(ui, 17), w * 0.42f), {bar.right() + ui.s(14), bar.center().y}, fz(ui, 17), sc, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            y += ui.s(30);
        } else {
            y += ui.labelWrapped(status, {x, y}, w, 17, sc).y + ui.s(10);
        }
        // What it opens.
        std::string opens;
        for (const auto& o : cat.achievements())
            for (const auto& r2 : o.after)
                if (r2 == a.id) opens += (opens.empty() ? "« " : ", « ") + o.name + " »";
        if (!opens.empty()) y += ui.labelWrapped("Ouvre la voie à " + opens, {x, y}, w, fz(ui, 15), th.textDim).y + ui.s(8);
        ui.separator({x, y + ui.s(4)}, w);
        y += ui.s(16);
        // The category's achievements.
        Rect list{x - ui.s(6), y, w + ui.s(12), p.bottom() - y - ui.s(12)};
        if (list.h < ui.s(60)) return;
        std::vector<size_t> rows;
        const auto& all = cat.achievements();
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].category == a.category) rows.push_back(i);
        float rh = ui.s(34);
        ui.beginScroll("catlist", list, static_cast<float>(rows.size()) * rh);
        float ry = list.y - ui.scrollOffset();
        for (size_t i : rows) {
            Rect row{list.x, ry, list.w - ui.s(14), rh - ui.s(4)};
            ry += rh;
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            bool hovered = false;
            if (ui.mouseOver(row) && ui.acceptsInput() && g_.app.input().mousePressed(SDL_BUTTON_LEFT)) {
                selected_ = static_cast<int>(i);
                centerOn_ = selected_;
                ui.setFocus("ach/n" + std::to_string(i));
            }
            hovered = static_cast<int>(i) == selected_;
            if (hovered) ui.r().fillRoundedRect(row, ui.s(6), col.withAlpha(36));
            bool done = g_.achievements.unlocked(all[i].id);
            Vec2 dot{row.x + ui.s(14), row.center().y};
            if (done) ui.r().fillCircle(dot, ui.s(6), col);
            else ui.r().drawCircle(dot, ui.s(6), g_.achievements.available(all[i]) ? col : Color{120, 130, 160, 120}, ui.s(1.5f));
            float starsW = ui.s(14) * 1.15f * 4.0f + ui.s(14);
            float nameRoom = row.w - ui.s(34) - starsW - ui.s(16);
            ui.label(ui.elide(all[i].name, FontFace::Body, fz(ui, 16), nameRoom), {row.x + ui.s(30), row.center().y}, 16,
                     done ? th.text : th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
            drawStarRow(ui.r(), {row.right() - starsW - ui.s(6), row.center().y}, ui.s(14), all[i].stars, Color{255, 214, 90, done ? 255 : 150},
                        Color{90, 100, 130, 110});
        }
        ui.endScroll();
    }

    Renderer& r() { return g_.ui.r(); }

    // ------------------------------------------------------------ list view

    void drawList(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const AchievementCatalog& cat = g_.achievementCatalog;
        const auto& all = cat.achievements();
        float rh = ui.s(40), hh = ui.s(52);
        float content = 0.0f;
        for (const auto& k : cat.categories()) {
            content += hh;
            for (const auto& a : all) content += a.category == k.id ? rh : 0.0f;
        }
        ui.beginScroll("all", area, content);
        float y = area.y - ui.scrollOffset();
        float w = area.w - ui.s(16);
        float starsW = ui.s(14) * 1.15f * 4.0f + ui.s(14);
        float cName = area.x + ui.s(34), cDesc = area.x + w * 0.27f, cProg = area.x + w * 0.70f, cStars = area.x + w - starsW;
        for (const auto& k : cat.categories()) {
            if (y + hh >= area.y && y <= area.bottom()) {
                ui.r().fillRect({area.x, y + hh - ui.s(8), w, std::max(1.0f, ui.s(1.5f))}, k.color.withAlpha(120));
                int have = g_.achievements.stars(cat, k.id), tot = 0;
                for (const auto& a : all) tot += a.category == k.id ? a.stars : 0;
                ui.label(k.name, {area.x, y + ui.s(26)}, 22, k.color, Align::Left, FontFace::Bold, VAlign::Middle);
                std::string s = std::to_string(have) + " / " + std::to_string(tot) + " étoiles";
                ui.label(s, {area.x + w, y + ui.s(26)}, fz(ui, 16), th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
                float nw = ui.text().measure(k.name, FontFace::Bold, ui.s(22)).x;
                float sw = ui.text().measure(s, FontFace::Body, ui.s(16)).x;
                float dr = w - nw - sw - ui.s(60);
                if (dr > ui.s(120))
                    ui.label(ui.elide(k.description, FontFace::Body, fz(ui, 15), dr), {area.x + nw + ui.s(24), y + ui.s(26)}, fz(ui, 15), th.textDim, Align::Left,
                             FontFace::Body, VAlign::Middle);
            }
            y += hh;
            for (size_t i = 0; i < all.size(); ++i) {
                const AchievementDef& a = all[i];
                if (a.category != k.id) continue;
                Rect row{area.x, y, w, rh - ui.s(4)};
                y += rh;
                std::string id = "l" + std::to_string(i);
                ui.hotspot(id, row);
                bool focus = ui.focused(id);
                if (focus) selected_ = static_cast<int>(i);
                if (row.bottom() < area.y || row.y > area.bottom()) continue;
                if (focus) ui.r().fillRoundedRect(row, ui.s(6), k.color.withAlpha(36));
                bool done = g_.achievements.unlocked(a.id);
                Vec2 dot{row.x + ui.s(14), row.center().y};
                if (done) ui.r().fillCircle(dot, ui.s(6), k.color);
                else ui.r().drawCircle(dot, ui.s(6), g_.achievements.available(a) ? k.color : Color{120, 130, 160, 120}, ui.s(1.5f));
                ui.label(ui.elide(a.name, FontFace::Bold, fz(ui, 16), cDesc - cName - ui.s(14)), {cName, row.center().y}, fz(ui, 16), done ? th.text : th.textDim,
                         Align::Left, FontFace::Bold, VAlign::Middle);
                ui.label(ui.elide(a.description, FontFace::Body, fz(ui, 15), cProg - cDesc - ui.s(16)), {cDesc, row.center().y}, fz(ui, 15), th.textDim, Align::Left,
                         FontFace::Body, VAlign::Middle);
                Color sc;
                std::string st = statusText(a, sc);
                if (done) {
                    std::string d = g_.achievements.unlockDate(a.id);
                    st = d.size() >= 10 ? "Obtenu le " + d.substr(8, 2) + "/" + d.substr(5, 2) + "/" + d.substr(0, 4) : "Obtenu";
                } else if (!g_.achievements.available(a)) {
                    st = "Verrouillé";
                }
                ui.label(ui.elide(st, FontFace::Body, fz(ui, 15), cStars - cProg - ui.s(16)), {cProg, row.center().y}, fz(ui, 15), sc, Align::Left, FontFace::Body,
                         VAlign::Middle);
                drawStarRow(ui.r(), {cStars, row.center().y}, ui.s(14), a.stars, Color{255, 214, 90, done ? 255 : 150}, Color{90, 100, 130, 110});
            }
        }
        ui.endScroll();
    }

    struct Sector {
        float a0, a1;  // where its nodes go
        float e0, e1;  // its edges
    };

    GameServices& g_;
    MenuBackground bg_;
    AchievementInputs inputs_;
    std::vector<Node> nodes_;
    std::vector<Sector> sectors_;
    int selected_ = 0;
    int view_ = 0;
    // Zoom and position of the web (display only).
    static constexpr float kMinZoom = 1.0f, kMaxZoom = 3.5f;
    float zoom_ = 1.0f;
    Vec2 pan_;
    bool dragging_ = false;
    float dragMoved_ = 0.0f;
    Vec2 dragLast_;
    int centerOn_ = -1;
    Rect webArea_;
    float t_ = 0.0f;
    float refresh_ = 2.0f;
};

}  // namespace

std::unique_ptr<Scene> makeAchievements(GameServices& g) { return std::make_unique<AchievementsScene>(g); }

}  // namespace cosmic
