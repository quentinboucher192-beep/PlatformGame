// Star map (planet selection) and level selection with records.

#include <cmath>

#include "game/GameServices.h"
#include "game/levels/LevelTimer.h"
#include "ui/online/RatingWidgets.h"
#include "ui/screens/MenuBackground.h"
#include "ui/screens/CreatureArt.h"
#include "ui/screens/LevelPowerKeysScene.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/ExtremeFrame.h"
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
            Vec2 ns = ui.label(unlocked || !p->teaser ? p->name : "Signal inconnu", {sp.x, sp.y + size * 0.66f + ui.s(4)}, 22,
                               unlocked ? ui.theme().text : ui.theme().textDim, Align::Center, FontFace::Bold);
            // « NOUVEAU » (2.34.1) after the name of a planet with something new in it (a level).
            if (unlocked && ui.isNew("planet." + p->id))
                ui.drawNewPill(sp.x + ns.x * 0.5f + ui.s(10) + ui.newPillWidth(true), sp.y + size * 0.66f + ui.s(4) + ns.y * 0.5f, true);
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
        Rect btn{panel.x + ui.s(34), panel.bottom() - ui.s(90), panel.w - ui.s(68), ui.s(62)};
        // The description stays above the progress row (measured: a smaller type rather than an
        // overlap on a short screen or for a long description, 2.52).
        const float descW = panel.w - ui.s(68);
        const float descRoom = btn.y - ui.s(92) - ui.s(8) - y;
        float descSize = 19.0f;
        while (descSize > 13.0f && ui.text().measureWrapped(desc, FontFace::Body, ui.s(descSize), descW).y > descRoom) descSize -= 1.0f;
        ui.labelWrapped(desc, {x, y}, descW, descSize, ui.theme().text);
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
        if (unlocked) ui.newBadge("planet_open", btn, "planet." + p.id);  // « NOUVEAU » (2.34.1): a new level inside
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
    LevelSelectScene(GameServices& g, std::string planet, std::string level, std::string fromBoss)
        : g_(g), planetId_(std::move(planet)), levelId_(std::move(level)), fromBoss_(std::move(fromBoss)) {}

    void onEnter() override {
        planet_ = g_.content.planet(planetId_);
        levels_ = g_.content.levelsForPlanet(planetId_);
        g_.playMusic(planet_ && !planet_->music.empty() ? planet_->music : "music/planet_map.ogg");
        const std::string& wanted = levelId_.empty() ? g_.progression.lastLevel : levelId_;
        for (size_t i = 0; i < levels_.size(); ++i)
            if (levels_[i]->id == wanted) selected_ = static_cast<int>(i);
        if (!levels_.empty()) g_.ui.setFocus("levels/l" + std::to_string(selected_));
        // Players' ratings of the levels (Cosmic Online, only when logged in; cached on disk).
        g_.online.refreshLevelRatings();
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
            if (isExtreme(*l)) drawExtremeRowAccent(ui, rr, static_cast<float>(i));
            // « NOUVEAU » (2.34.1) on a new level, until it gets the focus or a click.
            if (unlocked) ui.newBadge("l" + std::to_string(i), rr, "level." + l->id, true, true);
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
        Glyphs::drawHint(ui, Action::MenuBack, fromBoss_.empty() ? "Carte des planètes" : "Statistiques des boss", {x, fy}, ui.s(30));
        if (ui.backPressed()) {
            if (fromBoss_.empty()) g_.app.switchScene(makePlanetMap(g_));
            else g_.app.switchScene(makeBossStats(g_, fromBoss_), 0.3f);
        }
        ui.end();
    }

    /// The level's bosses between `left` and `right` (right-aligned), centred on `cy`: a
    /// portrait each, animated; when they do not all fit, the last place says how many more.
    /// The mouse over a portrait shows the boss's name under it.
    void drawBosses(float left, float right, float cy, float box, float gap) {
        UIContext& ui = g_.ui;
        const int n = static_cast<int>(cachedBosses_.size());
        {
            // Few bosses and room enough: each portrait with its name (on its left).
            const float nf = 18, ngap = ui.s(10);
            float need = 0.0f;
            for (const EnemyDefinition* d : cachedBosses_) need += ui.text().measure(d->name, FontFace::Bold, ui.s(nf)).x + ngap + box + gap * 2.0f;
            if (n <= 3 && need - gap * 2.0f <= right - left) {
                float x = right - (need - gap * 2.0f);
                for (const EnemyDefinition* d : cachedBosses_) {
                    Vec2 ns = ui.label(d->name, {x, cy}, nf, ui.theme().warm, Align::Left, FontFace::Bold, VAlign::Middle);
                    x += ns.x + ngap;
                    Rect b{x, cy - box * 0.5f, box, box};
                    ui.r().fillRoundedRect(b, ui.s(8), Color{12, 16, 32, 230});
                    ui.r().drawRoundedRect(b, ui.s(8), ui.theme().warm.withAlpha(170), std::max(1.0f, ui.s(1.5f)));
                    drawCreature(g_, *d, b.expanded(-ui.s(3)), t_, Color::white());
                    x += box + gap * 2.0f;
                }
                return;
            }
        }
        int fit = static_cast<int>((right - left + gap) / (box + gap));
        if (fit <= 0) return;
        const int shown = n <= fit ? n : std::max(0, fit - 1);
        const int slots = shown + (n > shown ? 1 : 0);
        float x = right - slots * box - (slots - 1) * gap;
        const Color frame = ui.theme().warm.withAlpha(170);
        const EnemyDefinition* hovered = nullptr;
        Rect hoverBox;
        for (int i = 0; i < shown; ++i) {
            Rect b{x, cy - box * 0.5f, box, box};
            ui.r().fillRoundedRect(b, ui.s(8), Color{12, 16, 32, 230});
            ui.r().drawRoundedRect(b, ui.s(8), frame, std::max(1.0f, ui.s(1.5f)));
            drawCreature(g_, *cachedBosses_[static_cast<size_t>(i)], b.expanded(-ui.s(3)), t_, Color::white());
            if (b.contains(ui.input().mousePos())) {
                hovered = cachedBosses_[static_cast<size_t>(i)];
                hoverBox = b;
            }
            x += box + gap;
        }
        if (n > shown) {
            Rect b{x, cy - box * 0.5f, box, box};
            ui.r().fillRoundedRect(b, ui.s(8), Color{12, 16, 32, 230});
            ui.r().drawRoundedRect(b, ui.s(8), frame, std::max(1.0f, ui.s(1.5f)));
            std::string more = "+" + std::to_string(n - shown);
            ui.label(more, b.center(), ui.fitFont(more, FontFace::Bold, 20, box - ui.s(8)), ui.theme().warm, Align::Center, FontFace::Bold, VAlign::Middle);
        }
        if (hovered) {
            const float fs = 17;
            Vec2 sz = ui.text().measure(hovered->name, FontFace::Bold, ui.s(fs));
            Rect tip{std::min(hoverBox.center().x - sz.x * 0.5f - ui.s(10), right - sz.x - ui.s(20)), hoverBox.bottom() + ui.s(6), sz.x + ui.s(20),
                     sz.y + ui.s(10)};
            ui.r().fillRoundedRect(tip, ui.s(6), Color{8, 12, 26, 245});
            ui.r().drawRoundedRect(tip, ui.s(6), frame, std::max(1.0f, ui.s(1.0f)));
            ui.label(hovered->name, tip.center(), fs, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        }
    }

    /// EXTRÊME and one life (read once per level).
    bool isExtreme(const LevelEntry& l) {
        auto it = extremeOf_.find(l.id);
        if (it != extremeOf_.end()) return it->second;
        bool e = false;
        if (auto data = g_.content.loadLevel(l.id)) e = data->extreme && data->oneLife();
        return extremeOf_[l.id] = e;
    }
    std::map<std::string, bool> extremeOf_;

    void drawDetails(const LevelEntry& l) {
        UIContext& ui = g_.ui;
        Rect panel{ui.s(720), ui.s(170), ui.screenW() - ui.s(780), ui.screenH() - ui.s(280)};
        if (cachedId_ != l.id) {
            cachedId_ = l.id;
            cached_ = g_.content.loadLevel(l.id);
            cachedKeyPowers_ = cached_ ? !levelKeyPowers(*cached_, g_.content).empty() : false;
            // The bosses of the level (placed in it), each once, in the level's order.
            cachedBosses_.clear();
            if (cached_)
                for (const LevelObject& o : cached_->objects) {
                    if (o.type != "enemy") continue;
                    const EnemyDefinition* def = g_.content.enemy(o.propStr("enemy"));
                    if (def && def->isBoss && std::find(cachedBosses_.begin(), cachedBosses_.end(), def) == cachedBosses_.end())
                        cachedBosses_.push_back(def);
                }
        }
        float x = panel.x + ui.s(36), y = panel.y + ui.s(30);
        float colW = (panel.w - ui.s(108)) * 0.5f;
        bool extreme = cached_ && cached_->extreme && cached_->oneLife();
        // An EXTRÊME level (one life) gets a frame that warns (2.32.3); the others the panel.
        if (extreme) drawExtremeFrame(ui, panel, 16, 0.0f, 84.0f);
        else ui.panel(panel, true, 16);
        {
            // The bosses of the level, on the right of the title (2.31.2): as many portraits as
            // fit, the others counted (« +N »); the title keeps room for one at least.
            const float bossBox = ui.s(44), bossGap = ui.s(8);
            const int nb = static_cast<int>(cachedBosses_.size());
            const float bossMin = nb > 0 ? ui.s(24) + bossBox : 0.0f;
            float room = panel.w - ui.s(72) - (extreme ? ui.s(170) : 0.0f) - bossMin;
            float tf = ui.fitFont(l.name, FontFace::Title, 40, room, 0.7f);
            Vec2 ts = ui.label(ui.elide(l.name, FontFace::Title, tf, room), {x, y}, tf, ui.theme().text, Align::Left, FontFace::Title);
            if (extreme) ui.extremeBadge({x + ts.x + ui.s(18), y + ts.y * 0.5f}, ui.s(42));
            if (nb > 0) drawBosses(x + ts.x + (extreme ? ui.s(170) : 0.0f) + ui.s(24), panel.right() - ui.s(36), y + ts.y * 0.5f, bossBox, bossGap);
        }
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
            // The players' rating (Cosmic Online, average computed by the server): a separate
            // line, never mixed with the difficulty computed above. Official levels only.
            if (l.source == ContentSource::Builtin) cy += drawCampaignRating(g_, l.id, {x, cy}, colW - ui.s(10), 19) + ui.s(6);
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
                                    (lives == 0 ? std::string("illimitées") : std::to_string(lives)) +
                                    (extreme ? std::string(" (EXTRÊME : course à la vérification)") : std::string{});
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
            line("Objets collectés", std::to_string(rec->mostCollectibles) + " / " + std::to_string(rec->totalCollectibles));
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
        // Ghost Racing: choose the ghosts (or a training segment) before the start.
        UIContext::ButtonStyle gs;
        gs.enabled = unlocked;
        Rect gbtn{btn.x - ui.s(16) - ui.s(250), btn.y, ui.s(250), btn.h};
        bool ghosts = ui.button("ghosts_level", gbtn, "Fantômes…", gs);
        ui.newBadge("ghosts_level", gbtn, "planet.ghosts");  // « NOUVEAU » (2.30.2): section training
        if (ghosts && unlocked) g_.app.pushScene(makeGhostSetup(g_, l.id));
        // The keys of its powers (2.31), for this player, before playing (also in the pause menu).
        if (cachedKeyPowers_ && cached_) {
            Rect kbtn{gbtn.x - ui.s(16) - ui.s(250), btn.y, ui.s(250), btn.h};
            UIContext::ButtonStyle ks;
            const std::string label = "Touches…";
            ks.fontSize = ui.fitFont(label, FontFace::Bold, 26, kbtn.w - ui.s(24), 0.6f);
            bool openKeys = ui.button("keys_level", kbtn, label, ks);
            ui.newBadge("keys_level", kbtn, "pause.level_keys");  // « NOUVEAU » (2.31)
            if (openKeys) g_.app.pushScene(makeLevelPowerKeys(g_, *cached_));
        }
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
    std::string levelId_;   // selected on arrival ("" = the last one played)
    std::string fromBoss_;  // opened from the boss statistics (« Retour » goes back there)
    const PlanetData* planet_ = nullptr;
    std::vector<const LevelEntry*> levels_;
    int selected_ = 0;
    float t_ = 0.0f;
    MenuBackground bg_;
    std::string cachedId_;
    std::optional<LevelData> cached_;
    std::vector<const EnemyDefinition*> cachedBosses_;  // the bosses placed in it (2.31.2)
    bool cachedKeyPowers_ = false;  // its powers include some used with a key of their own
};

}  // namespace

std::unique_ptr<Scene> makePlanetMap(GameServices& g) { return std::make_unique<PlanetMapScene>(g); }
std::unique_ptr<Scene> makeLevelSelect(GameServices& g, const std::string& planetId, const std::string& levelId, const std::string& fromBoss) {
    return std::make_unique<LevelSelectScene>(g, planetId, levelId, fromBoss);
}

}  // namespace cosmic
