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

void PauseScene::render(float /*alpha*/) {
    UIContext& ui = g_.ui;
    ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
    float k = std::min(1.0f, t_ / 0.25f);
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{4, 6, 14, 255}.withAlphaF(0.65f * k));

    float w = ui.s(560), h = ui.s(700);
    Rect panel{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f + (1.0f - applyEase(Ease::OutCubic, k)) * ui.s(40), w, h};
    ui.panel(panel, true, 18);
    ui.label("PAUSE", {panel.center().x, panel.y + ui.s(40)}, 52, ui.theme().text, Align::Center, FontFace::Title);
    GameWorld* world = game_.world();
    if (world) {
        std::string info = world->level().name + "  —  " + LevelTimer::format(world->timer().elapsed());
        ui.label(info, {panel.center().x, panel.y + ui.s(112)}, 22, ui.theme().textDim, Align::Center);
        std::string stats = "Éclats " + std::to_string(world->stats().shards) + "/" + std::to_string(world->stats().totalShards) +
                            "   Morts " + std::to_string(world->stats().deaths) + "   Ennemis " + std::to_string(world->stats().kills);
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
