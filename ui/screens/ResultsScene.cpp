#include "ui/screens/ResultsScene.h"

#include "ui/online/OnlineWidgets.h"
#include "ui/online/RatingWidgets.h"

#include <cmath>
#include <cstdio>

#include "engine/core/Easing.h"
#include "game/GameServices.h"
#include "game/ghost/GhostRace.h"
#include "game/levels/LevelTimer.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/CosmicScreensLayout.h"
#include "ui/widgets/CosmicStyle2.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {
const Color kMedalColors[5] = {{120, 130, 150, 255}, {205, 127, 50, 255}, {205, 215, 230, 255}, {255, 210, 90, 255}, {150, 240, 255, 255}};
}

void ResultsScene::onEnter() {
    g_.ui.setFocus(d_.run.completed && !d_.nextLevel.empty() ? "results/next" : "results/retry");
    // 2.84: the level's own planet behind the cards; « NOUVEAU RECORD » drops in after the time.
    scenery_.useLevel(d_.level);
    recordTitle_.restart(1.4f);
    // Cosmic Online (2.19): a finished official level of the campaign can be rated by a
    // connected player once the server has verified one of their times on it.
    const LevelEntry* e = g_.content.level(d_.run.levelId);
    if (d_.run.completed && !d_.editorTest && e && e->source == ContentSource::Builtin && !g_.online.communityInstall(d_.run.levelId) &&
        g_.online.usable() && g_.online.loggedIn()) {
        rating_ = std::make_shared<LevelRatingPanel>(g_);
        if (d_.online) rating_->watchAttempt(d_.online->attemptId);
        rating_->open(d_.run.levelId);
    }
}

void ResultsScene::update(float dt) {
    t_ += dt;
    dt_ = dt;
    scenery_.update(g_.ui, dt, cstyle::Motion::from(g_.settings.graphics.particleQuality, g_.settings.graphics.screenShake));
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
    // Collection (2.19): rewards just unlocked by the stars, once the score has rolled up.
    if (!rewardsChecked_ && t_ > 2.8f) {
        rewardsChecked_ = true;
        if (!g_.collection.fresh(g_.cosmetics, g_.cosmeticProgress()).empty()) g_.app.pushScene(makeNewRewards(g_));
    }
}

void ResultsScene::drawOnlineStatus(const Rect& area) {
    if (!d_.online || t_ < 1.0f) return;
    UIContext& ui = g_.ui;
    // Live status from the queue (it changes while the server verifies the run).
    const OnlineAttempt* a = g_.online.attempts().find(d_.online->attemptId);
    const OnlineAttempt& at = a ? *a : *d_.online;
    const float y = area.center().y;
    float x = area.x;
    Vec2 sz = ui.label("Cosmic Online", {x, y}, 20, ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
    x += sz.x + ui.s(20);
    for (const auto& sub : at.submissions) {
        std::string text = std::string(categoryLabel(sub.category)) + " " + recordStatusLabel(sub.status);
        if (sub.status == RecordStatus::Verified && sub.rank > 0) text += " n°" + std::to_string(sub.rank);
        float h = std::min(ui.s(26), area.h);
        float w = ui.text().measure(text, FontFace::Bold, h * 0.6f).x + h;
        if (x + w > area.right()) break;  // no room left: the rest of the pills are not drawn over the hint
        Color c = OnlineUi::statusColor(sub.status);
        Rect r{x, y - h * 0.5f, w, h};
        ui.r().fillRoundedRect(r, h * 0.5f, c.withAlphaF(0.16f));
        ui.r().drawRoundedRect(r, h * 0.5f, c.withAlphaF(0.85f), std::max(1.0f, ui.s(1.2f)));
        ui.label(text, r.center(), h * 0.6f / ui.scale(), c, Align::Center, FontFace::Bold, VAlign::Middle);
        x += w + ui.s(10);
    }
    std::string hint;
    if (!g_.online.usable()) hint = std::string(onlineStateLabel(g_.online.state())) + " : performance gardée en local.";
    else if (!g_.online.loggedIn()) hint = "Non connecté : envoyez-la depuis CLASSEMENTS (« Envoyer le record »).";
    else if (g_.online.state() == OnlineState::Offline) hint = "Hors ligne : envoi automatique au retour de la connexion.";
    else if (g_.online.syncing()) hint = "Envoi et vérification en cours…";
    const float room = area.right() - x - ui.s(10);
    if (!hint.empty() && room > ui.s(60)) {
        const float f = ui.fitFont(hint, FontFace::Body, 17, room, 0.8f);
        ui.label(ui.elide(hint, FontFace::Body, f, room), {area.right(), y}, f, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
    }
}

void ResultsScene::drawRace(const Rect& area) {
    if (!d_.raceScore || t_ < 0.9f) return;
    UIContext& ui = g_.ui;
    const float x = area.x, maxW = area.w;
    const float y = area.y + area.h * 0.28f;
    float bw = ui.extremeBadge({x, y}, std::min(ui.s(34), area.h * 0.5f));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", *d_.raceScore);
    std::string pct = buf;
    for (char& c : pct)
        if (c == '.') c = ',';
    std::string line = "Course à la vérification : " + pct + " %";
    if (d_.raceBossTotal > 0) line += " · boss " + std::to_string(d_.raceBosses) + "/" + std::to_string(d_.raceBossTotal);
    float tx = x + bw + ui.s(14);
    float fs = ui.fitFont(line, FontFace::Bold, 22, maxW - (tx - x));
    ui.label(ui.elide(line, FontFace::Bold, fs, maxW - (tx - x)), {tx, y}, fs, ui.theme().warm, Align::Left, FontFace::Bold, VAlign::Middle);
    std::string hint;
    if (!g_.online.usable() || !g_.online.loggedIn()) hint = "Connectez-vous à Cosmic Online pour entrer dans la course.";
    else if (d_.run.completed) hint = "Score recalculé par le serveur ; le premier à finir vérifie le niveau.";
    else hint = "Estimation : le serveur recalcule le score depuis le replay.";
    const float hf = ui.fitFont(hint, FontFace::Body, 17, maxW);
    ui.label(ui.elide(hint, FontFace::Body, hf, maxW), {x, area.y + area.h * 0.76f}, hf, ui.theme().textDim, Align::Left, FontFace::Body,
             VAlign::Middle);
}

void ResultsScene::drawBosses(const Rect& area) {
    // The race line takes this place on an EXTRÊME level.
    if (d_.raceScore || d_.bossKills.empty() || t_ < 0.9f) return;
    UIContext& ui = g_.ui;
    const float x = area.x, maxW = area.w;
    std::string line, sub;
    int noHit = 0;
    for (const auto& k : d_.bossKills) noHit += k.noHit ? 1 : 0;
    if (d_.bossKills.size() == 1) {
        const auto& k = d_.bossKills.front();
        line = k.bossName + " vaincu" + (k.noHit ? " sans dégât !" : "");
        sub = std::to_string(k.kills) + (k.kills == 1 ? "re victoire" : "e victoire") + " contre ce boss · détails dans Statistiques";
    } else {
        line = std::to_string(d_.bossKills.size()) + " boss vaincus";
        if (noHit > 0) line += " · " + std::to_string(noHit) + " sans dégât";
        sub = "Victoires et combats sans dégât : menu principal → Statistiques";
    }
    const float lf = ui.fitFont(line, FontFace::Bold, 22, maxW), sf = ui.fitFont(sub, FontFace::Body, 17, maxW);
    ui.label(ui.elide(line, FontFace::Bold, lf, maxW), {x, area.y + area.h * 0.28f}, lf, noHit > 0 ? ui.theme().success : ui.theme().warm,
             Align::Left, FontFace::Bold, VAlign::Middle);
    ui.label(ui.elide(sub, FontFace::Body, sf, maxW), {x, area.y + area.h * 0.76f}, sf, ui.theme().textDim, Align::Left, FontFace::Body,
             VAlign::Middle);
}

void ResultsScene::render(float /*alpha*/) {
    UIContext& ui = g_.ui;
    ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
    const RunResult& run = d_.run;
    const cstyle::Motion mo = cstyle::Motion::from(g_.settings.graphics.particleQuality, g_.settings.graphics.screenShake);
    const ResultsLayout L = ResultsLayout::compute(ui.screenW(), ui.screenH());
    const float u = L.unit;
    auto fz = [&](float ref) { return ref * u / ui.scale(); };  // a label size from reference pixels
    float k = std::min(1.0f, t_ / 0.5f);
    // 2.84: the level dimmed, then its own planet's layers over it (Cosmic kit).
    ui.r().fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{3, 5, 12, 255}.withAlphaF(0.78f * k));
    scenery_.render(ui, 0.0f, u, mo, 0.88f * k);

    // ---------------------------------------------------------------- title
    if (run.completed) title_.draw(ui, "NIVEAU TERMINÉ", L.title.center(), 50.0f * u, L.title.w, Align::Center, t_, mo, u);
    else cstyle::neonText(ui, "ÉCHEC", L.title.center(), 50.0f * u, L.title.w, Align::Center, t_, mo, u, ui.theme().danger);
    {
        const float f = ui.fitFont(d_.level.name, FontFace::Body, fz(18), L.levelName.w, 0.7f);
        ui.label(ui.elide(d_.level.name, FontFace::Body, f, L.levelName.w), L.levelName.center(), f, ui.theme().textDim, Align::Center, FontFace::Body,
                 VAlign::Middle);
    }
    if (!run.completed && !d_.failReason.empty()) {
        const float f = ui.fitFont(d_.failReason, FontFace::Body, fz(16), L.subLine.w, 0.7f);
        ui.label(ui.elide(d_.failReason, FontFace::Body, f, L.subLine.w), L.subLine.center(), f, ui.theme().danger, Align::Center, FontFace::Body,
                 VAlign::Middle);
    }
    // Ghost Racing: the final gap on each ghost raced.
    if (run.completed && d_.segments && !d_.segments->ghosts.empty() && t_ > 0.5f) {
        std::string line;
        for (const auto& gh : d_.segments->ghosts) {
            if (!line.empty()) line += "   ·   ";
            line += formatGhostDelta(gh.gap) + (gh.gap < -0.0005f ? " d'avance sur " : gh.gap > 0.0005f ? " de retard sur " : " : égalité avec ") + gh.name;
        }
        const float room = L.subLine.w;
        float f = ui.fitFont(line, FontFace::Bold, fz(14), room, 0.7f);
        Color c = d_.segments->ghosts.front().gap < -0.0005f ? ui.theme().success : d_.segments->ghosts.front().gap > 0.0005f ? ui.theme().danger : ui.theme().text;
        ui.label(ui.elide(line, FontFace::Bold, f, room), L.subLine.center(), f, c, Align::Center, FontFace::Bold, VAlign::Middle);
    }

    // ------------------------------------------------- left card: the run
    statsCard_.begin(ui, L.stats, t_, u, mo);
    {
        const Rect& tr = L.statsTime;
        ui.label("Temps réalisé", {tr.x, tr.y}, fz(13), Color{94, 224, 255, 255}, Align::Left, FontFace::Bold);
        // The time counts up (eased, the ticks in update()), then a light settles on it.
        const float p = std::clamp((t_ - 0.4f) / 1.2f, 0.0f, 1.0f);
        const float shown = run.time * (mo.reduced ? (p > 0.0f ? 1.0f : 0.0f) : applyEase(Ease::OutCubic, p));
        TextStyle ts;
        ts.face = FontFace::Title;
        ts.size = std::min(48.0f * u, tr.h * 0.62f);
        ts.color = p >= 1.0f ? Color::white() : Color{159, 243, 255, 255};
        ts.valign = VAlign::Bottom;
        ts.glow = 14.0f * u;
        ts.glowColor = Color{94, 224, 255, static_cast<uint8_t>(p >= 1.0f ? 90 : 50)};
        const std::string time = LevelTimer::format(shown);
        const Vec2 tsz = ui.text().draw(time, {tr.x, tr.bottom()}, ts);
        if (run.completed && d_.previous.bestTime > 0 && p >= 1.0f) {
            // The gap on the record, after the time (it fits, or is not shown).
            const std::string delta = LevelTimer::formatDelta(run.time - d_.previous.bestTime);
            const float df = fz(18);
            const float dw = ui.text().measure(delta, FontFace::Bold, ui.s(df)).x;
            if (tr.x + tsz.x + 16.0f * u + dw <= tr.right())
                ui.label(delta, {tr.x + tsz.x + 16.0f * u, tr.bottom() - 8.0f * u}, df, run.time < d_.previous.bestTime ? ui.theme().success : ui.theme().danger,
                         Align::Left, FontFace::Bold, VAlign::Bottom);
        }
    }
    {
        const Rect& rr = L.statsRows;
        auto row = [&](int index, const std::string& label, const std::string& value, Color vc = {}) {
            float appear = std::clamp((t_ - 0.4f - index * 0.12f) / 0.25f, 0.0f, 1.0f);
            if (appear <= 0.0f) return;
            const float y = rr.y + index * L.rowH, cy = y + L.rowH * 0.45f;
            const Color c = vc.a ? vc : ui.theme().text;
            const float vf = fz(17);
            const Vec2 vs = ui.label(value, {rr.right(), cy}, vf, c.withAlphaF(appear), Align::Right, FontFace::Bold, VAlign::Middle);
            const float room = rr.w - vs.x - 16.0f * u;  // the name never under its value
            const float lf = ui.fitFont(label, FontFace::Body, fz(16), room, 0.7f);
            ui.label(ui.elide(label, FontFace::Body, lf, room), {rr.x + (1 - appear) * 14.0f * u, cy}, lf, ui.theme().textDim.withAlphaF(appear),
                     Align::Left, FontFace::Body, VAlign::Middle);
            ui.r().fillRect({rr.x, y + L.rowH - 1.0f, rr.w, 1.0f}, Color{255, 255, 255, static_cast<uint8_t>(22 * appear)});
        };
        std::string best = d_.previous.bestTime > 0 ? LevelTimer::format(d_.previous.bestTime) : "—";
        row(0, "Record personnel", best, run.completed && d_.previous.bestTime > 0 && run.time < d_.previous.bestTime ? ui.theme().success : Color{});
        row(1, "Temps de référence", LevelTimer::format(d_.level.referenceTime));
        row(2, "Morts", std::to_string(run.deaths), run.deaths == 0 ? ui.theme().success : Color{});
        row(3, "Objets collectés", std::to_string(run.collectibles) + " / " + std::to_string(run.totalCollectibles));
        row(4, "Ennemis éliminés", std::to_string(run.kills));
        row(5, "Secrets", std::to_string(run.secrets) + " / " + std::to_string(run.totalSecrets));
        int scoreShown = static_cast<int>(run.score * std::clamp((t_ - 0.9f) / 1.3f, 0.0f, 1.0f));
        row(6, "Score", std::to_string(scoreShown), ui.theme().warm);
    }
    statsCard_.end(ui, t_);

    // ------------------------------------------- right card: the medal
    medalCard_.begin(ui, L.medalCard, t_, u, mo);
    if (d_.update.newBestTime && t_ > 1.4f)  // only when it is one: a record, or the first victory
        recordTitle_.draw(ui, d_.update.firstCompletion ? "PREMIÈRE VICTOIRE !" : "NOUVEAU RECORD !", L.record.center(), 32.0f * u, L.record.w,
                          Align::Center, t_, mo, u);
    if (run.completed) cstyle::novaClip(ui, "victory", t_ - 0.5f, L.nova.center(), std::min(L.nova.w, L.nova.h));  // Nova's victory (row 17)
    const float medalAppear = std::clamp((t_ - 2.3f) / 0.5f, 0.0f, 1.0f);
    const int mi = static_cast<int>(run.medal);
    {
        const Vec2 mc = L.medal.center();
        if (medalAppear > 0.0f) {
            const float ms = L.medal.h * applyEase(Ease::OutBack, medalAppear);
            ui.r().drawGlow(mc, L.medal.h * medalAppear, kMedalColors[mi].withAlpha(70));
            ui.icon("ui/icons_medals.png", mi, 32, Rect::fromCenter(mc, {ms, ms}));
            const std::string name = run.medal == Medal::None ? "Pas de médaille" : std::string("Médaille ") + medalName(run.medal);
            const float f = ui.fitFont(name, FontFace::Bold, fz(19), L.medalName.w, 0.7f);
            ui.label(ui.elide(name, FontFace::Bold, f, L.medalName.w), L.medalName.center(), f, kMedalColors[mi].withAlphaF(medalAppear), Align::Center,
                     FontFace::Bold, VAlign::Middle);
        }
    }
    {
        // The stars of the medal, filling one after the other (bronze, argent, or, platine).
        const float r = std::min(L.stars.h * 0.5f, 18.0f * u), gap = 22.0f * u;
        const float total = 4 * 2 * r + 3 * gap;
        float x = L.stars.center().x - total * 0.5f + r;
        for (int i = 1; i <= 4; ++i) {
            const float st = 2.3f + 0.18f * (i - 1);
            const float fill = mi >= i ? (mo.reduced ? (t_ >= st ? 1.0f : 0.0f) : applyEase(Ease::OutBack, std::clamp((t_ - st) / 0.3f, 0.0f, 1.0f))) : 0.0f;
            if (fill > 0.0f && mi >= i) ui.r().drawGlow({x, L.stars.center().y}, r * 2.2f * std::min(1.0f, fill), kMedalColors[i].withAlpha(60));
            cstyle::star(ui, {x, L.stars.center().y}, r, std::min(1.0f, fill), kMedalColors[i], Color{255, 255, 255, 30}, Color{255, 255, 255, 70});
            x += 2 * r + gap;
        }
    }
    {
        ui.label("Objectifs de médaille", {L.goalsTitle.x, L.goalsTitle.center().y}, fz(14), ui.theme().textDim, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        float gy = L.goals.y;
        const float is = std::min(26.0f * u, L.goalH - 6.0f * u);
        for (int i = 1; i <= 4; ++i) {
            Medal m = static_cast<Medal>(i);
            bool got = mi >= i;
            ui.icon("ui/icons_medals.png", got ? i : 0, 32, {L.goals.x, gy + (L.goalH - is) * 0.5f, is, is});
            const std::string g = std::string(medalName(m)) + " : " + RecordsManager::medalCondition(d_.level, m);
            const float room = L.goals.w - is - 12.0f * u;
            const float f = ui.fitFont(g, FontFace::Body, fz(15), room, 0.75f);
            ui.label(ui.elide(g, FontFace::Body, f, room), {L.goals.x + is + 12.0f * u, gy + L.goalH * 0.5f}, f, got ? ui.theme().text : ui.theme().textDim,
                     Align::Left, FontFace::Body, VAlign::Middle);
            gy += L.goalH;
        }
    }
    medalCard_.end(ui, t_);

    // ------------------------------------- the race or bosses, the online status or the note
    drawRace(L.info);
    drawBosses(L.info);
    if (d_.online) {
        drawOnlineStatus(L.status);
    } else {
        std::string note;
        if (d_.editorTest) note = "Test depuis l'éditeur : les records ne sont pas enregistrés";
        else if (d_.practice) note = "Entraînement avec réapparitions : hors classements, aucun record";
        else if (d_.hardcore && !run.completed) note = "« Réessayer en entraînement » : réapparition sur place après une chute, hors classements";
        if (!note.empty()) {
            const float f = ui.fitFont(note, FontFace::Body, fz(13), L.status.w, 0.7f);
            ui.label(ui.elide(note, FontFace::Body, f, L.status.w), L.status.center(), f, ui.theme().textDim, Align::Center, FontFace::Body,
                     VAlign::Middle);
        }
    }

    // ------------------------------------------------------------- actions
    bool hasNext = run.completed && !d_.nextLevel.empty();
    bool hasRace = d_.raceScore.has_value() && !d_.raceKind.empty();
    // 2.48: a hardcore level lost: try it again as a practice run (respawns, not ranked).
    bool hasPractice = d_.hardcore && !run.completed && !d_.practice;
    int count = (hasNext ? 3 : 2) + (hasRace ? 1 : 0) + (hasPractice ? 1 : 0);
    const float gap = L.buttonGap;
    float bw = std::min(300.0f * u, (L.buttons.w - (count - 1) * gap) / static_cast<float>(count));
    float bx = L.buttons.center().x - (count * bw + (count - 1) * gap) * 0.5f;
    ui.pushId("results");
    // The actions answer after 0.6 s; drawn (invisible) before, they keep the focus given on arrival.
    const bool live = t_ > 0.6f;
    const float pop = std::clamp((t_ - 0.6f) / 0.3f, 0.0f, 1.0f);
    {
        int index = 0;
        auto plate = [&](const char* id, const std::string& label, const char* icon, Color accent) {
            cstyle::PlateEx p;
            p.plate = {label, "", icon, accent};
            p.wrap = true;
            const float appear = mo.reduced ? (live ? 1.0f : 0.0f) : std::clamp((t_ - 0.6f - 0.06f * index++) / 0.4f, 0.0f, 1.0f);
            const bool act = cstyle::plateButtonEx(ui, anims_, id, {bx, L.buttons.y, bw, L.buttons.h}, p, L.buttons.h / 64.0f, appear, t_, dt_, mo);
            bx += bw + gap;
            return act && live;
        };
        const Color cyan{94, 224, 255, 255};
        if (plate("retry", "Recommencer", "challenges", cyan)) {
            g_.app.popScene();
            game_.restart();
        }
        if (hasNext && plate("next", "Niveau suivant", "continue", {255, 205, 110, 255})) g_.app.switchScene(makeGameScene(g_, d_.nextLevel), 0.4f);
        if (hasPractice && plate("practice", "Réessayer en entraînement", "achievements", {190, 150, 255, 255})) {
            g_.app.popScene();
            game_.restartPractice();
        }
        if (hasRace && plate("race", "Course à la vérification", "stats", {255, 112, 136, 255}))
            g_.app.pushScene(makeRaceBoard(g_, d_.raceKind, d_.raceLevelId, d_.raceVersion, d_.raceHash));
        std::string back = d_.editorTest ? "Retour à l'éditeur" : d_.fromOnline ? "Retour au menu En ligne"
                           : d_.fromWorkshop ? "Retour à l'Atelier"
                                             : "Sélection des niveaux";
        const bool backAct = plate("back", back, "quit", {140, 152, 186, 255});
        // « Revoir la partie » and « Segments »: diamonds at the top left, their name under them.
        std::string cap;
        int di = 0;
        if (d_.replay && d_.replay->exact()) {
            if (cstyle::diamondButton(ui, anims_, "watch", L.diamonds[di++], L.diamondHalf, {"continue"}, pop, t_, dt_) && live)
                g_.app.pushScene(makeLocalReplayViewer(g_, *d_.replay, "Votre partie"));
            if (ui.focused("watch")) cap = "Revoir la partie";
        }
        if (d_.segments) {
            if (cstyle::diamondButton(ui, anims_, "segments", L.diamonds[di++], L.diamondHalf, {"stats"}, pop, t_, dt_) && live)
                g_.app.pushScene(makeSegments(g_, d_.segments));
            if (ui.focused("segments")) cap = "Segments";
        }
        cstyle::caption(ui, cap, L.caption, 14.0f, u, Color{255, 205, 110, 255});
        if (rating_ && live) {
            // Under « Classement en ligne », above the ghosts' line (top right).
            ui.pushId("rating");
            rating_->drawCompact(L.rating);
            ui.popId();
        }
        if (d_.online && !d_.fromOnline) {
            cstyle::PlateEx p;
            p.plate = {"Classement en ligne", "", "online", {110, 225, 150, 255}};
            if (cstyle::plateButtonEx(ui, anims_, "online", L.online, p, L.online.h / 64.0f, pop, t_, dt_, mo) && live) {
                if (const Json* c = g_.online.communityInstall(d_.run.levelId)) g_.app.switchScene(makeOnlineLevels(g_, jstr(*c, "id")), 0.4f);
                else g_.app.switchScene(makeOnlineRanking(g_, d_.run.levelId), 0.4f);
            }
        }
        if (backAct || (live && ui.backPressed())) {
            g_.app.popScene();
            game_.quitToMenu();
        }
    }
    ui.popId();
    ui.end();
}

}  // namespace cosmic
