// « Effacer le HUD quand il cache l'action » (2.60): a block of the HUD fades (to about 28 %, in
// about 0.15 s) while the boss, Nova or an attack of the boss is under it, then comes back. The
// pieces checked here: the exact tests of the shapes drawn against the blocks, what a block
// really covers, the rule (the boss's own bar only gives way to Nova), the pace of the fade, the
// visible part of a sprite, the option (saved, on by default), and the boss arenas' framing
// under the HUD. All of it display only: the simulation never sees it.

#include <cmath>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/camera/Camera.h"
#include "engine/rendering/ShapeProbe.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/gameplay/HudFade.h"
#include "game/gameplay/entities/Entities.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("hud fade: a shape touches a block only where it really is drawn") {
    const Rect block{100, 100, 200, 80};  // a block of the HUD (the timer, say)
    // A triangle inside, across an edge, beside it; one that only meets an edge does not count.
    CHECK(ShapeTest::triangleTouchesRect({150, 120}, {200, 120}, {170, 160}, block));
    CHECK(ShapeTest::triangleTouchesRect({50, 50}, {150, 50}, {150, 150}, block));
    CHECK(!ShapeTest::triangleTouchesRect({0, 0}, {90, 0}, {0, 90}, block));
    CHECK(!ShapeTest::triangleTouchesRect({300, 100}, {400, 100}, {350, 180}, block));
    // A thin diagonal beam passing beside the corner (100, 100): its bounding box meets the
    // block, the beam does not; a little further, it crosses the corner.
    CHECK(Rect(0, 0, 192, 190).intersects(block));
    CHECK(!ShapeTest::triangleTouchesRect({0, 190}, {190, 0}, {192, 0}, block));
    CHECK(!ShapeTest::segmentTouchesRect({0, 190}, {190, 0}, block));
    CHECK(ShapeTest::segmentTouchesRect({0, 210}, {210, 0}, block));
    // A segment through it, one along its edge, one outside.
    CHECK(ShapeTest::segmentTouchesRect({0, 140}, {500, 140}, block));
    CHECK(!ShapeTest::segmentTouchesRect({0, 100}, {500, 100}, block));
    CHECK(!ShapeTest::segmentTouchesRect({0, 300}, {500, 300}, block));
    // A circle: by its distance to the block.
    CHECK(ShapeTest::circleTouchesRect({310, 140}, 15.0f, block));
    CHECK(!ShapeTest::circleTouchesRect({320, 200}, 20.0f, block));  // near the corner, not on it
    // An empty block is touched by nothing.
    CHECK(!ShapeTest::triangleTouchesRect({0, 0}, {500, 0}, {0, 500}, Rect{}));
    CHECK(!ShapeTest::circleTouchesRect({0, 0}, 500.0f, Rect{}));
}

TEST_CASE("hud fade: the probe tells which blocks the boss's attacks touch") {
    CoverageProbe probe;
    probe.watch({{0, 0, 300, 120}, {}, {1600, 0, 300, 200}, {600, 900, 700, 80}});
    CHECK(probe.size() == 4);
    for (size_t i = 0; i < probe.size(); ++i) CHECK(!probe.touched(i));
    probe.triangle({1700, 150}, {1750, 150}, {1720, 260});  // a feather under the timer
    CHECK(probe.touched(2));
    CHECK(!probe.touched(0) && !probe.touched(3));
    probe.segment({0, 1080}, {1920, 1000});  // a laser low across the screen, below the bar
    CHECK(!probe.touched(3));
    probe.segment({0, 1080}, {1920, 600});  // the same laser, aimed higher: it crosses the bar
    CHECK(probe.touched(3));
    probe.watch({{0, 0, 300, 120}, {}, {1600, 0, 300, 200}, {600, 900, 700, 80}});  // next frame
    probe.triangle({1700, 150}, {1750, 150}, {1720, 260});
    probe.rect({250, 100, 40, 40});  // a reticle on the corner of the hearts
    CHECK(probe.touched(0));
    probe.glow({900, 860}, 100.0f);  // a soft light: only its core (35 px) counts, it stops short
    CHECK(!probe.touched(3));
    probe.glow({900, 870}, 100.0f);
    CHECK(probe.touched(3));
    CHECK(probe.touched(2));
    CHECK(!probe.touched(1));  // an empty zone (a block not drawn) is never touched
    probe.watch({{0, 0, 10, 10}});  // a new frame: all untouched again
    CHECK(probe.size() == 1 && !probe.touched(0));
}

TEST_CASE("hud fade: what a block really covers, glows by their core") {
    ShapeBounds b;
    CHECK(b.area().w == 0.0f && b.area().h == 0.0f);
    b.rect({40, 34, 300, 44});
    b.triangle({40, 90}, {80, 90}, {60, 110});
    b.segment({20, 50}, {20, 60});  // a vertical line: a pixel wide
    b.glow({200, 50}, 100.0f);      // its halo would reach y = -50: only its core counts
    const Rect a = b.area();
    CHECK_NEAR(a.x, 20.0f, 1e-4);
    CHECK_NEAR(a.y, 15.0f, 1e-4);
    CHECK_NEAR(a.right(), 340.0f, 1e-4);
    CHECK_NEAR(a.bottom(), 110.0f, 1e-4);
}

TEST_CASE("hud fade: what is under a block, and the boss's bar only gives way to Nova") {
    const Rect timer{1600, 30, 280, 180}, bar{504, 950, 912, 78};
    const Rect phenix{1500, 120, 260, 160}, nova{900, 960, 40, 60}, far{100, 600, 100, 100};
    CHECK(HudFade::hides(timer, phenix));
    CHECK(!HudFade::hides(timer, far));
    CHECK(!HudFade::hides(timer, Rect{}));  // not on screen: nothing
    CHECK(!HudFade::hides(Rect{}, phenix)); // a block not drawn hides nothing
    CHECK(!HudFade::hides(timer, {1600, 210, 280, 20}));  // edge to edge: not under it
    // A block of the HUD: the boss, Nova or an attack of the boss.
    CHECK(HudFade::covered(timer, far, phenix, false, false));
    CHECK(HudFade::covered(timer, {1700, 100, 40, 60}, far, false, false));
    CHECK(HudFade::covered(timer, far, far, true, false));
    CHECK(!HudFade::covered(timer, far, far, false, false));
    CHECK(!HudFade::covered(Rect{}, phenix, phenix, true, false));
    // The boss's bar: the boss and its attacks are what it is about; Nova under it, it fades.
    CHECK(!HudFade::covered(bar, far, {600, 900, 300, 200}, true, true));
    CHECK(HudFade::covered(bar, nova, far, false, true));
}

TEST_CASE("hud fade: down to about 28 % in about 0.15 s, back the same way, at a steady pace") {
    CHECK(HudFade::kFaded >= 0.25f && HudFade::kFaded <= 0.30f);
    CHECK(HudFade::kSeconds >= 0.12f && HudFade::kSeconds <= 0.18f);
    const float dt = 1.0f / 120.0f;
    float o = 1.0f;
    int frames = 0;
    float last = o;
    while (o > HudFade::kFaded && frames < 1000) {
        o = HudFade::step(o, true, dt);
        CHECK(o < last);  // always going down, never below the floor
        CHECK(o >= HudFade::kFaded);
        last = o;
        ++frames;
    }
    CHECK_NEAR(static_cast<float>(frames) * dt, HudFade::kSeconds, 2.0f * dt);
    // It stays there while covered.
    for (int i = 0; i < 60; ++i) o = HudFade::step(o, true, dt);
    CHECK_NEAR(o, HudFade::kFaded, 1e-6);
    // Half way back, then covered again: it turns around at once (no jump).
    for (int i = 0; i < frames / 2; ++i) o = HudFade::step(o, false, dt);
    const float mid = o;
    CHECK(mid > HudFade::kFaded + 0.2f && mid < 0.8f);
    o = HudFade::step(o, true, dt);
    CHECK(o < mid && mid - o < 0.1f);
    // Back to 1, never above; a long frame does not overshoot.
    o = HudFade::step(o, false, 1.0f);
    CHECK_NEAR(o, 1.0f, 1e-6);
    CHECK_NEAR(HudFade::step(1.0f, false, dt), 1.0f, 1e-6);
    CHECK_NEAR(HudFade::step(1.0f, true, 0.0f), 1.0f, 1e-6);  // no time, no change
}

TEST_CASE("hud fade: the visible part of a sprite, mirrored with it") {
    const Rect dst{100, 200, 320, 240};  // a 256 x 192 frame drawn at 1.25
    const Vec2 frame{256, 192};
    const Rect seen{20, 60, 200, 120};
    Rect r = HudFade::visiblePart(dst, frame, seen, false);
    CHECK_NEAR(r.x, 125.0f, 1e-3);
    CHECK_NEAR(r.y, 275.0f, 1e-3);
    CHECK_NEAR(r.w, 250.0f, 1e-3);
    CHECK_NEAR(r.h, 150.0f, 1e-3);
    r = HudFade::visiblePart(dst, frame, seen, true);  // facing left: the art mirrored
    CHECK_NEAR(r.x, 100.0f + (256.0f - 220.0f) * 1.25f, 1e-3);
    CHECK_NEAR(r.w, 250.0f, 1e-3);
    // Unknown: the whole frame.
    r = HudFade::visiblePart(dst, frame, Rect{}, false);
    CHECK(r.x == dst.x && r.y == dst.y && r.w == dst.w && r.h == dst.h);
}

TEST_CASE("hud fade: the option is on by default and saved with the settings") {
    Settings s;
    CHECK(s.gameplay.fadeHud);
    s.gameplay.fadeHud = false;
    Settings r = Settings::fromJson(s.toJson());
    CHECK(!r.gameplay.fadeHud);
    r.gameplay.fadeHud = true;
    CHECK(Settings::fromJson(r.toJson()).gameplay.fadeHud);
    // A file from before 2.60: on.
    Settings old = Settings::fromJson(Json::parse(R"({"gameplay":{"showTimer":true}})"));
    CHECK(old.gameplay.fadeHud);
}

TEST_CASE("hud fade: the giga bosses' visible pixels, measured in their frames") {
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    for (const char* sheet : {"animations/phenix.json", "animations/singularis.json", "animations/pyrrhax.json", "animations/liche.json"}) {
        AnimationSetPtr set = res.animation(sheet);
        CHECK(set != nullptr);
        if (!set) continue;
        CHECK(!set->textureFile.empty());
        int frames = 0, smaller = 0;
        for (const auto& [name, clip] : set->clips)
            for (int f = 0; f < clip.frameCount(); ++f) {
                const Rect src = set->frameRect(clip, f);
                const Rect seen = res.visibleBox(*set, src);
                if (seen.w <= 0.0f) continue;  // an empty frame
                ++frames;
                // Inside its frame, and (wings, tails, smoke aside) smaller than it.
                CHECK(seen.x >= 0.0f && seen.y >= 0.0f);
                CHECK(seen.right() <= static_cast<float>(set->frameWidth) && seen.bottom() <= static_cast<float>(set->frameHeight));
                if (seen.h < static_cast<float>(set->frameHeight) * 0.95f) ++smaller;
            }
        CHECK(frames > 10);
        CHECK(smaller * 2 > frames);  // the frame alone would fade the HUD too early
    }
    // Unknown file: nothing measured (the caller takes the whole frame).
    AnimationSet none;
    none.frameWidth = none.frameHeight = 32;
    CHECK(res.visibleBox(none, {0, 0, 32, 32}).w == 0.0f);
}

TEST_CASE("hud fade: the Phénix and Nova on screen, where they are drawn") {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    auto level = content.loadLevel("confins_04");
    CHECK(level.has_value());
    if (!level) return;
    WorldServices s{res, audio, content, nullptr, 0.0f, 0.0f};
    GameWorld w(s, WorldMode::Play);
    w.load(*level);
    Hud::warmUp(w);
    EnemySpawnEntity* spawn = nullptr;
    for (const auto& o : level->objects)
        if (o.type == "enemy" && o.prop<bool>("boss", false)) spawn = dynamic_cast<EnemySpawnEntity*>(w.entity(o.id));
    CHECK(spawn != nullptr);
    if (!spawn) return;
    Rect arena;
    for (const auto& e : w.entities())
        if (const auto* cz = dynamic_cast<const CombatZoneEntity*>(e.get()))
            if (cz->lists(spawn->object().id) || cz->box.contains(spawn->box.center())) arena = cz->box;
    CHECK(arena.w > 0.0f);
    w.startAt({arena.x + 40.0f, arena.y + 40.0f});
    spawn->handleAction(w, "activate", Json::object());
    for (int i = 0; i < 240 && !w.activeBoss(); ++i) w.fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
    const Enemy* boss = w.activeBoss();
    CHECK(boss != nullptr);
    if (!boss) return;
    for (int i = 0; i < 60; ++i) w.fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
    const WorldView view = w.camera().view(1920, 1080, 1.0f);
    const Rect b = Hud::bossOnScreen(w, view);
    const Rect frame = view.toScreen(boss->animator().destRect(boss->box.bottomCenter(), boss->facing < 0.0f, boss->def().scale));
    CHECK(b.w > 0.0f && b.h > 0.0f);
    CHECK(frame.expanded(1.0f).containsRect(b));  // inside the frame drawn,
    CHECK(b.w * b.h < frame.w * frame.h);         // and smaller than it
    const Rect n = Hud::novaOnScreen(w, view);
    CHECK(n.w > 0.0f && n.h > 0.0f);
    CHECK(n.expanded(2.0f).contains(view.toScreen(w.player().box.center())));
}

TEST_CASE("hud fade: a boss arena keeps its vault under the HUD as far as its spare height allows") {
    // The pure placement: centred in the band between the margins, never out of view.
    const Rect area{0, 100, 800, 300};
    const float halfH = 200.0f, scale = 2.0f;  // the view: 400 world px tall (800 screen px)
    CHECK_NEAR(Camera::bandCenterY(area, halfH, scale, 0.0f, 0.0f), area.center().y, 1e-4);
    // 120 px of HUD at the top, 40 at the bottom: 20 world px lower on screen (the view up).
    CHECK_NEAR(Camera::bandCenterY(area, halfH, scale, 120.0f, 40.0f), area.center().y - 20.0f, 1e-4);
    // Too much asked: as far as the area stays whole in view (its bottom at the view's bottom).
    const float y = Camera::bandCenterY(area, halfH, scale, 600.0f, 0.0f);
    CHECK_NEAR(y, area.bottom() - halfH, 1e-4);

    // Every campaign boss fight: with the HUD's margins, an arena that fits stays whole and its
    // vault comes lower; a giga boss's arena (taller than the view) is framed as before.
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    const float top = 234.0f, bottom = 130.0f;  // the default HUD at 1920 x 1080, during a fight
    int lower = 0, fights = 0;
    for (const char* id : {"aethra_01", "aethra_02", "glacior_04", "noyau_02", "pyralis_04", "sylvanox_04", "glacior_02", "confins_04", "noyau_03"}) {
        auto level = content.loadLevel(id);
        CHECK(level.has_value());
        if (!level) continue;
        std::string bossId;
        for (const auto& o : level->objects)
            if (o.type == "enemy" && o.prop<bool>("boss", false)) bossId = o.id;
        float vault[2] = {0.0f, 0.0f};
        Rect arena;
        bool giant = false, shown[2] = {false, false}, fought[2] = {false, false};
        for (int withHud = 0; withHud < 2; ++withHud) {
            WorldServices s{res, audio, content, nullptr, 0.0f, 0.0f};
            GameWorld w(s, WorldMode::Play);
            w.load(*level);
            auto* spawn = dynamic_cast<EnemySpawnEntity*>(w.entity(bossId));
            CHECK(spawn != nullptr);
            if (!spawn) break;
            for (const auto& e : w.entities())
                if (const auto* cz = dynamic_cast<const CombatZoneEntity*>(e.get()))
                    if (cz->lists(bossId) || cz->box.contains(spawn->box.center())) {
                        arena = cz->box;
                        break;
                    }
            w.startAt({arena.x + 40.0f, arena.y + 40.0f});
            if (withHud) w.camera().setScreenMargins(top, bottom);
            (void)w.camera().visibleArea(1920, 1080);
            spawn->handleAction(w, "activate", Json::object());
            for (int i = 0; i < 600 && w.activeBoss(); ++i) w.fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
            if (!w.activeBoss()) break;
            const CameraZone* z = w.bossCameraZone();
            giant = z && w.camera().fitZoom(z->area) < Camera::kMinFitZoom;
            const WorldView v = w.camera().view(1920, 1080, 1.0f);
            const Rect seen = v.visibleWorld();
            shown[withHud] = seen.contains({arena.x + 2, arena.y + 2}) && seen.contains({arena.right() - 2, arena.bottom() - 2});
            vault[withHud] = v.toScreen({arena.x, arena.y}).y;
            fought[withHud] = true;
        }
        if (!fought[0] || !fought[1]) continue;  // Nova fell before the end (a respawn)
        ++fights;
        if (giant) {
            CHECK_NEAR(vault[1], vault[0], 0.5f);  // nothing to give: unchanged
            continue;
        }
        CHECK(shown[0] && shown[1]);  // never cut
        CHECK(vault[1] >= vault[0] - 0.5f);
        if (vault[1] > vault[0] + 20.0f) ++lower;
    }
    CHECK(fights >= 7);
    CHECK(lower >= 4);  // the wide arenas: their spare height goes under the HUD
}
