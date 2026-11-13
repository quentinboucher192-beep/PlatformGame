#include "engine/core/Easing.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Math.h"
#include "engine/core/Paths.h"
#include "engine/input/InputManager.h"
#include "engine/physics/Collision.h"
#include "engine/physics/PhysicsWorld.h"
#include "saves/SaveFile.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("math: rect intersection and colors") {
    Rect a{0, 0, 10, 10}, b{5, 5, 10, 10}, c{10, 0, 5, 5};
    CHECK(a.intersects(b));
    CHECK(!a.intersects(c));  // touching edges do not overlap
    CHECK(a.intersection(b) == Rect(5, 5, 5, 5));
    CHECK(Color::fromHex("#39f0d8") == Color(0x39, 0xf0, 0xd8, 255));
    CHECK(Color::fromHex("#39f0d880").a == 0x80);
    CHECK(Color::fromHex("zz", Color::black()) == Color::black());
    CHECK(Color(1, 2, 3).toHex() == "#010203");
}

TEST_CASE("easing: endpoints") {
    for (int e = 0; e <= static_cast<int>(Ease::InOutSine); ++e) {
        CHECK_NEAR(applyEase(static_cast<Ease>(e), 0.0f), 0.0, 1e-5);
        CHECK_NEAR(applyEase(static_cast<Ease>(e), 1.0f), 1.0, 1e-5);
    }
    CHECK(easeFromString(easeToString(Ease::OutBack)) == Ease::OutBack);
}

TEST_CASE("paths: safe relative paths") {
    CHECK(Paths::isSafeRelative("textures/tiles/ruin.png"));
    CHECK(Paths::isSafeRelative("COSMIC_LEVEL/assets/a b.png"));
    CHECK(!Paths::isSafeRelative("../evil.dll"));
    CHECK(!Paths::isSafeRelative("a/../../b"));
    CHECK(!Paths::isSafeRelative("/etc/passwd"));
    CHECK(!Paths::isSafeRelative("C:/Windows/system32"));
    CHECK(!Paths::isSafeRelative("a\\..\\b"));
    CHECK(!Paths::isSafeRelative("dir/CON.txt"));
    CHECK(!Paths::isSafeRelative("a//b"));
    CHECK(!Paths::isSafeRelative(""));
    CHECK(FileUtil::slugify("Les Ruines Suspendues!") == "les_ruines_suspendues");
}

TEST_CASE("collision: oriented box SAT") {
    OrientedBox ob{{50, 50}, {10, 2}, degToRad(45.0f)};
    CHECK(Collision::overlaps(Rect{45, 45, 10, 10}, ob));
    CHECK(!Collision::overlaps(Rect{0, 0, 10, 10}, ob));
    float t = 0;
    CHECK(Collision::segmentIntersectsRect({0, 5}, {20, 5}, Rect{10, 0, 5, 10}, &t));
    CHECK_NEAR(t, 0.5, 1e-4);
}

TEST_CASE("physics: landing on solid and one-way platforms") {
    PhysicsWorld w;
    w.add({0, 100, 200, 16}, ColliderKind::Solid);
    w.add({50, 60, 48, 4}, ColliderKind::OneWay);
    Rect body{10, 70, 10, 20};
    auto r = w.move(body, {0, 20});
    CHECK(r.hitBottom);
    CHECK_NEAR(body.bottom(), 100.0, 1e-3);
    // Jumping up through a one-way platform, then landing on it.
    Rect b2{60, 70, 10, 20};
    w.move(b2, {0, -30});
    CHECK_NEAR(b2.y, 40.0, 1e-3);
    auto r2 = w.move(b2, {0, 5});
    CHECK(r2.hitBottom);
    CHECK_NEAR(b2.bottom(), 60.0, 1e-3);
    // Dropping through.
    MoveOptions drop;
    drop.dropThrough = true;
    w.move(b2, {0, 5}, drop);
    CHECK(b2.bottom() > 60.0f);
    // Wall.
    w.add({150, 0, 16, 100}, ColliderKind::Solid);
    Rect b3{130, 70, 10, 20};
    auto r3 = w.move(b3, {30, 0});
    CHECK(r3.hitRight);
    CHECK_NEAR(b3.right(), 150.0, 1e-3);
    CHECK(w.raycast({0, 50}, {300, 50}).has_value());
    CHECK(!w.lineOfSight({0, 50}, {300, 50}));
}

TEST_CASE("saves: versioned file with migration v1 -> v2") {
    Json v1env = {{"format", "cosmic.settings"},
                  {"version", 1},
                  {"data", {{"audio", {{"master", 50}, {"music", 80}, {"sfx", 100}}}, {"input", Json::object()}}}};
    SaveFile f("settings", SettingsStore::kVersion, {&SettingsStore::migrateV1ToV2});
    int from = 0;
    auto data = f.migrate(v1env, &from);
    CHECK(data.has_value());
    CHECK(from == 1);
    Settings s = Settings::fromJson(*data);
    CHECK_NEAR(s.audio.master, 0.5, 1e-4);
    CHECK_NEAR(s.audio.effects, 1.0, 1e-4);
    // Round trip through disk.
    auto path = Paths::savesDir() / "test_settings.json";
    s.graphics.particleQuality = 1;
    CHECK(f.save(path, s.toJson()));
    auto back = f.load(path);
    CHECK(back.has_value());
    CHECK(Settings::fromJson(*back).graphics.particleQuality == 1);
    // Wrong format is rejected.
    CHECK(!f.migrate(Json{{"format", "cosmic.other"}, {"version", 1}}).has_value());
}

TEST_CASE("input: bindings serialization") {
    InputBindings b = InputBindings::defaults();
    b.keys[static_cast<int>(Action::Jump)][0] = SDL_SCANCODE_W;
    b.gamepad.invertY = true;
    InputBindings c = InputBindings::fromJson(b.toJson());
    CHECK(c.keys[static_cast<int>(Action::Jump)][0] == SDL_SCANCODE_W);
    CHECK(c.pad[static_cast<int>(Action::Wave)][0] == b.pad[static_cast<int>(Action::Wave)][0]);
    CHECK(c.gamepad.invertY);
}

TEST_CASE("input: key events feed actions and layout-aware shortcuts") {
    InputManager in;
    in.setBindings(InputBindings::defaults());
    in.beginFrame();
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = SDL_SCANCODE_SPACE;
    in.handleEvent(e);
    CHECK(in.keyPressed(SDL_SCANCODE_SPACE));
    CHECK(in.shortcutPressed(SDL_GetKeyFromScancode(SDL_SCANCODE_SPACE, SDL_KMOD_NONE, false)));
    CHECK(in.consumePlayerInput().isPressed(Action::Jump));
    // Shortcuts follow the key label of the active layout, not the physical position.
    e.key.scancode = SDL_SCANCODE_Z;
    in.handleEvent(e);
    SDL_Keycode label = SDL_GetKeyFromScancode(SDL_SCANCODE_Z, SDL_KMOD_NONE, false);
    CHECK(in.shortcutPressed(label));
    in.beginFrame();
    CHECK(!in.keyPressed(SDL_SCANCODE_SPACE));
    CHECK(!in.shortcutPressed(label));
}
