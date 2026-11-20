// 2.77.2 (docs/RAIDS.md §23, docs/COOP.md §1): a player who no longer plays.
//
// * « En groupe éliminé, le jeu tremble » : a wiped group's world no longer steps, but it was drawn with
//   the app's interpolation alpha, which changes at every frame — the camera swung between its last two
//   positions. Drawn with downed::drawAlpha, the view holds still; the spectator's free camera too; a
//   player down is shown their soul, still; another player watched is reached by a smooth move (never a
//   cut, never a back and forth); the cooperation's camera holds still around a player down.
// * The banners: « À TERRE — en attente de réanimation » while a player waits (its gauge while an ally
//   revives them), gone once revived; « GROUPE ÉLIMINÉ — mode spectateur jusqu'à la fin de l'événement »,
//   another banner; in cooperation, the team's revivals left.
// * Their layout, measured at 1280 × 720 and 1920 × 1080 in French and in English: each part inside the
//   panel and after the one before, the panel under the HUD, the notifications under the panel.
// * Nothing of it is simulation: the digests stay the same (tests/test_raid_revive.cpp, test_determinism).

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/camera/Camera.h"
#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/core/Scene.h"
#include "engine/input/InputManager.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/LevelFingerprint.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "saves/RaidHudLayout.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"
#include "ui/online/RaidHud.h"
#include "ui/online/RaidMenu.h"
#include "ui/screens/DownedView.h"
#include "ui/screens/RaidSpectator.h"
#include "ui/widgets/Glyphs.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

constexpr int kW = 1920, kH = 1080;

struct Svc {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Svc() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

InputFrame right() {
    InputFrame f;
    f.held |= 1u << static_cast<int>(Action::MoveRight);
    f.moveX = 127;
    return f;
}

InputFrame interact() {
    InputFrame f;
    f.held = 1u << static_cast<int>(Action::Interact);
    return f;
}

/// The frames of a raid step: `players` seats held, the others vacant; press / release edges.
struct Seats {
    std::vector<InputFrame> prev = std::vector<InputFrame>(raid::kMaxInstancePlayers);
    std::vector<InputFrame> frames(int players, std::vector<InputFrame> held) {
        held.resize(raid::kMaxInstancePlayers);
        for (int k = 0; k < raid::kMaxInstancePlayers; ++k) {
            InputFrame& f = held[static_cast<size_t>(k)];
            if (k >= players) {
                f = InputFrame{};
                f.flags = InputFrame::Vacant;
                continue;
            }
            f.pressed = f.held & ~prev[static_cast<size_t>(k)].held;
            f.released = prev[static_cast<size_t>(k)].held & ~f.held;
            prev[static_cast<size_t>(k)] = f;
        }
        return held;
    }
};

void killSeat(GameWorld& w, int i) {
    GameWorld::PlayerScope s(w, i);
    w.playerAt(i).kill(w);
}

float dist(Vec2 a, Vec2 b) { return (a - b).length(); }

/// What the screen draws a step after another: the camera's view, as RaidGameScene asks it.
WorldView shownView(const GameWorld& w, bool stepping, float alpha) {
    return const_cast<GameWorld&>(w).camera().view(kW, kH, downed::drawAlpha(stepping, alpha));
}

/// The app's interpolation alpha at frame `k` (what is left of its accumulator: it changes every frame).
float appAlpha(int k) { return std::fmod(0.13f + 0.37f * static_cast<float>(k), 1.0f); }

/// A team of the cooperation on a flat test level.
std::unique_ptr<GameWorld> coopWorld(Svc& s, int n) {
    LevelData l = LevelData::makeEmpty("test_coop_down", "Test", "sylvanox");
    l.intro.steps.clear();
    auto w = std::make_unique<GameWorld>(s.ws(), WorldMode::Play);
    w->setTeamSize(n);
    w->setKeysPerSlot(true);
    w->load(l);
    w->seedGameplay(levelSeed("test_coop_down"));
    return w;
}

void stepCoop(GameWorld& w, int ticks, int holder = -1) {
    std::vector<InputFrame> f(static_cast<size_t>(w.teamSize()));
    for (int t = 0; t < ticks; ++t) {
        if (holder >= 0) {
            f[static_cast<size_t>(holder)] = interact();
            f[static_cast<size_t>(holder)].pressed = t == 0 ? f[static_cast<size_t>(holder)].held : 0u;
        }
        stepTeam(w, f.data(), static_cast<int>(f.size()), kSimDt);
    }
}

/// A screen that draws nothing: the application's frames go on (the interface's scale follows the size).
struct Blank : Scene {
    void render(float) override {}
};

}  // namespace

TEST_CASE("2.77.2 groupe éliminé: the view holds still (no tremble), drawn as the world is; the free camera too") {
    Svc s;
    const raid::Branch* b = raid::Branch::builtin();
    if (!b) return;
    raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 1));
    run.setCameraFollow(0);
    Seats seats;
    auto step = [&](std::vector<InputFrame> held = {}) {
        auto f = seats.frames(2, std::move(held));
        run.step(f.data(), static_cast<int>(f.size()), 0);
    };
    for (int t = 0; t < 180; ++t) step();
    for (int t = 0; t < 40; ++t) step({right(), right()});  // the camera moves
    GameWorld& w = run.world();
    killSeat(w, 1);
    killSeat(w, 0);
    for (int t = 0; t < 600 && !run.wiped(); ++t) step();
    CHECK(run.wiped());
    // The cause: the world no longer steps, its camera keeps its last two positions; the app's alpha,
    // different at every frame, swung the view between them.
    float raw = 0.0f;
    for (int k = 1; k < 120; ++k) raw = std::max(raw, dist(w.camera().view(kW, kH, appAlpha(k)).topLeft, w.camera().view(kW, kH, appAlpha(k - 1)).topLeft));
    std::printf("  groupe éliminé : %.4f px de monde d'une image à l'autre avec l'alpha de l'application\n", static_cast<double>(raw));
    CHECK(raw > 1e-4f);
    // The fix: drawn as it is. N frames of spectator, no input: the same view, the same zoom.
    const WorldView first = shownView(w, !run.wiped(), appAlpha(0));
    float worst = 0.0f, zoom = 0.0f;
    for (int k = 0; k < 240; ++k) {
        if (k % 2 == 0) step();  // the relay still gives steps: nothing moves
        const WorldView v = shownView(w, !run.wiped(), appAlpha(k));
        worst = std::max(worst, dist(v.topLeft, first.topLeft));
        zoom = std::max(zoom, std::abs(v.scale - first.scale));
        // Every body as it is (its last state), whatever the alpha.
        for (int i = 0; i < w.teamSize(); ++i)
            CHECK(dist(w.playerAt(i).renderFeet(downed::drawAlpha(!run.wiped(), appAlpha(k))), w.playerAt(i).box.bottomCenter()) < 1e-4f);
    }
    CHECK(worst < 1e-3f);
    CHECK(zoom < 1e-6f);
    // The spectator's free camera (where the world's camera was), no input: still.
    raidspec::Spectator spec;
    spec.start(w.camera().center());
    InputManager in;
    const WorldView f0 = spec.view(kW, kH);
    for (int k = 0; k < 240; ++k) {
        in.beginFrame();
        spec.move(in, 1.0f / 60.0f, w.level().bounds);
        const WorldView v = spec.view(kW, kH);
        CHECK(dist(v.topLeft, f0.topLeft) < 1e-3f);
        CHECK(std::abs(v.scale - f0.scale) < 1e-6f);
    }
    // A world that steps keeps the app's alpha (the smooth motion of a game in play).
    CHECK(downed::drawAlpha(true, 0.25f) == 0.25f);
    CHECK(downed::drawAlpha(false, 0.25f) == 1.0f);
}

TEST_CASE("2.77.2 à terre: the camera rests on the soul; another player watched is reached smoothly, then the soul again") {
    Svc s;
    const raid::Branch* b = raid::Branch::builtin();
    if (!b) return;
    raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 1));
    run.setCameraFollow(0);
    Seats seats;
    auto step = [&](std::vector<InputFrame> held = {}) {
        auto f = seats.frames(2, std::move(held));
        run.step(f.data(), static_cast<int>(f.size()), 0);
    };
    for (int t = 0; t < 180; ++t) step();
    GameWorld& w = run.world();
    // The Épaule: firm ground from x 0 to 544 at y 576; seat 1 a little way from seat 0 (its own view).
    w.playerAt(1).teleportTo({450.0f, w.playerAt(0).feet().y});
    for (int t = 0; t < 60; ++t) step();
    killSeat(w, 0);
    for (int t = 0; t < 240; ++t) step();
    CHECK(w.member(0).presence == GameWorld::Presence::Down);
    CHECK(downed::stateOf(w, 0, run.wiped()) == downed::State::Down);
    // Its soul, framed and still (no input, the others idle): N steps, the same view.
    for (int t = 0; t < 360; ++t) step();
    const Vec2 soul = w.member(0).soul;
    CHECK(w.camera().visibleArea(kW, kH).contains(soul + Vec2{0.0f, -14.0f}));
    WorldView last = shownView(w, true, 1.0f);
    for (int t = 0; t < 120; ++t) {
        step();
        const WorldView v = shownView(w, true, appAlpha(t));
        CHECK(dist(v.topLeft, last.topLeft) < 0.02f);
        CHECK(std::abs(v.scale - last.scale) < 1e-4f);
        last = v;
    }
    // Another player watched (Q / E, LB / RB): the camera follows them; the view drawn moves there
    // without a cut nor a back and forth, its zoom too.
    auto watch = [&](int seat, Vec2 target) {
        downed::ViewBlend blend;
        blend.start(last);
        run.setCameraFollow(seat);
        std::vector<WorldView> shown;
        for (int k = 0; k < 150; ++k) {  // 60 images per second, two steps each
            step();
            step();
            shown.push_back(blend.apply(shownView(w, true, 1.0f), 1.0f / 60.0f));
        }
        const Vec2 end = downed::viewCenter(shown.back());
        const float d0 = dist(downed::viewCenter(last), end);
        std::printf("  vue changée : %.1f px de monde en %zu images\n", static_cast<double>(d0), shown.size());
        CHECK(d0 > 40.0f);
        CHECK(w.camera().visibleArea(kW, kH).contains(target));
        const float lo = std::min(last.scale, shown.back().scale) - 1e-3f, hi = std::max(last.scale, shown.back().scale) + 1e-3f;
        float prev = d0;
        for (size_t k = 0; k < shown.size(); ++k) {
            const float d = dist(downed::viewCenter(shown[k]), end);
            CHECK(d <= prev + 0.05f);                         // never back
            CHECK(std::abs(d - prev) <= 0.08f * d0 + 0.05f);  // never a jump
            if (k == 0) CHECK(prev - d <= 0.02f * d0 + 0.05f);  // a soft start
            CHECK(shown[k].scale >= lo);
            CHECK(shown[k].scale <= hi);
            prev = d;
        }
        // Arrived: still.
        for (size_t k = shown.size() - 20; k < shown.size(); ++k) CHECK(dist(shown[k].topLeft, shown.back().topLeft) < 0.05f);
        last = shown.back();
    };
    watch(1, w.playerAt(1).center());
    watch(0, soul + Vec2{0.0f, -14.0f});  // « Revenir sur mon âme »
    // The view moved: nothing of the simulation did (the camera is display only).
    CHECK(w.member(0).presence == GameWorld::Presence::Down);
    CHECK(w.member(1).presence == GameWorld::Presence::Playing);
}

TEST_CASE("2.77.2 a change of view: eased in and out from the view shown to the view wanted, then the view wanted itself") {
    WorldView a, b;
    a.screenW = b.screenW = kW;
    a.screenH = b.screenH = kH;
    a.scale = 4.0f;
    a.topLeft = Vec2{100.0f, 50.0f};
    b.scale = 3.0f;
    b.topLeft = Vec2{900.0f, 300.0f};
    downed::ViewBlend blend;
    CHECK(!blend.active());
    CHECK(dist(blend.apply(b, 1.0f / 60.0f).topLeft, b.topLeft) < 1e-4f);  // nothing to ease
    blend.start(a);
    const float d0 = dist(downed::viewCenter(a), downed::viewCenter(b));
    float prev = d0, scale = a.scale;
    int frames = 0;
    while (blend.active() && frames < 200) {
        const WorldView v = blend.apply(b, 1.0f / 60.0f);
        const float d = dist(downed::viewCenter(v), downed::viewCenter(b));
        CHECK(d <= prev + 1e-3f);
        CHECK(prev - d <= 0.05f * d0);  // at most 1.5 times the average pace
        CHECK(v.scale <= scale + 1e-5f);
        CHECK(v.scale >= b.scale - 1e-5f);
        prev = d;
        scale = v.scale;
        ++frames;
    }
    CHECK(frames >= 30);
    CHECK(frames <= 40);
    CHECK(!blend.active());
    const WorldView done = blend.apply(b, 1.0f / 60.0f);
    CHECK(dist(done.topLeft, b.topLeft) < 1e-4f);
    CHECK(done.scale == b.scale);
}

TEST_CASE("2.77.2 banners: « À TERRE » while waiting, gone once revived; « GROUPE ÉLIMINÉ » another one") {
    Svc s;
    const raid::Branch* b = raid::Branch::builtin();
    if (!b) return;
    raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 1));
    Seats seats;
    auto step = [&](std::vector<InputFrame> held = {}) {
        auto f = seats.frames(2, std::move(held));
        run.step(f.data(), static_cast<int>(f.size()), 0);
    };
    for (int t = 0; t < 180; ++t) step();
    GameWorld& w = run.world();
    CHECK(downed::stateOf(w, 0, run.wiped()) == downed::State::None);
    killSeat(w, 0);
    for (int t = 0; t < 240; ++t) step();
    CHECK(downed::stateOf(w, 0, run.wiped()) == downed::State::Down);
    CHECK(downed::stateOf(w, 1, run.wiped()) == downed::State::None);  // the other player still plays
    downed::DownInfo d;
    d.others = true;
    downed::Banner down = downed::downBanner(d);
    CHECK(down.state == downed::State::Down);
    CHECK(down.title == "À TERRE — en attente de réanimation");
    CHECK(!down.lines.empty());
    CHECK(down.lines[0] == "Un allié doit maintenir Interagir 2 s près de votre âme");
    CHECK(down.revive < 0.0f);  // nobody reviving yet: no gauge
    CHECK(down.commands.size() == 3);
    CHECK(down.commands[0].key == Action::MenuTabPrev);
    CHECK(down.commands[1].label == "Revenir sur mon âme");
    CHECK(!down.commands[1].enabled);  // on the soul already
    CHECK(down.commands[2].key == Action::MenuTabNext);
    // Seat 1 revives seat 0 (« Interagir » held near the soul): the gauge, then the banner goes.
    w.playerAt(1).teleportTo(w.member(0).soul + Vec2{16.0f, 0.0f});
    for (int t = 0; t < 120; ++t) step({InputFrame{}, interact()});
    CHECK(w.member(0).presence == GameWorld::Presence::Down);
    CHECK(w.member(0).revive > 0.5f);
    d.revive = w.member(0).revive;
    d.reviver = "Joueur 2";
    down = downed::downBanner(d);
    CHECK(down.revive > 0.25f);
    CHECK(down.revive < 1.0f);
    CHECK(down.reviveText.rfind("Joueur 2 vous réanime… ", 0) == 0);
    CHECK(down.reviveText.find(" %") != std::string::npos);
    for (int t = 0; t < 240 && w.member(0).presence == GameWorld::Presence::Down; ++t) step({InputFrame{}, interact()});
    CHECK(w.member(0).presence == GameWorld::Presence::Playing);
    CHECK(downed::stateOf(w, 0, run.wiped()) == downed::State::None);  // revived: no banner
    // Watching another player: said, « Revenir sur mon âme » offered.
    d.watching = "Joueur 2";
    d.revive = 0.0f;
    down = downed::downBanner(d);
    CHECK(down.commands[1].enabled);
    // The whole group down at once: the group wiped, a banner of its own.
    killSeat(w, 0);
    killSeat(w, 1);
    for (int t = 0; t < 600 && !run.wiped(); ++t) step();
    CHECK(run.wiped());
    CHECK(downed::stateOf(w, 0, run.wiped()) == downed::State::Wiped);
    const downed::Banner wiped = downed::wipedBanner("Caméra libre (flèches, stick, molette) · 3 joueurs à suivre", true, true);
    CHECK(wiped.state == downed::State::Wiped);
    CHECK(wiped.title == "GROUPE ÉLIMINÉ — mode spectateur jusqu'à la fin de l'événement");
    CHECK(wiped.title != down.title);
    CHECK(!(wiped.color == down.color));
    CHECK(wiped.commands.size() == 3);
    CHECK(wiped.commands[1].label == "Caméra libre");
    CHECK(!wiped.commands[1].enabled);  // the free camera already
    CHECK(wiped.revive < 0.0f);
}

TEST_CASE("2.77.2 cooperation: a player down sees « À TERRE », the team's revivals left; the team's camera holds still; revived, no banner") {
    Svc s;
    auto w = coopWorld(s, 2);
    stepCoop(*w, 120);
    CHECK(downed::stateOf(*w, 1, false) == downed::State::None);
    killSeat(*w, 1);
    stepCoop(*w, 240);
    CHECK(w->member(1).presence == GameWorld::Presence::Down);
    CHECK(downed::stateOf(*w, 1, false) == downed::State::Down);
    // The team's camera (the players in play): no input, still.
    stepCoop(*w, 240);
    WorldView last = shownView(*w, true, 1.0f);
    for (int t = 0; t < 120; ++t) {
        stepCoop(*w, 1);
        const WorldView v = shownView(*w, true, appAlpha(t));
        CHECK(dist(v.topLeft, last.topLeft) < 0.02f);
        CHECK(std::abs(v.scale - last.scale) < 1e-4f);
        last = v;
    }
    // A world that does not step (the end card, the pause on one PC, a power's choice): still too.
    const WorldView paused = shownView(*w, false, appAlpha(3));
    for (int k = 0; k < 60; ++k) CHECK(dist(shownView(*w, false, appAlpha(k)).topLeft, paused.topLeft) < 1e-4f);
    downed::DownInfo d;
    d.canRevive = w->canRevive();
    d.revivesLeft = w->revivesLeft();
    d.revivesMax = GameWorld::kTeamRevives;
    d.viewKeys = false;  // the team's camera
    const downed::Banner b = downed::downBanner(d);
    CHECK(b.title == "À TERRE — en attente de réanimation");
    CHECK(b.commands.empty());
    bool revives = false, camera = false;
    for (const std::string& l : b.lines) {
        revives = revives || l == "Réanimations restantes de l'équipe : 2 sur 2";
        camera = camera || l == "La caméra suit l'équipe en jeu";
    }
    CHECK(revives);
    CHECK(camera);
    // Two players on one PC: whose soul.
    d.who = "Joueur 2";
    CHECK(downed::downBanner(d).lines[0] == "Un allié doit maintenir Interagir 2 s près de l'âme de Joueur 2");
    // No revival left: said so.
    d.who.clear();
    d.canRevive = false;
    d.revivesLeft = 0;
    CHECK(downed::downBanner(d).lines[0].find("plus de réanimation") != std::string::npos);
    // Revived by seat 0: no banner any more, a revival used.
    w->playerAt(0).teleportTo(w->member(1).soul + Vec2{-16.0f, 0.0f});
    stepCoop(*w, 300, 0);
    CHECK(w->member(1).presence == GameWorld::Presence::Playing);
    CHECK(downed::stateOf(*w, 1, false) == downed::State::None);
    CHECK(w->revivesLeft() == GameWorld::kTeamRevives - 1);
}

TEST_CASE("2.77.2 layout: the banners measured, inside their panel, under the HUD, the notifications under them (1280 × 720, 1920 × 1080, FR / EN)") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        app.pushScene(std::make_unique<Blank>());
        Svc s;
        const raid::Branch* br = raid::Branch::builtin();
        CHECK(br != nullptr);
        raid::RaidRun run(s.ws(), *br, raid::kMaxInstancePlayers);
        CHECK(run.begin(0, 1));
        GameWorld& w = run.world();
        auto coop = coopWorld(s, 3);
        // The banners at their longest: a revival under way, another player watched, the soul out of view.
        downed::DownInfo d;
        d.revive = 1.2f;
        d.reviver = "Capitaine_Nébuleuse";
        d.watching = "Capitaine_Nébuleuse";
        d.soulInView = false;
        const downed::Banner raidDown = downed::downBanner(d);
        d.viewKeys = false;
        d.watching.clear();
        d.revivesLeft = 1;
        d.who = "Joueur 3";
        const downed::Banner coopDown = downed::downBanner(d);
        const downed::Banner wiped =
            downed::wipedBanner("Vous regardez Capitaine_Nébuleuse · [AUBE]·12 · zone 7", false, true);
        std::vector<raidmenu::Notice> ex(3);
        for (size_t i = 0; i < ex.size(); ++i) {
            ex[i].title = "ZONE TERMINÉE · Forge-cœur du Coude";
            ex[i].text = "Par un autre groupe (état validé par le serveur) : son tableau est dans le menu du raid.";
            ex[i].section = raidmenu::Section::Zones;
        }
        const std::vector<const raidmenu::Notice*> notices{&ex[0], &ex[1], &ex[2]};
        for (const Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}}) {
                app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
                app.renderer().refreshOutputSize();
                app.runFrames(1);
                app.renderer().refreshOutputSize();
                UIContext& ui = g.ui;
                ui.begin(app, 1.0f / 60.0f);
                const float W = ui.screenW(), H = ui.screenH();
                const Rect screen{0.0f, 0.0f, W, H};
                // The raid's HUD at its default places: this player's vitals and powers (the game's HUD at
                // the raids' places), then every block of the raids' own HUD but the notifications.
                Hud hud;
                hud.setRaidView(true);
                const GameplaySettings hs = raidhud::hudSettings(g.settings.gameplay);
                raidhud::HudAreas areas;
                {
                    GameWorld::PlayerScope me(w, 0);
                    auto areaOf = [&](HudElement e) { return hudVisible(hs, e) ? hud.area(ui, w, e, hud.place(ui, w, hs, e)) : Rect{}; };
                    areas.vitals = areaOf(HudElement::Vitals);
                    areas.powers = areaOf(HudElement::Powers);
                    areas.boss = areaOf(HudElement::Boss);
                }
                const RaidHudLayout l;
                for (const bool heartZone : {false, true}) {
                    std::vector<Rect> taken;
                    for (const Rect& a : {areas.vitals, areas.powers, areas.boss})
                        if (!a.empty()) taken.push_back(a);
                    for (int i = 0; i < kRaidHudElements; ++i) {
                        const auto e = static_cast<RaidHudElement>(i);
                        if (e == RaidHudElement::Notices || RaidHudLayout::hudElement(e) != HudElement::Count) continue;
                        if (!heartZone && (e == RaidHudElement::Heart || e == RaidHudElement::HeartEffects)) continue;
                        if (!(l[e].visible || !RaidHudLayout::canHide(e))) continue;
                        const Rect a = raidhud::area(ui, l, e, areas);
                        if (!a.empty()) taken.push_back(a);
                    }
                    for (const downed::Banner* b : {&raidDown, &wiped}) {
                        const downed::Layout L = downed::placed(ui, *b, taken);
                        const Rect& p = L.panel;
                        // At the top, inside the screen, over no block of the HUD.
                        CHECK(screen.containsRect(p));
                        CHECK(p.y < H * 0.45f);
                        for (const Rect& t : taken) {
                            if (p.intersects(t)) std::printf("  bandeau sur le HUD en %.0f x %.0f\n", size.x, size.y);
                            CHECK(!p.intersects(t));
                        }
                        // Each part inside the panel, after the one before.
                        CHECK(p.containsRect(L.title));
                        float y = L.title.bottom();
                        {
                            // The title whole (not shortened), at 75 % of its size at least.
                            const float fs = ui.fitFont(b->title, FontFace::Bold, 20, L.title.w, 0.75f);
                            CHECK(ui.text().measure(b->title, FontFace::Bold, ui.s(fs)).x <= L.title.w + 0.5f);
                            CHECK(ui.text().measure(b->title, FontFace::Bold, ui.s(fs)).y <= L.title.h + 0.5f);
                        }
                        CHECK(L.lines.size() == b->lines.size());
                        for (size_t i = 0; i < L.lineBoxes.size(); ++i) {
                            CHECK(p.containsRect(L.lineBoxes[i]));
                            CHECK(L.lineBoxes[i].y >= y - 0.01f);
                            y = L.lineBoxes[i].bottom();
                            CHECK(L.lines[i].size() <= 2);
                            i18n::Verbatim shown(true);
                            for (const std::string& t : L.lines[i]) {
                                const Vec2 m = ui.text().measure(t, FontFace::Body, ui.s(14));
                                CHECK(m.x <= L.lineBoxes[i].w + 0.5f);
                                CHECK(m.y <= ui.s(20) + 0.5f);
                            }
                        }
                        if (b->revive >= 0.0f) {
                            CHECK(p.containsRect(L.reviveLabel));
                            CHECK(p.containsRect(L.gauge));
                            CHECK(L.reviveLabel.y >= y - 0.01f);
                            CHECK(L.gauge.y >= L.reviveLabel.bottom() - 0.01f);
                            CHECK(ui.text().measure(b->reviveText, FontFace::Bold, ui.s(14)).x <= L.reviveLabel.w);
                            y = L.gauge.bottom();
                        }
                        CHECK(L.buttons.size() == b->commands.size());
                        for (size_t k = 0; k < L.buttons.size(); ++k) {
                            const Rect& bt = L.buttons[k];
                            CHECK(p.containsRect(bt));
                            CHECK(bt.y >= y - 0.01f);
                            for (size_t j = k + 1; j < L.buttons.size(); ++j) CHECK(!bt.intersects(L.buttons[j]));
                            // Its key, then its label, whole (not shortened) at 75 % at least.
                            const downed::Command& c = b->commands[k];
                            const float keyW = Glyphs::measureAction(ui, c.key, ui.s(24));
                            const float room = bt.w - ui.s(16) - keyW - ui.s(6);
                            const float fs = ui.fitFont(c.label, FontFace::Bold, 14, room, 0.75f);
                            const bool whole = ui.text().measure(c.label, FontFace::Bold, ui.s(fs)).x <= room + 0.5f;
                            if (!whole) std::printf("  « %s » trop long en %.0f x %.0f\n", c.label.c_str(), size.x, size.y);
                            CHECK(whole);
                        }
                        // The notifications under the banner: never over it nor over the HUD, inside the screen;
                        // in a usual zone, room for the menu's button and a notification at least.
                        std::vector<Rect> keep = taken;
                        keep.push_back(raidhud::area(ui, l, RaidHudElement::Danger, areas));
                        const Rect na = downed::below(raidhud::area(ui, l, RaidHudElement::Notices, areas), p, ui.s(10), keep);
                        CHECK(!na.intersects(p));
                        CHECK(na.h <= 0.0f || screen.containsRect(na));
                        for (const Rect& t : keep) CHECK(!na.intersects(t));
                        const raidmenu::NoticeLayout nl = raidmenu::layoutNotices(ui, na, 1.0f, notices, raidmenu::buttonText("Tab"), true);
                        for (const raidmenu::NoticeBox& nb : nl.boxes) {
                            CHECK(na.containsRect(nb.rect));
                            CHECK(!nb.rect.intersects(p));
                        }
                        if (!heartZone) {
                            CHECK(nl.button.w > 0.0f);
                            CHECK(!nl.boxes.empty());
                        }
                    }
                }
                // Cooperation: under the team panel and the HUD's blocks drawn this frame.
                {
                    WorldView view;
                    view.screenW = static_cast<int>(W);
                    view.screenH = static_cast<int>(H);
                    view.scale = H / Camera::kViewHeight;
                    Hud ch;
                    Hud::TeamInfo info;
                    info.names = {"Joueur 1", "Joueur 2", "Joueur 3"};
                    ch.setTeam(info);
                    ch.render(ui, *coop, view, nullptr, g.settings.gameplay);
                    std::vector<Rect> taken;
                    if (!ch.teamPanelRect().empty()) taken.push_back(ch.teamPanelRect());
                    for (int k = 0; k < Hud::kFadeBlocks; ++k)
                        if (!ch.drawn(k).empty()) taken.push_back(ch.drawn(k));
                    CHECK(!taken.empty());
                    const downed::Layout L = downed::placed(ui, coopDown, taken);
                    CHECK(screen.containsRect(L.panel));
                    CHECK(L.panel.y < H * 0.45f);
                    for (const Rect& t : taken) CHECK(!L.panel.intersects(t));
                    CHECK(L.buttons.empty());
                    for (size_t i = 0; i < L.lineBoxes.size(); ++i) CHECK(L.panel.containsRect(L.lineBoxes[i]));
                    CHECK(L.panel.containsRect(L.gauge));
                }
                // Drawn (once each), the banners take no focus.
                downed::draw(ui, raidDown, downed::placed(ui, raidDown, {}), "test/down");
                downed::draw(ui, wiped, downed::placed(ui, wiped, {}), "test/wiped");
                CHECK(!ui.hasFocus());
                ui.end();
            }
        }
        i18n::setLanguage(Language::French);
        // The raid's menu, « Groupe »: both states explained.
        raidmenu::MenuData data;
        data.groupTitle = "GROUPE · [AUBE]·1 · 2 / 8";
        data.groupStates = {{"À TERRE — en attente de réanimation", "Votre âme attend…", Color::white()},
                            {"GROUPE ÉLIMINÉ — mode spectateur jusqu'à la fin de l'événement", "Tout le groupe…", Color::white()}};
        app.runFrames(1);
        UIContext& ui = g.ui;
        ui.begin(app, 1.0f / 60.0f);
        raidmenu::MenuData none = data;
        none.groupStates.clear();
        CHECK(raidmenu::sectionHeight(ui, data, raidmenu::Section::Group, ui.s(600)) > raidmenu::sectionHeight(ui, none, raidmenu::Section::Group, ui.s(600)) + ui.s(60));
        ui.end();
    }
    app.shutdown();
}
