#include "engine/core/Easing.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Math.h"
#include "ui/screens/PauseMap.h"
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
    CHECK(c.pad[static_cast<int>(Action::Attack)][0] == b.pad[static_cast<int>(Action::Attack)][0]);
    CHECK(c.pad[static_cast<int>(Action::Attack)][0].kind != PadBinding::Kind::None);
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

TEST_CASE("input: a key pressed to rebind a command triggers nothing else") {
    InputManager in;
    in.setBindings(InputBindings::defaults());
    in.beginFrame();
    in.beginCapture(InputDevice::Keyboard);
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    // Enter (menu « valider ») and Escape (menu « retour ») while a command waits for its key:
    // only the rebinding sees them, no button, no tab, no « Retour », no game action.
    e.key.scancode = SDL_SCANCODE_RETURN;
    in.handleEvent(e);
    CHECK(in.captureDone());
    CHECK(in.capturedKey() == SDL_SCANCODE_RETURN);
    CHECK(!in.menuPressed(Action::MenuConfirm));
    CHECK(!in.keyPressed(SDL_SCANCODE_RETURN));
    CHECK(!in.shortcutPressed(SDL_GetKeyFromScancode(SDL_SCANCODE_RETURN, SDL_KMOD_NONE, false)));
    PlayerInput pi = in.consumePlayerInput();
    for (int i = 0; i < kFirstMenuAction; ++i) CHECK(!pi.pressed[i]);
    in.cancelCapture();
    in.beginFrame();
    in.beginCapture(InputDevice::Keyboard);
    e.key.scancode = SDL_SCANCODE_ESCAPE;
    in.handleEvent(e);
    CHECK(in.capturedKey() == SDL_SCANCODE_ESCAPE);
    CHECK(!in.menuPressed(Action::MenuBack));
    // Once the capture is over, the keys work as usual again.
    in.cancelCapture();
    in.beginFrame();
    in.update(0.016f);  // the captured key is released (no key is down in the tests)
    e.key.scancode = SDL_SCANCODE_SPACE;
    in.handleEvent(e);
    CHECK(in.keyPressed(SDL_SCANCODE_SPACE));
    CHECK(in.consumePlayerInput().isPressed(Action::Jump));
}

TEST_CASE("input: mouse buttons and the wheel can be bound to commands (any button, saved by name)") {
    InputManager in;
    InputBindings b = InputBindings::defaults();
    // Capture: a side button (5), then an extra button of a gaming mouse (9), then the wheel.
    in.setBindings(b);
    in.beginFrame();
    in.beginCapture(InputDevice::Keyboard);
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = 5;
    in.handleEvent(e);
    CHECK(in.captureDone());
    CHECK(in.capturedKey() == mouseButtonBinding(5));
    CHECK(!in.mousePressed(SDL_BUTTON_X2));  // the capturing click goes to nothing else
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    in.handleEvent(e);
    CHECK(!in.mouseReleased(SDL_BUTTON_X2));
    in.cancelCapture();
    in.beginFrame();
    in.beginCapture(InputDevice::Keyboard);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = 9;
    in.handleEvent(e);
    CHECK(in.capturedKey() == mouseButtonBinding(9));
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    in.handleEvent(e);
    in.cancelCapture();
    in.beginFrame();
    in.beginCapture(InputDevice::Keyboard);
    SDL_Event w{};
    w.type = SDL_EVENT_MOUSE_WHEEL;
    w.wheel.y = -1.0f;
    in.handleEvent(w);
    CHECK(static_cast<int>(in.capturedKey()) == kWheelDownBinding);
    in.cancelCapture();

    // Names: short for the HUD, in words for the settings, saved and read back.
    CHECK(keyName(mouseButtonBinding(4)) == "Souris 4");
    CHECK(mouseBindingDetail(mouseButtonBinding(1)) == "clic gauche");
    CHECK(!mouseBindingDetail(mouseButtonBinding(9)).empty());
    CHECK(mouseBindingDetail(SDL_SCANCODE_K).empty());
    b.keys[static_cast<int>(Action::Attack)] = {mouseButtonBinding(1), SDL_SCANCODE_UNKNOWN};
    b.keys[static_cast<int>(Action::Jump)] = {mouseButtonBinding(9), SDL_SCANCODE_UNKNOWN};
    b.keys[static_cast<int>(Action::Interact)] = {static_cast<SDL_Scancode>(kWheelUpBinding), SDL_SCANCODE_UNKNOWN};
    InputBindings back = InputBindings::fromJson(b.toJson());
    CHECK(back.keys[static_cast<int>(Action::Attack)][0] == mouseButtonBinding(1));
    CHECK(back.keys[static_cast<int>(Action::Jump)][0] == mouseButtonBinding(9));
    CHECK(static_cast<int>(back.keys[static_cast<int>(Action::Interact)][0]) == kWheelUpBinding);

    // Playing: the bound button is a held command while down, the wheel a one-frame press.
    in.setBindings(back);
    in.beginFrame();
    in.update(0.016f);
    (void)in.consumePlayerInput();
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = 9;
    in.handleEvent(e);
    in.update(0.016f);
    CHECK(in.held(Action::Jump));
    CHECK(in.consumePlayerInput().isPressed(Action::Jump));
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    in.handleEvent(e);
    in.beginFrame();
    in.update(0.016f);
    CHECK(!in.held(Action::Jump));
    w.wheel.y = 1.0f;
    in.handleEvent(w);
    in.update(0.016f);
    CHECK(in.consumePlayerInput().isPressed(Action::Interact));
    in.beginFrame();
    in.update(0.016f);
    CHECK(!in.held(Action::Interact));
    // The key of a power slot (2.31: the key of the power carried there) can be a notch too.
    in.setPowerSlotKey(0, static_cast<SDL_Scancode>(kWheelDownBinding), PadBinding{});
    in.beginFrame();
    in.update(0.016f);
    (void)in.consumePlayerInput();
    w.wheel.y = -1.0f;
    in.beginFrame();
    in.handleEvent(w);
    in.update(0.016f);
    CHECK(in.consumePlayerInput().isPressed(Action::PowerSlot1));
}

#include "game/progression/Progression.h"

TEST_CASE("progression: time played per level, shown in hours and minutes") {
    cosmic::Progression p;
    p.addPlayTime("sylvanox_01", 125.0);
    p.addPlayTime("sylvanox_01", 3600.0);
    p.addPlayTime("", 30.0);  // a level without id (editor data) counts in the total only
    p.addPlayTime("pyralis_02", -5.0);
    CHECK(std::abs(p.levelPlayTime("sylvanox_01") - 3725.0) < 1e-9);
    CHECK(p.levelPlayTime("pyralis_02") == 0.0);
    CHECK(std::abs(p.playTimeSeconds - 3755.0) < 1e-9);
    cosmic::Progression q;
    q.fromJson(p.toJson());
    CHECK(std::abs(q.levelPlayTime("sylvanox_01") - 3725.0) < 1e-9);
    CHECK(cosmic::playTimeText(0) == "0 min");
    CHECK(cosmic::playTimeText(59) == "0 min");
    CHECK(cosmic::playTimeText(45 * 60 + 10) == "45 min");
    CHECK(cosmic::playTimeText(3725) == "1 h 02 min");
    CHECK(cosmic::playTimeText(120 * 3600 + 5 * 60) == "120 h 05 min");
}

TEST_CASE("pause map: only what the camera has shown is explored") {
    cosmic::ExploredMap m;
    m.reset({0, 0, 1000, 500});
    CHECK(m.cols == 16);
    CHECK(m.rows == 8);
    CHECK(m.empty());
    m.mark({100, 100, 200, 100});
    CHECK(!m.empty());
    CHECK(m.seenPoint({150, 150}));
    CHECK(!m.seenPoint({900, 450}));
    cosmic::Rect box = m.seenBox();
    CHECK(box.x <= 100.0f);
    CHECK(box.right() >= 300.0f);
    CHECK(box.right() <= 400.0f);
    m.mark({-500, -500, 100, 100});  // outside the level: ignored
    m.mark({950, 450, 400, 400});    // partly outside: the inside part counts
    CHECK(m.seenPoint({990, 490}));
}
