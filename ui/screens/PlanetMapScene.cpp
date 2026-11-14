// Star map (planet selection) and level selection with records.

#include <cmath>

#include "game/GameServices.h"
#include "game/levels/LevelTimer.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

const Color kMedalColors[5] = {{120, 130, 150, 255}, {205, 127, 50, 255}, {205, 215, 230, 255}, {255, 210, 90, 255}, {150, 240, 255, 255}};

void drawStars(UIContext& ui, int count, float time, float diff) {
    for (int i = 0; i < count; ++i) {
        uint32_t h = hash2(i, 17, 3);
        float x = (h % 10000) / 10000.0f * ui.screenW();
        float y = ((h >> 12) % 10000) / 10000.0f * ui.screenH();
        float tw = 0.5f + 0.5f * std::sin(time * (1.0f + (h % 7) * 0.3f) + i);
        float sz = 1.0f + (h % 3) * diff;
        ui.r().fillRect({x, y, sz, sz}, Color{200, 230, 255, static_cast<uint8_t>(80 + 120 * tw)});
    }
}

// ================================================================ planet map

class PlanetMapScene : public Scene {
public:
    explicit PlanetMapScene(GameServices& g) : g_(g) {}

    void onEnter() override {
        g_.playMusic("music/planet_map.ogg");
        planets_ = g_.content.planets();
        for (size_t i = 0; i < planets_.size(); ++i)
            if (planets_[i]->id == g_.progression.lastPlanet) selected_ = static_cast<int>(i);
        focus_ = target(selected_);
    }
    void update(float dt) override {
        t_ += dt;
        focus_ = lerpv(focus_, target(selected_), damp(3.0f, dt));
        zoom_ = lerpf(zoom_, 1.0f, damp(2.0f, dt));
    }
    Vec2 planetPos(int i) const {
        const PlanetData* p = planets_[i];
        float ang = p->orbitPhase + t_ * p->orbitSpeed;
        return {std::cos(ang) * p->orbitRadius, std::sin(ang) * p->orbitRadius * 0.42f};
    }
    Vec2 target(int i) const { return planets_.empty() ? Vec2{} : planetPos(i) * 0.65f; }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Renderer& r = ui.r();
        float W = ui.screenW(), H = ui.screenH();
        r.fillRect({0, 0, W, H}, {5, 7, 16, 255});
        if (auto bg = g_.app.resources().texture("ui/map_background.png", false)) {
            float sc = std::max(W / bg->width(), H / bg->height()) * 1.1f;
            Vec2 off = focus_ * (-0.05f * ui.scale());
            r.drawTexture(*bg, nullptr, {(W - bg->width() * sc) * 0.5f + off.x, (H - bg->height() * sc) * 0.5f + off.y,
                                         bg->width() * sc, bg->height() * sc});
        }
        drawStars(ui, 160, t_, 0.6f);
        float scale = ui.scale() * 1.25f * zoom_;
        Vec2 center{W * 0.42f, H * 0.52f};
        auto toScreen = [&](Vec2 p) { return center + (p - focus_) * scale; };
        // Orbits
        for (const auto* p : planets_) {
            if (p->orbitRadius <= 0.0f) continue;  // the star at the centre has no orbit
            std::vector<Vec2> pts;
            int seg = 96;
            for (int k = 0; k <= seg; ++k) {
                float a = 2 * kPi * k / seg;
                Vec2 q = toScreen({std::cos(a) * p->orbitRadius, std::sin(a) * p->orbitRadius * 0.42f});
                if (k > 0) r.drawLine(pts.back(), q, Color{120, 160, 220, 45}, std::max(1.0f, ui.s(1.5f)));
                pts.push_back(q);
            }
        }
        // Sun
        Vec2 sun = toScreen({0, 0});
        r.drawGlow(sun, ui.s(260) * zoom_, {255, 200, 120, 90});
        if (auto st = g_.app.resources().texture("ui/planets/sun.png", false)) {
            float ss = ui.s(150) * zoom_;
            r.drawTexture(*st, nullptr, Rect::fromCenter(sun, {ss, ss}));
        }
        // Planets (sorted back to front by y)
        std::vector<int> order(planets_.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return planetPos(a).y < planetPos(b).y; });
        ui.pushId("map");
        for (int i : order) {
            const PlanetData* p = planets_[i];
            bool unlocked = g_.progression.planetUnlocked(*p, g_.content);
            Vec2 sp = toScreen(planetPos(i));
            float depth = 0.85f + 0.15f * (planetPos(i).y / std::max(1.0f, p->orbitRadius * 0.42f));
            float size = ui.s(130) * p->size * depth * zoom_ * (i == selected_ ? 1.15f : 1.0f);
            if (i == selected_) {
                float pulse = 0.8f + 0.2f * std::sin(t_ * 3.0f);
                r.drawRing(sp, size * 0.62f, ui.s(3), p->color.withAlphaF(0.7f * pulse));
                r.drawGlow(sp, size * 1.3f, p->atmosphere.withAlphaF(0.5f));
            }
            bool star = p->orbitRadius <= 0.0f;  // the sun itself (drawn above), opened at the end
            if (star) {
                if (unlocked) r.drawGlow(sp, size * (0.9f + 0.1f * std::sin(t_ * 2.0f)), p->color.withAlpha(90));
            } else if (auto tex = g_.app.resources().texture(p->illustration, false)) {
                Color tint = unlocked ? Color::white() : Color{60, 66, 90, 255};
                r.drawTexture(*tex, nullptr, Rect::fromCenter(sp, {size, size}), tint);
            }
            if (!unlocked) ui.icon("ui/icons_hud.png", 9, 16, Rect::fromCenter(sp, {ui.s(40), ui.s(40)}));
            Rect hit = Rect::fromCenter(sp, {size * 0.8f, size * 0.8f});
            if (ui.mouseClicked(hit)) {
                if (selected_ == i) open();
                selected_ = i;
                g_.uiSound("ui_move");
            }
            // Below the selection ring (radius 0.62 size), never across it.
            ui.label(unlocked || !p->teaser ? p->name : "Signal inconnu", {sp.x, sp.y + size * 0.66f + ui.s(4)}, 22,
                     unlocked ? ui.theme().text : ui.theme().textDim, Align::Center, FontFace::Bold);
        }
        ui.popId();

        InputManager& in = g_.app.input();
        int n = static_cast<int>(planets_.size());
        if (n > 0) {
            if (in.menuPressed(Action::MenuRight) || in.menuPressed(Action::MenuDown)) {
                selected_ = (selected_ + 1) % n;
                zoom_ = 0.96f;
                g_.uiSound("ui_move");
            }
            if (in.menuPressed(Action::MenuLeft) || in.menuPressed(Action::MenuUp)) {
                selected_ = (selected_ - 1 + n) % n;
                zoom_ = 0.96f;
                g_.uiSound("ui_move");
            }
            if (in.menuPressed(Action::MenuConfirm)) open();
            drawInfo(*planets_[selected_]);
        }
        ui.label("SYSTÈME KEPLER-VOSS", {ui.s(50), ui.s(40)}, 40, ui.theme().text, Align::Left, FontFace::Title);
        ui.label("Choisissez une destination", {ui.s(52), ui.s(96)}, 22, ui.theme().textDim);
        float fy = H - ui.s(44);
        float x = ui.s(50);
        x += Glyphs::drawHint(ui, Action::MenuLeft, "/", {x, fy}, ui.s(30));
        x += Glyphs::drawHint(ui, Action::MenuRight, "Changer de planète", {x + ui.s(6), fy}, ui.s(30)) + ui.s(40);
        x += Glyphs::drawHint(ui, Action::MenuConfirm, "Explorer", {x, fy}, ui.s(30)) + ui.s(40);
        Glyphs::drawHint(ui, Action::MenuBack, "Menu principal", {x, fy}, ui.s(30));
        if (ui.backPressed()) g_.app.switchScene(makeMainMenu(g_));
        ui.end();
    }

    void drawInfo(const PlanetData& p) {
        UIContext& ui = g_.ui;
        bool unlocked = g_.progression.planetUnlocked(p, g_.content);
        float w = ui.s(560), h = std::min(ui.s(820), ui.screenH() - ui.s(60));
        Rect panel{ui.screenW() - w - ui.s(50), (ui.screenH() - h) * 0.5f, w, h};
        ui.panel(panel, true, 18);
        float x = panel.x + ui.s(34), y = panel.y + ui.s(30);
        if (unlocked && !p.banner.empty()) {
            if (auto b = g_.app.resources().texture(p.banner)) {
                Rect br{panel.x + ui.s(16), panel.y + ui.s(16), panel.w - ui.s(32), (panel.w - ui.s(32)) * 9.0f / 16.0f};
                ui.r().drawTexture(*b, nullptr, br);
                ui.r().drawRoundedRect(br, ui.s(4), ui.theme().border, ui.s(1.5f));
                y = br.bottom() + ui.s(20);
            }
        }
        std::string name = unlocked || !p.teaser ? p.name : "Signal inconnu";
        ui.label(name, {x, y}, 40, p.color, Align::Left, FontFace::Title);
        y += ui.s(54);
        ui.label(unlocked || !p.teaser ? p.subtitle : planetTypeLabel(p.type), {x, y}, 22, ui.theme().textDim);
        y += ui.s(40);
        // Planet difficulty: mean of its levels' computed ratings (the planet file's value
        // until they are known).
        std::string diff = std::to_string(p.difficulty) + "/5";
        {
            float sum = 0.0f;
            int n = 0;
            bool all = true;
            for (const LevelEntry* le : g_.content.levelsForPlanet(p.id)) {
                if (const DifficultyRating* r = g_.ratings.get(*le)) {
                    sum += r->score;
                    ++n;
                } else {
                    all = false;
                }
            }
            if (n > 0 && all) {
                DifficultyRating mean;
                mean.score = std::round(sum / static_cast<float>(n) * 2.0f) / 2.0f;
                diff = mean.label();
            }
        }
        ui.label(std::string(planetTypeLabel(p.type)) + "   Difficulté " + diff, {x, y}, 20, ui.theme().warm);
        y += ui.s(38);
        std::string desc = unlocked || !p.teaser ? p.description
                                                 : "Les coordonnées de ce monde sont encore en cours de décodage. "
                                                   "Terminez les missions disponibles pour en apprendre davantage.";
        ui.labelWrapped(desc, {x, y}, panel.w - ui.s(68), 19, ui.theme().text);
        Rect btn{panel.x + ui.s(34), panel.bottom() - ui.s(90), panel.w - ui.s(68), ui.s(62)};
        // Progress row anchored above the button, so long descriptions never push it below.
        y = btn.y - ui.s(92);
        auto levels = g_.content.levelsForPlanet(p.id);
        int done = 0;
        for (const auto* l : levels) done += g_.progression.levelCompleted(l->id) ? 1 : 0;
        if (!levels.empty()) {
            ui.label(std::to_string(done) + " / " + std::to_string(levels.size()) + " niveaux terminés", {x, y}, 22,
                     ui.theme().text, Align::Left, FontFace::Bold);
            y += ui.s(36);
            float mx = x;
            for (const auto* l : levels) {
                const LevelRecord* rec = g_.records.get(l->id);
                int medal = rec ? static_cast<int>(rec->bestMedal) : 0;
                ui.icon("ui/icons_medals.png", medal, 32, {mx, y, ui.s(36), ui.s(36)});
                mx += ui.s(42);
            }
        }
        UIContext::ButtonStyle st;
        st.primary = true;
        st.enabled = unlocked;
        if (ui.button("planet_open", btn, unlocked ? "Explorer " + p.name : "Verrouillée", st)) open();
    }

    void open() {
        if (planets_.empty()) return;
        const PlanetData* p = planets_[selected_];
        if (!g_.progression.planetUnlocked(*p, g_.content)) {
            g_.uiSound("ui_error");
            return;
        }
        g_.progression.lastPlanet = p->id;
        g_.app.switchScene(makeLevelSelect(g_, p->id));
    }

private:
    GameServices& g_;
    std::vector<const PlanetData*> planets_;
    int selected_ = 0;
    float t_ = 0.0f;
    Vec2 focus_;
    float zoom_ = 0.9f;
};

// ============================================================= level select

class LevelSelectScene : public Scene {
public:
    LevelSelectScene(GameServices& g, std::string planet) : g_(g), planetId_(std::move(planet)) {}

    void onEnter() override {
        planet_ = g_.content.planet(planetId_);
        levels_ = g_.content.levelsForPlanet(planetId_);
        g_.playMusic(planet_ && !planet_->music.empty() ? planet_->music : "music/planet_map.ogg");
        for (size_t i = 0; i < levels_.size(); ++i)
            if (levels_[i]->id == g_.progression.lastLevel) selected_ = static_cast<int>(i);
        if (!levels_.empty()) g_.ui.setFocus("levels/l" + std::to_string(selected_));
    }
    void update(float dt) override {
        t_ += dt;
        bg_.update(dt);
    }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        bg_.render(g_, 0.45f);
        std::string title = planet_ ? planet_->name : planetId_;
        ui.label(title, {ui.s(60), ui.s(40)}, 52, planet_ ? planet_->color : ui.theme().accent, Align::Left, FontFace::Title);
        if (planet_) ui.label(planet_->subtitle, {ui.s(62), ui.s(108)}, 24, ui.theme().textDim);

        Rect list{ui.s(60), ui.s(170), ui.s(620), ui.screenH() - ui.s(280)};
        ui.panel(list, false, 16);
        float rowH = ui.s(96);
        ui.beginScroll("levels_scroll", list.expanded(-ui.s(12)), levels_.size() * (rowH + ui.s(10)));
        ui.pushId("levels");
        float y = list.y + ui.s(12) - ui.scrollOffset();
        for (size_t i = 0; i < levels_.size(); ++i) {
            const LevelEntry* l = levels_[i];
            bool unlocked = g_.progression.levelUnlocked(*l, g_.content);
            const LevelRecord* rec = g_.records.get(l->id);
            std::string sub = std::string(contentSourceLabel(l->source));
            if (rec && rec->bestTime > 0) sub += "  —  Record " + LevelTimer::format(rec->bestTime);
            if (!unlocked) sub = "Verrouillé : terminez le niveau précédent";
            Rect rr{list.x + ui.s(12), y, list.w - ui.s(36), rowH};
            Color accent = rec ? kMedalColors[static_cast<int>(rec->bestMedal)] : Color{};
            if (ui.listItem("l" + std::to_string(i), rr, l->name, static_cast<int>(i) == selected_, sub, accent)) {
                if (selected_ == static_cast<int>(i) && unlocked) play();
                selected_ = static_cast<int>(i);
            }
            if (ui.focused("l" + std::to_string(i))) selected_ = static_cast<int>(i);
            if (rec) ui.icon("ui/icons_medals.png", static_cast<int>(rec->bestMedal), 32,
                             {rr.right() - ui.s(64), rr.center().y - ui.s(24), ui.s(48), ui.s(48)});
            if (!unlocked) ui.icon("ui/icons_hud.png", 9, 16, {rr.right() - ui.s(56), rr.center().y - ui.s(16), ui.s(32), ui.s(32)});
            y += rowH + ui.s(10);
        }
        ui.popId();
        ui.endScroll();
        if (levels_.empty())
            ui.labelWrapped("Aucun niveau sur cette planète pour l'instant. Créez-en un avec l'éditeur ou importez un pack .cplanet.",
                            {list.x + ui.s(30), list.y + ui.s(30)}, list.w - ui.s(60), 22, ui.theme().textDim);
        if (!levels_.empty()) drawDetails(*levels_[std::clamp(selected_, 0, static_cast<int>(levels_.size()) - 1)]);

        float fy = ui.screenH() - ui.s(44);
        float x = ui.s(60);
        x += Glyphs::drawHint(ui, Action::MenuConfirm, "Jouer", {x, fy}, ui.s(30)) + ui.s(40);
        Glyphs::drawHint(ui, Action::MenuBack, "Carte des planètes", {x, fy}, ui.s(30));
        if (ui.backPressed()) g_.app.switchScene(makePlanetMap(g_));
        ui.end();
    }

    void drawDetails(const LevelEntry& l) {
        UIContext& ui = g_.ui;
        Rect panel{ui.s(720), ui.s(170), ui.screenW() - ui.s(780), ui.screenH() - ui.s(280)};
        ui.panel(panel, true, 16);
        if (cachedId_ != l.id) {
            cachedId_ = l.id;
            cached_ = g_.content.loadLevel(l.id);
        }
        float x = panel.x + ui.s(36), y = panel.y + ui.s(30);
        float colW = (panel.w - ui.s(108)) * 0.5f;
        ui.label(l.name, {x, y}, 40, ui.theme().text, Align::Left, FontFace::Title);
        y += ui.s(60);
        if (!l.author.empty()) {
            ui.label("par " + l.author, {x, y}, 20, ui.theme().textDim);
            y += ui.s(32);
        }
        Vec2 ds = ui.labelWrapped(l.description, {x, y}, panel.w - ui.s(72), 21, ui.theme().text);
        y += ds.y + ui.s(26);
        const LevelRecord* rec = g_.records.get(l.id);
        float cy = y;
        // Left column: level info and medal goals
        if (cached_) {
            const LevelData& d = *cached_;
            // Computed from the level itself (game/levels/DifficultyRating.h), never typed in.
            const DifficultyRating* rating = g_.ratings.get(l);
            std::string diff = rating ? rating->label() : "calcul…";
            ui.label("Difficulté " + diff + "   —   " + timerModeLabel(d.timerMode), {x, cy}, 21, ui.theme().warm);
            cy += ui.s(30);
            if (rating) {
                // Within its column (the records are on the right).
                Vec2 rs = ui.labelWrapped(rating->summary(), {x, cy}, colW - ui.s(10), 16, ui.theme().textDim);
                cy += rs.y + ui.s(8);
            }
            if (d.timerMode == TimerMode::Limited || d.timerMode == TimerMode::Challenge) {
                ui.label("Durée maximale : " + LevelTimer::format(d.timeLimit, false), {x, cy}, 21, ui.theme().text);
                cy += ui.s(32);
            }
            ui.label("Temps de référence : " + LevelTimer::format(d.referenceTime, false), {x, cy}, 21, ui.theme().text);
            cy += ui.s(32);
            {
                // The level's rules, always said before playing.
                char dmg[32];
                std::snprintf(dmg, sizeof dmg, "%.1f", d.damageFactor);
                for (char& ch : dmg)
                    if (ch == '.') ch = ',';
                int lives = d.livesAllowed();
                std::string rules = std::string("Dégâts reçus : ×") + dmg + "   —   Vies : " +
                                    (lives == 0 ? std::string("illimitées") : std::to_string(lives));
                bool hard = d.damageFactor > 1.0f || lives > 0;
                ui.label(rules, {x, cy}, ui.fitFont(rules, FontFace::Body, 21, colW - ui.s(10)), hard ? ui.theme().warm : ui.theme().text);
            }
            cy += ui.s(44);
            ui.label("Médailles", {x, cy}, 22, ui.theme().textDim, Align::Left, FontFace::Bold);
            cy += ui.s(36);
            for (int i = 1; i <= 4; ++i) {
                bool got = rec && static_cast<int>(rec->bestMedal) >= i;
                ui.icon("ui/icons_medals.png", got ? i : 0, 32, {x, cy, ui.s(32), ui.s(32)});
                ui.label(std::string(medalName(static_cast<Medal>(i))) + " : " + RecordsManager::medalCondition(d, static_cast<Medal>(i)),
                         {x + ui.s(44), cy + ui.s(16)}, 20, got ? ui.theme().text : ui.theme().textDim, Align::Left,
                         FontFace::Body, VAlign::Middle);
                cy += ui.s(40);
            }
        }
        // Right column: records + local leaderboard
        float rx = x + colW + ui.s(36), ry = y;
        ui.label("Records personnels", {rx, ry}, 22, ui.theme().textDim, Align::Left, FontFace::Bold);
        ry += ui.s(38);
        auto line = [&](const std::string& k, const std::string& v) {
            ui.label(k, {rx, ry}, 20, ui.theme().textDim);
            ui.label(v, {rx + colW, ry}, 21, ui.theme().text, Align::Right, FontFace::Bold);
            ry += ui.s(32);
        };
        line("Temps joué", playTimeText(g_.progression.levelPlayTime(l.id)));
        if (rec && rec->completions > 0) {
            line("Meilleur temps", LevelTimer::format(rec->bestTime));
            line("Meilleur score", std::to_string(rec->bestScore));
            line("Moins de morts", std::to_string(std::max(0, rec->fewestDeaths)));
            line("Collectibles", std::to_string(rec->mostCollectibles) + " / " + std::to_string(rec->totalCollectibles));
            line("Ennemis éliminés", std::to_string(rec->mostKills));
            line("Réalisé le", rec->bestTimeDate);
            line("Victoires / essais", std::to_string(rec->completions) + " / " + std::to_string(rec->attempts));
            std::string pw;
            for (const auto& p : rec->bestTimePowers) pw += (pw.empty() ? "" : ", ") + p;
            line("Pouvoirs (record)", pw.empty() ? "aucun" : pw);
            ry += ui.s(12);
            ui.label("Classement local", {rx, ry}, 22, ui.theme().textDim, Align::Left, FontFace::Bold);
            ry += ui.s(34);
            int rank = 1;
            const float listBottom = panel.bottom() - ui.s(100);  // above the "Jouer" button
            for (const auto& e : g_.records.providers().front()->top(l.id, 5)) {
                if (ry + ui.s(30) > listBottom) break;
                ui.icon("ui/icons_medals.png", static_cast<int>(e.medal), 32, {rx, ry - ui.s(4), ui.s(26), ui.s(26)});
                ui.label(std::to_string(rank++) + ".  " + LevelTimer::format(e.time) + "   " + std::to_string(e.score) + " pts",
                         {rx + ui.s(36), ry}, 19, ui.theme().text);
                ui.label(e.date, {rx + colW, ry}, 17, ui.theme().textDim, Align::Right);
                ry += ui.s(30);
            }
        } else {
            ui.labelWrapped(rec && rec->attempts > 0 ? "Pas encore terminé (" + std::to_string(rec->attempts) + " essais)."
                                                     : "Aucun record pour l'instant.",
                            {rx, ry}, colW, 20, ui.theme().textDim);
        }
        bool unlocked = g_.progression.levelUnlocked(l, g_.content);
        UIContext::ButtonStyle st;
        st.primary = true;
        st.enabled = unlocked;
        Rect btn{panel.right() - ui.s(360), panel.bottom() - ui.s(92), ui.s(320), ui.s(64)};
        if (ui.button("play_level", btn, unlocked ? "Jouer" : "Verrouillé", st)) play();
    }

    void play() {
        if (levels_.empty()) return;
        const LevelEntry* l = levels_[std::clamp(selected_, 0, static_cast<int>(levels_.size()) - 1)];
        if (!g_.progression.levelUnlocked(*l, g_.content)) {
            g_.uiSound("ui_error");
            return;
        }
        g_.app.switchScene(makeGameScene(g_, l->id), 0.5f);
    }

private:
    GameServices& g_;
    std::string planetId_;
    const PlanetData* planet_ = nullptr;
    std::vector<const LevelEntry*> levels_;
    int selected_ = 0;
    float t_ = 0.0f;
    MenuBackground bg_;
    std::string cachedId_;
    std::optional<LevelData> cached_;
};

}  // namespace

std::unique_ptr<Scene> makePlanetMap(GameServices& g) { return std::make_unique<PlanetMapScene>(g); }
std::unique_ptr<Scene> makeLevelSelect(GameServices& g, const std::string& planetId) {
    return std::make_unique<LevelSelectScene>(g, planetId);
}

}  // namespace cosmic
