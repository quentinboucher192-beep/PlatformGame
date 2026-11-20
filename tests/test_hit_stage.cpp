// 2.77.1: the « Où frapper » diagrams (STATISTIQUES › boss › « Où frapper », Asterion's sheets, the enemy editor's
// Boss › Parties) share one stage (ui/widgets/HitPartsView, HitStage): pause and frame by frame, a zoom held between
// 0,5× and 6×, the tooltip of a part read from the boss's data, a legend with one entry per role; the boss's box is
// never drawn; nothing overlaps at 1280 x 720 and 1920 x 1080, in French and in English.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShapeProbe.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/HitParts.h"
#include "game/online/LevelFingerprint.h"
#include "tests/TestFramework.h"
#include "ui/screens/Navigation.h"
#include "ui/widgets/HitPartsView.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

bool near(float a, float b, float e = 1e-3f) { return std::abs(a - b) <= e; }

bool overlaps(const Rect& a, const Rect& b) {
    const float e = 0.5f;
    return a.x < b.right() - e && b.x < a.right() - e && a.y < b.bottom() - e && b.y < a.bottom() - e;
}

bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

/// Every rectangle the renderer is asked to fill or outline (its listener), to find the boss's box among them.
struct Rects final : ShapeListener {
    std::vector<Rect> rects;
    void triangle(Vec2, Vec2, Vec2) override {}
    void segment(Vec2, Vec2) override {}
    void rect(const Rect& r) override { rects.push_back(r); }
    void glow(Vec2, float) override {}
    /// Whether one of them is the box `b` or one of its four edges (Renderer::drawRect).
    bool drew(const Rect& b) const {
        if (b.w < 4.0f || b.h < 4.0f) return false;
        for (const Rect& r : rects) {
            const bool whole = near(r.x, b.x, 1.5f) && near(r.y, b.y, 1.5f) && near(r.w, b.w, 2.5f) && near(r.h, b.h, 2.5f);
            const bool horizontal = near(r.x, b.x, 1.5f) && near(r.w, b.w, 2.5f) && r.h < 8.0f && (near(r.y, b.y, 1.5f) || near(r.bottom(), b.bottom(), 1.5f));
            const bool vertical = near(r.h, b.h, 16.0f) && r.w < 8.0f && (near(r.x, b.x, 1.5f) || near(r.right(), b.right(), 1.5f)) && r.y >= b.y - 1.0f &&
                                  r.bottom() <= b.bottom() + 1.0f && r.h > b.h * 0.8f;
            if (whole || horizontal || vertical) return true;
        }
        return false;
    }
};

void push(const SDL_Event& e) {
    SDL_Event c = e;
    SDL_PushEvent(&c);
}

void key(SDL_Scancode sc, SDL_Keycode k) {
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.scancode = sc;
    down.key.key = k;
    down.key.down = true;
    push(down);
    SDL_Event up = down;
    up.type = SDL_EVENT_KEY_UP;
    up.key.down = false;
    push(up);
}

void mouseTo(Vec2 p) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = p.x;
    e.motion.y = p.y;
    e.motion.xrel = 1.0f;
    push(e);
}

void wheel(Vec2 p, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.y = y;
    e.wheel.mouse_x = p.x;
    e.wheel.mouse_y = p.y;
    push(e);
}

/// What the boss's file says (read here, apart from the game's own reading).
Json enemyJson(const std::string& id) {
    auto t = FileUtil::readText(Paths::dataDir() / "enemies" / (id + ".json"));
    return t ? Json::parse(*t, nullptr, false) : Json();
}

/// A headless game over its menu.
struct Rig {
    Application app;
    std::unique_ptr<GameServices> g;
    Rig() {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = 1280;
        cfg.height = 720;
        cfg.vsync = false;
        app.init(cfg);
        app.headlessDt = 1.0f / 30.0f;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->progression.asterionFilmSeen = true;
        g->settings.online.enabled = false;  // never a real server in a test
        g->online.setSettings(g->settings.online);
        app.pushScene(makeMainMenu(*g));
        app.runFrames(3);
    }
    ~Rig() {
        i18n::setLanguage(Language::French);
        app.shutdown();
        app.onSceneRender = nullptr;
    }
    void size(Vec2 s) {
        app.setWindowSize(static_cast<int>(s.x), static_cast<int>(s.y));
        app.renderer().refreshOutputSize();
    }
};

/// The checks of a diagram drawn: its box never drawn, every text in its room, nothing over another, inside the screen.
void checkLayout(UIContext& ui, const HitStageLayout& L, const Rects& shapes, const char* where, Vec2 screen) {
    CHECK(L.view.w > 50.0f && L.view.h > 50.0f);
    if (shapes.drew(L.body)) std::printf("  %s %gx%g : la boîte du boss est dessinée\n", where, screen.x, screen.y);
    CHECK(!shapes.drew(L.body));
    for (const HitStageLayout::Text& t : L.texts) {
        const Vec2 m = ui.text().measureWrapped(t.text, t.face, t.px, t.r.w + 0.5f);
        if (m.x > t.r.w + 0.5f || m.y > t.r.h + 0.5f)
            std::printf("  %s %gx%g : « %s » %.1f x %.1f dans %.1f x %.1f\n", where, screen.x, screen.y, t.text.c_str(), m.x, m.y, t.r.w, t.r.h);
        CHECK(m.x <= t.r.w + 0.5f);
        CHECK(m.y <= t.r.h + 0.5f);
        CHECK(t.px >= 11.4f);
        CHECK(t.r.x >= -0.5f && t.r.right() <= screen.x + 0.5f);
    }
    for (size_t i = 0; i < L.boxes.size(); ++i) {
        CHECK(L.boxes[i].x >= -0.5f && L.boxes[i].right() <= screen.x + 0.5f);
        for (size_t j = i + 1; j < L.boxes.size(); ++j) {
            if (overlaps(L.boxes[i], L.boxes[j])) std::printf("  %s %gx%g : éléments %zu et %zu se chevauchent\n", where, screen.x, screen.y, i, j);
            CHECK(!overlaps(L.boxes[i], L.boxes[j]));
        }
    }
    // The legend: its lines in its column, one under another (it scrolls: none is cut).
    if (L.legend.w > 0.0f) {
        CHECK(!L.legendBoxes.empty());
        for (size_t i = 0; i < L.legendBoxes.size(); ++i) {
            CHECK(L.legendBoxes[i].x >= L.legend.x - 0.5f && L.legendBoxes[i].right() <= L.legend.right() + 0.5f);
            for (size_t j = i + 1; j < L.legendBoxes.size(); ++j) CHECK(!overlaps(L.legendBoxes[i], L.legendBoxes[j]));
        }
    }
    if (L.tip.w > 0.0f) {
        CHECK(L.tip.x >= 0.0f && L.tip.y >= 0.0f && L.tip.right() <= screen.x && L.tip.bottom() <= screen.y);
        if (L.tipFromMouse) CHECK(!L.tip.contains(L.tipAnchor));
    }
}

}  // namespace

TEST_CASE("hit stage (2.77.1): the zoom stays between 0,5x and 6x, the point under the mouse stays under it; « Recentrer »") {
    HitStage s;
    s.place({100.0f, 100.0f, 400.0f, 300.0f}, {-50.0f, -100.0f, 100.0f, 100.0f}, 8.0f);
    CHECK(near(s.zoom(), 1.0f));
    const Vec2 p{-20.0f, -60.0f};
    const Vec2 before = s.toScreen(p);
    s.zoomBy(2.0f, before);
    CHECK(near(s.zoom(), 2.0f));
    const Vec2 after = s.toScreen(p);
    CHECK(near(after.x, before.x, 0.01f) && near(after.y, before.y, 0.01f));
    for (int i = 0; i < 40; ++i) s.zoomBy(1.5f, {320.0f, 260.0f});
    CHECK(s.zoom() == HitStage::kMaxZoom);
    CHECK(near(HitStage::kMaxZoom, 6.0f));
    for (int i = 0; i < 80; ++i) s.zoomBy(0.5f, {150.0f, 120.0f});
    CHECK(s.zoom() == HitStage::kMinZoom);
    CHECK(near(HitStage::kMinZoom, 0.5f));
    s.setZoom(100.0f, s.view().center());
    CHECK(s.zoom() == HitStage::kMaxZoom);
    // Panned as far as it goes: the centre of the view stays on the drawing.
    s.panBy({100000.0f, -100000.0f});
    const Vec2 c = s.view().center();
    const Vec2 w0 = s.toScreen(Vec2{s.world().x, s.world().y}), w1 = s.toScreen(Vec2{s.world().right(), s.world().bottom()});
    CHECK(c.x >= w0.x - 0.5f && c.x <= w1.x + 0.5f && c.y >= w0.y - 0.5f && c.y <= w1.y + 0.5f);
    s.recenter();
    CHECK(near(s.zoom(), 1.0f) && near(s.pan().x, 0.0f) && near(s.pan().y, 0.0f));
}

TEST_CASE("hit stage (2.77.1): pause, frame by frame (round), playing again from the frame shown") {
    HitStage s;
    const std::vector<float> d = {0.1f, 0.2f, 0.1f};
    s.advance(0.0f);
    CHECK(s.frame(d) == 0 && s.frameCount() == 3);
    s.advance(0.15f);  // clamped to 0,1 s
    CHECK(s.frame(d) == 1);
    s.togglePause();
    CHECK(s.paused());
    s.advance(0.1f);
    s.advance(0.1f);
    CHECK(s.frame(d) == 1);  // frozen
    s.step(1);
    CHECK(s.frame(d) == 2);
    s.step(1);
    CHECK(s.frame(d) == 0);
    s.step(-1);
    CHECK(s.frame(d) == 2);
    s.togglePause();
    CHECK(!s.paused());
    s.advance(0.05f);
    CHECK(s.frame(d) == 2);  // from its start (0,3 s) on
    s.advance(0.1f);
    CHECK(s.frame(d) == 0);  // 0,45 s: round again
    s.step(1);  // a step pauses it
    CHECK(s.paused() && s.frame(d) == 1);
    s.reset();
    CHECK(!s.paused() && near(s.zoom(), 1.0f));
}

TEST_CASE("hit stage (2.77.1): the tooltip's values are the boss's data (weak point, armour, a part that hurts), in French and in English") {
    ContentDatabase content;
    content.reload();
    const i18n::Dictionary& en = i18n::dictionary(Language::English);
    struct Case {
        const char* boss;
        const char* part;
        const char* role;
    };
    const Case cases[] = {{"dreadfang", "jointures", "weak"}, {"dreadfang", "épaule", "armour"}, {"dreadfang", "griffes", "hurt"},
                          {"pyrrhax", "tête", "weak"},        {"pyrrhax", "queue", "armour"},   {"pyrrhax", "griffes", "hurt"},
                          {"cryowyrm", "pointes", "hurt"}};
    for (const Case& c : cases) {
        const EnemyDefinition* d = content.enemy(c.boss);
        CHECK(d != nullptr);
        if (!d) continue;
        const Json j = enemyJson(c.boss);
        CHECK(j.is_object() && j.contains("parts"));
        const Json role = j["parts"]["roles"].value(c.part, Json::object());
        CHECK(role.value("role", std::string()) == c.role);
        const PartFacts f = partFacts(c.part, PartRules::of(c.boss), *d);
        CHECK(f.byParts);
        CHECK(std::string(partRoleId(f.role)) == c.role);
        const bool hurt = std::string(c.role) == "hurt";
        CHECK(near(f.dealt, hurt ? 0.0f : role.value("damage", 1.0f)));
        CHECK(near(f.contact, j.value("contactDamage", 1.0f)));
        // The attacks during which it strikes, from the file: a part that hurts, each attack with damage (its rush
        // whatever its damage); another, those of its « hurtDuring »; each name once.
        std::vector<std::pair<std::string, float>> want;
        const Json during = role.value("hurtDuring", Json::array());
        for (const Json& a : j["attacks"]) {
            const std::string id = a.value("id", std::string()), name = a.value("name", std::string()), type = a.value("type", std::string());
            const float dmg = a.value("damage", 1.0f);
            const bool strikes = hurt ? (type == "lunge" || dmg > 0.0f) : std::find(during.begin(), during.end(), Json(id)) != during.end();
            if (!strikes || name.empty()) continue;
            if (std::none_of(want.begin(), want.end(), [&](const auto& w) { return w.first == name && w.second == dmg; })) want.push_back({name, dmg});
        }
        CHECK(f.during.size() == want.size());
        for (size_t i = 0; i < std::min(want.size(), f.during.size()); ++i) {
            CHECK(f.during[i].attack == want[i].first);
            CHECK(near(f.during[i].damage, want[i].second));
        }
        if (std::string(c.role) == "weak") CHECK(!f.during.empty());  // these weak points also hurt during an attack (their « hurtDuring »)
        // Its tooltip says them.
        const PartTip t = partTip(f);
        CHECK(t.title == partNameLabel(c.part));
        PartRule r;
        r.role = f.role;
        r.damage = f.dealt;
        CHECK(has(t.lines, "Dégâts infligés au boss : " + (hurt ? std::string("aucun") : partDamageLabel(r))));
        CHECK(has(t.lines, "Dégâts reçus au contact : " + heartsLabel(f.contact)));
        CHECK(has(t.lines, std::string("Rôle : ") + partRoleLabel(f.role)));
        if (want.size() <= 8)
            for (const auto& w : want) {
                const int n = std::max(1, static_cast<int>(std::lround(w.second)));
                const std::string line = "Pendant " + w.first + " : " + std::to_string(n) + (n > 1 ? " cœurs" : " cœur");
                if (!has(t.lines, line)) std::printf("  %s, %s : « %s » absente\n", c.boss, c.part, line.c_str());
                CHECK(has(t.lines, line));
            }
        else
            for (const auto& w : want) {
                bool named = false;
                for (const std::string& l : t.lines) named = named || (l.rfind("Pendant ", 0) == 0 && l.find(w.first) != std::string::npos);
                CHECK(named);
            }
        for (const std::string& l : t.lines) {
            std::string out;
            if (!en.lookup(l, out)) std::printf("  %s, %s : « %s » sans anglais\n", c.boss, c.part, l.c_str());
            CHECK(en.lookup(l, out));
        }
    }
    // Dreadfang's joints: × 2, its contact 2 hearts, « Ruée des griffes » 2 hearts (the user's capture).
    const PartTip dj = partTip(partFacts("jointures", PartRules::of("dreadfang"), *content.enemy("dreadfang")));
    CHECK(has(dj.lines, "Dégâts infligés au boss : × 2"));
    CHECK(has(dj.lines, "Dégâts reçus au contact : 2 cœurs"));
    CHECK(has(dj.lines, "Pendant Ruée des griffes : 2 cœurs"));
    // A boss that still fights with its box says so (the player's copy of a boss: no rules).
    const PartFacts box = partFacts("tête", PartRules{}, *content.enemy("pyrrhax"));
    CHECK(!box.byParts);
    const PartTip bt = partTip(box);
    bool says = false;
    for (const std::string& l : bt.lines) says = says || l.find("sa boîte") != std::string::npos;
    CHECK(says);
    for (const std::string& l : bt.lines) {
        std::string out;
        CHECK(en.lookup(l, out));
    }
}

TEST_CASE("hit stage (2.77.1): the legend has one entry per role, its rule whole, « blessent » agreeing with the part") {
    ContentDatabase content;
    content.reload();
    const i18n::Dictionary& en = i18n::dictionary(Language::English);
    CHECK(partIsPlural("jointures") && partIsPlural("griffes") && partIsPlural("yeux") && partIsPlural("flammes d'âme"));
    CHECK(!partIsPlural("tête") && !partIsPlural("corps") && !partIsPlural("avant-bras") && !partIsPlural("bras") && !partIsPlural("aile droite"));
    const EnemyDefinition& d = *content.enemy("dreadfang");
    PartRule j = PartRules::of("dreadfang").ruleOr("jointures");
    j.name = "jointures";
    CHECK(partHurtsAlsoLine(j, d) == "Jointures blessent aussi pendant : Ruée des griffes");
    int boards = 0;
    for (const EnemyDefinition* e : content.enemies()) {
        if (!e->isBoss) continue;
        const HitPartsSheet* sheet = HitPartsSheet::of(e->animation);
        if (!sheet) continue;
        const std::vector<HitLegendLine> lines = partsLegend(sheet, PartRules::of(e->id), *e);
        int roles[4] = {0, 0, 0, 0};
        for (const HitLegendLine& l : lines) {
            if (l.role >= 0 && !l.sub && l.face == FontFace::Bold) ++roles[l.role];
            CHECK(l.text.find("Cadre blanc") == std::string::npos);
            std::string out;
            if (!en.lookup(l.text, out)) std::printf("  %s : « %s » sans anglais\n", e->id.c_str(), l.text.c_str());
            CHECK(en.lookup(l.text, out));
        }
        for (int n : roles) CHECK(n <= 1);
        const PartRules& rules = PartRules::of(e->id);
        if (rules.fromVersion > 0 && rules.fromVersion <= kSimulationVersion)
            CHECK(lines.back().text ==
                  "Combat par parties : vos coups touchent la partie visée (point faible d'abord). Tout le corps blesse au contact ; les parties "
                  "rouges blessent aussi pendant leurs attaques.");
        ++boards;
    }
    CHECK(boards >= 30);
}

TEST_CASE("hit stage (2.77.1): the three diagrams draw no box, hold their texts, nothing overlaps (1280 x 720, 1920 x 1080, FR / EN)") {
    Rig rig;
    GameServices& g = *rig.g;
    for (Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}})
        for (Language lang : {Language::French, Language::English}) {
            rig.size(size);
            i18n::setLanguage(lang);
            rig.app.runFrames(1);
            for (int where = 0; where < 3; ++where) {
                const char* name = where == 0 ? "fiche d'Asterion" : where == 1 ? "statistiques" : "éditeur";
                if (where == 0) rig.app.pushScene(makeRaidSheets(g, "dreadfang", 1));
                if (where == 1) {
                    g.onlineOpen = "boss:pyrrhax:frapper";
                    rig.app.pushScene(makeBossStats(g, "pyrrhax"));
                }
                if (where == 2) {
                    g.onlineOpen = "parts";
                    rig.app.pushScene(makeEnemyEditor(g, "pyrrhax", 6));
                }
                rig.app.runFrames(3);
                Rects shapes;
                const uint64_t before = rig.app.frameIndex();
                rig.app.renderer().setListener(&shapes);
                rig.app.runFrames(1);
                rig.app.renderer().setListener(nullptr);
                const HitStageLayout L = HitStage::last();
                CHECK(L.drawnAt >= before);
                checkLayout(g.ui, L, shapes, name, size);
                CHECK(!L.partAt.empty());
                // The mouse over a part (inside the view and the screen): its tooltip, inside the screen, never over the pointer.
                Vec2 target{-1.0f, -1.0f};
                for (const auto& [part, at] : L.partAt)
                    if (L.view.contains(at) && at.y > 10.0f && at.y < size.y - 10.0f && target.x < 0.0f) target = at;
                if (target.x >= 0.0f) {
                    mouseTo(target);
                    rig.app.runFrames(2);
                    const HitStageLayout T = HitStage::last();
                    if (T.tipPart.empty()) std::printf("  %s %gx%g : pas d'infobulle au survol\n", name, size.x, size.y);
                    CHECK(!T.tipPart.empty());
                    CHECK(T.tip.w > 0.0f && T.tipFromMouse);
                    CHECK(!T.tip.intersects({target.x - 4.0f, target.y - 4.0f, 28.0f, 36.0f}));
                    checkLayout(g.ui, T, shapes, name, size);
                    // The wheel zooms around the pointer (the page under it does not scroll).
                    if (where == 0 && size.x > 1500.0f) {
                        const float z0 = T.zoom;
                        wheel(target, 1.0f);
                        rig.app.runFrames(1);
                        CHECK(near(HitStage::last().zoom, std::min(HitStage::kMaxZoom, z0 * 1.2f), 0.01f));
                        CHECK(near(HitStage::last().view.y, T.view.y, 0.5f));  // the page did not move
                        // Espace pauses (the view has the focus since the pointer moved over it); → the next frame.
                        key(SDL_SCANCODE_SPACE, SDLK_SPACE);
                        rig.app.runFrames(1);
                        const HitStageLayout P = HitStage::last();
                        CHECK(P.paused);
                        key(SDL_SCANCODE_RIGHT, SDLK_RIGHT);
                        rig.app.runFrames(1);
                        CHECK(HitStage::last().frame == (P.frame + 1) % std::max(1, P.frames));
                        CHECK(HitStage::last().paused);
                        // Tab: the next part, its tooltip at it (the keys, not the pointer).
                        key(SDL_SCANCODE_TAB, SDLK_TAB);
                        rig.app.runFrames(2);
                        const HitStageLayout K = HitStage::last();
                        CHECK(!K.tipPart.empty() && !K.tipFromMouse);
                        checkLayout(g.ui, K, shapes, name, size);
                    }
                    mouseTo({2.0f, 2.0f});
                    rig.app.runFrames(1);
                }
                rig.app.popScene();
                rig.app.runFrames(2);
            }
        }
    i18n::setLanguage(Language::French);
    rig.size({1280, 720});
}
