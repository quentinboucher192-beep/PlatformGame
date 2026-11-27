// 2.91 — StartPos and measure points (training): what a StartPos takes and gives back, its
// placement through the inputs (a replay plays back the same marks, the same run), the
// attempts from it played back exactly, the StartPos kept per level version, and the A → B
// chronometer of the measure points along a stream of positions (training and replays).
// 2.92: a StartPos placed from the menu (held by the terrain, refused in it, started as the
// training of its segment, its attempts played back exactly), moved and kept with its origin.

#include <cmath>
#include <system_error>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/StartPos.h"
#include "game/gameplay/entities/Entities.h"
#include "game/ghost/MeasurePoints.h"
#include "game/ghost/ReplayPlayback.h"
#include "game/ghost/StartPosStore.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/player/Player.h"
#include "game/player/PlayerConfig.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

fs::path tempFile(const char* what) { return fs::temp_directory_path() / (std::string("cosmic_") + what + "_" + makeUuidV4().substr(0, 8) + ".json"); }

struct Fixture {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Fixture() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices services() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

Fixture& fx() {
    static Fixture f;
    return f;
}

/// Flat level: checkpoints at 800 and 1500, exit at the end.
LevelData flatLevel() {
    LevelData l = LevelData::makeEmpty("test_training_marks", "Test startpos", "sylvanox");
    l.intro.steps.clear();
    for (float x : {800.0f, 1500.0f}) {
        LevelObject c;
        c.id = x < 1000.0f ? "cp1" : "cp2";
        c.type = "checkpoint";
        c.layer = "gameplay";
        c.rect = {x, 736, 32, 64};
        l.objects.push_back(c);
    }
    l.startPowers = {"dash"};
    return l;
}

std::unique_ptr<GameWorld> loadWorld(const LevelData& level) {
    auto w = std::make_unique<GameWorld>(fx().services(), WorldMode::Play);
    w->setKeysPerSlot(true);
    w->load(level);
    w->seedGameplay(levelSeed(level.id));
    return w;
}

/// Running right with a jump now and then; `marks` set on the given steps.
std::vector<InputFrame> runRight(int ticks, std::vector<std::pair<int, uint8_t>> marks = {}) {
    std::vector<InputFrame> out;
    InputFrame prev;
    for (int i = 0; i < ticks; ++i) {
        InputFrame f;
        f.held = 1u << static_cast<int>(Action::MoveRight);
        f.moveX = 127;
        if ((i / 40) % 4 == 1) f.held |= 1u << static_cast<int>(Action::Jump);
        f.pressed = f.held & ~prev.held;
        f.released = prev.held & ~f.held;
        prev = f;
        for (const auto& [at, flag] : marks)
            if (at == i) f.flags |= flag;
        out.push_back(f);
    }
    return out;
}

struct Played {
    ReplayData replay;
    std::vector<TrainingMark> marks;
    uint64_t hash = 0;
    float time = 0.0f;
};

/// Plays the inputs in a world set up by `setup` (none: the level's start) and records them.
Played play(const LevelData& level, const std::vector<InputFrame>& inputs, const SectionTrainingStart* setup = nullptr) {
    Played p;
    auto w = loadWorld(level);
    if (setup) applySectionTraining(*w, *setup);
    ReplayRecorder rec;
    rec.begin(level.id, fingerprintGameplay(fx().content, level), makeUuidV4(), w->gameplaySeed());
    for (InputFrame f : inputs) {
        if (w->cinematic().active() && w->cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
        rec.recordInput(f);
        stepWorld(*w, f, kSimDt);
        rec.tick(*w);
        for (TrainingMark& m : w->takeMarks()) p.marks.push_back(std::move(m));
    }
    p.time = w->timer().elapsed();
    rec.finishAttempt(*w, p.time);
    p.hash = simulationStateHash(*w);
    p.replay = rec.take();
    return p;
}

}  // namespace

TEST_CASE("startpos: placed through the inputs, the same marks and the same run in its replay") {
    const LevelData level = flatLevel();
    // A StartPos placed after cp1 (step 720, 6 s at 150 px/s), the next one asked for, a measure point.
    const auto inputs = runRight(1000, {{720, InputFrame::PlaceStartPos}, {730, InputFrame::NextStartPos}, {800, InputFrame::PlaceMeasure}});
    const Played live = play(level, inputs);
    CHECK(live.marks.size() == 3);
    CHECK(live.marks[0].flag == InputFrame::PlaceStartPos && live.marks[0].valid);
    CHECK(live.marks[1].flag == InputFrame::NextStartPos);
    CHECK(live.marks[2].flag == InputFrame::PlaceMeasure);
    const StartPosState& sp = live.marks[0].state;
    CHECK(sp.feet.x > 832.0f && sp.feet.x < 1500.0f);  // past cp1, before cp2
    CHECK(sp.reached == std::vector<std::string>{"cp1"});
    CHECK(sp.powers == std::vector<std::string>{"dash"});
    CHECK(sp.health >= 1);
    CHECK(live.marks[0].chrono > 0.0f);
    // The marks never change the simulation: the same run without them.
    const Played plain = play(level, runRight(1000));
    CHECK(plain.hash == live.hash);
    CHECK(plain.marks.empty());
    // Its replay (the flags kept as they are) gives back the same marks at the same steps.
    auto back = ReplayData::decode(live.replay.encode());
    CHECK(back.has_value());
    auto w = loadWorld(level);
    ReplaySimulator sim(*back, *w);
    std::vector<TrainingMark> again;
    while (sim.step())
        for (TrainingMark& m : w->takeMarks()) again.push_back(std::move(m));
    CHECK(sim.finished() && !sim.desynced());
    CHECK(simulationStateHash(*w) == live.hash);
    CHECK(again.size() == live.marks.size());
    for (size_t i = 0; i < again.size() && i < live.marks.size(); ++i) {
        CHECK(again[i].flag == live.marks[i].flag);
        CHECK(again[i].chrono == live.marks[i].chrono);
        CHECK(again[i].state == live.marks[i].state);
    }
}

TEST_CASE("startpos: an attempt from it starts as it was taken and its replay plays back exactly") {
    const LevelData level = flatLevel();
    const Played first = play(level, runRight(900, {{720, InputFrame::PlaceStartPos}}));
    CHECK(first.marks.size() == 1 && first.marks[0].valid);
    StartPosState sp = first.marks[0].state;
    sp.health = 2;     // as if hurt before
    sp.energy = 40.0f;
    sp.faceLeft = true;
    // Kept on disk with the attempt's start (its replay's setup), read back the same.
    SectionTrainingStart start;
    start.startPos = sp;
    auto read = SectionTrainingStart::fromJson(Json::parse(start.toJson().dump()));
    CHECK(read && read->startPos && *read->startPos == sp);
    // Damaged: refused (never trusted).
    Json bad = start.toJson();
    bad["startpos"]["health"] = 0;
    CHECK(!SectionTrainingStart::fromJson(bad));
    bad = start.toJson();
    bad["startpos"]["feet"] = {1.0f};
    CHECK(!SectionTrainingStart::fromJson(bad));
    // Applied: Nova there, facing left, her hearts and energy, cp1 lit without a split.
    {
        auto w = loadWorld(level);
        applySectionTraining(*w, *read);
        const Player& p = w->player();
        CHECK((p.feet() - sp.feet).length() < 0.5f);
        CHECK(p.facing < 0.0f);
        CHECK(p.health == 2);
        CHECK(std::abs(p.energy - 40.0f) < 1e-4f);
        CHECK(p.hasPower("dash"));
        CHECK(w->timer().splits().empty());
        const auto* cp = dynamic_cast<const CheckpointEntity*>(w->entity("cp1"));
        CHECK(cp && cp->activated());
        CHECK((w->respawnPoint() - sp.feet).length() < 0.5f);
    }
    // Two attempts from it with the same inputs: the same run; its replay, with the same start,
    // plays back exactly (every digest), and reaches cp2 with a split.
    const auto inputs = runRight(600);
    const Played a = play(level, inputs, &*read);
    const Played b = play(level, inputs, &*read);
    CHECK(a.hash == b.hash);
    CHECK(a.replay.exact());
    ReplayPlayback p(fx().services(), level, a.replay, [s = *read](GameWorld& w) { applySectionTraining(w, s); });
    while (p.step()) {
    }
    CHECK(p.finished() && !p.desynced());
    CHECK(p.checksPassed() >= 2);
    CHECK(p.world().checkpointId() == "cp2");
    CHECK(p.world().timer().splits().size() == 1);
    // Without its start, the replay is another run: it desynchronises.
    ReplayPlayback other(fx().services(), level, a.replay);
    while (other.step()) {
    }
    CHECK(other.desynced());
}

TEST_CASE("startpos: kept per level version, in the order of the route, cycled and removed") {
    const fs::path file = tempFile("startpos");
    StartPosState s;
    s.health = 3;
    {
        StartPosStore st(file);
        st.load();
        CHECK(st.list("lv-1").empty());
        CHECK(st.cycle("lv-1", 1) == 0);
        s.feet = {900.0f, 800.0f};
        const int a = st.add("lv-1", s, 0.5f, "2026-11-21 10:00");
        s.feet = {300.0f, 800.0f};
        const int b = st.add("lv-1", s, 0.2f, "2026-11-22 10:00");
        s.feet = {1200.0f, 800.0f};
        const int c = st.add("lv-1", s, -1.0f, "2026-11-23 10:00");  // where along the route: unknown
        CHECK(a > 0 && b > 0 && c > 0 && a != b && b != c);
        CHECK(st.activeId("lv-1") == c);  // the last placed
        auto l = st.list("lv-1");
        CHECK(l.size() == 3);
        CHECK(l[0].id == b && l[1].id == a);  // in the order of the route
        CHECK(st.rank("lv-1", b) == 0);
        st.setActive("lv-1", b);
        CHECK(st.cycle("lv-1", -1) == l[2].id);  // around the ends
        CHECK(st.cycle("lv-1", 1) == b);
        CHECK(st.cycle("lv-1", 1) == a);
        st.noteAttempt("lv-1", a);
        st.noteAttempt("lv-1", a);
        CHECK(st.remove("lv-1", c));
        CHECK(!st.remove("lv-1", c));
        st.show = false;
        CHECK(st.save());
    }
    {
        StartPosStore st(file);
        st.load();
        auto l = st.list("lv-1");
        CHECK(l.size() == 2);
        CHECK(!st.show);
        CHECK(st.activeId("lv-1") == l[1].id);
        CHECK(l[1].attempts == 2);
        CHECK(l[1].state.health == 3);
        CHECK(st.list("lv-2").empty());  // another version: its own list
        // A new one never reuses an id.
        const int d = st.add("lv-1", s, 0.9f, "2026-11-24 10:00");
        CHECK(d > l[0].id && d > l[1].id);
        st.clear("lv-1");
        CHECK(st.list("lv-1").empty() && st.activeId("lv-1") == 0);
    }
    std::error_code ec;
    fs::remove(file, ec);
}

TEST_CASE("measure points: A starts, B stops, between two positions, either way") {
    MeasureGate a{{100.0f, 800.0f}}, b{{300.0f, 800.0f}};
    // Crossing a gate: inside its height only, at the exact fraction of the move.
    CHECK(a.crossing({90.0f, 800.0f}, {110.0f, 800.0f}).has_value());
    CHECK_NEAR(*a.crossing({90.0f, 800.0f}, {110.0f, 800.0f}), 0.5f, 1e-5);
    CHECK(a.crossing({110.0f, 800.0f}, {90.0f, 800.0f}).has_value());  // either way
    CHECK(!a.crossing({90.0f, 800.0f}, {99.0f, 800.0f}));
    CHECK(!a.crossing({90.0f, 500.0f}, {110.0f, 500.0f}));   // far above the gate
    CHECK(!a.crossing({90.0f, 900.0f}, {110.0f, 900.0f}));   // below it
    CHECK(a.crossing({90.0f, 800.0f}, {100.0f, 800.0f}).has_value());   // ending on the line
    CHECK(!a.crossing({100.0f, 800.0f}, {110.0f, 800.0f}));             // starting on it
    // A run at 200 px/s sampled at 30 Hz (a replay's track): A at 0.5 s, B at 1.5 s.
    std::vector<std::pair<float, Vec2>> track;
    for (int i = 0; i <= 90; ++i) {
        float t = static_cast<float>(i) / 30.0f;
        track.push_back({t, {0.0f + 200.0f * t, 800.0f}});
    }
    auto times = measureTimes(track, a, b);
    CHECK(times.size() == 1);
    CHECK_NEAR(times[0], 1.0f, 1e-4);
    // The same at 120 Hz (live): the same time, whatever the rate.
    MeasureTimer timer(a, b);
    std::optional<float> got;
    for (int i = 0; i <= 360; ++i) {
        float t = static_cast<float>(i) / 120.0f;
        if (auto d = timer.feed(t, {200.0f * t, 800.0f})) got = d;
        if (t > 0.6f && t < 1.4f) CHECK(timer.running());
    }
    CHECK(got && std::abs(*got - 1.0f) < 1e-4f);
    CHECK(!timer.running());
    // B before A: nothing. Back over A: the chronometer starts again from there.
    MeasureTimer t2(a, b);
    t2.feed(0.0f, {320.0f, 800.0f});
    CHECK(!t2.feed(0.1f, {280.0f, 800.0f}));  // B crossed first, not running
    t2.feed(1.0f, {90.0f, 800.0f});            // A crossed (leftwards)
    CHECK(t2.running());
    t2.feed(1.1f, {120.0f, 800.0f});           // A again: started again here
    auto again = t2.feed(2.0f, {310.0f, 800.0f});
    CHECK(again && *again < 1.0f && *again > 0.8f);
    // A jump (a respawn, a seek): the chronometer stops, nothing crossed in between.
    MeasureTimer t3(a, b);
    t3.feed(0.0f, {90.0f, 800.0f});
    t3.feed(0.1f, {110.0f, 800.0f});
    CHECK(t3.running());
    CHECK(!t3.feed(0.2f, {700.0f, 800.0f}));
    CHECK(!t3.running());
    CHECK_NEAR(metersOf(48.0f), 3.0f, 1e-6);
}

TEST_CASE("measure points: placed A then B, times kept per level version") {
    const fs::path file = tempFile("measures");
    {
        MeasureStore m(file);
        m.load();
        const int id = m.place("lv-1", {100.0f, 800.0f}, "2026-11-21 10:00");
        CHECK(id > 0);
        CHECK(!m.find("lv-1", id)->complete());
        CHECK(!m.addTime("lv-1", id, 3.0f, "x"));  // no B yet: nothing to time
        CHECK(m.place("lv-1", {300.0f, 800.0f}, "2026-11-21 10:01") == id);
        CHECK(m.find("lv-1", id)->complete());
        CHECK(m.addTime("lv-1", id, 3.4f, "2026-11-21 10:02"));
        CHECK(m.addTime("lv-1", id, 3.182f, "2026-11-21 10:03"));
        CHECK(!m.addTime("lv-1", id, 3.1824f, "2026-11-21 10:04"));  // a tie to the millisecond
        CHECK(!m.addTime("lv-1", id, 3.5f, "2026-11-21 10:05"));
        const int second = m.place("lv-1", {900.0f, 800.0f}, "2026-11-21 10:06");
        CHECK(second > id);
        m.rename("lv-1", id, "Traversée de l'acide\n et un nom bien trop long pour tenir sur la ligne");
        CHECK(m.find("lv-1", id)->name.find('\n') == std::string::npos);
        CHECK(m.find("lv-1", id)->name.size() <= MeasureStore::kNameMax);
        m.inReplays = false;
        CHECK(m.save());
    }
    {
        MeasureStore m(file);
        m.load();
        auto l = m.list("lv-1");
        CHECK(l.size() == 2);
        CHECK(!m.inReplays && m.showGates);
        CHECK_NEAR(l[0].best, 3.182f, 1e-4);
        CHECK(l[0].attempts == 4 && l[0].last.size() == 4);
        CHECK_NEAR(l[0].last.back(), 3.5f, 1e-4);
        CHECK(!l[1].complete());
        // Moved: its times no longer compare.
        m.move("lv-1", l[0].id, true, {320.0f, 800.0f});
        CHECK(m.find("lv-1", l[0].id)->best < 0.0f && m.find("lv-1", l[0].id)->last.empty());
        CHECK(m.remove("lv-1", l[1].id));
        CHECK(m.list("lv-1").size() == 1);
        CHECK(m.list("lv-2").empty());
    }
    std::error_code ec;
    fs::remove(file, ec);
}

TEST_CASE("startpos: F5 to F8 by default, rebindable, never taken from another command") {
    const InputBindings d = InputBindings::defaults();
    const std::pair<Action, SDL_Scancode> marks[] = {{Action::StartPosPlace, SDL_SCANCODE_F5},
                                                     {Action::StartPosPrev, SDL_SCANCODE_F6},
                                                     {Action::StartPosNext, SDL_SCANCODE_F7},
                                                     {Action::MeasurePlace, SDL_SCANCODE_F8}};
    for (const auto& [a, key] : marks) {
        CHECK(actionIsRebindable(a));
        CHECK(d.keys[static_cast<int>(a)][0] == key);
        CHECK(actionFromId(actionId(a)) == a);
        // No other command read in a game (2.93: the level editor's « Tester », F5, is another
        // screen's) nor any menu uses that key by default.
        for (int o = 0; o < kActionCount; ++o)
            if (o != static_cast<int>(a) && (o >= kFirstMenuAction || (actionScope(static_cast<Action>(o)) & kScopeRun) != 0u))
                CHECK(d.keys[static_cast<size_t>(o)][0] != key && d.keys[static_cast<size_t>(o)][1] != key);
        // The gamepad: View held + the d-pad (no button of their own).
        CHECK(d.pad[static_cast<int>(a)][0].kind == PadBinding::Kind::None);
    }
    // A settings file of before them: their keys, unless that file gave one to another command.
    Json old = d.toJson();
    for (const auto& [a, key] : marks) {
        old["keyboard"].erase(actionId(a));
        old["gamepad"].erase(actionId(a));
    }
    old["keyboard"][actionId(Action::Jump)] = Json::array({"F6", ""});
    const InputBindings b = InputBindings::fromJson(old);
    CHECK(b.keys[static_cast<int>(Action::StartPosPlace)][0] == SDL_SCANCODE_F5);
    CHECK(b.keys[static_cast<int>(Action::StartPosPrev)][0] == SDL_SCANCODE_UNKNOWN);  // F6 is the jump there
    CHECK(b.keys[static_cast<int>(Action::Jump)][0] == SDL_SCANCODE_F6);
    // Kept in the file once rebound.
    InputBindings c = d;
    c.keys[static_cast<int>(Action::MeasurePlace)][0] = SDL_SCANCODE_M;
    c.keys[static_cast<int>(Action::RaidMap)] = {SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN};
    CHECK(InputBindings::fromJson(c.toJson()).keys[static_cast<int>(Action::MeasurePlace)][0] == SDL_SCANCODE_M);
}

TEST_CASE("startpos placed from the menu: held by the terrain, refused in it, started as its segment's training, played back exactly") {
    LevelData level = flatLevel();
    LevelObject wall;  // a wall after cp2 (out of the runs' way)
    wall.id = "wall";
    wall.type = "solid";
    wall.layer = "terrain";
    wall.rect = {1900, 700, 64, 100};
    level.objects.push_back(wall);
    LevelObject ledge;  // a one-way platform before cp1
    ledge.id = "ledge";
    ledge.type = "oneway";
    ledge.layer = "terrain";
    ledge.rect = {400, 700, 96, 16};
    level.objects.push_back(ledge);
    const PlayerConfig cfg = PlayerConfig::fromJson(fx().content.playerConfig());
    const Vec2 body = cfg.size;
    // On the ground: free; one pixel lower: in it. In the wall, against its side, out of the level.
    CHECK(startPosProblem(level, body, {1000.0f, 800.0f}) == StartPosProblem::None);
    CHECK(startPosProblem(level, body, {1000.0f, 801.0f}) == StartPosProblem::InSolid);
    CHECK(startPosProblem(level, body, {1930.0f, 760.0f}) == StartPosProblem::InSolid);
    CHECK(startPosProblem(level, body, {1900.0f - body.x * 0.5f, 800.0f}) == StartPosProblem::None);
    CHECK(startPosProblem(level, body, {1900.0f - body.x * 0.5f + 1.0f, 800.0f}) == StartPosProblem::InSolid);
    CHECK(startPosProblem(level, body, {2.0f, 800.0f}) == StartPosProblem::OutOfLevel);
    CHECK(startPosProblem(level, body, {1000.0f, 1000.0f}) == StartPosProblem::OutOfLevel);
    CHECK(startPosProblem(level, body, {1000.0f, -200.0f}) == StartPosProblem::OutOfLevel);
    // Where Nova stands near a place: the ground under it, the ledge from above, the top of the
    // wall from inside it.
    auto s1 = standingSpot(level, body, {1000.0f, 500.0f});
    CHECK(s1 && s1->x == 1000.0f && s1->y == 800.0f);
    auto s2 = standingSpot(level, body, {440.0f, 650.0f});
    CHECK(s2 && s2->y == 700.0f);
    auto s3 = standingSpot(level, body, {1930.0f, 760.0f});
    CHECK(s3 && s3->y == 700.0f);
    CHECK(startPosProblem(level, body, *s3) == StartPosProblem::None);
    // Its state: the training of its segment (cp1 → cp2): the start powers and the tablets
    // before it, cp1 lit, full hearts and energy, facing right.
    const StartPosState sp = segmentStartPos({1000.0f, 800.0f}, {"cp1"}, level.startPowers, {"glide"}, level.powerSlots(), nullptr, cfg.maxHealth,
                                             cfg.maxEnergy);
    CHECK((sp.powers == std::vector<std::string>{"dash", "glide"}));
    CHECK(sp.reached == std::vector<std::string>{"cp1"});
    CHECK(sp.health == cfg.maxHealth && sp.bonusHealth == 0);
    CHECK(sp.energy == cfg.maxEnergy);
    CHECK(!sp.faceLeft);
    // The powers chosen for that segment: exactly those; more tablets than slots: the last ones.
    const std::vector<std::string> chosen{"glide"};
    CHECK(segmentStartPos({1000.0f, 800.0f}, {}, level.startPowers, {"boomerang"}, 4, &chosen, 5, 100.0f).powers == chosen);
    CHECK((segmentStartPos({1000.0f, 800.0f}, {}, {}, {"boomerang", "glide", "magnet"}, 2, nullptr, 5, 100.0f).powers ==
           std::vector<std::string>{"glide", "magnet"}));
    // Kept with the attempt's start (its replay's), read back the same.
    SectionTrainingStart start;
    start.startPos = sp;
    auto read = SectionTrainingStart::fromJson(Json::parse(start.toJson().dump()));
    CHECK(read && read->startPos && *read->startPos == sp);
    // Applied: Nova there with that state, cp1 lit without a split.
    {
        auto w = loadWorld(level);
        applySectionTraining(*w, *read);
        const Player& p = w->player();
        CHECK((p.feet() - sp.feet).length() < 0.5f);
        CHECK(p.health == p.maxHealth());
        CHECK(std::abs(p.energy - cfg.maxEnergy) < 1e-4f);
        CHECK(p.hasPower("dash") && p.hasPower("glide"));
        CHECK(w->timer().splits().empty());
        const auto* cp = dynamic_cast<const CheckpointEntity*>(w->entity("cp1"));
        CHECK(cp && cp->activated());
    }
    // An attempt from it, its replay (encoded, decoded) with the same start: played back exactly.
    const auto inputs = runRight(600);
    const Played a = play(level, inputs, &*read);
    CHECK(a.replay.exact());
    auto back = ReplayData::decode(a.replay.encode());
    CHECK(back.has_value());
    ReplayPlayback p(fx().services(), level, *back, [s = *read](GameWorld& w) { applySectionTraining(w, s); });
    while (p.step()) {
    }
    CHECK(p.finished() && !p.desynced());
    CHECK(p.checksPassed() >= 2);
    CHECK(p.world().checkpointId() == "cp2");
    const Played b = play(level, inputs, &*read);
    CHECK(a.hash == b.hash);
}

TEST_CASE("startpos placed from the menu: moved and kept, its origin with it") {
    const fs::path file = tempFile("startpos_menu");
    StartPosState s;
    s.health = 5;
    s.energy = 100.0f;
    s.feet = {1000.0f, 800.0f};
    int id = 0;
    {
        StartPosStore st(file);
        st.load();
        id = st.add("lv-m", s, 0.4f, "2026-11-27 10:00", true);
        CHECK(id > 0);
        CHECK(st.find("lv-m", id)->menu);
        StartPosState moved = s;
        moved.feet.x += 1.0f;
        CHECK(st.update("lv-m", id, moved, 0.41f));
        CHECK(!st.update("lv-m", id + 7, moved, 0.41f));
        CHECK(!st.update("lv-other", id, moved, 0.41f));
        const int inRun = st.add("lv-m", s, 0.2f, "2026-11-27 10:01");
        CHECK(!st.find("lv-m", inRun)->menu);
        st.noteAttempt("lv-m", id);
        CHECK(st.save());
    }
    {
        StartPosStore st(file);
        st.load();
        const StartPosEntry* e = st.find("lv-m", id);
        CHECK(e && e->menu);
        CHECK(e->state.feet.x == 1001.0f && e->state.feet.y == 800.0f);
        CHECK(std::abs(e->at - 0.41f) < 1e-5f);
        CHECK(e->date == "2026-11-27 10:00" && e->attempts == 1);  // its date and attempts kept
        CHECK(st.list("lv-m").size() == 2);
        CHECK(st.remove("lv-m", id));
        CHECK(st.save());
    }
    {
        StartPosStore st(file);
        st.load();
        CHECK(st.find("lv-m", id) == nullptr);
        CHECK(st.list("lv-m").size() == 1);
    }
    std::error_code ec;
    fs::remove(file, ec);
}
