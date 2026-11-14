#include "ui/screens/ResultsScene.h"

#include "ui/online/OnlineWidgets.h"

#include <cmath>

#include "engine/core/Easing.h"
#include "game/GameServices.h"
#include "game/levels/LevelTimer.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {
const Color kMedalColors[5] = {{120, 130, 150, 255}, {205, 127, 50, 255}, {205, 215, 230, 255}, {255, 210, 90, 255}, {150, 240, 255, 255}};
}

void ResultsScene::onEnter() { g_.ui.setFocus(d_.run.completed && !d_.nextLevel.empty() ? "results/next" : "results/retry"); }

void ResultsScene::update(float dt) {
    t_ += dt;
    // Counting ticks during the score roll-up.
    int tick = static_cast<int>(t_ * 20.0f);
    if (t_ > 0.8f && t_ < 2.2f && tick != lastTick_) {
        lastTick_ = tick;
        g_.uiSound("score_tick", 0.25f);
    }
    if (!medalPlayed_ && t_ > 2.4f && d_.run.medal != Medal::None) {
        medalPlayed_ = true;
        g_.uiSound("medal", 0.9f);
    }
}

void ResultsScene::drawOnlineStatus(const Rect& panel) {
    if (!d_.online || t_ < 1.0f) return;
    UIContext& ui = g_.ui;
    // Live status from the queue (it changes while the server verifies the run).
    const OnlineAttempt* a = g_.online.attempts().find(d_.online->attemptId);
    const OnlineAttempt& at = a ? *a : *d_.online;
    float y = panel.bottom() - ui.s(150);
    float x = panel.x + ui.s(60);
    Vec2 sz = ui.label("Cosmic Online", {x, y}, 20, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
    x += sz.x + ui.s(20);
    for (const auto& sub : at.submissions) {
        std::string text = std::string(categoryLabel(sub.category)) + " " + recordStatusLabel(sub.status);
        if (sub.status == RecordStatus::Verified && sub.rank > 0) text += " n°" + std::to_string(sub.rank);
        float h = ui.s(26);
        float w = ui.text().measure(text, FontFace::Bold, h * 0.6f).x + h;
        Color c = OnlineUi::statusColor(sub.status);
        Rect r{x, y - h * 0.5f, w, h};
        ui.r().fillRoundedRect(r, h * 0.5f, c.withAlphaF(0.16f));
        ui.r().drawRoundedRect(r, h * 0.5f, c.withAlphaF(0.85f), std::max(1.0f, ui.s(1.2f)));
        ui.label(text, r.center(), h * 0.6f / ui.scale(), c, Align::Center, FontFace::Bold, VAlign::Middle);
        x += w + ui.s(10);
    }
    std::string hint;
    if (!g_.online.usable()) hint = std::string(onlineStateLabel(g_.online.state())) + " : performance gardée en local.";
    else if (!g_.online.loggedIn()) hint = "Non connecté : envoyez-la depuis ONLINE RANKING (SUBMIT RECORD).";
    else if (g_.online.state() == OnlineState::Offline) hint = "Hors ligne : envoi automatique au retour de la connexion.";
    else if (g_.online.syncing()) hint = "Envoi et vérification en cours…";
    if (!hint.empty())
        ui.label(hint, {panel.right() - ui.s(60), y}, 17, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
}

void ResultsScene::render(float /*alpha*/) {
    UIContext& ui = g_.ui;
    ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
    const RunResult& run = d_.run;
    float k = std::min(1.0f, t_ / 0.5f);
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{3, 5, 12, 255}.withAlphaF(0.78f * k));

    float w = ui.s(1240), h = ui.s(820);
    Rect panel{(ui.screenW() - w) * 0.5f, (ui.screenH() - h) * 0.5f + (1.0f - applyEase(Ease::OutBack, k)) * ui.s(60), w, h};
    ui.panel(panel, true, 20);
    std::string title = run.completed ? "NIVEAU TERMINÉ" : "ÉCHEC";
    TextStyle ts;
    ts.face = FontFace::Title;
    ts.size = ui.s(56);
    ts.color = run.completed ? Color::white() : ui.theme().danger;
    ts.align = Align::Center;
    ts.glow = ui.s(20);
    ts.glowColor = (run.completed ? ui.theme().accent : ui.theme().danger).withAlpha(60);
    ui.text().draw(title, {panel.center().x, panel.y + ui.s(30)}, ts);
    ui.label(d_.level.name, {panel.center().x, panel.y + ui.s(104)}, 26, ui.theme().textDim, Align::Center);
    if (!run.completed && !d_.failReason.empty())
        ui.label(d_.failReason, {panel.center().x, panel.y + ui.s(140)}, 24, ui.theme().danger, Align::Center);

    // ---------------------------------------------------------- left: stats
    float lx = panel.x + ui.s(60), ly = panel.y + ui.s(190);
    float colW = ui.s(560);
    auto row = [&](int index, const std::string& label, const std::string& value, Color vc = {}) {
        float appear = std::clamp((t_ - 0.4f - index * 0.12f) / 0.25f, 0.0f, 1.0f);
        if (appear <= 0.0f) return;
        float y = ly + index * ui.s(50);
        Color lc = ui.theme().textDim.withAlphaF(appear);
        ui.label(label, {lx + (1 - appear) * ui.s(20), y}, 24, lc);
        Color c = vc.a ? vc : ui.theme().text;
        ui.label(value, {lx + colW, y}, 26, c.withAlphaF(appear), Align::Right, FontFace::Bold);
        ui.separator({lx, y + ui.s(38)}, colW);
    };
    float timeShown = run.time * std::clamp((t_ - 0.4f) / 0.8f, 0.0f, 1.0f);
    row(0, "Temps réalisé", LevelTimer::format(timeShown), ui.theme().accent);
    std::string best = d_.previous.bestTime > 0 ? LevelTimer::format(d_.previous.bestTime) : "—";
    if (run.completed && d_.previous.bestTime > 0) {
        float delta = run.time - d_.previous.bestTime;
        best += "  (" + LevelTimer::formatDelta(delta) + ")";
    }
    row(1, "Record personnel", best,
        run.completed && d_.previous.bestTime > 0 && run.time < d_.previous.bestTime ? ui.theme().success : Color{});
    row(2, "Temps de référence", LevelTimer::format(d_.level.referenceTime));
    row(3, "Morts", std::to_string(run.deaths), run.deaths == 0 ? ui.theme().success : Color{});
    row(4, "Collectibles", std::to_string(run.collectibles) + " / " + std::to_string(run.totalCollectibles));
    row(5, "Ennemis éliminés", std::to_string(run.kills));
    row(6, "Secrets", std::to_string(run.secrets) + " / " + std::to_string(run.totalSecrets));
    int scoreShown = static_cast<int>(run.score * std::clamp((t_ - 0.9f) / 1.3f, 0.0f, 1.0f));
    row(7, "Score", std::to_string(scoreShown), ui.theme().warm);

    // ------------------------------------------------- right: medal & goals
    float rx = panel.x + ui.s(700), ry = panel.y + ui.s(190);
    float medalAppear = std::clamp((t_ - 2.3f) / 0.5f, 0.0f, 1.0f);
    float ms = ui.s(150) * applyEase(Ease::OutBack, medalAppear);
    Vec2 mc{rx + ui.s(240), ry + ui.s(90)};
    if (medalAppear > 0.0f) {
        int mi = static_cast<int>(run.medal);
        ui.r().drawGlow(mc, ui.s(150) * medalAppear, kMedalColors[mi].withAlpha(70));
        ui.icon("ui/icons_medals.png", mi, 32, Rect::fromCenter(mc, {ms, ms}));
        ui.label(run.medal == Medal::None ? "Pas de médaille" : std::string("Médaille ") + medalName(run.medal),
                 {mc.x, mc.y + ui.s(96)}, 28, kMedalColors[mi].withAlphaF(medalAppear), Align::Center, FontFace::Bold);
    }
    float gy = ry + ui.s(240);
    ui.label("Objectifs de médaille", {rx, gy}, 22, ui.theme().textDim);
    gy += ui.s(36);
    for (int i = 1; i <= 4; ++i) {
        Medal m = static_cast<Medal>(i);
        bool got = static_cast<int>(run.medal) >= i;
        ui.icon("ui/icons_medals.png", got ? i : 0, 32, {rx, gy, ui.s(34), ui.s(34)});
        ui.label(std::string(medalName(m)) + " : " + RecordsManager::medalCondition(d_.level, m), {rx + ui.s(48), gy + ui.s(17)},
                 21, got ? ui.theme().text : ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        gy += ui.s(42);
    }
    if (d_.update.newBestTime && t_ > 1.4f) {
        float pulse = 0.8f + 0.2f * std::sin(t_ * 6.0f);
        TextStyle nb;
        nb.face = FontFace::Title;
        nb.size = ui.s(30) * pulse;
        nb.color = ui.theme().warm;
        nb.align = Align::Center;
        nb.glow = ui.s(14);
        nb.glowColor = ui.theme().warm.withAlpha(60);
        ui.text().draw(d_.update.firstCompletion ? "PREMIÈRE VICTOIRE !" : "NOUVEAU RECORD !", {mc.x, ry - ui.s(30)}, nb);
    }
    if (d_.editorTest)
        ui.label("Test depuis l'éditeur : les records ne sont pas enregistrés", {panel.center().x, panel.bottom() - ui.s(150)}, 20,
                 ui.theme().textDim, Align::Center);
    drawOnlineStatus(panel);

    // ------------------------------------------------------------- buttons
    float bh = ui.s(64), by = panel.bottom() - ui.s(100);
    float bw = ui.s(340);
    bool hasNext = run.completed && !d_.nextLevel.empty();
    int count = hasNext ? 3 : 2;
    float total = count * bw + (count - 1) * ui.s(24);
    float bx = panel.center().x - total * 0.5f;
    ui.pushId("results");
    if (t_ > 0.6f) {
        if (ui.button("retry", {bx, by, bw, bh}, "Recommencer")) {
            g_.app.popScene();
            game_.restart();
        }
        bx += bw + ui.s(24);
        if (hasNext) {
            UIContext::ButtonStyle p;
            p.primary = true;
            if (ui.button("next", {bx, by, bw, bh}, "Niveau suivant", p)) {
                g_.app.switchScene(makeGameScene(g_, d_.nextLevel), 0.4f);
            }
            bx += bw + ui.s(24);
        }
        std::string back = d_.editorTest ? "Retour à l'éditeur" : d_.fromOnline ? "Retour à ONLINE" : "Sélection des niveaux";
        if (d_.replay && d_.replay->exact()) {
            UIContext::ButtonStyle rs;
            rs.small = true;
            if (ui.button("watch", {panel.x + ui.s(30), panel.y + ui.s(24), ui.s(250), ui.s(44)}, "Revoir la partie", rs))
                g_.app.pushScene(makeLocalReplayViewer(g_, *d_.replay, "Votre partie"));
        }
        if (d_.online && !d_.fromOnline) {
            UIContext::ButtonStyle os;
            os.small = true;
            if (ui.button("online", {panel.right() - ui.s(300), panel.y + ui.s(24), ui.s(270), ui.s(44)}, "Classement en ligne", os)) {
                if (const Json* c = g_.online.communityInstall(d_.run.levelId)) g_.app.switchScene(makeOnlineLevels(g_, jstr(*c, "id")), 0.4f);
                else g_.app.switchScene(makeOnlineRanking(g_, d_.run.levelId), 0.4f);
            }
        }
        if (ui.button("back", {bx, by, bw, bh}, back) || ui.backPressed()) {
            g_.app.popScene();
            game_.quitToMenu();
        }
    }
    ui.popId();
    ui.end();
}

}  // namespace cosmic
