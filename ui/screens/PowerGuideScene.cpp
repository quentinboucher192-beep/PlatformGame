// Power guide (help): every power of the player with a live example, played in a loop by
// the game engine itself (a small scripted level per power), its button and what it does.

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <memory>

#include "engine/audio/AudioSystem.h"
#include "game/GameServices.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/powers/PowerDemos.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

constexpr float kDt = 1.0f / 120.0f;

struct Demo {
    PowerDemo def;
    std::unique_ptr<GameWorld> world;
    GameRenderer renderer;
    int step = 0;
};

class PowerGuideScene : public Scene {
public:
    PowerGuideScene(GameServices& g, int page) : g_(g), page_(std::max(0, page)) {}

    void onEnter() override {
        for (auto& def : powerDemos()) {
            if (!g_.content.power(def.power)) continue;  // only the powers the game knows
            auto d = std::make_unique<Demo>();
            d->def = std::move(def);
            demos_.push_back(std::move(d));
        }
        for (size_t i = 0; i < demos_.size(); ++i) {
            reset(*demos_[i]);
            demos_[i]->step = static_cast<int>(i * 7) % 60;  // not all in phase
        }
        g_.ui.setFocus("guide/back");
    }

    void fixedUpdate(float) override {
        for (auto& d : demos_) {
            if (!d->world) continue;
            d->world->fixedUpdate(d->def.script(d->step), kDt);
            if (++d->step >= d->def.period) reset(*d);
        }
    }

    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
        for (auto& d : demos_)
            if (d->world) d->world->update(dt);
    }

    void render(float alpha) override {
        UIContext& ui = g_.ui;
        Renderer& r = g_.app.renderer();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.6f);
        float margin = ui.s(48);
        ui.label("GUIDE DES POUVOIRS", {margin, ui.s(34)}, 40, ui.theme().text, Align::Left, FontFace::Title);
        ui.label("Chaque exemple est joué en boucle par le moteur du jeu.", {margin, ui.s(84)}, 18, ui.theme().textDim);
        ui.pushId("guide");
        bool back = ui.button("back", {ui.screenW() - margin - ui.s(180), ui.s(34), ui.s(180), ui.s(52)}, "Retour") || ui.backPressed();
        ui.popId();
        // Pages of 8 large cards (4 x 2): the descriptions stay readable.
        constexpr int cols = 4, rows = 2, perPage = cols * rows;
        int pages = std::max(1, static_cast<int>((demos_.size() + perPage - 1) / perPage));
        page_ = std::min(page_, pages - 1);
        InputManager& in = g_.app.input();
        if (in.menuPressed(Action::MenuTabPrev)) page_ = (page_ - 1 + pages) % pages;
        if (in.menuPressed(Action::MenuTabNext)) page_ = (page_ + 1) % pages;
        float gap = ui.s(18);
        float top = ui.s(124);
        float footH = ui.s(64);
        float cardW = (ui.screenW() - 2 * margin - gap * (cols - 1)) / cols;
        float cardH = (ui.screenH() - top - footH - gap * (rows - 1) - ui.s(12)) / rows;
        float textH = ui.s(118);
        for (int k = 0; k < perPage; ++k) {
            size_t i = static_cast<size_t>(page_ * perPage + k);
            if (i >= demos_.size()) break;
            Demo& d = *demos_[i];
            const PowerDefinition* def = g_.content.power(d.def.power);
            Rect card{margin + static_cast<float>(k % cols) * (cardW + gap), top + static_cast<float>(k / cols) * (cardH + gap), cardW, cardH};
            ui.panel(card, false, 12);
            Rect vp{card.x + ui.s(6), card.y + ui.s(6), card.w - ui.s(12), card.h - textH - ui.s(6)};
            if (d.world && vp.h > 10.0f) {
                r.pushClip(vp);
                WorldView view;
                view.scale = vp.h / d.def.viewH;
                view.screenW = static_cast<int>(vp.w);
                view.screenH = static_cast<int>(vp.h);
                view.topLeft = {d.def.cam.x - vp.w / view.scale * 0.5f - vp.x / view.scale, d.def.cam.y - vp.h / view.scale * 0.5f - vp.y / view.scale};
                view.origin = vp.pos();
                RenderOptions opt;
                opt.lighting = false;
                d.renderer.render(r, g_.app.resources(), *d.world, view, alpha, opt);
                r.setView(WorldView{});
                if (d.world->timeSlowRemaining() > 0.0f) r.fillRect(vp, Color{150, 90, 255, 40});
                r.popClip();
                r.drawRoundedRect(vp, ui.s(6), def ? def->color.withAlpha(120) : ui.theme().border, ui.s(1.5f));
            }
            if (!def) continue;
            // Name row: icon, name, then its button on the right (passive powers have none).
            float ty = vp.bottom() + ui.s(10);
            Rect icon{card.x + ui.s(12), ty, ui.s(34), ui.s(34)};
            ui.icon("ui/icons_powers.png", def->icon, 32, icon);
            bool passive = powerIsPassive(d.def.power);
            float keyH = ui.s(28);
            float keyW = passive ? ui.text().measure("passif", FontFace::Body, ui.s(15)).x
                                 : Glyphs::drawAction(ui, def->input, {-10000, -10000}, keyH);  // measure
            float keyX = card.right() - ui.s(12) - keyW;
            if (passive) ui.label("passif", {keyX, icon.center().y}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            else Glyphs::drawAction(ui, def->input, {keyX, icon.center().y}, keyH);
            float nameX = icon.right() + ui.s(10);
            float nameRoom = keyX - ui.s(10) - nameX;
            ui.label(def->name, {nameX, icon.center().y}, ui.fitFont(def->name, FontFace::Bold, 21, nameRoom), def->color, Align::Left,
                     FontFace::Bold, VAlign::Middle);
            // Description below, wrapped and sized to fit the card.
            float dy = icon.bottom() + ui.s(8);
            float dw = card.w - ui.s(24), dh = card.bottom() - ui.s(8) - dy;
            float fs = 16.0f;
            while (fs > 12.5f && ui.text().measureWrapped(def->description, FontFace::Body, ui.s(fs), dw).y > dh) fs -= 0.5f;
            r.pushClip({card.x, dy - ui.s(2), card.w, dh + ui.s(4)});
            ui.labelWrapped(def->description, {card.x + ui.s(12), dy}, dw, fs, ui.theme().textDim);
            r.popClip();
        }
        // Footer: pages (buttons, Q / E, LB / RB).
        float fy = ui.screenH() - footH + ui.s(6);
        float bw = ui.s(200), bh = ui.s(48);
        ui.pushId("guide");
        bool prev = ui.button("prev", {margin, fy, bw, bh}, "Précédent");
        bool next = ui.button("next", {ui.screenW() - margin - bw, fy, bw, bh}, "Suivant");
        ui.popId();
        if (prev) page_ = (page_ - 1 + pages) % pages;
        if (next) page_ = (page_ + 1) % pages;
        char pg[64];
        std::snprintf(pg, sizeof pg, "Page %d / %d", page_ + 1, pages);
        float hx = margin + bw + ui.s(24);
        float hw = Glyphs::drawHint(ui, Action::MenuTabPrev, "/", {hx, fy + bh * 0.5f}, ui.s(28));
        Glyphs::drawHint(ui, Action::MenuTabNext, "Pages", {hx + hw + ui.s(6), fy + bh * 0.5f}, ui.s(28));
        ui.label(pg, {ui.screenW() * 0.5f, fy + bh * 0.5f}, 20, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        ui.end();
        if (back) g_.app.popScene();
    }

private:
    void reset(Demo& d) {
        WorldServices s{g_.app.resources(), g_.app.audio(), g_.content, nullptr, 0.0f, 0.6f};
        d.world = std::make_unique<GameWorld>(s, WorldMode::Arena);
        d.world->silent = true;
        d.world->load(powerDemoLevel(d.def));
        d.renderer.build(g_.app.renderer(), g_.app.resources(), *d.world);
        d.step = 0;
    }

    GameServices& g_;
    MenuBackground bg_;
    std::vector<std::unique_ptr<Demo>> demos_;
    float t_ = 0.0f;
    int page_ = 0;
};

}  // namespace

std::unique_ptr<Scene> makePowerGuide(GameServices& g, int page) { return std::make_unique<PowerGuideScene>(g, page); }

}  // namespace cosmic
