// End of a level (2.19): the cosmetic rewards just unlocked by the achievements' stars.
// The player ticks what to wear now (« Équiper ») or keeps everything for later
// (« Plus tard »): either way they stay in Collection → Vestiaire. Display only.

#include <algorithm>
#include <cmath>
#include <map>

#include "game/GameServices.h"
#include "ui/screens/CosmeticFx.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }
const Color kGold{255, 214, 90, 255};

class RewardsScene : public Scene {
public:
    explicit RewardsScene(GameServices& g) : g_(g) {}
    bool isOverlay() const override { return true; }

    void onEnter() override {
        progress_ = g_.cosmeticProgress();
        items_ = g_.collection.fresh(g_.cosmetics, progress_);
        // One item per category can be worn: the last of each is ticked by default.
        std::map<std::string, size_t> lastOf;
        for (size_t i = 0; i < items_.size(); ++i) lastOf[items_[i]->category] = i;
        picked_.assign(items_.size(), false);
        for (const auto& [c, i] : lastOf) picked_[i] = true;
        g_.ui.setFocus("rw/equip");
        g_.uiSound("medal", 0.7f);
    }

    void update(float dt) override {
        t_ += dt;
        float a = t_ * 2.4f;
        trail_.update(trailCenter_ + Vec2{std::cos(a) * trailRadius_, std::sin(a * 2.0f) * trailRadius_ * 0.25f}, dt, true);
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{4, 6, 14, 215});
        if (items_.empty()) {
            ui.end();
            g_.app.popScene();
            return;
        }
        ui.pushId("rw");
        float cw = std::min(ui.s(1160), ui.screenW() - ui.s(60));
        float chh = std::min(ui.s(620), ui.screenH() - ui.s(90));
        Rect card = Rect::fromCenter({ui.screenW() * 0.5f, ui.screenH() * 0.5f - ui.s(16)}, {cw, chh});
        r.fillRoundedRect(card, ui.s(20), Color{10, 15, 32, 248});
        r.drawRoundedRect(card, ui.s(20), kGold.withAlpha(150), std::max(1.0f, ui.s(1.5f)));
        float x = card.x + ui.s(30), y = card.y + ui.s(26), w = card.w - ui.s(60);
        // Title.
        ui.label("NOUVELLES RÉCOMPENSES", {x, y + ui.s(20)}, 28, kGold, Align::Left, FontFace::Title, VAlign::Middle);
        float tw = ui.text().measure("NOUVELLES RÉCOMPENSES", FontFace::Title, ui.s(28)).x;
        std::string sub = "Seulement visuelles : rien ne change dans la partie.";
        if (tw + ui.s(40) < w)
            ui.label(ui.elide(sub, FontFace::Body, fz(ui, 15), w - tw - ui.s(40)), {x + w, y + ui.s(20)}, fz(ui, 15), ui.theme().textDim, Align::Right,
                     FontFace::Body, VAlign::Middle);
        y += ui.s(62);
        // Preview (left) with the ticked items.
        float pw = std::min(ui.s(340), w * 0.32f);
        float bottomBar = ui.s(64);
        Rect prev{x, y, pw, card.bottom() - y - bottomBar - ui.s(24)};
        auto lookOf = [&](const std::string& cat) {
            for (size_t i = 0; i < items_.size(); ++i)
                if (picked_[i] && items_[i]->category == cat) return items_[i]->look;
            return g_.cosmeticLook(cat);
        };
        r.fillRoundedRectGradient(prev, ui.s(16), Color{17, 26, 54, 255}, Color{10, 15, 32, 255});
        r.drawRoundedRect(prev, ui.s(16), ui.theme().accent.withAlpha(80), std::max(1.0f, ui.s(1.5f)));
        float ground = prev.bottom() - prev.h * 0.18f;
        r.fillRect({prev.x + ui.s(2), ground, prev.w - ui.s(4), ui.s(2)}, ui.theme().accent.withAlpha(90));
        float ns = std::min(prev.w * 0.6f, prev.h * 0.45f);
        Rect nova = Rect::fromCenter({prev.center().x, ground - ns * 0.5f}, {ns, ns});
        trailCenter_ = {nova.center().x, nova.center().y + ns * 0.12f};
        trailRadius_ = ns * 0.62f;
        r.pushClip(prev.expanded(-ui.s(2)));
        trail_.draw(r, lookOf("trail"), false, ns / 110.0f);
        drawNovaPreview(lookOf("suit"), nova);
        if (std::string v = lookOf("victory"); v != "classic") drawVictoryFx(r, v, {nova.center().x, nova.bottom() - ns * 0.1f}, std::fmod(t_, 3.2f), false, ns / 140.0f);
        r.popClip();
        ui.label("Nova", {prev.center().x, prev.y + ui.s(28)}, 20, ui.theme().text, Align::Center, FontFace::Title, VAlign::Middle);
        std::string title = lookOf("title");
        if (!title.empty()) {
            float fs = fz(ui, 14);
            float cw2 = ui.text().measure(title, FontFace::Bold, ui.s(fs)).x + ui.s(22);
            Rect tr = Rect::fromCenter({prev.center().x, prev.y + ui.s(58)}, {std::min(cw2, prev.w - ui.s(16)), ui.s(fs) + ui.s(10)});
            r.drawRoundedRect(tr, tr.h * 0.5f, kGold.withAlpha(160), std::max(1.0f, ui.s(1.2f)));
            ui.label(ui.elide(title, FontFace::Bold, fs, tr.w - ui.s(14)), tr.center(), fs, kGold, Align::Center, FontFace::Bold, VAlign::Middle);
        }
        // Stars and rewards (right).
        float rx = prev.right() + ui.s(26), rw = card.right() - ui.s(30) - rx;
        float ry = y;
        Rect band{rx, ry, rw, ui.s(96)};
        ui.panel(band, false, 14);
        auto ms = g_.cosmetics.milestones(progress_.totalStars);
        int lo = 0, hi = progress_.totalStars;
        for (const auto& m : ms) {
            if (m.stars <= progress_.stars) lo = m.stars;
            else {
                hi = m.stars;
                break;
            }
        }
        std::string head = std::to_string(progress_.stars) + " étoiles sur " + std::to_string(progress_.totalStars);
        ui.label(head, {band.x + ui.s(18), band.y + ui.s(26)}, 19, ui.theme().text, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string nextT = hi > progress_.stars ? "prochain palier : " + std::to_string(hi) + " étoiles (encore " + std::to_string(hi - progress_.stars) + ")"
                                                 : "tous les paliers sont atteints";
        ui.label(nextT, {band.right() - ui.s(18), band.y + ui.s(26)}, fz(ui, 15), Color{143, 255, 240, 255}, Align::Right, FontFace::Body, VAlign::Middle);
        Rect bar{band.x + ui.s(18), band.y + ui.s(52), band.w - ui.s(36), ui.s(12)};
        r.fillRoundedRect(bar, bar.h * 0.5f, Color{20, 28, 52, 255});
        float k = hi > lo ? std::clamp((progress_.stars - lo) / static_cast<float>(hi - lo), 0.0f, 1.0f) : 1.0f;
        if (k > 0.0f) r.fillRoundedRect({bar.x, bar.y, bar.w * k, bar.h}, bar.h * 0.5f, kGold);
        ui.label(std::to_string(lo), {bar.x, bar.bottom() + ui.s(14)}, fz(ui, 13), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(std::to_string(hi), {bar.right(), bar.bottom() + ui.s(14)}, fz(ui, 13), ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        ry = band.bottom() + ui.s(18);
        int n = static_cast<int>(std::count(picked_.begin(), picked_.end(), true));
        ui.label("Choisissez ce que vous équipez maintenant", {rx, ry + ui.s(10)}, fz(ui, 15), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(std::to_string(n) + " sur " + std::to_string(items_.size()) + (n > 1 ? " sélectionnées" : " sélectionnée"), {rx + rw, ry + ui.s(10)}, fz(ui, 15),
                 ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
        ry += ui.s(30);
        // Tiles (3 per row; the list scrolls when there are more).
        int cols = 3;
        float gap = ui.s(12);
        float th = ui.s(176);
        float listBottom = card.bottom() - bottomBar - ui.s(24);
        float listH = std::ceil(items_.size() / static_cast<float>(cols)) * (th + gap);
        // Many rewards at once (2.24 adds a lot): the scroll bar gets its own lane.
        float tilesW = rw - (listH - gap > listBottom - ry ? ui.s(14) : 0.0f);
        float tw2 = (tilesW - gap * (cols - 1)) / cols;
        ui.beginScroll("tiles", {rx, ry, rw, listBottom - ry}, listH);
        float so = ui.scrollOffset();
        for (size_t i = 0; i < items_.size(); ++i) {
            const CosmeticItem& it = *items_[i];
            Rect c{rx + (tw2 + gap) * static_cast<float>(i % cols), ry - so + (th + gap) * static_cast<float>(i / cols), tw2, th};
            bool on = picked_[i];
            if (ui.hotspot("tile" + std::to_string(i), c)) {
                picked_[i] = !on;
                // Only one item of a category can be worn.
                if (picked_[i])
                    for (size_t j = 0; j < items_.size(); ++j)
                        if (j != i && items_[j]->category == it.category) picked_[j] = false;
            }
            r.fillRoundedRect(c, ui.s(12), on ? ui.theme().accent.withAlpha(30) : Color{14, 20, 40, 235});
            r.drawRoundedRect(c, ui.s(12), on ? ui.theme().accent : ui.theme().accent.withAlpha(40), std::max(1.0f, ui.s(1.5f)));
            Rect thumb{c.x + ui.s(10), c.y + ui.s(10), c.w - ui.s(20), ui.s(84)};
            r.fillRoundedRect(thumb, ui.s(8), Color{11, 18, 38, 255});
            drawThumb(it, thumb);
            const CosmeticCategory* cat = g_.cosmetics.category(it.category);
            std::string kind = cat ? cat->name : it.category;
            ui.label(ui.elide(it.name, FontFace::Bold, 17, c.w - ui.s(20)), {c.x + ui.s(10), c.y + ui.s(100)}, 17, ui.theme().text, Align::Left, FontFace::Bold);
            std::string origin = !it.achievement.empty() ? kind + " · succès" : "Palier de " + std::to_string(cosmeticThreshold(it, progress_.totalStars)) + " étoiles";
            ui.label(ui.elide(origin, FontFace::Body, fz(ui, 13), c.w - ui.s(20)), {c.x + ui.s(10), c.y + ui.s(124)}, fz(ui, 13), ui.theme().textDim);
            Rect box{c.x + ui.s(10), c.y + ui.s(146), ui.s(20), ui.s(20)};
            if (on) r.fillRoundedRect(box, ui.s(5), ui.theme().accent);
            else r.drawRoundedRect(box, ui.s(5), Color{90, 106, 130, 255}, std::max(1.0f, ui.s(1.5f)));
            if (on) {
                r.drawLine({box.x + ui.s(4), box.center().y}, {box.x + ui.s(8), box.bottom() - ui.s(5)}, Color{6, 32, 28, 255}, ui.s(2.5f));
                r.drawLine({box.x + ui.s(8), box.bottom() - ui.s(5)}, {box.right() - ui.s(4), box.y + ui.s(5)}, Color{6, 32, 28, 255}, ui.s(2.5f));
            }
            ui.label(on ? "À équiper" : "Garder pour plus tard", {box.right() + ui.s(8), box.center().y}, fz(ui, 14),
                     on ? Color{143, 255, 240, 255} : ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
        }
        ui.endScroll();
        // Bottom bar.
        float by = card.bottom() - bottomBar - ui.s(4);
        ui.label("« Plus tard » garde tout dans Collection → Vestiaire.", {x, by + ui.s(26)}, fz(ui, 15), ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        UIContext::ButtonStyle eq;
        eq.primary = n > 0;
        eq.enabled = n > 0;
        float ew = ui.s(190), lw = ui.s(150);
        bool equip = ui.button("equip", {card.right() - ui.s(30) - ew, by, ew, ui.s(52)}, n > 0 ? "Équiper (" + std::to_string(n) + ")" : "Équiper", eq);
        bool later = ui.button("later", {card.right() - ui.s(30) - ew - ui.s(14) - lw, by, lw, ui.s(52)}, "Plus tard");
        // Controls under the card.
        float hy = card.bottom() + ui.s(24);
        if (hy < ui.screenH() - ui.s(10)) {
            Glyphs::drawHint(ui, Action::MenuConfirm, "cocher / choisir", {ui.screenW() * 0.5f - ui.s(230), hy}, ui.s(30));
            Glyphs::drawHint(ui, Action::MenuBack, "plus tard", {ui.screenW() * 0.5f + ui.s(90), hy}, ui.s(30));
        }
        ui.popId();
        if (equip && n > 0) {
            for (size_t i = 0; i < items_.size(); ++i)
                if (picked_[i]) g_.collection.equip(items_[i]->category, items_[i]->id);
            g_.syncCosmetics();
            g_.noteOutfit();
            close();
        } else if (later || ui.backPressed()) {
            close();
        }
        ui.end();
    }

private:
    void close() {
        for (const CosmeticItem* it : items_) g_.collection.markAnnounced(it->id);
        g_.collection.save();
        g_.app.popScene();
    }
    void drawNovaPreview(const std::string& suit, const Rect& dst) {
        AnimationSetPtr set = g_.app.resources().animation("animations/nova.json");
        if (!set || !set->texture) return;
        const AnimationClip* c = set->clip("run");
        if (!c || c->frameCount() == 0) return;
        float total = std::max(0.01f, c->totalDuration()), local = std::fmod(t_, total), acc = 0.0f;
        int frame = 0;
        for (int i = 0; i < c->frameCount(); ++i) {
            acc += c->durations[static_cast<size_t>(i)];
            if (local < acc) {
                frame = i;
                break;
            }
        }
        TexturePtr tex = set->texture;
        if (!suit.empty() && suit != "standard")
            if (TexturePtr s = g_.app.resources().texture("sprites/player/suits/nova_" + suit + ".png")) tex = s;
        Rect src = set->frameRect(*c, frame);
        g_.app.renderer().drawTexture(*tex, &src, dst);
    }
    void drawThumb(const CosmeticItem& it, const Rect& box) {
        UIContext& ui = g_.ui;
        Renderer& r = ui.r();
        if (it.category == "suit") {
            float s = std::min(box.w, box.h);
            drawNovaPreview(it.look, Rect::fromCenter(box.center(), {s, s}));
        } else if (it.category == "title") {
            float fs = fz(ui, 14);
            float cw = ui.text().measure(it.look, FontFace::Bold, ui.s(fs)).x + ui.s(22);
            Rect tr = Rect::fromCenter(box.center(), {std::min(cw, box.w - ui.s(8)), ui.s(fs) + ui.s(10)});
            r.drawRoundedRect(tr, tr.h * 0.5f, kGold.withAlpha(160), std::max(1.0f, ui.s(1.2f)));
            ui.label(ui.elide(it.look, FontFace::Bold, fs, tr.w - ui.s(12)), tr.center(), fs, kGold, Align::Center, FontFace::Bold, VAlign::Middle);
        } else if (it.category == "trail") {
            // The trail as the game draws it (2.24: one look per trail, the new ones too).
            r.pushClip(box);
            drawTrailSample(r, it.look, box.expanded(-ui.s(4)), t_);
            r.popClip();
        } else if (it.category == "victory") {
            r.pushClip(box);
            drawVictoryFx(r, it.look, {box.center().x, box.bottom() - ui.s(6)}, std::fmod(t_, 2.4f) + 0.15f, false, box.h / 220.0f);
            r.popClip();
        } else if (it.category == "frame") {
            // The frame itself around an empty avatar (as on the profile and the leaderboards).
            float scale = ui.s(0.7f);
            float rad = std::max(ui.s(8), box.h * 0.5f - kAvatarFrameMargin * scale - ui.s(2));
            r.fillCircle(box.center(), rad, Color{24, 34, 60, 255});
            drawAvatarFrame(r, box.center(), rad, it.look, scale, t_);
        }
    }

    GameServices& g_;
    CosmeticProgress progress_;
    std::vector<const CosmeticItem*> items_;
    std::vector<bool> picked_;
    TrailFx trail_;
    Vec2 trailCenter_;
    float trailRadius_ = 60.0f;
    float t_ = 0.0f;
};

}  // namespace

std::unique_ptr<Scene> makeNewRewards(GameServices& g) { return std::make_unique<RewardsScene>(g); }

}  // namespace cosmic
