// Star map (planet selection) and level selection with records.

#include <chrono>
#include <cmath>
#include <future>

#include "engine/core/FileUtil.h"
#include "engine/core/Random.h"
#include "game/GameServices.h"
#include "game/levels/LevelTimer.h"
#include "ui/online/RatingWidgets.h"
#include "ui/screens/CosmicScreensLayout.h"
#include "ui/screens/CreatureArt.h"
#include "ui/screens/LevelPowerKeysScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/TasUi.h"
#include "ui/widgets/CosmicStyle2.h"
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
        // 2.84: the planet chosen in the background (its own sky and layers), the card and « Explorer ».
        scenery_.start(g_.content);
        if (!planets_.empty()) scenery_.show(planets_[static_cast<size_t>(selected_)]->id);
        g_.ui.setFocus("map/planet_open");
        title_.restart(0.0f);
    }
    void update(float dt) override {
        t_ += dt;
        dt_ = dt;
        zoom_ = lerpf(zoom_, 1.0f, damp(2.0f, dt));
        scenery_.update(g_.ui, dt, motion());
    }
    Vec2 planetPos(int i) const {
        const PlanetData* p = planets_[i];
        float ang = p->orbitPhase + t_ * p->orbitSpeed;
        return {std::cos(ang) * p->orbitRadius, std::sin(ang) * p->orbitRadius * 0.42f};
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        Renderer& r = ui.r();
        const cstyle::Motion mo = motion();
        const PlanetMapLayout L = PlanetMapLayout::compute(ui.screenW(), ui.screenH());
        const float u = L.unit;
        scenery_.render(ui, L.map.right(), u, mo);
        drawStars(ui, mo.particles <= 0 ? 60 : 120, t_, 0.6f);
        // The system, scaled to fit the map (never under the card), the sun at its centre.
        const float K = L.mapScale * zoom_;  // screen pixels per orbit unit
        const float k = K / 1.25f;            // the old drawing's « ui.s(1) »
        const Vec2 center = L.map.center();
        auto toScreen = [&](Vec2 p) { return center + p * K; };
        r.pushClip(L.map);
        for (const auto* p : planets_) {
            if (p->orbitRadius <= 0.0f) continue;  // the star at the centre has no orbit
            std::vector<Vec2> pts;
            int seg = 96;
            for (int q = 0; q <= seg; ++q) {
                float a = 2 * kPi * q / seg;
                Vec2 s = toScreen({std::cos(a) * p->orbitRadius, std::sin(a) * p->orbitRadius * 0.42f});
                if (q > 0) r.drawLine(pts.back(), s, Color{120, 160, 220, 55}, std::max(1.0f, 1.5f * k));
                pts.push_back(s);
            }
        }
        Vec2 sun = toScreen({0, 0});
        r.drawGlow(sun, 260 * k, {255, 200, 120, 90});
        if (auto st = g_.app.resources().texture("ui/planets/sun.png", false)) r.drawTexture(*st, nullptr, Rect::fromCenter(sun, {150 * k, 150 * k}));
        // Planets (sorted back to front by y)
        std::vector<int> order(planets_.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return planetPos(a).y < planetPos(b).y; });
        ui.pushId("map");
        const float nameSize = std::max(16.0f, 22.0f * k / ui.scale());
        for (int i : order) {
            const PlanetData* p = planets_[i];
            bool unlocked = g_.progression.planetUnlocked(*p, g_.content);
            Vec2 sp = toScreen(planetPos(i));
            float depth = 0.85f + 0.15f * (planetPos(i).y / std::max(1.0f, p->orbitRadius * 0.42f));
            float size = 130 * k * p->size * depth * (i == selected_ ? 1.15f : 1.0f);
            if (i == selected_) {
                float pulse = 0.8f + 0.2f * std::sin(t_ * 3.0f);
                r.drawRing(sp, size * 0.62f, std::max(1.5f, 3 * k), p->color.withAlphaF(0.7f * pulse));
                r.drawGlow(sp, size * 1.3f, p->atmosphere.withAlphaF(0.5f));
            }
            bool star = p->orbitRadius <= 0.0f;  // the sun itself (drawn above), opened at the end
            if (star) {
                if (unlocked) r.drawGlow(sp, size * (0.9f + 0.1f * std::sin(t_ * 2.0f)), p->color.withAlpha(90));
            } else if (auto tex = g_.app.resources().texture(p->illustration, false)) {
                Color tint = unlocked ? Color::white() : Color{60, 66, 90, 255};
                r.drawTexture(*tex, nullptr, Rect::fromCenter(sp, {size, size}), tint);
            }
            if (!unlocked) ui.icon("ui/icons_hud.png", 9, 16, Rect::fromCenter(sp, {40 * k, 40 * k}));
            Rect hit = Rect::fromCenter(sp, {size * 0.8f, size * 0.8f});
            if (L.map.contains(ui.input().mousePos()) && ui.mouseClicked(hit)) {
                if (selected_ == i) open();
                select(i);
            }
            // Below the selection ring (radius 0.62 size), never across it.
            Vec2 ns = ui.label(unlocked || !p->teaser ? p->name : "Signal inconnu", {sp.x, sp.y + size * 0.66f + 4 * k}, nameSize,
                               unlocked ? ui.theme().text : ui.theme().textDim, Align::Center, FontFace::Bold);
            // « NOUVEAU » (2.34.1) after the name of a planet with something new in it (a level).
            if (unlocked && ui.isNew("planet." + p->id))
                ui.drawNewPill(sp.x + ns.x * 0.5f + ui.s(10) + ui.newPillWidth(true), sp.y + size * 0.66f + 4 * k + ns.y * 0.5f, true);
        }
        r.popClip();

        InputManager& in = g_.app.input();
        int n = static_cast<int>(planets_.size());
        if (n > 0) {
            // The arrows choose the planet (the focus stays on « Explorer »).
            if (in.menuPressed(Action::MenuRight) || in.menuPressed(Action::MenuDown)) {
                select((selected_ + 1) % n);
                ui.consumeNavigation(true, true);
            }
            if (in.menuPressed(Action::MenuLeft) || in.menuPressed(Action::MenuUp)) {
                select((selected_ - 1 + n) % n);
                ui.consumeNavigation(true, true);
            }
            drawInfo(*planets_[selected_], L, mo);
        }
        // « Menu principal »: a diamond (its name beside it), also Échap / B.
        bool backNew = false;
        const bool backAct = cstyle::diamondButton(ui, anims_, "back", L.back, L.diamondHalf, {"quit"}, 1.0f, t_, dt_, &backNew);
        cstyle::caption(ui, "Menu principal", L.backCaption, 15.0f, u, ui.focused("back") ? Color{255, 205, 110, 255} : Color{140, 152, 186, 255});
        ui.popId();
        // Enter with nothing focused (a click elsewhere): the planet chosen.
        if (n > 0 && !ui.hasFocus() && in.menuPressed(Action::MenuConfirm)) open();
        title_.draw(ui, "SYSTÈME KEPLER-VOSS", {L.title.x, L.title.center().y}, 44.0f * u, L.title.w, Align::Left, t_, mo, u);
        {
            const float f = ui.fitFont("Choisissez une destination", FontFace::Body, 16.0f * u / ui.scale(), L.subtitle.w, 0.7f);
            ui.label(ui.elide("Choisissez une destination", FontFace::Body, f, L.subtitle.w), {L.subtitle.x, L.subtitle.center().y}, f,
                     Color{140, 152, 186, 255}, Align::Left, FontFace::Body, VAlign::Middle);
        }
        const float gh = 24.0f * u;
        cstyle::hintBar(ui,
                        {{Action::MenuLeft, "/", 6.0f * u}, {Action::MenuRight, "Changer de planète"}, {Action::MenuConfirm, "Explorer"},
                         {Action::MenuBack, "Menu principal"}},
                        {L.hints.x, L.hints.center().y}, gh, 28.0f * u, L.hints.w);
        if (backAct || ui.backPressed()) g_.app.switchScene(makeMainMenu(g_));
        ui.end();
    }

    void select(int i) {
        if (i == selected_) return;
        selected_ = i;
        zoom_ = 0.96f;
        g_.uiSound("ui_move");
        scenery_.show(planets_[static_cast<size_t>(i)]->id);
        card_.flicker(t_);
    }

    /// The planet chosen, in the holographic card: its banner, type and difficulty, name, levels
    /// done, description, medals; « Explorer » under it.
    void drawInfo(const PlanetData& p, const PlanetMapLayout& L, const cstyle::Motion& mo) {
        UIContext& ui = g_.ui;
        const float u = L.unit;
        bool unlocked = g_.progression.planetUnlocked(p, g_.content);
        card_.begin(ui, L.card, t_, u, mo);
        Renderer& r = ui.r();
        if (!p.banner.empty() && unlocked) {
            if (auto b = g_.app.resources().texture(p.banner)) {
                const Rect& br = L.banner;
                const float tw = static_cast<float>(b->width()), th = static_cast<float>(b->height());
                Rect src{0, 0, tw, th};  // cropped to the frame, never stretched
                if (tw / th > br.w / br.h) src.w = th * br.w / br.h, src.x = (tw - src.w) * 0.5f;
                else src.h = tw * br.h / br.w, src.y = (th - src.h) * 0.5f;
                r.drawTexture(*b, &src, br);
                r.drawRect(br, p.color.withAlpha(150), std::max(1.0f, u));
            }
        } else if (auto tex = g_.app.resources().texture(p.illustration, false)) {
            const float s = std::min(L.banner.w, L.banner.h) * 0.9f;
            r.drawGlow(L.banner.center(), s * 0.7f, p.atmosphere.withAlphaF(0.35f));
            r.drawTexture(*tex, nullptr, Rect::fromCenter(L.banner.center(), {s, s}), unlocked ? Color::white() : Color{60, 66, 90, 255});
        }
        // Planet difficulty: mean of its levels' computed ratings (the planet file's value
        // until they are known).
        std::string diff = std::to_string(p.difficulty) + "/5";
        {
            float sum = 0.0f;
            int n = 0;
            bool all = true;
            for (const LevelEntry* le : g_.content.levelsForPlanet(p.id)) {
                if (const DifficultyRating* dr = g_.ratings.get(*le)) {
                    sum += dr->score;
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
        auto levels = g_.content.levelsForPlanet(p.id);
        int done = 0;
        for (const auto* l : levels) done += g_.progression.levelCompleted(l->id) ? 1 : 0;
        cstyle::HoloCard::Text tx;
        tx.kicker = std::string(planetTypeLabel(p.type)) + "   Difficulté " + diff;
        tx.head = unlocked || !p.teaser ? p.name : "Signal inconnu";
        tx.big = unlocked || !p.teaser ? p.subtitle : planetTypeLabel(p.type);
        tx.bigColor = p.color;
        tx.para = unlocked || !p.teaser ? p.description
                                        : "Les coordonnées de ce monde sont encore en cours de décodage. "
                                          "Terminez les missions disponibles pour en apprendre davantage.";
        cstyle::HoloCard::Text fitted = tx;
        {
            // The subtitle is a sentence: in the paragraph's place when it is long (the « big » line is one line).
            const float bf = 30.0f * u;
            if (ui.text().measure(tx.big, FontFace::Title, bf * 0.6f).x > L.cardText.w) {
                fitted.big.clear();
                fitted.para = tx.big + "\n" + tx.para;
            }
        }
        cstyle::HoloCard::text(ui, L.cardText, fitted, u);
        if (!levels.empty()) {
            const Rect& pr = L.progress;
            const std::string line = std::to_string(done) + " / " + std::to_string(levels.size()) + " niveaux terminés";
            const float f = ui.fitFont(line, FontFace::Bold, 15.0f * u / ui.scale(), pr.w, 0.7f);
            ui.label(ui.elide(line, FontFace::Bold, f, pr.w), {pr.x, pr.y}, f, ui.theme().text, Align::Left, FontFace::Bold);
            const float is = std::min(26.0f * u, (pr.w + 4.0f * u) / static_cast<float>(levels.size()) - 4.0f * u);
            float mx = pr.x;
            for (const auto* l : levels) {
                const LevelRecord* rec = g_.records.get(l->id);
                int medal = rec ? static_cast<int>(rec->bestMedal) : 0;
                ui.icon("ui/icons_medals.png", medal, 32, {mx, pr.bottom() - is - 4.0f * u, is, is});
                mx += is + 4.0f * u;
            }
        }
        card_.end(ui, t_);
        cstyle::PlateEx pl;
        pl.plate = {"Explorer", unlocked ? p.name : std::string("Verrouillée"), "campaign", p.color};
        pl.enabled = unlocked;
        if (unlocked) pl.plate.feature = "planet." + p.id;  // « NOUVEAU » (2.34.1): a new level inside
        if (cstyle::plateButtonEx(ui, anims_, "planet_open", L.explore, pl, u, 1.0f, t_, dt_, mo)) open();
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
    cstyle::Motion motion() const { return cstyle::Motion::from(g_.settings.graphics.particleQuality, g_.settings.graphics.screenShake); }

    GameServices& g_;
    std::vector<const PlanetData*> planets_;
    int selected_ = 0;
    float t_ = 0.0f, dt_ = 0.0f;
    float zoom_ = 0.9f;
    cstyle::PlanetScenery scenery_;
    cstyle::ChromeText title_;
    cstyle::HoloCard card_;
    cstyle::Anims anims_;
};

// ============================================================= level select

class LevelSelectScene : public Scene {
public:
    LevelSelectScene(GameServices& g, std::string planet, std::string level, std::string fromBoss, std::string fromAsset = {})
        : g_(g), planetId_(std::move(planet)), levelId_(std::move(level)), fromBoss_(std::move(fromBoss)), fromAsset_(std::move(fromAsset)) {}

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
        // 2.84: the planet's own scenery behind; each level's EXTRÊME and bosses read off the main thread.
        scenery_.start(g_.content);
        scenery_.show(planetId_, true);
        std::vector<fs::path> files;
        for (const LevelEntry* l : levels_) files.push_back(l->path);
        cardsLoading_ = std::async(std::launch::async, [files] {
            std::vector<CardInfo> out;
            for (const auto& f : files) {
                CardInfo c;
                if (auto j = FileUtil::loadJson(f))
                    if (auto d = LevelData::fromJson(*j)) {
                        c.extreme = d->extreme && d->oneLife();
                        for (const LevelObject& o : d->objects)
                            if (o.type == "enemy") c.enemies.push_back(o.propStr("enemy"));
                    }
                out.push_back(std::move(c));
            }
            return out;
        });
    }
    void update(float dt) override {
        t_ += dt;
        dt_ = dt;
        scenery_.update(g_.ui, dt, motion());
        if (cardsLoading_.valid() && cardsLoading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            cards_ = cardsLoading_.get();
            for (CardInfo& c : cards_) {  // the bosses among the enemies placed, each once
                for (const std::string& id : c.enemies) {
                    const EnemyDefinition* def = g_.content.enemy(id);
                    if (def && def->isBoss && std::find(c.bosses.begin(), c.bosses.end(), def) == c.bosses.end()) c.bosses.push_back(def);
                }
                c.enemies.clear();
            }
        }
    }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        const cstyle::Motion mo = motion();
        const LevelSelectLayout L = LevelSelectLayout::compute(ui.screenW(), ui.screenH());
        const float u = L.unit;
        scenery_.render(ui, L.list.right() + 40.0f * u, u, mo);
        const std::string title = planet_ ? planet_->name : planetId_;
        title_.draw(ui, title, {L.title.x, L.title.center().y}, 46.0f * u, L.title.w, Align::Left, t_, mo, u);
        if (planet_) {
            const float f = ui.fitFont(planet_->subtitle, FontFace::Body, 16.0f * u / ui.scale(), L.subtitle.w, 0.7f);
            ui.label(ui.elide(planet_->subtitle, FontFace::Body, f, L.subtitle.w), {L.subtitle.x, L.subtitle.center().y}, f, planet_->color,
                     Align::Left, FontFace::Body, VAlign::Middle);
        }

        // ----- the level cards (cockpit plates): name, source and record, medal and its stars,
        // EXTRÊME, bosses, locked, « NOUVEAU »
        const Rect& list = L.list;
        const float rowH = L.cardH, step = L.cardH + L.cardGap;
        const Rect view = list;
        ui.beginScroll("levels_scroll", view, levels_.size() * step);
        ui.pushId("levels");
        float y = view.y + 2.0f * u - ui.scrollOffset();
        const float cardW = view.w - 14.0f * u - 30.0f * u;  // the scroll bar, and room for the focused card's slide
        for (size_t i = 0; i < levels_.size(); ++i) {
            const LevelEntry* l = levels_[i];
            const bool unlocked = g_.progression.levelUnlocked(*l, g_.content);
            const LevelRecord* rec = g_.records.get(l->id);
            std::string sub = std::string(contentSourceLabel(l->source));
            if (rec && rec->bestTime > 0) sub += "  —  Record " + LevelTimer::format(rec->bestTime);
            if (!unlocked) sub = "Verrouillé : terminez le niveau précédent";
            const Rect rr{view.x + 6.0f * u, y, cardW, rowH};
            const CardInfo* info = i < cards_.size() ? &cards_[i] : nullptr;
            const int medal = rec ? static_cast<int>(rec->bestMedal) : 0;
            const float pu = rowH / 66.0f;  // the plate's unit
            cstyle::PlateEx pl;
            pl.plate = {l->name, sub, "campaign", rec ? kMedalColors[medal] : (planet_ ? planet_->color : Color{94, 224, 255, 255})};
            pl.enabled = unlocked;
            pl.selected = static_cast<int>(i) == selected_;
            if (unlocked) pl.plate.feature = "level." + l->id;  // « NOUVEAU » (2.34.1), until it gets the focus or a click
            pl.rightW = 124.0f * pu;
            pl.drawRight = [&, unlocked, rec, medal, info, pu](const Rect& room, float) {
                // Top row: EXTRÊME and the bosses; bottom row: the stars of the medal, the medal.
                const float cy1 = room.center().y - 13.0f * pu, cy2 = room.center().y + 12.0f * pu;
                float right = room.right();
                if (info && !info->bosses.empty()) {
                    const std::string b = info->bosses.size() > 1 ? "BOSS ×" + std::to_string(info->bosses.size()) : std::string("BOSS");
                    const float h = 18.0f * pu, fs = 11.0f * pu;
                    const float w = ui.text().measure(b, FontFace::Bold, fs).x + 12.0f * pu;
                    const Rect pill{right - w, cy1 - h * 0.5f, w, h};
                    ui.r().fillRoundedRect(pill, h * 0.5f, ui.theme().warm.withAlpha(46));
                    ui.r().drawRoundedRect(pill, h * 0.5f, ui.theme().warm.withAlpha(200), std::max(1.0f, pu));
                    ui.label(b, pill.center(), fs / ui.scale(), ui.theme().warm, Align::Center, FontFace::Bold, VAlign::Middle);
                    right = pill.x - 6.0f * pu;
                }
                if (info && info->extreme) {
                    float h = 18.0f * pu;
                    const float w = cstyle::extremeBadgeWidth(ui, h);
                    if (right - w < room.x) h *= std::max(0.4f, (right - room.x) / std::max(1.0f, w));
                    ui.extremeBadge({right - cstyle::extremeBadgeWidth(ui, h), cy1}, h);
                }
                const float ms = 24.0f * pu;
                if (!unlocked) ui.icon("ui/icons_hud.png", 9, 16, Rect::fromCenter({room.right() - ms * 0.5f, cy2}, {ms, ms}));
                else if (rec) ui.icon("ui/icons_medals.png", medal, 32, Rect::fromCenter({room.right() - ms * 0.5f, cy2}, {ms, ms}));
                for (int s = 0; s < 4; ++s)
                    cstyle::star(ui, {room.right() - ms - 8.0f * pu - (3 - s) * 18.0f * pu - 8.0f * pu, cy2}, 8.0f * pu, s < medal ? 1.0f : 0.0f,
                                 kMedalColors[std::max(1, medal)], Color{255, 255, 255, 40});
            };
            const std::string id = "l" + std::to_string(i);
            if (cstyle::plateButtonEx(ui, anims_, id, rr, pl, pu, 1.0f, t_, dt_, mo)) {
                if (selected_ == static_cast<int>(i) && unlocked) play();
                else if (selected_ == static_cast<int>(i)) g_.uiSound("ui_error");
                choose(static_cast<int>(i));
            }
            if (ui.focused(id)) choose(static_cast<int>(i));
            if (info && info->extreme) drawExtremeRowAccent(ui, rr, static_cast<float>(i));
            y += step;
        }
        ui.popId();
        ui.endScroll();
        if (levels_.empty())
            ui.labelWrapped("Aucun niveau sur cette planète pour l'instant. Créez-en un avec l'éditeur ou importez un pack .cplanet.",
                            {list.x + 20.0f * u, list.y + 20.0f * u}, list.w - 40.0f * u, 16.0f * u / ui.scale(), ui.theme().textDim);
        ui.pushId("levels");
        if (!levels_.empty()) drawDetails(*levels_[std::clamp(selected_, 0, static_cast<int>(levels_.size()) - 1)], L, mo);
        // The way back: a diamond with its name, also Échap / B.
        const std::string backLabel = !fromAsset_.empty() ? "Administration" : fromBoss_.empty() ? "Carte des planètes" : "Statistiques des boss";
        const bool backAct = cstyle::diamondButton(ui, anims_, "back", L.back, L.diamondHalf, {"quit"}, 1.0f, t_, dt_);
        cstyle::caption(ui, backLabel, L.backCaption, 15.0f, u, ui.focused("back") ? Color{255, 205, 110, 255} : Color{140, 152, 186, 255});
        ui.popId();
        cstyle::hintBar(ui, {{Action::MenuConfirm, "Jouer"}, {Action::MenuBack, backLabel}}, {L.hints.x, L.hints.center().y}, 24.0f * u, 28.0f * u,
                        L.hints.w);
        if (backAct || ui.backPressed()) {
            if (!fromAsset_.empty()) g_.app.switchScene(makeAdmin(g_, "assets:" + fromAsset_), 0.3f);  // 2.63
            else if (fromBoss_.empty()) g_.app.switchScene(makePlanetMap(g_));
            else g_.app.switchScene(makeBossStats(g_, fromBoss_), 0.3f);
        }
        ui.end();
    }

    void choose(int i) {
        if (i == selected_) return;
        selected_ = i;
        card_.flicker(t_);
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

    /// The level chosen, in the holographic card (its name, EXTRÊME, bosses, author, description,
    /// difficulty, ratings, rules, medal goals, records and the local leaderboard), then « Jouer »
    /// and the diamonds Fantômes… and Touches… under it.
    void drawDetails(const LevelEntry& l, const LevelSelectLayout& L, const cstyle::Motion& mo) {
        UIContext& ui = g_.ui;
        const float u = L.unit;
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
        const Rect panel = L.card;
        bool extreme = cached_ && cached_->extreme && cached_->oneLife();
        // An EXTRÊME level (one life) gets a frame that warns (2.32.3) under the card.
        if (extreme) drawExtremeFrame(ui, panel, 16, 0.0f, 40.0f);
        card_.begin(ui, panel, t_, u, mo);
        float x = panel.x + ui.s(32), y = panel.y + ui.s(26);
        const float inner = panel.w - ui.s(64);
        float colW = (inner - ui.s(36)) * 0.5f;
        {
            // The bosses of the level, on the right of the title (2.31.2): as many portraits as
            // fit, the others counted (« +N »); the title keeps room for one at least.
            const float bossBox = ui.s(44), bossGap = ui.s(8);
            const int nb = static_cast<int>(cachedBosses_.size());
            const float bossMin = nb > 0 ? ui.s(24) + bossBox : 0.0f;
            const float badgeW = extreme ? ui.s(18) + cstyle::extremeBadgeWidth(ui, ui.s(42)) : 0.0f;
            float room = inner - badgeW - bossMin;
            float tf = ui.fitFont(l.name, FontFace::Title, 40, room, 0.7f);
            Vec2 ts = ui.label(ui.elide(l.name, FontFace::Title, tf, room), {x, y}, tf, ui.theme().text, Align::Left, FontFace::Title);
            if (extreme) ui.extremeBadge({x + ts.x + ui.s(18), y + ts.y * 0.5f}, ui.s(42));
            if (nb > 0) drawBosses(x + ts.x + badgeW + ui.s(24), panel.right() - ui.s(32), y + ts.y * 0.5f, bossBox, bossGap);
        }
        y += ui.s(60);
        if (!l.author.empty()) {
            const std::string by = "par " + l.author;
            ui.label(ui.elide(by, FontFace::Body, 20, inner), {x, y}, 20, ui.theme().textDim);
            y += ui.s(32);
        }
        {
            // The description in at most a quarter of the card (smaller type rather than an overlap).
            float ds = 21.0f;
            while (ds > 14.0f && ui.text().measureWrapped(l.description, FontFace::Body, ui.s(ds), inner).y > panel.h * 0.22f) ds -= 1.0f;
            Vec2 d = ui.labelWrapped(l.description, {x, y}, inner, ds, ui.theme().text);
            y += d.y + ui.s(22);
        }
        const LevelRecord* rec = g_.records.get(l.id);
        float cy = y;
        const float bottom = panel.bottom() - ui.s(20);
        // Left column: level info and medal goals
        if (cached_) {
            const LevelData& d = *cached_;
            // Computed from the level itself (game/levels/DifficultyRating.h), never typed in.
            const DifficultyRating* rating = g_.ratings.get(l);
            std::string diff = rating ? rating->label() : "calcul…";
            const std::string dl = "Difficulté " + diff + "   —   " + timerModeLabel(d.timerMode);
            ui.label(dl, {x, cy}, ui.fitFont(dl, FontFace::Body, 21, colW - ui.s(10)), ui.theme().warm);
            cy += ui.s(30);
            if (rating) {
                // Within its column (the records are on the right).
                Vec2 rs = ui.labelWrapped(rating->summary(), {x, cy}, colW - ui.s(10), 16, ui.theme().textDim);
                cy += rs.y + ui.s(8);
            }
            // The players' rating (Cosmic Online, average computed by the server): a separate
            // line, never mixed with the difficulty computed above. Official levels only.
            if (l.source == ContentSource::Builtin) cy += drawCampaignRating(g_, l.id, {x, cy}, colW - ui.s(10), 19) + ui.s(6);
            auto plain = [&](const std::string& s) {
                ui.label(s, {x, cy}, ui.fitFont(s, FontFace::Body, 21, colW - ui.s(10)), ui.theme().text);
                cy += ui.s(32);
            };
            if (d.timerMode == TimerMode::Limited || d.timerMode == TimerMode::Challenge) plain("Durée maximale : " + LevelTimer::format(d.timeLimit, false));
            plain("Temps de référence : " + LevelTimer::format(d.referenceTime, false));
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
                const float rf = ui.fitFont(rules, FontFace::Body, 21, colW - ui.s(10));
                ui.label(ui.elide(rules, FontFace::Body, rf, colW - ui.s(10)), {x, cy}, rf, hard ? ui.theme().warm : ui.theme().text);
            }
            cy += ui.s(40);
            // The medal goals: each a medal and the stars it gives, as many rows as fit.
            if (cy + ui.s(34) <= bottom) {
                ui.label("Médailles", {x, cy}, 22, ui.theme().textDim, Align::Left, FontFace::Bold);
                cy += ui.s(34);
            }
            for (int i = 1; i <= 4 && cy + ui.s(36) <= bottom; ++i) {
                bool got = rec && static_cast<int>(rec->bestMedal) >= i;
                ui.icon("ui/icons_medals.png", got ? i : 0, 32, {x, cy, ui.s(32), ui.s(32)});
                const std::string g = std::string(medalName(static_cast<Medal>(i))) + " : " + RecordsManager::medalCondition(d, static_cast<Medal>(i));
                const float room = colW - ui.s(54);
                const float gf = ui.fitFont(g, FontFace::Body, 20, room, 0.75f);
                ui.label(ui.elide(g, FontFace::Body, gf, room), {x + ui.s(44), cy + ui.s(16)}, gf, got ? ui.theme().text : ui.theme().textDim,
                         Align::Left, FontFace::Body, VAlign::Middle);
                cy += ui.s(38);
            }
        }
        // Right column: records + local leaderboard
        float rx = x + colW + ui.s(36), ry = y;
        ui.label("Records personnels", {rx, ry}, 22, ui.theme().textDim, Align::Left, FontFace::Bold);
        ry += ui.s(36);
        auto line = [&](const std::string& k, const std::string& v, Color keyColor = {}, Color valueColor = {}) {
            if (ry + ui.s(30) > bottom) return;
            Vec2 ks = ui.label(k, {rx, ry}, 20, keyColor.a ? keyColor : ui.theme().textDim);
            const float room = colW - ks.x - ui.s(14);  // the value never over its name
            const float vf = ui.fitFont(v, FontFace::Bold, 21, room, 0.7f);
            ui.label(ui.elide(v, FontFace::Bold, vf, room), {rx + colW, ry}, vf, valueColor.a ? valueColor : ui.theme().text, Align::Right, FontFace::Bold);
            ry += ui.s(31);
        };
        // The machine's run of this version (« meilleur temps trouvé par la machine », never ranked).
        const tas::TasRun* machine = tas::catalog().tasFor(g_, l.id, RunCategory::AnyPercent);
        auto tasLine = [&] {
            if (machine) line("TAS · machine (ANY%)", formatGhostTime(machine->seconds()), tasui::kTas, tasui::kTas);
        };
        line("Temps joué", playTimeText(g_.progression.levelPlayTime(l.id)));
        if (!(rec && rec->completions > 0)) tasLine();
        if (rec && rec->completions > 0) {
            line("Meilleur temps", LevelTimer::format(rec->bestTime));
            tasLine();
            line("Meilleur score", std::to_string(rec->bestScore));
            line("Moins de morts", std::to_string(std::max(0, rec->fewestDeaths)));
            line("Objets collectés", std::to_string(rec->mostCollectibles) + " / " + std::to_string(rec->totalCollectibles));
            line("Ennemis éliminés", std::to_string(rec->mostKills));
            line("Réalisé le", rec->bestTimeDate);
            line("Victoires / essais", std::to_string(rec->completions) + " / " + std::to_string(rec->attempts));
            std::string pw;
            for (const auto& p : rec->bestTimePowers) pw += (pw.empty() ? "" : ", ") + p;
            line("Pouvoirs (record)", pw.empty() ? "aucun" : pw);
            ry += ui.s(10);
            if (ry + ui.s(64) <= bottom) {
                ui.label("Classement local", {rx, ry}, 22, ui.theme().textDim, Align::Left, FontFace::Bold);
                ry += ui.s(34);
                int rank = 1;
                for (const auto& e : g_.records.providers().front()->top(l.id, 5)) {
                    if (ry + ui.s(30) > bottom) break;
                    ui.icon("ui/icons_medals.png", static_cast<int>(e.medal), 32, {rx, ry - ui.s(4), ui.s(26), ui.s(26)});
                    Vec2 ds = ui.label(e.date, {rx + colW, ry}, 17, ui.theme().textDim, Align::Right);
                    const std::string row = std::to_string(rank++) + ".  " + LevelTimer::format(e.time) + "   " + std::to_string(e.score) + " pts";
                    const float room = colW - ui.s(36) - ds.x - ui.s(10);
                    const float rf = ui.fitFont(row, FontFace::Body, 19, room, 0.7f);
                    ui.label(ui.elide(row, FontFace::Body, rf, room), {rx + ui.s(36), ry}, rf, ui.theme().text);
                    ry += ui.s(30);
                }
            }
        } else {
            ui.labelWrapped(rec && rec->attempts > 0 ? "Pas encore terminé (" + std::to_string(rec->attempts) + " essais)."
                                                     : "Aucun record pour l'instant.",
                            {rx, ry}, colW, 20, ui.theme().textDim);
        }
        card_.end(ui, t_);

        bool unlocked = g_.progression.levelUnlocked(l, g_.content);
        cstyle::PlateEx pp;
        pp.plate = {unlocked ? "Jouer" : "Verrouillé", l.name, "continue", {255, 205, 110, 255}};
        pp.enabled = unlocked;
        if (cstyle::plateButtonEx(ui, anims_, "play_level", L.play, pp, u, 1.0f, t_, dt_, mo)) play();
        // Ghost Racing: choose the ghosts (or a training segment) before the start; the keys of its
        // powers (2.31), for this player, before playing (also in the pause menu).
        std::string capLabel;
        bool capNew = false;
        bool fresh = false;
        cstyle::Diamond gd{"coop", 0, false, "planet.ghosts"};  // « NOUVEAU » (2.30.2): section training
        if (cstyle::diamondButton(ui, anims_, "ghosts_level", L.diamonds[0], L.diamondHalf, gd, 1.0f, t_, dt_, &fresh)) {
            if (unlocked) g_.app.pushScene(makeGhostSetup(g_, l.id));
            else g_.uiSound("ui_error");
        }
        if (ui.focused("ghosts_level")) capLabel = "Fantômes…", capNew = fresh;
        if (cachedKeyPowers_ && cached_) {
            cstyle::Diamond kd{"settings", 0, false, "pause.level_keys"};  // « NOUVEAU » (2.31)
            if (cstyle::diamondButton(ui, anims_, "keys_level", L.diamonds[1], L.diamondHalf, kd, 1.0f, t_, dt_, &fresh))
                g_.app.pushScene(makeLevelPowerKeys(g_, *cached_));
            if (ui.focused("keys_level")) capLabel = "Touches…", capNew = fresh;
        }
        cstyle::caption(ui, capLabel, L.caption, 15.0f, u, Color{255, 205, 110, 255}, capNew);
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
    cstyle::Motion motion() const { return cstyle::Motion::from(g_.settings.graphics.particleQuality, g_.settings.graphics.screenShake); }

    /// What a level card shows besides its record: EXTRÊME (one life) and the bosses placed in it.
    struct CardInfo {
        bool extreme = false;
        std::vector<std::string> enemies;  // read off the main thread, resolved on it
        std::vector<const EnemyDefinition*> bosses;
    };

    GameServices& g_;
    std::string planetId_;
    std::string levelId_;   // selected on arrival ("" = the last one played)
    std::string fromBoss_;  // opened from the boss statistics (« Retour » goes back there)
    std::string fromAsset_;  // 2.63: opened from ADMINISTRATION › Assets on this asset (« Retour » goes back there)
    const PlanetData* planet_ = nullptr;
    std::vector<const LevelEntry*> levels_;
    int selected_ = 0;
    float t_ = 0.0f, dt_ = 0.0f;
    std::string cachedId_;
    std::optional<LevelData> cached_;
    std::vector<const EnemyDefinition*> cachedBosses_;  // the bosses placed in it (2.31.2)
    bool cachedKeyPowers_ = false;  // its powers include some used with a key of their own
    std::vector<CardInfo> cards_;
    std::future<std::vector<CardInfo>> cardsLoading_;
    cstyle::PlanetScenery scenery_;
    cstyle::ChromeText title_;
    cstyle::HoloCard card_;
    cstyle::Anims anims_;
};

}  // namespace

std::unique_ptr<Scene> makePlanetMap(GameServices& g) { return std::make_unique<PlanetMapScene>(g); }
std::unique_ptr<Scene> makeLevelSelect(GameServices& g, const std::string& planetId, const std::string& levelId, const std::string& fromBoss,
                                       const std::string& fromAsset) {
    return std::make_unique<LevelSelectScene>(g, planetId, levelId, fromBoss, fromAsset);
}

}  // namespace cosmic
