#include "ui/screens/PauseScene.h"

#include "engine/core/Easing.h"
#include "game/GameServices.h"
#include "game/achievements/AchievementSuggestions.h"
#include "ui/screens/GameScene.h"
#include "ui/screens/LevelPowerKeysScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/PauseLayout.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

void PauseScene::onEnter() {
    g_.app.audio().setMusicDuck(0.35f);
    g_.uiSound("ui_confirm", 0.5f);
    g_.ui.setFocus("pause/resume");
    // « Succès suivis » (2.52): the counters with this run's progress, read once (nothing moves
    // while paused; the inputs scan the user folders).
    liveStats_ = game_.liveAchievementStats();
    achInputs_ = g_.achievementInputs();
    achInputs_.stats = &liveStats_;
}

void PauseScene::drawTracked(const Rect& r, const PauseLayout& L, const std::vector<const AchievementDef*>& rows, bool note) {
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    const float fs = std::max(16.0f, 11.5f / ui.s(1.0f));  // never under 11.5 pixels
    // Title, the count, and « Modifier » (opens Succès over the run, to tick or untick).
    const float headH = ui.s(L.headerH);
    const std::string edit = rows.empty() ? "Choisir des succès" : "Modifier";
    UIContext::ButtonStyle st;
    st.small = true;
    st.fontSize = fs;
    const float ebw = ui.text().measure(edit, FontFace::Bold, ui.s(fs)).x + ui.s(36);
    const Rect eb{r.right() - ebw, r.y + ui.s(3), ebw, headH - ui.s(8)};
    if (ui.button("trackedit", eb, edit, st)) {
        resumeFocus_ = "pause/trackedit";  // back on this button afterwards
        g_.app.pushScene(makeAchievementsFromPause(g_));
    }
    // « NOUVEAU » just left of the button (the button is too low for a pill on its edge).
    float room = eb.x - ui.s(12) - r.x;
    if (ui.newBadge("trackedit", eb, "pause.tracked_achievements", false, true)) {
        ui.drawNewPill(eb.x - ui.s(8), eb.center().y, true);
        room -= ui.newPillWidth(true) + ui.s(8);
    }
    const std::string title = "SUCCÈS SUIVIS";
    const std::string count = std::to_string(rows.size()) + " / " + std::to_string(AchievementBook::kMaxTracked);
    const float cw = ui.text().measure(count, FontFace::Body, ui.s(fs)).x;
    const float tf = std::max(ui.fitFont(title, FontFace::Bold, 18, std::max(ui.s(40), room - cw - ui.s(10)), 0.64f), 11.5f / ui.s(1.0f));
    const Vec2 tsz = ui.label(title, {r.x, r.y + headH * 0.5f}, tf, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
    if (tsz.x + ui.s(10) + cw <= room)
        ui.label(count, {r.x + tsz.x + ui.s(10), r.y + headH * 0.5f}, fs, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    float y = r.y + headH;
    if (rows.empty()) {
        const std::string invite = "Cochez jusqu'à 5 succès dans Succès pour suivre leur avancée ici.";
        const float f = std::max(ui.fitFont(invite, FontFace::Body, 15, r.w, 0.78f), 11.5f / ui.s(1.0f));
        ui.label(ui.elide(invite, FontFace::Body, f, r.w), {r.x, y + ui.s(L.emptyH) * 0.5f}, f, th.textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        return;
    }
    const float rowH = ui.s(L.rowH), barH = std::max(3.0f, ui.s(7));
    for (const AchievementDef* a : rows) {
        const AchievementProgress p = AchievementBook::progress(*a, achInputs_);
        const bool live = achievementMovesDuringRun(a->condition);
        const AchievementCategory* k = g_.achievementCatalog.category(a->category);
        const Color col = p.done() ? th.success : k ? k->color : th.accent;
        // Name on the left, numbers on the right (the name gives way), the bar under both.
        const std::string nums = achievementProgressText(*a, p) + (live ? "" : " *");
        const float textY = y + (rowH - barH - ui.s(4)) * 0.5f;
        const float nw = ui.text().measure(nums, FontFace::Bold, ui.s(fs)).x;
        ui.label(nums, {r.right(), textY}, fs, live ? th.text : th.textDim, Align::Right, FontFace::Bold, VAlign::Middle);
        ui.label(ui.elide(a->name, FontFace::Body, fs, r.w - nw - ui.s(14)), {r.x, textY}, fs, th.text, Align::Left, FontFace::Body, VAlign::Middle);
        const float f = std::clamp(static_cast<float>(p.value) / static_cast<float>(std::max<int64_t>(1, p.target)), 0.0f, 1.0f);
        const Rect bar{r.x, y + rowH - barH - ui.s(4), r.w, barH};
        ui.r().fillRoundedRect(bar, barH * 0.5f, Color{20, 28, 52, 255});
        if (f > 0.0f) ui.r().fillRoundedRect({bar.x, bar.y, std::max(bar.h, bar.w * f), bar.h}, barH * 0.5f, col);
        y += rowH;
    }
    if (note) {
        const std::string n = "* compté après la partie, pas pendant";
        const float f = std::max(ui.fitFont(n, FontFace::Body, 14, r.w, 0.82f), 11.5f / ui.s(1.0f));
        ui.label(ui.elide(n, FontFace::Body, f, r.w), {r.x, y + ui.s(L.noteH) * 0.5f}, f, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
}

void PauseScene::onExit() { g_.app.audio().setMusicDuck(1.0f); }

void PauseScene::onResume() {
    if (resumeFocus_.empty()) return;
    g_.ui.setFocus(resumeFocus_);
    resumeFocus_.clear();
}

void PauseScene::update(float dt) {
    t_ += dt;
    // Automated screenshots: --pause --online-open powerkeys[:…] opens « Touches de ce niveau ».
    if (!keysOpened_ && g_.onlineOpen.rfind("powerkeys", 0) == 0) {
        keysOpened_ = true;
        g_.app.pushScene(makeLevelPowerKeys(g_, game_.level(), game_.powerKeyExtra(), game_.carriedPowers()));
    }
}

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

    const bool ghosts = game_.hasGhosts();
    // « Touches de ce niveau » (2.31): when the level has powers used with a key of their own.
    const bool keys = !game_.powerKeys().powers.empty();
    // 2.51: « Invincible » for administrators in a training run.
    const bool invincible = game_.invincibleAllowed();
    // 2.52: « Succès suivis » under the buttons; the column is measured (PauseLayout): it shrinks
    // when the window is short, never overlapping, and the map keeps its place on the right.
    std::vector<const AchievementDef*> tracked;
    bool note = false;
    for (const std::string& id : g_.achievements.tracked())
        if (const AchievementDef* a = g_.achievementCatalog.find(id); a && !g_.achievements.unlocked(id)) {
            tracked.push_back(a);
            note = note || !achievementMovesDuringRun(a->condition);
        }
    const int buttons = 6 + (keys ? 1 : 0) + (invincible ? 1 : 0) + (ghosts ? 1 : 0);
    const PauseLayout L = pauseLayout(buttons, static_cast<int>(tracked.size()), note, ui.scale(), ui.screenH());
    float w = ui.s(560), h = ui.s(L.height);
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

    float bx = panel.x + ui.s(60), bw = panel.w - ui.s(120), bh = ui.s(L.buttonH);
    const float step = bh + ui.s(L.gap);
    float y = panel.y + ui.s(L.top);
    ui.pushId("pause");
    UIContext::ButtonStyle primary;
    primary.primary = true;
    bool resume = ui.button("resume", {bx, y, bw, bh}, "Reprendre", primary);
    y += step;
    if (ui.button("restart", {bx, y, bw, bh}, "Recommencer")) {
        g_.app.popScene();
        game_.restart();
    }
    y += step;
    bool openSettings = ui.button("settings", {bx, y, bw, bh}, "Paramètres");
    ui.newBadge("settings", {bx, y, bw, bh}, "menu.settings");
    if (openSettings) g_.app.pushScene(makeSettingsScene(g_, true));
    y += step;
    bool openGuide = ui.button("guide", {bx, y, bw, bh}, "Guide des pouvoirs");
    ui.newBadge("guide", {bx, y, bw, bh}, "powers.guide");  // « NOUVEAU »: cards of the guide changed (2.25)
    if (openGuide) g_.app.pushScene(makePowerGuide(g_));
    y += step;
    if (keys) {
        // The keys of this level's powers, for this player (each power keeps its own key).
        bool openKeys = ui.button("levelkeys", {bx, y, bw, bh}, "Touches de ce niveau");
        ui.newBadge("levelkeys", {bx, y, bw, bh}, "pause.level_keys");  // « NOUVEAU » (2.31)
        if (openKeys) g_.app.pushScene(makeLevelPowerKeys(g_, game_.level(), game_.powerKeyExtra(), game_.carriedPowers()));
        y += step;
    }
    if (invincible) {
        // Administrators: no damage in this training run, a fall goes back to solid ground.
        const bool on = game_.invincible();
        UIContext::ButtonStyle st;
        st.selected = on;
        const std::string label = std::string("Invincible (admin) : ") + (on ? "activé" : "désactivé");
        st.fontSize = ui.fitFont(label, FontFace::Bold, 26, bw - ui.s(24));
        if (ui.button("invincible", {bx, y, bw, bh}, label, st)) game_.setInvincible(!on);
        ui.newBadge("invincible", {bx, y, bw, bh}, "pause.invincible");
        y += step;
    }
    {
        // « Mode concentré »: the reduced HUD (also T / Partage in game).
        // A button like the others (label centred, same font and size), its state on a small
        // switch at the right; the label never reaches the switch.
        bool focus = g_.settings.gameplay.focusMode;
        const Rect fr{bx, y, bw, bh};
        if (ui.button("focus", fr, "")) {
            g_.settings.gameplay.focusMode = focus = !focus;
            g_.saveSettings();
        }
        const Rect track{fr.right() - ui.s(16) - ui.s(48), fr.center().y - ui.s(11), ui.s(48), ui.s(22)};
        const std::string fl = "Mode concentré";
        const float half = fr.center().x - (track.x - ui.s(10));
        const float ffs = ui.fitFont(fl, FontFace::Bold, 26.0f, 2.0f * half, 0.6f);
        ui.label(fl, fr.center(), ffs, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        ui.r().fillRoundedRect(track, ui.s(11), focus ? ui.theme().accent.withAlpha(210) : Color{40, 50, 70, 255});
        ui.r().fillCircle({focus ? track.right() - ui.s(11) : track.x + ui.s(11), track.center().y}, ui.s(8.5f), Color::white());
        y += step;
    }
    if (ghosts) {
        if (ui.button("ghosts", {bx, y, bw, bh}, game_.ghostsVisible() ? "Fantômes" : "Fantômes (masqués)")) g_.app.pushScene(makeGhostOptions(g_, &game_));
        y += step;
    }
    UIContext::ButtonStyle danger;
    danger.danger = true;
    std::string quitLabel = game_.editorTest()        ? "Retour à l'éditeur"
                            : game_.sectionTraining() ? "Retour à l'entraînement"  // its setup, same segment
                            : (confirmQuit_ ? "Confirmer : quitter le niveau" : "Retour au menu");
    if (ui.button("quit", {bx, y, bw, bh}, quitLabel, danger)) {
        if (game_.editorTest() || game_.sectionTraining() || confirmQuit_) {
            g_.app.popScene();
            game_.quitToMenu();
        } else {
            confirmQuit_ = true;
        }
    }
    drawTracked({bx, panel.y + ui.s(L.sectionTop(buttons)), bw, ui.s(L.sectionHeight(static_cast<int>(tracked.size()), note))}, L, tracked, note);
    ui.popId();
    if (resume || ui.backPressed() || (g_.app.input().menuPressed(Action::Pause) && t_ > 0.1f)) {
        g_.app.popScene();
    }
    Glyphs::drawHint(ui, Action::MenuConfirm, "Valider", {panel.x + ui.s(60), panel.bottom() - ui.s(40)}, ui.s(30));
    Glyphs::drawHint(ui, Action::MenuBack, "Reprendre", {panel.center().x + ui.s(20), panel.bottom() - ui.s(40)}, ui.s(30));
    ui.end();
}

}  // namespace cosmic
