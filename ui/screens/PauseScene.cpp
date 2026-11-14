#include "ui/screens/PauseScene.h"

#include "engine/core/Easing.h"
#include "game/GameServices.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

void PauseScene::onEnter() {
    g_.app.audio().setMusicDuck(0.35f);
    g_.uiSound("ui_confirm", 0.5f);
    g_.ui.setFocus("pause/resume");
}

void PauseScene::onExit() { g_.app.audio().setMusicDuck(1.0f); }

void PauseScene::update(float dt) { t_ += dt; }

void PauseScene::drawMapPanel(const Rect& panel) {
    UIContext& ui = g_.ui;
    GameWorld* world = game_.world();
    if (!world) return;
    ui.panel(panel, false, 18);
    const float pad = ui.s(24), x = panel.x + pad, w = panel.w - 2 * pad;
    ui.label("CARTE", {x, panel.y + ui.s(36)}, 30, ui.theme().text, Align::Left, FontFace::Title, VAlign::Middle);
    std::string sub = "Seulement ce que vous avez déjà exploré";
    ui.label(sub, {x, panel.y + ui.s(70)}, ui.fitFont(sub, FontFace::Body, 16, w), ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    // Legend at the bottom, measured and wrapped: each entry keeps its marker and its words together.
    struct Entry {
        Color c;
        int shape;  // 0 dot, 1 square, 2 diamond, 3 flag, 4 ring
        const char* text;
    };
    const Entry entries[] = {{ui.theme().accent, 4, "Vous"},
                             {ui.theme().success, 3, "Point de passage"},
                             {Color{255, 206, 84, 255}, 1, "Sortie"},
                             {Color{190, 110, 255, 255}, 2, "Pouvoir à reprendre"},
                             {Color{255, 230, 120, 255}, 0, "Objet au sol"}};
    const float lineH = ui.s(26), marker = ui.s(16);
    std::vector<std::pair<float, float>> pos;  // x offset, line
    float lx = 0.0f;
    int lines = 1;
    for (const auto& e : entries) {
        float ew = marker + ui.s(8) + ui.text().measure(e.text, FontFace::Body, ui.s(15)).x;
        if (lx > 0.0f && lx + ew > w) {
            lx = 0.0f;
            ++lines;
        }
        pos.push_back({lx, static_cast<float>(lines - 1)});
        lx += ew + ui.s(22);
    }
    const float legendTop = panel.bottom() - pad - lines * lineH;
    for (size_t i = 0; i < pos.size(); ++i) {
        const Entry& e = entries[i];
        Vec2 m{x + pos[i].first + marker * 0.5f, legendTop + pos[i].second * lineH + lineH * 0.5f};
        Renderer& r = ui.r();
        float u = marker * 0.32f;
        if (e.shape == 4) r.drawRing(m, u * 1.6f, 2.0f, e.c);
        else if (e.shape == 0) r.fillCircle(m, u * 1.2f, e.c);
        else if (e.shape == 1) r.fillRect({m.x - u * 1.2f, m.y - u * 1.2f, u * 2.4f, u * 2.4f}, e.c);
        else if (e.shape == 2) {
            r.fillTriangle({m.x, m.y - u * 1.6f}, {m.x + u * 1.6f, m.y}, {m.x - u * 1.6f, m.y}, e.c);
            r.fillTriangle({m.x - u * 1.6f, m.y}, {m.x + u * 1.6f, m.y}, {m.x, m.y + u * 1.6f}, e.c);
        } else r.fillRect({m.x - u, m.y - u * 1.5f, u * 2, u * 3}, e.c);
        ui.label(e.text, {x + pos[i].first + marker + ui.s(8), m.y}, 15, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
    // The map, and under it how to scroll it when it is wider than its frame.
    const float hintH = ui.s(26);
    Rect area{x, panel.y + ui.s(96), w, legendTop - ui.s(14) - hintH - (panel.y + ui.s(96))};
    PauseMapView v = drawPauseMap(ui, *world, game_.explored(), area, [this](const Rect& dst, const Rect& win) { game_.renderMapView(dst, win); },
                                  mapPan_);
    if (v.maxPan > v.minPan) {
        std::string hint = "Faire défiler : glisser, molette, ← → ou LB / RB";
        ui.label(hint, {x, v.dst.bottom() + ui.s(16)}, ui.fitFont(hint, FontFace::Body, 14, w), ui.theme().textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        InputManager& in = g_.app.input();
        const float step = 220.0f / std::max(0.01f, v.scale) * ui.s(1.0f);
        if (ui.mouseOver(v.dst)) {
            if (in.mouseDown(SDL_BUTTON_LEFT)) mapPan_ -= in.mouseDelta().x / v.scale;
            if (in.wheel() != 0.0f) mapPan_ -= in.wheel() * step * 0.5f;
        }
        const float dt = g_.app.frameTime() / 1000.0f;
        if (in.keyDown(SDL_SCANCODE_LEFT) || in.held(Action::MenuTabPrev)) mapPan_ -= step * 2.0f * dt;
        if (in.keyDown(SDL_SCANCODE_RIGHT) || in.held(Action::MenuTabNext)) mapPan_ += step * 2.0f * dt;
        mapPan_ = std::clamp(mapPan_, v.minPan, v.maxPan);
    }
}

void PauseScene::render(float /*alpha*/) {
    UIContext& ui = g_.ui;
    ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
    float k = std::min(1.0f, t_ / 0.25f);
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{4, 6, 14, 255}.withAlphaF(0.65f * k));

    float w = ui.s(560), h = ui.s(700);
    // The map of what was explored sits on the right of the menu when the window has room.
    GameWorld* mapWorld = game_.world();
    const float gap = ui.s(24);
    float mapW = mapWorld ? std::min(ui.s(820), ui.screenW() - ui.s(80) - w - gap) : 0.0f;
    if (mapW < ui.s(360)) mapW = 0.0f;
    float total = w + (mapW > 0.0f ? gap + mapW : 0.0f);
    float slide = (1.0f - applyEase(Ease::OutCubic, k)) * ui.s(40);
    Rect panel{(ui.screenW() - total) * 0.5f, (ui.screenH() - h) * 0.5f + slide, w, h};
    if (mapW > 0.0f) drawMapPanel({panel.right() + gap, panel.y, mapW, h});
    ui.panel(panel, true, 18);
    ui.label("PAUSE", {panel.center().x, panel.y + ui.s(40)}, 52, ui.theme().text, Align::Center, FontFace::Title);
    GameWorld* world = game_.world();
    if (world) {
        std::string info = world->level().name + "  —  " + LevelTimer::format(world->timer().elapsed());
        ui.label(info, {panel.center().x, panel.y + ui.s(112)}, 22, ui.theme().textDim, Align::Center);
        std::string stats = "Éclats " + std::to_string(world->stats().shards) + "/" + std::to_string(world->stats().totalShards) +
                            "   Morts " + std::to_string(world->stats().deaths) + "   Ennemis " + std::to_string(world->stats().kills);
        if (int lives = world->level().livesAllowed(); lives > 0)
            stats += "   Vies " + std::to_string(std::max(0, lives - world->stats().deaths)) + "/" + std::to_string(lives);
        ui.label(stats, {panel.center().x, panel.y + ui.s(142)}, 20, ui.theme().textDim, Align::Center);
    }

    float bx = panel.x + ui.s(60), bw = panel.w - ui.s(120), bh = ui.s(64);
    float y = panel.y + ui.s(200);
    ui.pushId("pause");
    UIContext::ButtonStyle primary;
    primary.primary = true;
    bool resume = ui.button("resume", {bx, y, bw, bh}, "Reprendre", primary);
    y += bh + ui.s(16);
    if (ui.button("restart", {bx, y, bw, bh}, "Recommencer")) {
        g_.app.popScene();
        game_.restart();
    }
    y += bh + ui.s(16);
    if (ui.button("settings", {bx, y, bw, bh}, "Paramètres")) g_.app.pushScene(makeSettingsScene(g_, true));
    y += bh + ui.s(16);
    if (ui.button("guide", {bx, y, bw, bh}, "Guide des pouvoirs")) g_.app.pushScene(makePowerGuide(g_));
    y += bh + ui.s(16);
    UIContext::ButtonStyle danger;
    danger.danger = true;
    std::string quitLabel = game_.editorTest() ? "Retour à l'éditeur" : (confirmQuit_ ? "Confirmer : quitter le niveau" : "Retour au menu");
    if (ui.button("quit", {bx, y, bw, bh}, quitLabel, danger)) {
        if (game_.editorTest() || confirmQuit_) {
            g_.app.popScene();
            game_.quitToMenu();
        } else {
            confirmQuit_ = true;
        }
    }
    ui.popId();
    if (resume || ui.backPressed() || (g_.app.input().menuPressed(Action::Pause) && t_ > 0.1f)) {
        g_.app.popScene();
    }
    Glyphs::drawHint(ui, Action::MenuConfirm, "Valider", {panel.x + ui.s(60), panel.bottom() - ui.s(40)}, ui.s(30));
    Glyphs::drawHint(ui, Action::MenuBack, "Reprendre", {panel.center().x + ui.s(20), panel.bottom() - ui.s(40)}, ui.s(30));
    ui.end();
}

}  // namespace cosmic
