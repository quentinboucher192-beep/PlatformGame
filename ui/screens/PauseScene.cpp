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

namespace {

/// `text` in at most `maxLines` lines no wider than `width` at `size` (unscaled), the last one
/// shortened with « … » when words are left over.
std::vector<std::string> wrapLines(UIContext& ui, const std::string& text, float size, float width, int maxLines) {
    std::vector<std::string> words, lines;
    std::string w;
    for (char ch : text) {
        if (ch == ' ') {
            if (!w.empty()) words.push_back(w);
            w.clear();
        } else {
            w += ch;
        }
    }
    if (!w.empty()) words.push_back(w);
    const float px = ui.s(size);
    std::string cur;
    size_t i = 0;
    for (; i < words.size(); ++i) {
        std::string cand = cur.empty() ? words[i] : cur + " " + words[i];
        if (!cur.empty() && ui.text().measure(cand, FontFace::Body, px).x > width) {
            lines.push_back(cur);
            cur = words[i];
            if (static_cast<int>(lines.size()) == maxLines) break;
        } else {
            cur = std::move(cand);
        }
    }
    if (static_cast<int>(lines.size()) < maxLines) {
        if (!cur.empty()) lines.push_back(cur);
    } else {
        // Words left over: the last line takes them, shortened.
        std::string rest = lines.back();
        for (; i < words.size(); ++i) rest += " " + words[i];
        lines.back() = ui.elide(rest, FontFace::Body, size, width);
    }
    for (auto& l : lines)
        if (ui.text().measure(l, FontFace::Body, px).x > width) l = ui.elide(l, FontFace::Body, size, width);
    return lines;
}

}  // namespace

void PauseScene::drawTrackedHeader(const Rect& r, float headH, float fs, int count) {
    // Title, the count, and « Modifier » (opens Succès over the run, to tick or untick).
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    const std::string edit = count == 0 ? "Choisir des succès" : "Modifier";
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
    const std::string countText = std::to_string(count) + " / " + std::to_string(AchievementBook::kMaxTracked);
    const float cw = ui.text().measure(countText, FontFace::Body, ui.s(fs)).x;
    const float tf = std::max(ui.fitFont(title, FontFace::Bold, 18, std::max(ui.s(40), room - cw - ui.s(10)), 0.64f), 11.5f / ui.s(1.0f));
    const Vec2 tsz = ui.label(title, {r.x, r.y + headH * 0.5f}, tf, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
    if (tsz.x + ui.s(10) + cw <= room)
        ui.label(countText, {r.x + tsz.x + ui.s(10), r.y + headH * 0.5f}, fs, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
}

void PauseScene::drawTrackedRow(const AchievementDef& a, const Rect& row, float nameH, float lineH, float barH, const std::vector<std::string>& desc,
                                float fs, float dfs) {
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    const AchievementProgress p = AchievementBook::progress(a, achInputs_);
    const bool live = achievementMovesDuringRun(a.condition);
    const AchievementCategory* k = g_.achievementCatalog.category(a.category);
    const Color col = p.done() ? th.success : k ? k->color : th.accent;
    // Name on the left, numbers on the right (the name gives way), the description under
    // them, the bar under everything.
    const std::string nums = achievementProgressText(a, p) + (live ? "" : " *");
    const float textY = row.y + nameH * 0.5f;
    const float nw = ui.text().measure(nums, FontFace::Bold, ui.s(fs)).x;
    ui.label(nums, {row.right(), textY}, fs, live ? th.text : th.textDim, Align::Right, FontFace::Bold, VAlign::Middle);
    ui.label(ui.elide(a.name, FontFace::Bold, fs, row.w - nw - ui.s(14)), {row.x, textY}, fs, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
    float y = row.y + nameH;
    for (const std::string& l : desc) {
        ui.label(l, {row.x, y + lineH * 0.5f}, dfs, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        y += lineH;
    }
    const float f = std::clamp(static_cast<float>(p.value) / static_cast<float>(std::max<int64_t>(1, p.target)), 0.0f, 1.0f);
    const Rect bar{row.x, y + ui.s(4), row.w, barH};
    ui.r().fillRoundedRect(bar, barH * 0.5f, Color{20, 28, 52, 255});
    if (f > 0.0f) ui.r().fillRoundedRect({bar.x, bar.y, std::max(bar.h, bar.w * f), bar.h}, barH * 0.5f, col);
}

void PauseScene::drawTracked(const Rect& r, const PauseLayout& L, const std::vector<const AchievementDef*>& rows, bool note) {
    // The column (no map on the right): name, numbers, one line of description when there is
    // room, the bar; then how many places are free.
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    const float fs = std::max(16.0f, 11.5f / ui.s(1.0f));  // never under 11.5 pixels
    const float dfs = std::max(14.0f, 11.5f / ui.s(1.0f));
    const float headH = ui.s(L.headerH);
    drawTrackedHeader(r, headH, fs, static_cast<int>(rows.size()));
    float y = r.y + headH;
    if (rows.empty()) {
        const std::string invite = "Cochez jusqu'à 5 succès dans Succès pour suivre leur avancée ici.";
        const float f = std::max(ui.fitFont(invite, FontFace::Body, 15, r.w, 0.78f), 11.5f / ui.s(1.0f));
        ui.label(ui.elide(invite, FontFace::Body, f, r.w), {r.x, y + ui.s(L.emptyH) * 0.5f}, f, th.textDim, Align::Left, FontFace::Body,
                 VAlign::Middle);
        return;
    }
    const float rowH = ui.s(L.rowH), barH = std::max(3.0f, ui.s(7));
    const bool withDesc = L.rowDescription();
    const float lineH = withDesc ? ui.s(20) : 0.0f;
    const float nameH = rowH - lineH - barH - ui.s(10);
    for (const AchievementDef* a : rows) {
        std::vector<std::string> desc;
        if (withDesc) desc = wrapLines(ui, a->description, dfs, r.w, 1);
        drawTrackedRow(*a, {r.x, y, r.w, rowH}, nameH, lineH, barH, desc, fs, dfs);
        y += rowH;
    }
    if (rows.size() < static_cast<size_t>(AchievementBook::kMaxTracked)) {
        const int free = AchievementBook::kMaxTracked - static_cast<int>(rows.size());
        const std::string t = free == 1 ? "1 place libre : « Modifier » pour suivre un autre succès"
                                        : std::to_string(free) + " places libres : « Modifier » pour suivre d'autres succès";
        const float f = std::max(ui.fitFont(t, FontFace::Body, 14, r.w, 0.8f), 11.5f / ui.s(1.0f));
        ui.label(ui.elide(t, FontFace::Body, f, r.w), {r.x, y + ui.s(L.emptyH) * 0.5f}, f, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        y += ui.s(L.emptyH);
    }
    if (note) {
        const std::string n = "* compté après la partie, pas pendant";
        const float f = std::max(ui.fitFont(n, FontFace::Body, 14, r.w, 0.82f), 11.5f / ui.s(1.0f));
        ui.label(ui.elide(n, FontFace::Body, f, r.w), {r.x, y + ui.s(L.noteH) * 0.5f}, f, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
    }
}

std::vector<int> PauseScene::trackedSideLines(const std::vector<const AchievementDef*>& rows, float width, int maxLines) {
    UIContext& ui = g_.ui;
    const float dfs = std::max(14.0f, 11.5f / ui.s(1.0f));
    std::vector<int> out;
    for (const AchievementDef* a : rows) out.push_back(std::max(1, static_cast<int>(wrapLines(ui, a->description, dfs, width, maxLines).size())));
    return out;
}

void PauseScene::drawTrackedSide(const Rect& r, const PauseSideTracked& M, const std::vector<const AchievementDef*>& rows, bool note,
                                 int maxLines) {
    // Under the map (2.60): the five places, each achievement with its description.
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    const float fs = std::max(17.0f, 11.5f / ui.s(1.0f));
    const float dfs = std::max(14.0f, 11.5f / ui.s(1.0f));
    const float headH = ui.s(M.headerH);
    drawTrackedHeader(r, headH, fs, static_cast<int>(rows.size()));
    float y = r.y + headH;
    const float barH = std::max(3.0f, ui.s(M.barH));
    for (const AchievementDef* a : rows) {
        const std::vector<std::string> desc = wrapLines(ui, a->description, dfs, r.w, maxLines);
        const int n = std::max(1, static_cast<int>(desc.size()));
        const float h = ui.s(M.rowHeight(n));
        drawTrackedRow(*a, {r.x, y, r.w, h}, ui.s(M.nameH), ui.s(M.lineH), barH, desc, fs, dfs);
        y += h;
    }
    for (int i = static_cast<int>(rows.size()); i < AchievementBook::kMaxTracked; ++i) {
        // A free place: a dashed slot, what it is for.
        const Rect slot{r.x, y, r.w, ui.s(M.freeH)};
        const float dash = ui.s(10), gap = ui.s(6);
        const Color c = th.textDim.withAlpha(90);
        for (float x = slot.x; x < slot.right(); x += dash + gap) {
            const float w = std::min(dash, slot.right() - x);
            ui.r().fillRect({x, slot.y, w, std::max(1.0f, ui.s(1.5f))}, c);
            ui.r().fillRect({x, slot.bottom() - std::max(1.0f, ui.s(1.5f)), w, std::max(1.0f, ui.s(1.5f))}, c);
        }
        for (float yy = slot.y; yy < slot.bottom(); yy += dash + gap) {
            const float h = std::min(dash, slot.bottom() - yy);
            ui.r().fillRect({slot.x, yy, std::max(1.0f, ui.s(1.5f)), h}, c);
            ui.r().fillRect({slot.right() - std::max(1.0f, ui.s(1.5f)), yy, std::max(1.0f, ui.s(1.5f)), h}, c);
        }
        const std::string t = "Place libre — « Modifier » pour suivre un succès";
        const float f = std::max(ui.fitFont(t, FontFace::Body, 14, slot.w - ui.s(24), 0.8f), 11.5f / ui.s(1.0f));
        ui.label(ui.elide(t, FontFace::Body, f, slot.w - ui.s(24)), {slot.x + ui.s(12), slot.center().y}, f, th.textDim, Align::Left,
                 FontFace::Body, VAlign::Middle);
        y += ui.s(M.freeH + 8.0f);
    }
    if (note) {
        const std::string n = "* compté après la partie, pas pendant";
        const float f = std::max(ui.fitFont(n, FontFace::Body, 14, r.w, 0.82f), 11.5f / ui.s(1.0f));
        ui.label(ui.elide(n, FontFace::Body, f, r.w), {r.x, y + ui.s(M.noteH) * 0.5f}, f, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
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

void PauseScene::drawMapPanel(const Rect& panel, const std::vector<const AchievementDef*>* tracked, bool note) {
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
    // 2.60: « Succès suivis » at the bottom, the legend just under the map. The descriptions
    // take two lines while the map keeps 150 px, one line otherwise.
    PauseSideTracked M;
    int descMax = 2;
    float trackedH = 0.0f;
    if (tracked) {
        const float hintRoom = ui.s(26) + ui.s(14);
        auto mapRoom = [&](float th) { return panel.bottom() - pad - th - ui.s(22) - lines * lineH - hintRoom - (panel.y + ui.s(96)); };
        trackedH = ui.s(M.height(trackedSideLines(*tracked, w, 2), note));
        if (mapRoom(trackedH) < ui.s(150)) {
            descMax = 1;
            trackedH = ui.s(M.height(trackedSideLines(*tracked, w, 1), note));
        }
    }
    const float trackedTop = panel.bottom() - pad - trackedH;
    const float legendTop = (tracked ? trackedTop - ui.s(22) : panel.bottom() - pad) - lines * lineH;
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
    if (tracked) {
        ui.separator({x, trackedTop - ui.s(11)}, w);
        drawTrackedSide({x, trackedTop, w, trackedH}, M, *tracked, note, descMax);
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
    // The map of what was explored sits on the right of the menu when the window has room;
    // « Succès suivis » then goes under it (2.60), the column keeping the buttons.
    float w = ui.s(560);
    GameWorld* mapWorld = game_.world();
    const float gap = ui.s(24);
    float mapW = mapWorld ? std::min(ui.s(820), ui.screenW() - ui.s(80) - w - gap) : 0.0f;
    if (mapW < ui.s(360)) mapW = 0.0f;
    const bool side = mapW > 0.0f;
    const PauseLayout L = pauseLayout(buttons, static_cast<int>(tracked.size()), note, ui.scale(), ui.screenH(), !side);
    float h = ui.s(L.height);
    if (side) {
        // Tall enough for the map (160 px at least), its legend and the five places.
        const PauseSideTracked M;
        const float inner = mapW - 2.0f * ui.s(24);
        const float need = ui.s(96 + 160 + 40 + 52 + 22 + 24) + ui.s(M.height(trackedSideLines(tracked, inner, 2), note));
        h = std::min(std::max(h, need), ui.screenH() - ui.s(40));
    }
    float total = w + (side ? gap + mapW : 0.0f);
    float slide = (1.0f - applyEase(Ease::OutCubic, k)) * ui.s(40);
    Rect panel{(ui.screenW() - total) * 0.5f, (ui.screenH() - h) * 0.5f + slide, w, h};
    if (side) drawMapPanel({panel.right() + gap, panel.y, mapW, h}, &tracked, note);
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
        // Never ranked: only in training runs, which record nothing (said on the button).
        const std::string label = std::string("Invincible (admin) : ") + (on ? "activé · non classé" : "désactivé");
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
                            : game_.tutorial()        ? (confirmQuit_ ? "Confirmer : passer le didacticiel" : "Passer le didacticiel")  // 2.64
                            : (confirmQuit_ ? "Confirmer : quitter le niveau" : "Retour au menu");
    if (ui.button("quit", {bx, y, bw, bh}, quitLabel, danger)) {
        if (game_.editorTest() || game_.sectionTraining() || confirmQuit_) {
            g_.app.popScene();
            game_.quitToMenu();
        } else {
            confirmQuit_ = true;
        }
    }
    if (!side)
        drawTracked({bx, panel.y + ui.s(L.sectionTop(buttons)), bw, ui.s(L.sectionHeight(static_cast<int>(tracked.size()), note))}, L, tracked,
                    note);
    ui.popId();
    if (resume || ui.backPressed() || (g_.app.input().menuPressed(Action::Pause) && t_ > 0.1f)) {
        g_.app.popScene();
    }
    Glyphs::drawHint(ui, Action::MenuConfirm, "Valider", {panel.x + ui.s(60), panel.bottom() - ui.s(40)}, ui.s(30));
    Glyphs::drawHint(ui, Action::MenuBack, "Reprendre", {panel.center().x + ui.s(20), panel.bottom() - ui.s(40)}, ui.s(30));
    ui.end();
}

}  // namespace cosmic
