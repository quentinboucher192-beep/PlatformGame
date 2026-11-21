// Main menu → Succès: every achievement of the game on a spider web. Each category is a
// sector of the web; the rings are the difficulty (1 star outside, 5 stars at the heart);
// threads link an achievement to the ones it opens. A list view shows them all as text.
// Everything shown comes from the player's own saves (game/achievements/Achievements.h).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "game/GameServices.h"
#include "game/achievements/AchievementSuggestions.h"
#include "game/content/CreaturePlaces.h"
#include "ui/widgets/CosmicFrame.h"
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
    explicit AchievementsScene(GameServices& g, bool fromPause = false) : g_(g), fromPause_(fromPause) {}

    void onEnter() override {
        bg_.init(g_);
        g_.checkAchievements();
        inputs_ = g_.achievementInputs();
        // Suggestions (2.52): where each boss is fought is read from the levels on a worker
        // thread (ready at once after a first visit).
        atlas_ = officialCreatureAtlas(g_.content);
        computeSuggestions();
        const auto& all = g_.achievementCatalog.achievements();
        // 2.84: the unlocks new since the last visit (banner, pills, glows), oldest first.
        for (const std::string& id : g_.achievements.unseen())
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].id == id) fresh_.push_back(static_cast<int>(i));
        // First focus: the first achievement still to get (or the first one).
        for (size_t i = 0; i < all.size(); ++i)
            if (!g_.achievements.unlocked(all[i].id) && g_.achievements.available(all[i])) {
                selected_ = static_cast<int>(i);
                break;
            }
        if (fromPause_ || g_.onlineOpen == "ach-suggestions") {
            // Over a paused run, or automated screenshots (--achievements suggestions).
            if (!fromPause_) g_.onlineOpen.clear();
            view_ = 2;
            g_.ui.setFocus(suggestions_.empty() ? "ach/back" : "ach/sg0");
            return;
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
        } else if (g_.onlineOpen.rfind("ach-list:", 0) == 0) {
            // The list, scrolled to this achievement (--online-open ach-list:<id>).
            std::string id = g_.onlineOpen.substr(9);
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].id == id) selected_ = static_cast<int>(i);
            view_ = 1;
            g_.onlineOpen.clear();
            g_.ui.setFocus("ach/l" + std::to_string(selected_));
            return;
        }
        if (!fresh_.empty()) {  // 2.84: the first new one chosen, the web centred on it
            freshAt_ = 0;
            selected_ = fresh_.front();
            centerOn_ = selected_;
        }
        g_.ui.setFocus("ach/n" + std::to_string(selected_));
    }

    void onExit() override {
        // 2.84: what was new was shown on this visit (the banner, the pills, the glows): seen once it is left.
        if (g_.achievements.unseen().empty()) return;
        const std::vector<std::string> ids = g_.achievements.unseen();
        for (const std::string& id : ids) g_.achievements.markSeen(id);
        g_.achievements.save();
    }

    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
        if (flashT_ > 0.0f) flashT_ -= dt;
        refresh_ -= dt;
        if (refresh_ <= 0.0f) {  // the counters move only between runs; a slow refresh is enough
            inputs_ = g_.achievementInputs();
            refresh_ = 2.0f;
        }
        // The bosses' levels known: the suggestions are computed again, but never under the
        // player's eyes (when the tab is not shown; else the next time it is opened).
        if (!atlas_) {
            atlas_ = officialCreatureAtlas(g_.content);
            if (atlas_) suggestionsStale_ = true;
        }
        if (suggestionsStale_ && view_ != 2) computeSuggestions();
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.7f);
        ui.pushId("ach");
        const float W = ui.screenW(), H = ui.screenH(), m = ui.s(48);
        const AchievementCatalog& cat = g_.achievementCatalog;
        Rect back{W - m - ui.s(180), ui.s(30), ui.s(180), ui.s(46)};
        bool leave = cframe::back(ui, "back", back);
        Rect tabs{back.x - ui.s(20) - ui.s(450), ui.s(30), ui.s(450), ui.s(46)};
        cframe::title(ui, "SUCCÈS", {m, ui.s(52)}, 40, Align::Left, VAlign::Middle, tabs.x - m - ui.s(20));
        int view = view_;
        if (cframe::tabs(ui, "view", tabs, {"Toile", "Liste", "Suggestions"}, view)) {
            view_ = view;
            if (view_ == 2 && suggestionsStale_) computeSuggestions();  // opened again: up to date
        }
        // « NOUVEAU » on the Suggestions tab (2.52): gone once the tab is opened.
        if (ui.isNew("achievements.suggestions")) {
            if (view_ == 2 && ui.featureSeen) ui.featureSeen("achievements.suggestions");
            if (ui.isNew("achievements.suggestions")) ui.drawNewPill(tabs.right() - ui.s(10), tabs.y, true);
        }
        drawSummary({m, ui.s(96), W - 2 * m, ui.s(50)});
        float top = ui.s(166);
        if (!fresh_.empty()) {  // 2.84: the unlocks new since the last visit, under the counters
            const Rect banner = freshBanner();
            drawFresh(banner);
            top = banner.bottom() + ui.s(12);
        }
        if (cat.achievements().empty()) {
            ui.label("Le catalogue des succès est introuvable (data/achievements).", {W * 0.5f, H * 0.5f}, 22, ui.theme().danger,
                     Align::Center, FontFace::Body, VAlign::Middle);
        } else {
            const float bottom = H - ui.s(62);
            if (view_ == 0) {
                float side = std::min(bottom - top, W * 0.56f);
                Rect web{m, top, side, bottom - top};
                drawWeb(web);
                drawDetail({web.right() + ui.s(24), top, W - m - web.right() - ui.s(24), bottom - top});
            } else if (view_ == 1) {
                drawList({m, top, W - 2 * m, bottom - top});
            } else {
                drawSuggestions({m, top, W - 2 * m, bottom - top});
            }
            // Category jumps (LB / RB, Q / E).
            InputManager& in = g_.app.input();
            if (ui.acceptsInput() && view_ == 0 && (in.menuPressed(Action::MenuTabPrev) || in.menuPressed(Action::MenuTabNext)))
                jumpCategory(in.menuPressed(Action::MenuTabNext) ? 1 : -1);
        }
        // Footer hints.
        float fy = H - ui.s(30), fx = m;
        // On the web and in the list, Valider follows the achievement chosen (or stops).
        fx += Glyphs::drawHint(ui, Action::MenuConfirm, view_ == 2 ? "Choisir" : "Suivre / ne plus suivre", {fx, fy}, ui.s(30)) + ui.s(36);
        if (view_ == 0) fx += Glyphs::drawHint(ui, Action::MenuTabNext, "Catégorie suivante", {fx, fy}, ui.s(30)) + ui.s(36);
        fx += Glyphs::drawHint(ui, Action::MenuBack, "Retour", {fx, fy}, ui.s(30)) + ui.s(36);
        if (view_ == 0 && !cat.achievements().empty()) drawZoomControls(fx, fy);
        if (leave || ui.backPressed()) {
            if (fromPause_) g_.app.popScene();  // back to the paused run
            else g_.app.switchScene(makeMainMenu(g_), 0.3f);
        }
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

public:
    /// 2.84: the banner of the unlocks new since the last visit (under the counters, above the views).
    Rect freshBanner() const {
        const UIContext& ui = g_.ui;
        const float m = ui.s(48);
        return {m, ui.s(156), ui.screenW() - 2 * m, ui.s(84)};
    }
    struct FreshLayout {
        Rect see;                 // « Voir les nouveaux »
        Rect head;                // « Nouveaux succès depuis votre dernière visite : N » (fitted: `headFont`)
        float headFont = 20.0f;
        std::string headText;
        std::vector<Rect> chips;  // the names shown, in order
        std::vector<std::string> chipText;
        float chipFont = 15.0f;
        Rect more;                // « +K »: the ones beyond the room (empty: none)
        std::string moreText;
    };
    /// Where each piece of the banner `r` goes: one after the other, each fitted or shortened.
    FreshLayout layoutFresh(const Rect& r) const {
        UIContext& ui = g_.ui;
        const auto& all = g_.achievementCatalog.achievements();
        FreshLayout L;
        const float pad = ui.s(18);
        const float vw = std::max(ui.s(150), ui.text().measure("Voir les nouveaux", FontFace::Bold, ui.s(20)).x + ui.s(48));
        L.see = {r.right() - pad - vw, r.center().y - ui.s(22), vw, ui.s(44)};
        const float x0 = r.x + pad, right = L.see.x - ui.s(20), room = right - x0;
        const std::string head = "Nouveaux succès depuis votre dernière visite : " + std::to_string(fresh_.size());
        L.headFont = ui.fitFont(head, FontFace::Bold, 20, room, 0.55f);
        L.headText = ui.elide(head, FontFace::Bold, L.headFont, room);
        const float hh = ui.text().measure(L.headText, FontFace::Bold, ui.s(L.headFont)).y;
        L.head = {x0, r.y + ui.s(24) - hh * 0.5f, ui.text().measure(L.headText, FontFace::Bold, ui.s(L.headFont)).x, hh};
        const float ch = ui.s(30), cy = r.bottom() - ui.s(10) - ch, gap = ui.s(8);
        L.chipFont = fz(ui, 15);
        const float cf = L.chipFont;
        float x = x0;
        for (size_t k = 0; k < fresh_.size(); ++k) {
            const AchievementDef& a = all[static_cast<size_t>(fresh_[k])];
            const size_t after = fresh_.size() - k - 1;  // room kept for « +K » of the ones after it
            const float moreW = after > 0 ? ui.text().measure("+" + std::to_string(after), FontFace::Bold, ui.s(cf)).x + gap : 0.0f;
            const float avail = right - x - moreW;
            float cw = std::min(ui.s(280), ui.text().measure(a.name, FontFace::Bold, ui.s(cf)).x + ui.s(32));
            if (cw > avail) {
                if (avail < ui.s(90)) {  // no room for this one: counted with the rest
                    L.moreText = "+" + std::to_string(fresh_.size() - k);
                    const Vec2 ms = ui.text().measure(L.moreText, FontFace::Bold, ui.s(cf));
                    L.more = {x, cy + (ch - ms.y) * 0.5f, ms.x, ms.y};
                    break;
                }
                cw = avail;
            }
            L.chips.push_back({x, cy, cw, ch});
            L.chipText.push_back(ui.elide(a.name, FontFace::Bold, cf, cw - ui.s(24)));
            x += cw + gap;
        }
        return L;
    }
    /// The unlocks new since the last visit (catalogue indices), oldest first.
    const std::vector<int>& fresh() const { return fresh_; }

private:
    /// 2.84: « Nouveaux succès depuis votre dernière visite : N », their names in a row (each one shows it; the
    /// ones beyond the room counted in « +K ») and « Voir les nouveaux » (the next one, in turn).
    void drawFresh(const Rect& r) {
        UIContext& ui = g_.ui;
        const Color gold{255, 205, 110, 255};
        cframe::panel(ui, r, true, 12);
        ui.r().drawRoundedRect(r, ui.s(12), gold.withAlphaF(0.5f + 0.25f * std::sin(t_ * 3.0f)), std::max(1.0f, ui.s(2.0f)));
        const FreshLayout L = layoutFresh(r);
        UIContext::ButtonStyle vs;
        vs.primary = true;
        vs.small = true;
        if (cframe::button(ui, "fresh/see", L.see, "Voir les nouveaux", vs)) showFresh(static_cast<size_t>(freshAt_ + 1));
        ui.label(L.headText, {L.head.x, L.head.center().y}, L.headFont, gold, Align::Left, FontFace::Bold, VAlign::Middle);
        UIContext::ButtonStyle cs;
        cs.small = true;
        cs.fontSize = L.chipFont;
        for (size_t k = 0; k < L.chips.size(); ++k)
            if (cframe::button(ui, "fresh/c" + std::to_string(k), L.chips[k], L.chipText[k], cs)) showFresh(k);
        if (!L.moreText.empty()) ui.label(L.moreText, {L.more.x, L.more.center().y}, L.chipFont, gold, Align::Left, FontFace::Bold, VAlign::Middle);
    }

    /// 2.84: the `k`-th new unlock chosen (in turn), its node (web) or its row (list) focused; called in « ach ».
    void showFresh(size_t k) {
        if (fresh_.empty()) return;
        freshAt_ = static_cast<int>(k % fresh_.size());
        selected_ = fresh_[static_cast<size_t>(freshAt_)];
        if (view_ == 2) view_ = 0;
        centerOn_ = selected_;
        g_.ui.setFocus((view_ == 1 ? "l" : "n") + std::to_string(selected_));
    }

    void drawSummary(const Rect& r) {
        UIContext& ui = g_.ui;
        if (flashT_ > 0.0f) {
            // A message about « Suivre » (the limit of 5...) takes the place of the counters for
            // a few seconds: it covers nothing.
            ui.r().fillRoundedRect(r, ui.s(10), Color{40, 26, 8, 235});
            ui.r().drawRoundedRect(r, ui.s(10), ui.theme().warm.withAlphaF(0.8f), std::max(1.0f, ui.s(1.5f)));
            const float room = r.w - ui.s(36);
            const float f = std::max(ui.fitFont(flash_, FontFace::Bold, 19, room, 0.7f), 11.5f / ui.s(1.0f));
            ui.label(ui.elide(flash_, FontFace::Bold, f, room), r.center(), f, ui.theme().warm, Align::Center, FontFace::Bold, VAlign::Middle);
            return;
        }
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

    /// Heart of the web (radius / web radius) and the angle kept free at the top for the
    /// rings' legend.
    static constexpr float kHub = 0.30f, kLegend = 0.30f;
    /// Radial offset of the two staggered rows of a crowded ring (in rings' widths).
    static constexpr float kZig = 0.25f;

    float ringMid(float hub, float band, int stars) const { return hub + band * (5.0f - static_cast<float>(stars)) + band * 0.5f; }
    /// Node radius and the free space between two nodes at 100 % (the zoom scales them).
    float nodeRadius(float band1) const { return std::min(g_.ui.s(9), band1 * 0.24f); }
    float nodeGap() const { return g_.ui.s(5); }

    /// Places every achievement on its ring (1 star outside, 5 stars next to the heart), in
    /// its category's sector. Each sector is as wide as its most crowded ring needs (nodes
    /// side by side, or on two staggered rows when the web is full), with a margin at its
    /// edges on every ring: no node ever covers another one. Angles are those of 100 %, so the
    /// zoom only scales the web.
    void layout(Vec2 c, float R, float hub) {
        const AchievementCatalog& cat = g_.achievementCatalog;
        const auto& all = cat.achievements();
        const auto& cats = cat.categories();
        nodes_.assign(all.size(), Node{});
        sectors_.clear();
        const float z = std::max(zoom_, 0.01f);
        const float band = (R - hub) / 5.0f;
        const float nr = nodeRadius(band / z), gap = nodeGap();
        // Angle of one node and of the margin at a sector's edge, on the ring of `st` stars.
        auto step1 = [&](int st) { return (2.0f * nr + gap) / (ringMid(hub, band, st) / z); };
        auto pad1 = [&](int st) { return (nr + gap * 0.5f) / (ringMid(hub, band, st) / z); };
        std::vector<std::array<int, 6>> count(cats.size(), std::array<int, 6>{});
        std::vector<int> total(cats.size(), 0);
        for (const auto& a : all)
            for (size_t ci = 0; ci < cats.size(); ++ci)
                if (cats[ci].id == a.category) {
                    count[ci][static_cast<size_t>(std::clamp(a.stars, 1, 5))]++;
                    total[ci]++;
                }
        const float span = 2.0f * kPi - kLegend;
        std::vector<float> one(cats.size(), 0.0f), two(cats.size(), 0.0f);
        float sumOne = 0.0f, sumTwo = 0.0f;
        int all1 = 0;
        for (size_t ci = 0; ci < cats.size(); ++ci) {
            for (int st = 1; st <= 5; ++st) {
                float n = static_cast<float>(count[ci][static_cast<size_t>(st)]);
                if (n <= 0.0f) continue;
                one[ci] = std::max(one[ci], n * step1(st) + 2.0f * pad1(st));
                two[ci] = std::max(two[ci], std::max(1.0f, n * 0.5f) * step1(st) + 2.0f * pad1(st));
            }
            sumOne += one[ci];
            sumTwo += two[ci];
            all1 += total[ci];
        }
        // Side by side when everything fits, else crowded rings on two rows; what is left of
        // the circle is shared by the number of achievements (too many: all shrink alike).
        const bool rows2 = sumOne > span;
        const std::vector<float>& need = rows2 ? two : one;
        const float sum = rows2 ? sumTwo : sumOne;
        float a = -kPi / 2.0f + kLegend * 0.5f;
        for (size_t ci = 0; ci < cats.size(); ++ci) {
            float w = sum <= span ? need[ci] + (span - sum) * static_cast<float>(total[ci]) / static_cast<float>(std::max(1, all1))
                                  : need[ci] * span / std::max(sum, 0.001f);
            sectors_.push_back(Sector{a, a + w, a, a + w});
            const std::string& id = cats[ci].id;
            for (int st = 1; st <= 5; ++st) {
                std::vector<size_t> ring;
                for (size_t i = 0; i < all.size(); ++i)
                    if (all[i].category == id && all[i].stars == st) ring.push_back(i);
                if (ring.empty()) continue;
                const float mid = ringMid(hub, band, st);
                const float lo = a + pad1(st), hi = a + w - pad1(st);
                const float step = (hi - lo) / static_cast<float>(ring.size());
                const bool zig = ring.size() > 1 && step * (mid / z) < 2.0f * nr + gap - 0.01f;
                for (size_t k = 0; k < ring.size(); ++k) {
                    float ang = lo + step * (static_cast<float>(k) + 0.5f);
                    float rr = mid + (zig ? (k % 2 ? band * kZig : -band * kZig) : 0.0f);
                    Node& n = nodes_[ring[k]];
                    n.p = {c.x + std::cos(ang) * rr, c.y + std::sin(ang) * rr};
                    n.r = nr * std::min(z, 2.2f);
                }
            }
            a += w;
        }
    }

    /// The mark of a new achievement on the web: a gold dot (the colour of the « NOUVEAU »
    /// pills, explained in a corner of the web) at the node's upper right.
    void newDot(const Node& n) {
        UIContext& ui = g_.ui;
        const float dr = std::max(ui.s(3.5f), 3.0f) * (0.9f + 0.1f * std::sin(t_ * 4.0f));
        Vec2 p = n.p + Vec2{n.r * 0.72f, -n.r * 0.72f};
        ui.r().fillCircle(p, dr, Color{255, 196, 70, 255});
        ui.r().drawCircle(p, dr, Color{120, 60, 0, 220}, std::max(1.0f, ui.s(1.0f)));
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
        float hub = R * kHub;
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
        // The rings' legend in the free space at the top: 5 stars at the heart, 1 outside
        // (smaller where the gap between the first and the last sector is narrow).
        for (int st = 1; st <= 5; ++st) {
            float mid = ringMid(hub, band, st);
            Vec2 p{c.x, c.y - mid};
            float room = kLegend * mid + nodeGap() * std::min(zoom_, 2.2f) - ui.s(4);
            float sz = std::min({ui.s(9), band * 0.22f, room / (0.95f * static_cast<float>(st - 1) + 1.0f)});
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
            // The node and half the free space around it: two neighbours never share a point.
            float hs = 2.0f * n.r + nodeGap() * std::min(zoom_, 2.2f) * 0.9f;
            Rect hit = Rect::fromCenter(n.p, {hs, hs});
            // Only the nodes in view can be pointed at or reached (a drag never selects).
            // « NOUVEAU » (an achievement added by an update): seen on focus (pointed at) or a click.
            const std::string feature = "ach." + a.id;
            bool fresh = false;
            if (area.contains(n.p) && !(dragging_ && dragMoved_ > ui.s(6))) {
                // Valider (keyboard, gamepad) follows it; a click only chooses it (the detail
                // panel has its own « Suivre » button).
                if (ui.hotspot(id, hit) && !g_.app.input().mouseReleased(SDL_BUTTON_LEFT)) toggleTrack(a);
                fresh = ui.newBadge(id, hit, feature, false);
            } else {
                fresh = ui.isNew(feature);
            }
            if (ui.focused(id) && selected_ != static_cast<int>(i)) {
                selected_ = static_cast<int>(i);
                if (!g_.ui.usingPointer()) centerOn_ = selected_;
            }
            bool done = g_.achievements.unlocked(a.id);
            bool open = g_.achievements.available(a);
            if (g_.achievements.isUnseen(a.id)) {  // 2.84: new since the last visit: a golden glow and the gold dot
                fresh = true;
                r.drawGlow(n.p, n.r * 3.6f, Color{255, 196, 70, static_cast<uint8_t>(120 + 60 * std::sin(t_ * 4.0f))}, BlendMode::Additive);
            }
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
            } else if (g_.achievements.isTracked(a.id)) {
                r.drawCircle(n.p, n.r + ui.s(3), ui.theme().accent, ui.s(1.6f));  // followed (2.52)
            }
            if (fresh) newDot(n);
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
        // What the gold dot means, in a corner of the web (while something is new).
        std::vector<Rect> placed;
        if (ui.isNew("menu.achievements") || !fresh_.empty()) {
            const float ph = ui.newPillHeight(true), dotR = std::max(ui.s(4.5f), 3.5f);
            Rect legend{area.x + ui.s(6), area.bottom() - ui.s(6) - ph, dotR * 2.0f + ui.s(8) + ui.newPillWidth(true), ph};
            r.fillCircle({legend.x + dotR, legend.center().y}, dotR, Color{255, 196, 70, 255});
            r.drawCircle({legend.x + dotR, legend.center().y}, dotR, Color{120, 60, 0, 220}, std::max(1.0f, ui.s(1.0f)));
            ui.drawNewPill(legend.right(), legend.center().y, true);
            placed.push_back(legend.expanded(ui.s(4)));
        }
        // Category names around the web, kept apart (first: the names of the achievements make
        // room for them, never the reverse).
        for (size_t ci = 0; ci < sectors_.size(); ++ci) {
            const AchievementCategory& k = cat.categories()[ci];
            float a = (sectors_[ci].a0 + sectors_[ci].a1) * 0.5f;
            int have = g_.achievements.stars(cat, k.id), tot = 0;
            for (const auto& x : all) tot += x.category == k.id ? x.stars : 0;
            std::string sub = std::to_string(have) + " / " + std::to_string(tot);
            float fs = 16.0f;
            Vec2 sz = ui.text().measure(k.name, FontFace::Bold, ui.s(fs));
            Vec2 sz2 = ui.text().measure(sub, FontFace::Body, ui.s(fz(ui, 13)));
            // « NOUVEAU » on a third line while one of its achievements is new (no wider: the
            // names keep to the room left around the web); without it where it has no room.
            bool catNew = ui.isNew("achievements.cat." + k.id);
            auto place = [&](bool withPill, Rect& box) {
                const float pillH = withPill ? ui.s(3) + ui.newPillHeight(true) : 0.0f;
                float bw = std::max({sz.x, sz2.x + ui.s(16), withPill ? ui.newPillWidth(true) : 0.0f}), bh = sz.y + sz2.y + pillH;
                float rr = R + ui.s(12);
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
                    if (!hit) return true;
                }
                return false;
            };
            Rect box;
            if (!(catNew && place(true, box))) {
                catNew = false;
                place(false, box);
            }
            placed.push_back(box.expanded(ui.s(3)));  // the names keep a margin off it (their backdrop too)
            bool on = ci == static_cast<size_t>(categoryIndex(sel().category));
            ui.label(k.name, {box.x + box.w * 0.5f, box.y}, fs, on ? k.color : k.color.withAlpha(200), Align::Center, FontFace::Bold);
            if (catNew)
                ui.drawNewPill(box.x + (box.w + ui.newPillWidth(true)) * 0.5f, box.y + sz.y + sz2.y + ui.s(3) + ui.newPillHeight(true) * 0.5f, true);
            drawStar(ui.r(), {box.x + (box.w - sz2.x - ui.s(16)) * 0.5f + ui.s(6), box.y + sz.y + sz2.y * 0.5f}, ui.s(6), Color{255, 214, 90, 200}, true);
            ui.label(sub, {box.x + (box.w - sz2.x - ui.s(16)) * 0.5f + ui.s(16), box.y + sz.y}, fz(ui, 13), ui.theme().textDim, Align::Left);
        }
        // Zoomed in: each achievement's name next to its node (when there is room for it), a
        // « NOUVEAU » after the name of a new one.
        if (zoom_ >= 1.7f) {
            for (const Node& n : nodes_) placed.push_back(Rect::fromCenter(n.p, {n.r * 2.0f, n.r * 2.0f}));
            placed.push_back(Rect::fromCenter(c, {hub * 2.0f, hub * 2.0f}));
            for (size_t i = 0; i < all.size(); ++i) {
                const Node& n = nodes_[i];
                if (!area.contains(n.p)) continue;
                const float fs = std::max(13.0f, 11.5f / ui.s(1.0f));  // never below 11.5 px
                Vec2 sz = ui.text().measure(all[i].name, FontFace::Body, ui.s(fs));
                const bool fresh = ui.isNew("ach." + all[i].id) || g_.achievements.isUnseen(all[i].id);
                const float nameW = sz.x;
                if (fresh) {
                    sz.x += ui.s(6) + ui.newPillWidth(true);
                    sz.y = std::max(sz.y, ui.newPillHeight(true));
                }
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
                    ui.label(all[i].name, {b.x, b.center().y}, fs, done ? colorOf(all[i].category) : ui.theme().textDim, Align::Left, FontFace::Body,
                             VAlign::Middle);
                    if (fresh) ui.drawNewPill(b.x + nameW + ui.s(6) + ui.newPillWidth(true), b.center().y, true);
                    break;
                }
            }
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
        if (cframe::button(ui, "zoomout", {x, y - h * 0.5f, bw, h}, "−", st)) zoomAt(zoom_ / 1.25f, webArea_.center(), webArea_);
        x += bw + ui.s(8);
        char pct[16];
        std::snprintf(pct, sizeof pct, "%d %%", static_cast<int>(std::lround(zoom_ * 100.0f)));
        ui.label(pct, {x + ui.s(36), y}, fz(ui, 17), ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        x += ui.s(72) + ui.s(8);
        if (cframe::button(ui, "zoomin", {x, y - h * 0.5f, bw, h}, "+", st)) zoomAt(zoom_ * 1.25f, webArea_.center(), webArea_);
        x += bw + ui.s(10);
        UIContext::ButtonStyle rs;
        rs.small = true;
        rs.fontSize = 17;
        rs.enabled = zoom_ != 1.0f || pan_.x != 0.0f || pan_.y != 0.0f;
        if (cframe::button(ui, "zoomreset", {x, y - h * 0.5f, ui.s(130), h}, "Recentrer", rs)) {
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
        cframe::panel(ui, p, true, 16);
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
        // « NOUVEAU » after the category's name while one of its achievements is new.
        const bool catNew = ui.isNew("achievements.cat." + a.category);
        const float catPill = catNew ? ui.s(10) + ui.newPillWidth(true) : 0.0f;
        std::string shownCat = ui.elide(cname, FontFace::Bold, 22, w - rw - ui.s(40) - catPill);
        Vec2 csz = ui.label(shownCat, {x + ui.s(22), y}, 22, col, Align::Left, FontFace::Bold);
        if (catNew) ui.drawNewPill(x + ui.s(22) + csz.x + catPill, y + csz.y * 0.5f, true);
        ui.label(right, {x + w, y + ui.s(3)}, fz(ui, 16), th.textDim, Align::Right);
        y += ui.s(34);
        if (k) y += ui.labelWrapped(k->description, {x, y}, w, std::max(15.0f, 11.5f / ui.s(1.0f)), th.textDim).y + ui.s(10);
        ui.separator({x, y}, w);
        y += ui.s(14);
        // The achievement.
        const bool selNew = ui.isNew("ach." + a.id) || g_.achievements.isUnseen(a.id);
        const float selPill = selNew ? ui.s(12) + ui.newPillWidth() : 0.0f;
        float nf = ui.fitFont(a.name, FontFace::Title, 30, w - selPill, 0.6f);
        Vec2 nsz = ui.label(a.name, {x, y}, nf, th.text, Align::Left, FontFace::Title);
        if (selNew) ui.drawNewPill(x + nsz.x + selPill, y + nsz.y * 0.5f);
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
        // « Suivre » (2.52): its progress is shown in the pause menu (5 at most).
        if (!g_.achievements.unlocked(a.id)) {
            const bool on = g_.achievements.isTracked(a.id);
            const std::string tl = on ? "Ne plus suivre" : "Suivre ce succès";
            UIContext::ButtonStyle ts;
            ts.small = true;
            ts.selected = on;
            ts.fontSize = fz(ui, 17);
            const float tw = std::min(w, ui.text().measure(tl, FontFace::Bold, ui.s(ts.fontSize)).x + ui.s(44));
            const Rect tb{x, y, tw, ui.s(40)};
            if (cframe::button(ui, "track", tb, tl, ts)) toggleTrack(a);
            ui.newBadge("track", tb, "achievements.track", true, true);
            const std::string cnt = std::to_string(g_.achievements.tracked().size()) + " / " + std::to_string(AchievementBook::kMaxTracked) + " suivis";
            const float room = w - tw - ui.s(16);
            if (ui.text().measure(cnt, FontFace::Body, ui.s(fz(ui, 15))).x <= room)
                ui.label(cnt, {tb.right() + ui.s(16), tb.center().y}, fz(ui, 15), th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
            y += ui.s(52);
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
            const bool fresh = ui.isNew("ach." + all[i].id) || g_.achievements.isUnseen(all[i].id);
            const float pill = fresh ? ui.s(8) + ui.newPillWidth(true) : 0.0f;
            float nameRoom = row.w - ui.s(34) - starsW - ui.s(16) - pill;
            Vec2 nsz = ui.label(ui.elide(all[i].name, FontFace::Body, fz(ui, 16), nameRoom), {row.x + ui.s(30), row.center().y}, fz(ui, 16),
                                done ? th.text : th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
            if (fresh) ui.drawNewPill(row.x + ui.s(30) + nsz.x + pill, row.center().y, true);
            drawStarRow(ui.r(), {row.right() - starsW - ui.s(6), row.center().y}, ui.s(14), all[i].stars, Color{255, 214, 90, static_cast<uint8_t>(done ? 255 : 150)},
                        Color{90, 100, 130, 110});
        }
        ui.endScroll();
    }

    Renderer& r() { return g_.ui.r(); }

    // ------------------------------------------------------------ suggestions and « Suivre » (2.52)

    void flash(const std::string& message) {
        flash_ = message;
        flashT_ = 3.5f;
    }

    /// Ticks or unticks an achievement (saved at once); a sixth one is refused, and says why.
    void toggleTrack(const AchievementDef& a) {
        AchievementBook& book = g_.achievements;
        if (book.unlocked(a.id)) {
            flash("« " + a.name + " » est déjà obtenu : rien à suivre.");
            g_.uiSound("ui_error", 0.5f);
            return;
        }
        if (book.isTracked(a.id)) {
            book.untrack(a.id);
        } else if (!book.track(a.id)) {
            flash("Déjà 5 succès suivis : décochez-en un pour suivre « " + a.name + " ».");
            g_.uiSound("ui_error", 0.5f);
            return;
        }
        book.save();
        g_.uiSound("ui_move", 0.5f);
    }

    /// The box of « Suivre » (ticked: followed).
    void drawCheckBox(const Rect& b, bool on, bool hot) {
        UIContext& ui = g_.ui;
        const Color acc = ui.theme().accent;
        ui.r().fillRoundedRect(b, ui.s(5), on ? acc.withAlpha(220) : Color{10, 16, 34, 230});
        ui.r().drawRoundedRect(b, ui.s(5), on ? acc : hot ? Color{226, 236, 245, 230} : Color{140, 160, 185, 170}, std::max(1.0f, ui.s(1.6f)));
        if (on) {
            const Color ink{8, 20, 30, 255};
            const float t = std::max(1.5f, ui.s(2.6f));
            ui.r().drawLine({b.x + b.w * 0.22f, b.y + b.h * 0.52f}, {b.x + b.w * 0.42f, b.y + b.h * 0.72f}, ink, t);
            ui.r().drawLine({b.x + b.w * 0.42f, b.y + b.h * 0.72f}, {b.x + b.w * 0.78f, b.y + b.h * 0.30f}, ink, t);
        }
    }

    void computeSuggestions() {
        SuggestionContext ctx;
        ctx.recentLevel = g_.progression.lastLevel;
        ctx.recentPlanet = g_.progression.lastPlanet;
        ctx.online = g_.online.loggedIn();
        if (atlas_) {
            ctx.bossLevelsKnown = true;
            for (const auto& a : g_.achievementCatalog.achievements())
                if (!a.condition.boss.empty() && !ctx.bossLevels.count(a.condition.boss)) {
                    auto& levels = ctx.bossLevels[a.condition.boss];
                    for (const CreaturePlace& p : atlas_->fightsOf(g_.content, a.condition.boss)) levels.push_back(p.levelId);
                }
        }
        suggestions_ = suggestAchievements(g_.achievementCatalog, g_.achievements, inputs_, ctx);
        suggestionsStale_ = false;
    }

    /// A progress bar and its numbers on one line from `x`, `w` wide, centred on `cy`.
    void progressLine(float x, float cy, float w, const AchievementDef& a, Color col) {
        UIContext& ui = g_.ui;
        const AchievementProgress p = AchievementBook::progress(a, inputs_);
        const float f = std::clamp(static_cast<float>(p.value) / static_cast<float>(std::max<int64_t>(1, p.target)), 0.0f, 1.0f);
        const Rect bar{x, cy - ui.s(5), w * 0.55f, ui.s(10)};
        ui.r().fillRoundedRect(bar, ui.s(5), Color{20, 28, 52, 255});
        if (f > 0.0f) ui.r().fillRoundedRect({bar.x, bar.y, std::max(bar.h, bar.w * f), bar.h}, ui.s(5), col);
        const float room = x + w - bar.right() - ui.s(12);
        ui.label(ui.elide(achievementProgressText(a, p), FontFace::Bold, fz(ui, 15), room), {bar.right() + ui.s(12), cy}, fz(ui, 15), ui.theme().text,
                 Align::Left, FontFace::Bold, VAlign::Middle);
    }

    void drawSuggestions(const Rect& area) {
        const float gap = g_.ui.s(28);
        const Rect left{area.x, area.y, std::floor(area.w * 0.58f), area.h};
        const Rect right{left.right() + gap, area.y, area.right() - left.right() - gap, area.h};
        drawSuggestionList(left);
        drawTrackedList(right);
    }

    void drawSuggestionList(const Rect& p) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        cframe::panel(ui, p, false, 14);
        const float x = p.x + ui.s(22), w = p.w - ui.s(44);
        float y = p.y + ui.s(16);
        ui.label("Suggestions", {x, y}, 24, th.text, Align::Left, FontFace::Bold);
        y += ui.s(36);
        y += ui.labelWrapped("Calculées d'après votre progression : les plus proches du but, faisables maintenant, de catégories variées.", {x, y}, w,
                             fz(ui, 15), th.textDim)
                 .y +
             ui.s(14);
        if (suggestions_.empty()) {
            ui.labelWrapped("Aucune suggestion : tous les succès accessibles sont obtenus.", {x, y}, w, 18, th.text);
            return;
        }
        const float cardGap = ui.s(10);
        const float n = static_cast<float>(suggestions_.size());
        const float cardH = std::min(ui.s(150), (p.bottom() - ui.s(16) - y - cardGap * (n - 1.0f)) / n);
        for (size_t i = 0; i < suggestions_.size(); ++i) {
            drawSuggestionCard({x, y, w, cardH}, i);
            y += cardH + cardGap;
        }
    }

    void drawSuggestionCard(const Rect& c, size_t i) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const AchievementSuggestion& s = suggestions_[i];
        const AchievementDef& a = *s.def;
        const AchievementCategory* k = g_.achievementCatalog.category(a.category);
        const Color col = k ? k->color : th.accent;
        ui.r().fillRoundedRect(c, ui.s(10), Color{10, 16, 34, 220});
        ui.r().drawRoundedRect(c, ui.s(10), col.withAlphaF(0.45f), std::max(1.0f, ui.s(1.2f)));
        ui.r().fillRoundedRect({c.x, c.y, ui.s(6), c.h}, ui.s(3), col);
        // « Suivre » on the right, vertically centred.
        const bool on = g_.achievements.isTracked(a.id);
        UIContext::ButtonStyle bs;
        bs.small = true;
        bs.selected = on;
        bs.fontSize = fz(ui, 18);
        const float bw = ui.s(150), bh = std::min(ui.s(46), c.h - ui.s(20));
        const Rect b{c.right() - ui.s(14) - bw, c.center().y - bh * 0.5f, bw, bh};
        const std::string bid = "sg" + std::to_string(i);
        if (cframe::button(ui, bid, b, on ? "Suivi" : "Suivre", bs)) toggleTrack(a);
        ui.newBadge(bid, b, "achievements.track", true, true);
        // Name and stars, the category and description, the bar, why.
        const float x0 = c.x + ui.s(22), cw = b.x - ui.s(18) - x0;
        const bool roomy = c.h >= ui.s(118);
        const float l1 = ui.s(26), l2 = roomy ? ui.s(22) : 0.0f, l3 = ui.s(22), l4 = ui.s(22);
        float y = c.y + (c.h - (l1 + l2 + l3 + l4)) * 0.5f;
        const float starsW = ui.s(12) * 1.15f * 4.0f + ui.s(12);
        ui.label(ui.elide(a.name, FontFace::Bold, fz(ui, 19), cw - starsW - ui.s(12)), {x0, y + l1 * 0.5f}, fz(ui, 19), th.text, Align::Left,
                 FontFace::Bold, VAlign::Middle);
        drawStarRow(ui.r(), {x0 + cw - starsW, y + l1 * 0.5f}, ui.s(12), a.stars, Color{255, 214, 90, 255}, Color{90, 100, 130, 110});
        y += l1;
        if (roomy) {
            const std::string line = (k ? k->name + " · " : std::string{}) + a.description;
            ui.label(ui.elide(line, FontFace::Body, fz(ui, 15), cw), {x0, y + l2 * 0.5f}, fz(ui, 15), th.textDim, Align::Left, FontFace::Body,
                     VAlign::Middle);
            y += l2;
        }
        progressLine(x0, y + l3 * 0.5f, cw, a, col);
        y += l3;
        ui.label(ui.elide(s.reason, FontFace::Bold, fz(ui, 15), cw), {x0, y + l4 * 0.5f}, fz(ui, 15), th.warm, Align::Left, FontFace::Bold, VAlign::Middle);
    }

    void drawTrackedList(const Rect& p) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        cframe::panel(ui, p, false, 14);
        const float x = p.x + ui.s(22), w = p.w - ui.s(44);
        float y = p.y + ui.s(16);
        const auto& ids = g_.achievements.tracked();
        const std::string count = std::to_string(ids.size()) + " / " + std::to_string(AchievementBook::kMaxTracked);
        const float cw = ui.text().measure(count, FontFace::Bold, ui.s(20)).x;
        ui.label(ui.elide("Succès suivis", FontFace::Bold, 24, w - cw - ui.s(16)), {x, y}, 24, th.text, Align::Left, FontFace::Bold);
        ui.label(count, {x + w, y + ui.s(4)}, 20, ids.size() >= AchievementBook::kMaxTracked ? th.warm : th.accent, Align::Right, FontFace::Bold);
        y += ui.s(36);
        y += ui.labelWrapped("Leur avancée s'affiche dans le menu pause, avec ce que la partie en cours y ajoute.", {x, y}, w, fz(ui, 15), th.textDim).y +
             ui.s(14);
        if (ids.empty()) {
            ui.labelWrapped("Aucun succès suivi. Choisissez « Suivre » sur une suggestion, cochez la case d'un succès dans la Liste ou "
                            "validez-le sur la Toile : jusqu'à 5 à la fois.",
                            {x, y}, w, fz(ui, 17), th.text);
            return;
        }
        const float rowGap = ui.s(10);
        const float rowH = std::min(ui.s(84), (p.bottom() - ui.s(16) - y - rowGap * 4.0f) / 5.0f);
        int removed = -1;
        for (size_t i = 0; i < ids.size(); ++i) {
            const AchievementDef* a = g_.achievementCatalog.find(ids[i]);
            const Rect row{x, y, w, rowH};
            y += rowH + rowGap;
            if (!a) continue;
            const AchievementCategory* k = g_.achievementCatalog.category(a->category);
            const Color col = k ? k->color : th.accent;
            ui.r().fillRoundedRect(row, ui.s(10), Color{10, 16, 34, 220});
            ui.r().fillRoundedRect({row.x, row.y, ui.s(6), row.h}, ui.s(3), col);
            UIContext::ButtonStyle bs;
            bs.small = true;
            bs.fontSize = fz(ui, 16);
            const std::string ul = "Ne plus suivre";
            const float bw = ui.text().measure(ul, FontFace::Bold, ui.s(bs.fontSize)).x + ui.s(36), bh = std::min(ui.s(42), row.h - ui.s(16));
            const Rect b{row.right() - ui.s(12) - bw, row.center().y - bh * 0.5f, bw, bh};
            if (cframe::button(ui, "tr" + std::to_string(i), b, ul, bs)) removed = static_cast<int>(i);
            const float x0 = row.x + ui.s(20), cw2 = b.x - ui.s(14) - x0;
            ui.label(ui.elide(a->name, FontFace::Bold, fz(ui, 17), cw2), {x0, row.y + row.h * 0.32f}, fz(ui, 17), th.text, Align::Left, FontFace::Bold,
                     VAlign::Middle);
            progressLine(x0, row.y + row.h * 0.70f, cw2, *a, col);
        }
        if (removed >= 0) {
            g_.achievements.untrack(ids[static_cast<size_t>(removed)]);
            g_.achievements.save();
            g_.uiSound("ui_move", 0.5f);
            const int left = static_cast<int>(g_.achievements.tracked().size());
            ui.setFocus(left == 0 ? (suggestions_.empty() ? "ach/back" : "ach/sg0") : "ach/tr" + std::to_string(std::min(removed, left - 1)));
        }
    }

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
        // 2.52: the « Suivre » box of each achievement still to get, before its stars.
        const float box = ui.s(22), cTrack = cStars - ui.s(22) - box;
        for (const auto& k : cat.categories()) {
            if (y + hh >= area.y && y <= area.bottom()) {
                ui.r().fillRect({area.x, y + hh - ui.s(8), w, std::max(1.0f, ui.s(1.5f))}, k.color.withAlpha(120));
                int have = g_.achievements.stars(cat, k.id), tot = 0;
                for (const auto& a : all) tot += a.category == k.id ? a.stars : 0;
                ui.label(k.name, {area.x, y + ui.s(26)}, 22, k.color, Align::Left, FontFace::Bold, VAlign::Middle);
                std::string s = std::to_string(have) + " / " + std::to_string(tot) + " étoiles";
                ui.label(s, {area.x + w, y + ui.s(26)}, fz(ui, 16), th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
                float nw = ui.text().measure(k.name, FontFace::Bold, ui.s(22)).x;
                // « NOUVEAU » after the name while one of its achievements is new (the description moves after it).
                if (ui.isNew("achievements.cat." + k.id)) {
                    nw += ui.s(12) + ui.newPillWidth();
                    ui.drawNewPill(area.x + nw, y + ui.s(26));
                }
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
                const Rect check{cTrack, row.center().y - box * 0.5f, box, box};
                // Valider follows it (or stops); with the mouse, a click on its box.
                if (ui.hotspot(id, row) && (!g_.app.input().mouseReleased(SDL_BUTTON_LEFT) || check.expanded(ui.s(6)).contains(g_.app.input().mousePos())))
                    toggleTrack(a);
                // « NOUVEAU » (an achievement added by an update): seen on focus (pointed at) or a click.
                const bool unseen = g_.achievements.isUnseen(a.id);  // 2.84: new since the last visit
                const bool fresh = ui.newBadge(id, row, "ach." + a.id, false, true) || unseen;
                bool focus = ui.focused(id);
                if (focus) selected_ = static_cast<int>(i);
                if (row.bottom() < area.y || row.y > area.bottom()) continue;
                if (unseen) {  // a golden glow inside the row
                    const float pulse = 0.5f + 0.5f * std::sin(t_ * 4.0f);
                    ui.r().fillRoundedRect(row, ui.s(6), Color{255, 196, 70, static_cast<uint8_t>(22 + 18 * pulse)});
                    ui.r().drawRoundedRect(row, ui.s(6), Color{255, 196, 70, static_cast<uint8_t>(110 + 80 * pulse)}, std::max(1.0f, ui.s(1.5f)));
                }
                if (focus) ui.r().fillRoundedRect(row, ui.s(6), k.color.withAlpha(36));
                bool done = g_.achievements.unlocked(a.id);
                Vec2 dot{row.x + ui.s(14), row.center().y};
                if (done) ui.r().fillCircle(dot, ui.s(6), k.color);
                else ui.r().drawCircle(dot, ui.s(6), g_.achievements.available(a) ? k.color : Color{120, 130, 160, 120}, ui.s(1.5f));
                const float pill = fresh ? ui.s(8) + ui.newPillWidth(true) : 0.0f;
                Vec2 nsz = ui.label(ui.elide(a.name, FontFace::Bold, fz(ui, 16), cDesc - cName - ui.s(14) - pill), {cName, row.center().y}, fz(ui, 16),
                                    done ? th.text : th.textDim, Align::Left, FontFace::Bold, VAlign::Middle);
                if (fresh) ui.drawNewPill(cName + nsz.x + pill, row.center().y, true);
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
                ui.label(ui.elide(st, FontFace::Body, fz(ui, 15), cTrack - cProg - ui.s(16)), {cProg, row.center().y}, fz(ui, 15), sc, Align::Left, FontFace::Body,
                         VAlign::Middle);
                if (!done) drawCheckBox(check, g_.achievements.isTracked(a.id), focus || ui.mouseOver(check));
                drawStarRow(ui.r(), {cStars, row.center().y}, ui.s(14), a.stars, Color{255, 214, 90, static_cast<uint8_t>(done ? 255 : 150)}, Color{90, 100, 130, 110});
            }
        }
        ui.endScroll();
    }

    struct Sector {
        float a0, a1;  // where its nodes go
        float e0, e1;  // its edges
    };

    GameServices& g_;
    bool fromPause_ = false;  // over a paused run: « Retour » goes back to it
    cframe::Backdrop bg_;
    AchievementInputs inputs_;
    // 2.52: the suggestions (computed on entry), the bosses' levels, a message about « Suivre ».
    std::vector<AchievementSuggestion> suggestions_;
    std::shared_ptr<const CreatureAtlas> atlas_;
    bool suggestionsStale_ = false;
    std::string flash_;
    float flashT_ = 0.0f;
    std::vector<Node> nodes_;
    std::vector<Sector> sectors_;
    int selected_ = 0;
    int view_ = 0;
    std::vector<int> fresh_;  // 2.84: the unlocks new since the last visit (catalogue indices), oldest first
    int freshAt_ = -1;        // the one « Voir les nouveaux » showed last
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
std::unique_ptr<Scene> makeAchievementsFromPause(GameServices& g) { return std::make_unique<AchievementsScene>(g, true); }

std::vector<Rect> achievementsFreshPieces(const Scene* s) {
    const auto* a = dynamic_cast<const AchievementsScene*>(s);
    if (!a || a->fresh().empty()) return {};
    const Rect banner = a->freshBanner();
    const auto L = a->layoutFresh(banner);
    std::vector<Rect> out{banner, L.see, L.head};
    out.insert(out.end(), L.chips.begin(), L.chips.end());
    if (L.more.w > 0.0f) out.push_back(L.more);
    return out;
}

}  // namespace cosmic
