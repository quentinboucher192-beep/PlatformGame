// 2.80 (rules of cooperation 4, docs/COOP.md « Passages qui se ferment »): « en coopération attention à ne pas
// fermer la porte si tout le monde n'est pas passé ». A passage that closes behind the players (an arena's
// doors, a door, a gate or a laser an event closes) waits, open, until every player still there is through it:
//
// * every level a team can play (the campaign's EXTRÊME levels, the raid zones): each arena's open door, the
//   first player inside, the others behind — it waits; the last one through — it closes as before, nobody
//   on the wrong side; 2 and 3 players;
// * a player down outside (its soul) keeps it open until revived; a player disconnected (standby) does not;
// * a door closed by an event: on the side of the object that closed it (a trigger), else the team's side;
// * rules 3 (a replay of before 2.80) close at once, as they did: that was the bug;
// * the same inputs, the same digests on two computers; the solo game unchanged (test_determinism);
// * the HUD's line « Porte : en attente de MVP (12 m) », measured at 1280 × 720 and 1920 × 1080, FR / EN.

#include <SDL3/SDL.h>

#include <algorithm>
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
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/coop/CoopLevels.h"
#include "game/coop/CoopProtocol.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/LevelFingerprint.h"
#include "game/raid/RaidBranch.h"
#include "tests/TestFramework.h"
#include "ui/screens/DownedView.h"
#include "ui/screens/PassageWaitView.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

constexpr int kR = static_cast<int>(Action::MoveRight);
constexpr int kL = static_cast<int>(Action::MoveLeft);
constexpr int kI = static_cast<int>(Action::Interact);

InputFrame walk(int dir) {
    InputFrame f;
    if (dir > 0) f.held |= 1u << kR;
    if (dir < 0) f.held |= 1u << kL;
    f.moveX = static_cast<int8_t>(dir * 127);
    return f;
}

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

/// A team of `n` on `level` (a raid zone: in raid mode), rules `rules`.
struct Team {
    std::unique_ptr<GameWorld> w;
    std::vector<InputFrame> prev;
    Team(Svc& s, const LevelData& level, int n, bool raid = false, int rules = kCoopSimulationVersion) {
        w = std::make_unique<GameWorld>(s.ws(), WorldMode::Play);
        w->setTeamSize(n);
        w->setCoopVersion(rules);
        if (raid) w->setRaidMode(true);
        w->setKeysPerSlot(true);
        w->load(level);
        w->seedGameplay(levelSeed(level.id));
        prev.assign(static_cast<size_t>(n), InputFrame{});
    }
    void step(std::vector<InputFrame> frames) {
        frames.resize(prev.size());
        const bool skip = w->cinematic().active() && w->cinematic().skippable();
        for (size_t i = 0; i < frames.size(); ++i) {
            frames[i].pressed = frames[i].held & ~prev[i].held;
            frames[i].released = prev[i].held & ~frames[i].held;
            if (skip && i == 0) frames[i].flags |= InputFrame::SkipCinematic;
            prev[i] = frames[i];
        }
        stepTeam(*w, frames.data(), static_cast<int>(frames.size()), kSimDt);
    }
    void idle(int ticks) {
        for (int i = 0; i < ticks; ++i) step({});
    }
    Player& p(int i) { return w->playerAt(i); }
    void place(int i, Vec2 feet) {
        p(i).teleportTo(feet);
        p(i).vel = {};
        p(i).setInvulnerable(30.0f);  // the arena's enemies do not end the test
    }
};

/// An arena that closes a passage behind the players: its zone, its doors open at the start (the ones it
/// closes; a door closed from the start is its way out, opened once cleared).
struct Arena {
    std::string zone;
    Rect box;
    std::vector<std::string> doors;
};

std::vector<std::string> ids(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s + ",") {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (c != ' ') {
            cur.push_back(c);
        }
    }
    return out;
}

std::vector<Arena> arenasOf(const LevelData& l) {
    std::vector<Arena> out;
    for (const LevelObject& z : l.objects) {
        if (z.type != "combat_zone") continue;
        Arena a{z.id, z.rect, {}};
        for (const std::string& d : ids(z.propStr("doors")))
            for (const LevelObject& o : l.objects)
                if (o.id == d && o.type == "door" && o.prop<bool>("open", false)) a.doors.push_back(d);
        if (!a.doors.empty()) out.push_back(a);
    }
    return out;
}

DoorEntity* door(GameWorld& w, const std::string& id) { return dynamic_cast<DoorEntity*>(w.entity(id)); }

/// The side of the door the arena is on (+1: right), the feet just inside / outside it.
struct Sides {
    int in = 1;
    Vec2 inside, outside;
};
Sides sidesOf(const Rect& d, const Rect& zone, int k) {
    Sides s;
    s.in = zone.center().x > d.center().x ? 1 : -1;
    const float in = static_cast<float>(s.in);
    s.inside = {d.center().x + in * (d.w * 0.5f + 40.0f + 26.0f * static_cast<float>(k)), d.bottom()};
    s.outside = {d.center().x - in * (d.w * 0.5f + 40.0f + 26.0f * static_cast<float>(k)), d.bottom()};
    return s;
}

/// Player `i` beyond door `d`, on side `in` (its whole body).
bool through(const Player& p, const Rect& d, int in) { return in > 0 ? p.box.x >= d.right() : p.box.right() <= d.x; }

/// Every arena of `level`, one after the other: the first player in, the others behind (the door waits),
/// then each of them in (the door closes after the last). Returns the passages checked; `label` for reports.
int auditLevel(Svc& s, const LevelData& level, int n, bool raid, const std::string& label) {
    const std::vector<Arena> arenas = arenasOf(level);
    if (arenas.empty()) return 0;
    Team t(s, level, n, raid);
    t.idle(150);  // the intro (skipped), the team placed
    int checked = 0;
    for (const Arena& a : arenas) {
        const std::string& first = a.doors.front();
        DoorEntity* d = door(*t.w, first);
        CHECK(d != nullptr);
        if (!d || !d->isOpen()) continue;
        const Rect db = d->box;
        // The first player inside the arena, the others behind the door.
        t.place(0, sidesOf(db, a.box, 0).inside);
        for (int i = 1; i < n; ++i) t.place(i, sidesOf(db, a.box, i - 1).outside);
        t.idle(4);
        auto* zone = dynamic_cast<CombatZoneEntity*>(t.w->entity(a.zone));
        CHECK(zone != nullptr);
        const bool started = zone && zone->started();
        if (!started) std::printf("  %s (%d joueurs) : l'arène %s n'a pas commencé\n", label.c_str(), n, a.zone.c_str());
        CHECK(started);
        // It waits, open, for the players behind; the HUD knows who and how far.
        bool waits = true;
        for (const std::string& id : a.doors)
            if (DoorEntity* o = door(*t.w, id)) waits = waits && o->isOpen() && o->closingWait() != nullptr;
        if (!waits) std::printf("  %s (%d joueurs) : la porte %s s'est fermée sur un joueur dehors\n", label.c_str(), n, first.c_str());
        CHECK(waits);
        std::vector<GameWorld::Straggler> behind;
        const Vec2 arenaCentre = a.box.center();
        CHECK(!t.w->teamThrough(db, &arenaCentre, &behind));
        CHECK(static_cast<int>(behind.size()) == n - 1);
        t.idle(30);
        CHECK(d->isOpen());
        // The others in, one by one: still open while one is behind, closed with the last.
        for (int i = 1; i < n; ++i) {
            t.place(i, sidesOf(db, a.box, i).inside);
            t.idle(2);
            if (i < n - 1) CHECK(d->isOpen());
        }
        const bool closed = !d->isOpen() && d->closingWait() == nullptr;
        if (!closed) std::printf("  %s (%d joueurs) : la porte %s ne s'est pas fermée, toute l'équipe passée\n", label.c_str(), n, first.c_str());
        CHECK(closed);
        const int in = sidesOf(db, a.box, 0).in;
        for (int i = 0; i < n; ++i) CHECK(through(t.p(i), db, in));
        ++checked;
    }
    return checked;
}

LevelData level(Svc& s, const std::string& id) {
    auto l = s.content.loadLevel(id);
    CHECK(l.has_value());
    return l ? *l : LevelData{};
}

/// A flat test level with a door at x 1000 (open) and, beyond it, `closer` (a trigger zone, or none: the door
/// is closed by the level's clock), the event closing the door.
LevelData doorLevel(bool trigger) {
    LevelData l = LevelData::makeEmpty("test_coop_door", "Test", "sylvanox");
    l.intro.steps.clear();
    LevelObject d;
    d.id = "door1";
    d.type = "door";
    d.layer = "gameplay";
    d.rect = {1000, 672, 16, 128};
    d.props = {{"open", true}};
    l.objects.push_back(d);
    LevelEvent e;
    e.id = "ev1";
    if (trigger) {
        LevelObject z;
        z.id = "trig1";
        z.type = "trigger";
        z.layer = "gameplay";
        z.rect = {1100, 600, 120, 200};
        l.objects.push_back(z);
        e.source = "trig1";
        e.trigger = "enter";
    } else {
        e.source = "level";
        e.trigger = "time";
        e.params = {{"seconds", 1.0}};
    }
    EventAction a;
    a.type = "close";
    a.target = "door1";
    e.actions.push_back(a);
    l.events.push_back(e);
    return l;
}

struct Blank : Scene {
    void render(float) override {}
};

}  // namespace

TEST_CASE("2.80 the bug (rules 3): an arena's door closed on the player left behind; rules 4: it waits") {
    CHECK(kCoopSimulationVersion == 4);
    Svc s;
    const LevelData l = level(s, "noyau_01");
    const std::vector<Arena> arenas = arenasOf(l);
    CHECK(!arenas.empty());
    if (arenas.empty()) return;
    const Arena& a = arenas.front();
    for (const int rules : {3, 4}) {
        Team t(s, l, 2, false, rules);
        t.idle(150);
        DoorEntity* d = door(*t.w, a.doors.front());
        CHECK(d && d->isOpen());
        if (!d) return;
        const Rect db = d->box;
        t.place(0, sidesOf(db, a.box, 0).inside);
        t.place(1, sidesOf(db, a.box, 0).outside);
        t.idle(4);
        // Rules 3 (the screenshot: MVP outside, NovaRider in the arena): closed at once. Rules 4: open.
        CHECK(d->isOpen() == (rules >= 4));
        CHECK((d->closingWait() != nullptr) == (rules >= 4));
        if (rules < 4) continue;
        // Player 2 walks in by itself: the door closes behind it, never on it.
        const int in = sidesOf(db, a.box, 0).in;
        bool closed = false;
        for (int k = 0; k < 240 && !closed; ++k) {
            t.step({InputFrame{}, walk(in)});
            closed = !d->isOpen();
            if (closed) {
                CHECK(through(t.p(1), db, in));
                CHECK(through(t.p(0), db, in));
            }
        }
        CHECK(closed);
    }
}

TEST_CASE("2.80 audit: every arena of every level a team plays waits for the whole team, 2 and 3 players") {
    Svc s;
    // The campaign's EXTRÊME levels (one life: the levels of cooperation, findCoopLevels).
    const std::vector<CoopLevel> coop = findCoopLevels(s.content, Json::object(), {});
    CHECK(coop.size() >= 8);
    int passages = 0, levels = 0;
    for (const CoopLevel& c : coop) {
        const LevelData l = level(s, c.id);
        const int arenas = static_cast<int>(arenasOf(l).size());
        if (arenas == 0) continue;
        ++levels;
        for (const int n : {2, 3}) passages += auditLevel(s, l, n, false, c.id);
        std::printf("  %s : %d passage(s) qui se ferment derrière l'équipe\n", c.id.c_str(), arenas);
    }
    CHECK(levels >= 8);
    // The raid zones (2.78: each player changes zone alone; the players of the zone).
    int raidPassages = 0;
    std::vector<const raid::Branch*> branches = raid::Branch::all();
    CHECK(branches.size() >= 7);
    if (const raid::Branch* heart = raid::Branch::builtin("coeur"); heart && std::find(branches.begin(), branches.end(), heart) == branches.end())
        branches.push_back(heart);
    for (const raid::Branch* b : branches) {
        std::vector<const LevelData*> zones;
        for (const LevelData& l : b->levels) zones.push_back(&l);
        if (b->armoryLevel) zones.push_back(&*b->armoryLevel);
        if (b->lairArmoryLevel) zones.push_back(&*b->lairArmoryLevel);
        for (const LevelData* l : zones) {
            const int arenas = static_cast<int>(arenasOf(*l).size());
            if (arenas == 0) continue;
            for (const int n : {2, 3}) raidPassages += auditLevel(s, *l, n, true, b->part + "/" + l->id);
            std::printf("  raid %s/%s : %d passage(s) qui se ferment derrière le groupe\n", b->part.c_str(), l->id.c_str(), arenas);
        }
    }
    std::printf("  %d niveaux EXTRÊME (%d vérifications), %d vérifications dans les zones du raid\n", levels, passages, raidPassages);
    CHECK(passages > 0);
}

TEST_CASE("2.80 a player down outside keeps the door open until revived; a player disconnected does not count") {
    Svc s;
    const LevelData l = level(s, "noyau_01");
    const Arena a = arenasOf(l).front();
    {
        Team t(s, l, 2);
        t.idle(150);
        DoorEntity* d = door(*t.w, a.doors.front());
        const Rect db = d->box;
        const Sides sd = sidesOf(db, a.box, 0);
        t.place(0, sd.inside);
        t.place(1, sd.outside);
        t.idle(4);
        CHECK(d->closingWait() != nullptr);
        // Player 2 falls outside: its soul waits there, the door with it.
        {
            GameWorld::PlayerScope k(*t.w, 1);
            t.p(1).kill(*t.w);
        }
        t.idle(360);
        CHECK(t.w->member(1).presence == GameWorld::Presence::Down);
        CHECK(d->isOpen());
        std::vector<GameWorld::Straggler> behind;
        const Vec2 arenaCentre = a.box.center();
        CHECK(!t.w->teamThrough(db, &arenaCentre, &behind));
        CHECK(behind.size() == 1 && behind[0].down);
        // Player 1 comes out to revive it (« Interagir » held 2 s near its soul): both outside, still open.
        t.place(0, t.w->member(1).soul);
        InputFrame hold;
        hold.held = 1u << kI;
        for (int k = 0; k < 300 && t.w->member(1).presence == GameWorld::Presence::Down; ++k) t.step({hold, InputFrame{}});
        CHECK(t.w->member(1).presence == GameWorld::Presence::Playing);
        t.idle(120);
        CHECK(d->isOpen());
        // Both in: it closes.
        t.place(0, sidesOf(db, a.box, 0).inside);
        t.place(1, sidesOf(db, a.box, 1).inside);
        t.idle(2);
        CHECK(!d->isOpen());
    }
    {
        // Three players: the third disconnects outside (standby): the door no longer waits for it; back, it
        // comes near the team, inside.
        Team t(s, l, 3);
        t.idle(150);
        DoorEntity* d = door(*t.w, a.doors.front());
        const Rect db = d->box;
        t.place(0, sidesOf(db, a.box, 0).inside);
        t.place(1, sidesOf(db, a.box, 1).inside);
        t.place(2, sidesOf(db, a.box, 0).outside);
        t.idle(4);
        CHECK(d->isOpen());
        std::vector<InputFrame> away(3);
        away[2].flags = InputFrame::Standby;
        t.step(away);
        t.step(away);
        CHECK(t.w->member(2).presence == GameWorld::Presence::Standby);
        CHECK(!d->isOpen());
        t.idle(2);
        CHECK(t.w->member(2).presence == GameWorld::Presence::Playing);
        CHECK(through(t.p(2), db, sidesOf(db, a.box, 0).in));
    }
}

TEST_CASE("2.80 a door closed by an event: on the side of its trigger; else once the team is on one side") {
    Svc s;
    for (const bool trigger : {true, false}) {
        Team t(s, doorLevel(trigger), 2);
        t.idle(30);
        DoorEntity* d = door(*t.w, "door1");
        CHECK(d && d->isOpen());
        if (!d) return;
        // Player 1 beyond the door (in the trigger), player 2 before it.
        t.place(0, {1160.0f, 800.0f});
        t.place(1, {900.0f, 800.0f});
        t.idle(trigger ? 4 : 240);  // the trigger, or the level's clock (1 s)
        CHECK(d->isOpen());
        CHECK(d->closingWait() != nullptr);
        t.idle(60);
        CHECK(d->isOpen());
        if (trigger) {
            // Player 1 coming back out does not close it on the trigger's side: it waits for both beyond.
            t.place(0, {900.0f, 800.0f});
            t.idle(4);
            CHECK(d->isOpen());
            t.place(0, {1160.0f, 800.0f});
        }
        t.place(1, {1190.0f, 800.0f});
        t.idle(2);
        CHECK(!d->isOpen());
        CHECK(t.p(0).box.x >= d->box.right());
        CHECK(t.p(1).box.x >= d->box.right());
    }
    // Alone, the same door closes at once (the solo game never waits).
    Team solo(s, doorLevel(true), 1);
    solo.idle(30);
    solo.place(0, {1160.0f, 800.0f});
    solo.idle(4);
    CHECK(!door(*solo.w, "door1")->isOpen());
}

TEST_CASE("2.80 the same inputs give the same digests on two computers; rules 3 and 4 differ where a door waits") {
    Svc s;
    const LevelData l = level(s, "noyau_01");
    const Arena a = arenasOf(l).front();
    auto run = [&](int n, int rules) {
        Team t(s, l, n, false, rules);
        std::vector<uint64_t> digests;
        t.idle(150);
        const Rect db = door(*t.w, a.doors.front())->box;
        const Sides sd = sidesOf(db, a.box, 0);
        t.place(0, sd.inside);
        for (int i = 1; i < n; ++i) t.place(i, sidesOf(db, a.box, i - 1).outside);
        for (int k = 0; k < 600; ++k) {
            std::vector<InputFrame> f(static_cast<size_t>(n));
            for (int i = 1; i < n; ++i) f[static_cast<size_t>(i)] = walk(k > 200 + 60 * i ? sd.in : 0);
            t.step(f);
            if ((k + 1) % 60 == 0) digests.push_back(simulationStateHash(*t.w));
        }
        return digests;
    };
    // The reference digests (noyau_01, its first arena: the first player in, the others walking in later).
    struct Golden {
        int players, rules;
        const char* digest;
    };
    const Golden golden[] = {{2, 3, "e8a3a534b94445b3"}, {2, 4, "cbe132da7d60e543"}, {3, 3, "7b1cab268fc62ade"}, {3, 4, "2887c97997d446e2"}};
    int mismatches = 0;
    for (const int n : {2, 3}) {
        const auto a4 = run(n, 4), b4 = run(n, 4), a3 = run(n, 3);
        CHECK(a4 == b4);
        CHECK(!a4.empty() && a4 != a3);
        for (const Golden& g : golden) {
            if (g.players != n) continue;
            const auto& got = g.rules == 4 ? a4 : a3;
            char hex[17];
            std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(got.empty() ? 0 : got.back()));
            if (std::string(hex) != g.digest) {
                std::printf("  noyau_01, équipe de %d, coop %d : %s (attendu %s)\n", n, g.rules, hex, g.digest);
                ++mismatches;
            }
        }
    }
    CHECK(mismatches == 0);
}

TEST_CASE("2.80 the relay plays one version of the rules: HELLO says its rules, a game before 2.80 says none") {
    using namespace coopnet;
    const auto h = hello(255, "Nova");
    CHECK(h.back() == static_cast<uint8_t>(kCoopSimulationVersion));
    CHECK(static_cast<int>(EndReason::Rules) == 5);  // 4: Removed (an administrator's test, 2.80)
    CHECK(kRulesBefore280 == 3);
}

TEST_CASE("2.80 layout: « Porte : en attente de … » measured, inside its panel, under the HUD (1280 × 720, 1920 × 1080, FR / EN)") {
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
        Team team(s, level(s, "noyau_01"), 3);
        team.idle(10);
        // The longest lines: names of 24 characters, one down, far away.
        const std::string longName(24, 'W');
        for (const Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            // The text itself: the kind and the names, the distance in metres.
            const std::string one = passagewait::line("door", {{"MVP", 12, false}});
            CHECK(one == (lang == Language::French ? "Porte : en attente de MVP (12 m)" : "Door: waiting for MVP (12 m)"));
            const std::string down = passagewait::line("door", {{"Nova", 3, true}});
            CHECK(down == (lang == Language::French ? "Porte : en attente de Nova (à terre, 3 m)" : "Door: waiting for Nova (down, 3 m)"));
            const std::vector<std::string> shown{passagewait::line("door", {{"MVP", 12, false}}),
                                                 passagewait::line("door", {{longName, 1234, true}, {longName + "2", 987, false}})};
            for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}}) {
                app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
                app.renderer().refreshOutputSize();
                app.runFrames(1);
                app.renderer().refreshOutputSize();
                UIContext& ui = g.ui;
                ui.begin(app, 1.0f / 60.0f);
                const float W = ui.screenW(), H = ui.screenH();
                const Rect screen{0.0f, 0.0f, W, H};
                // The cooperation's HUD drawn this frame: its team panel and blocks.
                WorldView view;
                view.screenW = static_cast<int>(W);
                view.screenH = static_cast<int>(H);
                view.scale = H / Camera::kViewHeight;
                Hud ch;
                Hud::TeamInfo info;
                info.names = {longName, "MVP", "NovaRider"};
                ch.setTeam(info);
                ch.render(ui, *team.w, view, nullptr, g.settings.gameplay);
                std::vector<Rect> taken;
                if (!ch.teamPanelRect().empty()) taken.push_back(ch.teamPanelRect());
                for (int k = 0; k < Hud::kFadeBlocks; ++k)
                    if (!ch.drawn(k).empty()) taken.push_back(ch.drawn(k));
                CHECK(!taken.empty());
                // With and without a player down's banner above it.
                downed::DownInfo di;
                di.revivesLeft = 1;
                const downed::Layout banner = downed::placed(ui, downed::downBanner(di), taken);
                for (const bool withBanner : {false, true}) {
                    std::vector<Rect> keep = taken;
                    if (withBanner) keep.push_back(banner.panel);
                    const passagewait::Layout L = passagewait::placed(ui, shown, keep);
                    CHECK(screen.containsRect(L.panel));
                    CHECK(L.panel.y < H * 0.6f);
                    for (const Rect& r : keep) {
                        if (L.panel.intersects(r)) std::printf("  « en attente » sur le HUD en %.0f x %.0f\n", size.x, size.y);
                        CHECK(!L.panel.intersects(r));
                    }
                    CHECK(L.lineBoxes.size() == shown.size());
                    float y = L.panel.y;
                    i18n::Verbatim v(true);
                    for (size_t i = 0; i < L.lineBoxes.size(); ++i) {
                        const Rect& b = L.lineBoxes[i];
                        CHECK(L.panel.containsRect(b));
                        CHECK(b.y >= y - 0.01f);
                        y = b.bottom();
                        for (size_t j = i + 1; j < L.lineBoxes.size(); ++j) CHECK(!b.intersects(L.lineBoxes[j]));
                        const Vec2 m = ui.text().measure(L.shown[i], FontFace::Bold, ui.s(L.sizes[i]));
                        CHECK(m.x <= b.w + 0.5f);
                        CHECK(m.y <= b.h + 0.5f);
                        CHECK(L.sizes[i] >= passagewait::kLineSize * 0.75f - 0.01f);
                    }
                    // The short line whole, never shortened.
                    CHECK(L.shown[0] == shown[0]);
                    passagewait::draw(ui, L);
                }
                CHECK(!ui.hasFocus());
                ui.end();
            }
        }
        i18n::setLanguage(Language::French);
    }
    app.shutdown();
}
