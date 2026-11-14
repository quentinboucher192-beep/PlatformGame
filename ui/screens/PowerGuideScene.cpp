// Power guide (help): every power of the player with a live example, played in a loop by
// the game engine itself (a small scripted level per power), its button, its energy cost and
// what it does. A card opens the power's sheet: a larger example and every characteristic
// (energy, damage, radius, reach, recharge...), read from its definition.

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
#include "game/powers/PowerStats.h"
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
        if (g_.onlineOpen.rfind("power:", 0) == 0) {  // automated screenshots: a power's sheet
            std::string id = g_.onlineOpen.substr(6);
            for (size_t i = 0; i < demos_.size(); ++i)
                if (demos_[i]->def.power == id) sheet_ = static_cast<int>(i);
            g_.onlineOpen.clear();
        }
        energyMax_ = std::max(1.0f, jget<float>(g_.content.playerConfig(), "maxEnergy", 150.0f));
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
        const bool sheetOpen = sheet_ >= 0 && sheet_ < static_cast<int>(demos_.size());
        ui.inputEnabled = !sheetOpen;
        ui.pushId("guide");
        bool back = ui.button("back", {ui.screenW() - margin - ui.s(180), ui.s(34), ui.s(180), ui.s(52)}, "Retour") || ui.backPressed();
        ui.popId();
        // Pages of 8 large cards (4 x 2): the descriptions stay readable.
        constexpr int cols = 4, rows = 2, perPage = cols * rows;
        int pages = std::max(1, static_cast<int>((demos_.size() + perPage - 1) / perPage));
        page_ = std::min(page_, pages - 1);
        InputManager& in = g_.app.input();
        if (!sheetOpen && in.menuPressed(Action::MenuTabPrev)) page_ = (page_ - 1 + pages) % pages;
        if (!sheetOpen && in.menuPressed(Action::MenuTabNext)) page_ = (page_ + 1) % pages;
        float gap = ui.s(18);
        float top = ui.s(124);
        float footH = ui.s(64);
        float cardW = (ui.screenW() - 2 * margin - gap * (cols - 1)) / cols;
        float cardH = (ui.screenH() - top - footH - gap * (rows - 1) - ui.s(12)) / rows;
        float textH = ui.s(150);
        for (int k = 0; k < perPage; ++k) {
            size_t i = static_cast<size_t>(page_ * perPage + k);
            if (i >= demos_.size()) break;
            Demo& d = *demos_[i];
            const PowerDefinition* def = g_.content.power(d.def.power);
            Rect card{margin + static_cast<float>(k % cols) * (cardW + gap), top + static_cast<float>(k / cols) * (cardH + gap), cardW, cardH};
            // The whole card opens the power's sheet (mouse, keys, gamepad).
            ui.pushId("guide");
            if (ui.button("card" + std::to_string(i), card, "")) sheet_ = static_cast<int>(i);
            ui.popId();
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
            // Its cost and first characteristics.
            float sy = icon.bottom() + ui.s(6);
            float pillW = drawEnergyPill(*def, {card.x + ui.s(12), sy}, ui.s(26));
            // As many characteristics as fit at a readable size (the sheet has them all).
            float lx = card.x + ui.s(12) + pillW + ui.s(10), lroom = card.right() - ui.s(12) - lx;
            std::string line;
            for (const auto& st : powerStats(*def)) {
                if (st.key == "energy") continue;
                std::string more = (line.empty() ? "" : "  ·  ") + st.label + " " + st.value;
                if (!line.empty() && ui.text().measure(line + more, FontFace::Body, ui.s(14)).x > lroom) break;
                line += more;
            }
            float lf = ui.fitFont(line, FontFace::Body, 15, lroom, 0.85f);
            ui.label(ui.elide(line, FontFace::Body, lf, lroom), {lx, sy + ui.s(13)}, lf, ui.theme().text, Align::Left, FontFace::Body, VAlign::Middle);
            // Description below, wrapped and sized to fit the card.
            float dy = sy + ui.s(34);
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
        ui.inputEnabled = true;
        if (sheetOpen) drawSheet(*demos_[static_cast<size_t>(sheet_)], alpha);
        ui.end();
        if (back && !sheetOpen) g_.app.popScene();
    }

private:
    /// "⚡ 20" pill (or "gratuit"), coloured by the cost against the charged shot's 20.
    float drawEnergyPill(const PowerDefinition& def, Vec2 topLeft, float h) {
        UIContext& ui = g_.ui;
        std::string t = def.energyCost > 0.0f ? powerStats(def).front().value + " énergie" : "Sans énergie";
        float w = ui.text().measure(t, FontFace::Bold, h * 0.58f).x + h * 0.9f;
        Rect r{topLeft.x, topLeft.y, w, h};
        Color c = def.energyCost <= 0.0f ? ui.theme().success : def.energyCost <= 12.0f ? ui.theme().accent : def.energyCost <= 25.0f ? ui.theme().warm
                                                                                                                                  : ui.theme().danger;
        ui.r().fillRoundedRect(r, h * 0.5f, c.withAlphaF(0.18f));
        ui.r().drawRoundedRect(r, h * 0.5f, c.withAlphaF(0.8f), std::max(1.0f, ui.s(1.2f)));
        ui.label(t, r.center(), h * 0.58f / ui.scale(), c, Align::Center, FontFace::Bold, VAlign::Middle);
        return w;
    }

    void drawDemo(Demo& d, const PowerDefinition* def, const Rect& vp, float alpha) {
        Renderer& r = g_.app.renderer();
        if (!d.world || vp.h <= 10.0f) return;
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
        r.drawRoundedRect(vp, g_.ui.s(6), def ? def->color.withAlpha(140) : g_.ui.theme().border, g_.ui.s(1.5f));
    }

    /// The power's sheet: a large example, its button, cost against the energy gauge, and
    /// every characteristic of its definition.
    void drawSheet(Demo& d, float alpha) {
        UIContext& ui = g_.ui;
        const PowerDefinition* def = g_.content.power(d.def.power);
        if (!def) {
            sheet_ = -1;
            return;
        }
        ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{2, 4, 12, 215});
        float W = std::min(ui.screenW() - ui.s(80), ui.s(1500)), H = std::min(ui.screenH() - ui.s(80), ui.s(740));
        Rect box{(ui.screenW() - W) * 0.5f, (ui.screenH() - H) * 0.5f, W, H};
        ui.r().fillRoundedRect(box, ui.s(18), Color{9, 14, 32, 250});
        ui.r().drawRoundedRect(box, ui.s(18), def->color.withAlphaF(0.7f), ui.s(1.6f));
        ui.pushId("sheet");
        // Header: icon, name, its button.
        float x = box.x + ui.s(32), y = box.y + ui.s(28);
        Rect icon{x, y, ui.s(64), ui.s(64)};
        ui.r().fillRoundedRect(icon, ui.s(12), {7, 11, 24, 255});
        ui.r().drawRoundedRect(icon, ui.s(12), def->color, ui.s(2));
        ui.icon("ui/icons_powers.png", def->icon, 32, icon.expanded(-ui.s(8)));
        UIContext::ButtonStyle cs;
        cs.small = true;
        Rect close{box.right() - ui.s(190), y + ui.s(8), ui.s(170), ui.s(46)};
        bool leave = ui.button("close", close, "Retour", cs);
        Rect prevB{close.x - ui.s(130), close.y, ui.s(56), close.h}, nextB{close.x - ui.s(66), close.y, ui.s(56), close.h};
        bool prev = ui.button("prev", prevB, "◀", cs), next = ui.button("next", nextB, "▶", cs);
        float nx = icon.right() + ui.s(20);
        float nroom = prevB.x - ui.s(20) - nx;
        float nf = ui.fitFont(def->name, FontFace::Title, 34, nroom, 0.6f);
        ui.label(def->name, {nx, y + ui.s(20)}, nf, def->color, Align::Left, FontFace::Title, VAlign::Middle);
        if (powerIsPassive(d.def.power)) ui.label("Passif : toujours actif", {nx, y + ui.s(52)}, 17, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        else Glyphs::drawHint(ui, def->input, "pour l'utiliser", {nx, y + ui.s(52)}, ui.s(30));
        y += ui.s(92);
        // Left: the example, larger. Right: what it does and its characteristics.
        float leftW = (box.w - ui.s(96)) * 0.56f;
        Rect vp{x, y, leftW, std::min(box.bottom() - ui.s(32) - y, leftW * 9.0f / 16.0f)};
        drawDemo(d, def, vp, alpha);
        float rx = vp.right() + ui.s(32), rw = box.right() - ui.s(32) - rx, ry = y;
        Vec2 ds = ui.labelWrapped(def->description, {rx, ry}, rw, 18, ui.theme().text);
        ry += ds.y + ui.s(18);
        // Energy: its cost on a gauge as long as the player's.
        ui.label("Énergie", {rx, ry + ui.s(10)}, 17, ui.theme().textDim, Align::Left, FontFace::Bold, VAlign::Middle);
        std::string cost = def->energyCost > 0.0f ? powerStats(*def).front().value + " / " + std::to_string(static_cast<int>(energyMax_)) : "gratuit";
        ui.label(cost, {rx + rw, ry + ui.s(10)}, 18, ui.theme().warm, Align::Right, FontFace::Bold, VAlign::Middle);
        ry += ui.s(26);
        Rect gauge{rx, ry, rw, ui.s(14)};
        ui.r().fillRoundedRect(gauge, ui.s(7), Color{24, 32, 58, 255});
        float f = std::clamp(def->energyCost / energyMax_, 0.0f, 1.0f);
        if (f > 0.0f) ui.r().fillRoundedRect({gauge.x, gauge.y, std::max(gauge.h, gauge.w * f), gauge.h}, ui.s(7), ui.theme().warm);
        ry += gauge.h + ui.s(8);
        const std::string ref = "Référence : le tir chargé coûte 20 ; une attaque moins forte coûte moins.";
        ui.label(ref, {rx, ry + ui.s(8)}, ui.fitFont(ref, FontFace::Body, 14, rw), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ry += ui.s(30);
        // Every characteristic, label and value on a line.
        ui.label("Caractéristiques", {rx, ry + ui.s(10)}, 19, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
        ry += ui.s(30);
        auto stats = powerStats(*def);
        float rowH = std::min(ui.s(34), (box.bottom() - ui.s(28) - ry) / std::max<float>(1.0f, static_cast<float>(stats.size())));
        for (size_t i = 0; i < stats.size(); ++i) {
            Rect row{rx, ry, rw, rowH - ui.s(3)};
            if (i % 2 == 0) ui.r().fillRoundedRect(row, ui.s(6), Color{14, 20, 40, 220});
            float lf = std::min(17.0f, rowH / ui.scale() * 0.55f);
            ui.label(stats[i].label, {row.x + ui.s(12), row.center().y}, ui.fitFont(stats[i].label, FontFace::Body, lf, rw * 0.6f), ui.theme().textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(stats[i].value, {row.right() - ui.s(12), row.center().y}, ui.fitFont(stats[i].value, FontFace::Bold, lf, rw * 0.35f), ui.theme().text,
                     Align::Right, FontFace::Bold, VAlign::Middle);
            ry += rowH;
        }
        int n = static_cast<int>(demos_.size());
        InputManager& in = g_.app.input();
        if (prev || in.menuPressed(Action::MenuTabPrev)) sheet_ = (sheet_ - 1 + n) % n;
        if (next || in.menuPressed(Action::MenuTabNext)) sheet_ = (sheet_ + 1) % n;
        if (leave || ui.backPressed()) sheet_ = -1;
        ui.popId();
    }

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
    int sheet_ = -1;  // the power whose sheet is open
    float energyMax_ = 150.0f;
};

}  // namespace

std::unique_ptr<Scene> makePowerGuide(GameServices& g, int page) { return std::make_unique<PowerGuideScene>(g, page); }

}  // namespace cosmic
