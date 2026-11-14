// Ghost Racing options (pause menu → Fantômes, or the pre-race screen): what the ghosts look
// like and what the HUD shows. Display only: nothing here changes the run.

#include <algorithm>
#include <cstdio>

#include "engine/core/Easing.h"
#include "game/GameServices.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

class GhostOptionsScene : public Scene {
public:
    GhostOptionsScene(GameServices& g, GameScene* game) : g_(g), game_(game) {}
    bool isOverlay() const override { return true; }
    void onEnter() override { g_.ui.setFocus("gopt/visible"); }
    void update(float dt) override { t_ += dt; }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        float k = std::min(1.0f, t_ / 0.2f);
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{4, 6, 14, 255}.withAlphaF(0.6f * k));
        GhostSettings& s = g_.settings.ghosts;
        const float rowH = ui.s(52), gap = ui.s(10);
        const int rows = 10;
        float w = std::min(ui.s(760), ui.screenW() - ui.s(80));
        float h = ui.s(110) + rows * (rowH + gap) + ui.s(90);
        h = std::min(h, ui.screenH() - ui.s(40));
        Rect panel{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f, w, h};
        ui.panel(panel, true, 18);
        ui.label("FANTÔMES", {panel.x + ui.s(32), panel.y + ui.s(44)}, 36, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
        std::string sub = "Affichage seulement : les fantômes ne touchent jamais à la partie.";
        ui.label(sub, {panel.x + ui.s(32), panel.y + ui.s(84)}, ui.fitFont(sub, FontFace::Body, 16, w - ui.s(64)), ui.theme().textDim,
                 Align::Left, FontFace::Body, VAlign::Middle);
        ui.pushId("gopt");
        float x = panel.x + ui.s(32), cw = w - ui.s(64), y = panel.y + ui.s(110);
        Rect area{x, y, cw, panel.bottom() - ui.s(80) - y};
        float content = rows * (rowH + gap);
        ui.beginScroll("rows", area, content);
        y = area.y - ui.scrollOffset();
        bool changed = false;
        auto row = [&]() {
            Rect r{x, y, cw - ui.s(14), rowH};
            y += rowH + gap;
            return r;
        };
        // In game: the quick toggle (also G / left stick click).
        bool shown = game_ ? game_->ghostsVisible() : s.enabled;
        if (ui.toggle("visible", row(), "Afficher les fantômes", shown)) {
            if (game_) game_->setGhostsVisible(shown);
            s.enabled = shown;
            changed = true;
        }
        changed |= ui.toggle("pb", row(), "Courir contre mon meilleur temps par défaut", s.personalByDefault);
        float maxG = static_cast<float>(s.maxGhosts);
        if (ui.slider("max", row(), "Fantômes en même temps", maxG, 1.0f, 8.0f, 1.0f, "%.0f")) {
            s.maxGhosts = static_cast<int>(maxG);
            changed = true;
        }
        {
            Rect r = row();
            float lw = r.w * 0.42f;
            ui.label("Opacité", {r.x + ui.s(16), r.center().y}, ui.fitFont("Opacité", FontFace::Bold, 22, lw - ui.s(20)), ui.theme().text,
                     Align::Left, FontFace::Bold, VAlign::Middle);
            int op = s.opacity;
            if (ui.tabs("opacity", {r.x + lw, r.y + ui.s(4), r.w - lw, r.h - ui.s(8)}, {"Discrète", "Normale", "Marquée"}, op)) {
                s.opacity = op;
                changed = true;
            }
        }
        changed |= ui.toggle("names", row(), "Nom et temps au-dessus des fantômes", s.showNames);
        changed |= ui.toggle("delta", row(), "Écart en direct", s.showDelta);
        changed |= ui.toggle("panel", row(), "Panneau des segments", s.showPanel);
        // Colours, by kind of ghost: a click takes the next colour of the palette.
        struct Kind {
            const char* id;
            const char* label;
            int* value;
        };
        Kind kinds[] = {{"cmine", "Mes fantômes", &s.colorMine},
                        {"cworld", "Records mondiaux", &s.colorWorld},
                        {"cfriend", "Amis", &s.colorFriend}};
        for (const Kind& kd : kinds) {
            Rect r = row();
            std::string text = std::string("Couleur : ") + kd.label + " — " + ghostPaletteName(*kd.value);
            UIContext::ButtonStyle st;
            st.align = Align::Left;
            st.small = true;
            if (ui.button(kd.id, r, text, st)) {
                *kd.value = (*kd.value + 1) % kGhostPaletteSize;
                changed = true;
            }
            Vec2 c{r.right() - ui.s(30), r.center().y};
            ui.r().fillCircle(c, ui.s(13), ghostPaletteColor(*kd.value));
            ui.r().drawCircle(c, ui.s(13), Color::white().withAlphaF(0.7f), ui.s(1.5f));
        }
        ui.endScroll();
        UIContext::ButtonStyle primary;
        primary.primary = true;
        bool close = ui.button("close", {panel.right() - ui.s(32) - ui.s(220), panel.bottom() - ui.s(66), ui.s(220), ui.s(50)}, "Fermer", primary);
        Glyphs::drawHint(ui, Action::MenuBack, "Fermer", {panel.x + ui.s(32), panel.bottom() - ui.s(41)}, ui.s(30));
        ui.popId();
        if (changed) {
            dirty_ = true;
            if (game_) game_->applyGhostSettings();
        }
        if (close || ui.backPressed()) {
            if (dirty_) g_.saveSettings();
            g_.app.popScene();
        }
        ui.end();
    }

private:
    GameServices& g_;
    GameScene* game_;
    float t_ = 0.0f;
    bool dirty_ = false;
};

}  // namespace

std::unique_ptr<Scene> makeGhostOptions(GameServices& g, GameScene* game) { return std::make_unique<GhostOptionsScene>(g, game); }

}  // namespace cosmic
