// Daily challenges (2.31, docs/online/DAILY_CHALLENGES.md): the canonical form of the rules
// and its SHA-256 (the fixture shared with the server), the rules in the simulation (hearts,
// slots, imposed powers, stations, checkpoints, a death going back to the start), the
// determinism of a challenge run and the replay format 4 (written, read, played again).

#include <cstdio>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/ChallengeRules.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/RunCounters.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/Replay.h"
#include "game/records/Records.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

ChallengeRules sampleRules(bool stations = false, bool checkpoints = false) {
    ChallengeRules r;
    r.challengeId = "daily-2026-11-16";
    r.hearts = 2;
    r.slots = 3;
    r.powers = {"grapple", "glide", "dash"};
    r.stations = stations;
    r.checkpoints = checkpoints;
    return r;
}

struct ChallengeSim {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;

    explicit ChallengeSim(const std::string& levelId, const ChallengeRules* rules, bool keysPerSlot = true) {
        content.reload();
        res.init(nullptr, &audio);
        auto level = content.loadLevel(levelId);
        CHECK(level.has_value());
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->setKeysPerSlot(keysPerSlot);
        w->load(*level);
        w->seedGameplay(levelSeed(levelId));
        if (rules) w->setChallenge(*rules);
    }

    void step(InputFrame f = {}) {
        if (w->cinematic().active() && w->cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
        stepWorld(*w, f, kSimDt);
    }

    std::vector<std::string> powers() const {
        std::vector<std::string> out;
        for (const auto& p : w->player().powers()) out.push_back(p->id());
        return out;
    }
};

/// Plausible play (running right, jumps, attacks, dashes), the same for every run.
std::vector<InputFrame> playInputs(uint64_t seed, int ticks) {
    Random r(seed);
    std::vector<InputFrame> out;
    InputFrame prev, cur;
    int hold = 0;
    for (int i = 0; i < ticks; ++i) {
        if (hold-- <= 0) {
            cur = InputFrame{};
            float k = r.unit();
            int dir = k < 0.75f ? 1 : (k < 0.88f ? -1 : 0);
            if (dir > 0) cur.held |= 1u << static_cast<int>(Action::MoveRight);
            if (dir < 0) cur.held |= 1u << static_cast<int>(Action::MoveLeft);
            cur.moveX = static_cast<int8_t>(dir * 127);
            if (r.chance(0.45f)) cur.held |= 1u << static_cast<int>(Action::Jump);
            if (r.chance(0.2f)) cur.held |= 1u << static_cast<int>(Action::Attack);
            if (r.chance(0.1f)) cur.held |= 1u << static_cast<int>(Action::PowerSlot1);
            hold = r.rangeInt(4, 40);
        }
        InputFrame f = cur;
        f.pressed = f.held & ~prev.held;
        f.released = prev.held & ~f.held;
        out.push_back(f);
        prev = f;
    }
    return out;
}

std::vector<uint64_t> runDigests(ChallengeSim& s, const std::vector<InputFrame>& inputs) {
    std::vector<uint64_t> d;
    for (size_t i = 0; i < inputs.size(); ++i) {
        s.step(inputs[i]);
        if ((i + 1) % 60 == 0) d.push_back(simulationStateHash(*s.w));
    }
    return d;
}

}  // namespace

TEST_CASE("challenges: canonical rules and rules_hash match the fixture shared with the server") {
    auto text = FileUtil::readText(Paths::fromUtf8("tests/data/challenge_rules_fixture.json"));
    CHECK(text.has_value());
    Json fx = Json::parse(*text);
    CHECK(fx["cases"].size() >= 3);
    for (const Json& c : fx["cases"]) {
        const Json& in = c["input"];
        std::string err;
        auto r = ChallengeRules::fromJson(in["challenge"].get<std::string>(), in, &err);
        CHECK(r.has_value());
        CHECK(r->canonical() == c["canonical"].get<std::string>());
        CHECK(r->hash() == c["sha256"].get<std::string>());
        // The canonical form read back (replay section) is the same rules.
        auto back = ChallengeRules::fromCanonical(r->canonical());
        CHECK(back.has_value());
        CHECK(*back == *r);
    }
}

TEST_CASE("challenges: rules out of the contract are refused, never clamped") {
    const Json ok = {{"hearts", 2}, {"slots", 3}, {"powers", {"dash"}}, {"stations", true}, {"checkpoints", false}};
    CHECK(ChallengeRules::fromJson("daily-2026-11-16", ok).has_value());
    auto with = [&](const char* key, const Json& v) {
        Json j = ok;
        j[key] = v;
        return ChallengeRules::fromJson("daily-2026-11-16", j).has_value();
    };
    CHECK(!with("hearts", 0));
    CHECK(!with("hearts", 21));
    CHECK(!with("hearts", 2.5));
    CHECK(!with("slots", 0));
    CHECK(!with("slots", 9));
    CHECK(!with("powers", Json::array({"dash", "dash"})));
    CHECK(!with("powers", Json::array({"dash", "glide", "grapple", "wave"})));  // more than the 3 slots
    CHECK(!with("powers", Json::array({"Dash\""})));
    CHECK(!with("stations", 1));
    Json missing = ok;
    missing.erase("checkpoints");
    CHECK(!ChallengeRules::fromJson("daily-2026-11-16", missing).has_value());
    CHECK(!ChallengeRules::fromJson("daily 2026\"", ok).has_value());
    // Only the exact canonical form is read from a replay.
    std::string canon = sampleRules().canonical();
    CHECK(ChallengeRules::fromCanonical(canon).has_value());
    CHECK(!ChallengeRules::fromCanonical("{\"challenge\": \"daily-2026-11-16\",\"hearts\":2,\"slots\":3,\"powers\":[\"dash\",\"glide\",\"grapple\"],\"stations\":false,\"checkpoints\":false}"));
    CHECK(!ChallengeRules::fromCanonical("{\"challenge\":\"daily-2026-11-16\",\"hearts\":2,\"slots\":3,\"powers\":[\"glide\",\"dash\",\"grapple\"],\"stations\":false,\"checkpoints\":false}"));
    CHECK(!ChallengeRules::fromCanonical(canon + " "));
    CHECK(!ChallengeRules::fromCanonical(""));
}

TEST_CASE("challenges: the start state of the run is the challenge's (hearts, slots, imposed powers)") {
    ChallengeRules rules = sampleRules();
    ChallengeSim s("glacior_01", &rules);
    const Player& p = s.w->player();
    CHECK(p.maxHealth() == 2);
    CHECK(p.health == 2);
    CHECK(p.powerSlots() == 3);
    // Granted in canonical order; the level's own start powers are not given.
    CHECK(s.powers() == (std::vector<std::string>{"dash", "glide", "grapple"}));
    CHECK(s.w->challenge() != nullptr);
    // Nothing of it counts (records, progression, achievements, boss statistics…).
    CHECK(s.w->training());
    CHECK(runResultOf(*s.w, true).training);
    // Still the challenge's after the intro is skipped and the level starts.
    for (int i = 0; i < 240; ++i) s.step();
    CHECK(p.maxHealth() == 2);
    CHECK(s.powers() == (std::vector<std::string>{"dash", "glide", "grapple"}));
    // Without a challenge: the level's own start (unchanged behaviour).
    ChallengeSim plain("glacior_01", nullptr);
    CHECK(!plain.w->training());
    CHECK(plain.w->challenge() == nullptr);
    CHECK(plain.powers().size() == plain.w->level().startPowers.size());
}

TEST_CASE("challenges: heart containers give nothing, stations and checkpoints follow the rules") {
    ChallengeRules off = sampleRules(false, false);
    ChallengeSim s("glacior_01", &off);
    for (int i = 0; i < 240; ++i) s.step();
    GameWorld& w = *s.w;
    // A heart container: no extra heart.
    LevelEntity* heart = w.entity("o220");
    CHECK(heart != nullptr);
    heart->onPlayerEnter(w);
    CHECK(w.player().maxHealth() == 2);
    // A power station (glide is carried: the shield station is tried): offers nothing.
    auto* station = dynamic_cast<PowerUnlockEntity*>(w.entity("station_2_2"));
    CHECK(station != nullptr);
    CHECK(station->disabledByChallenge(w));
    station->onPlayerEnter(w);
    CHECK(!w.player().hasPower("shield"));
    CHECK(!w.powerOffer().has_value());
    // A checkpoint: never activates.
    LevelEntity* cp = w.entity("o90");
    CHECK(cp != nullptr);
    cp->onPlayerEnter(w);
    cp->handleAction(w, "activate", Json::object());
    CHECK(w.checkpointId().empty());

    // The same level with stations and checkpoints on.
    ChallengeRules on = sampleRules(true, true);
    ChallengeSim t("glacior_01", &on);
    for (int i = 0; i < 240; ++i) t.step();
    auto* st = dynamic_cast<PowerUnlockEntity*>(t.w->entity("station_2_2"));
    CHECK(st != nullptr);
    CHECK(!st->disabledByChallenge(*t.w));
    st->onPlayerEnter(*t.w);  // every slot is taken: the player is asked what to leave
    CHECK(t.w->powerOffer().has_value());
    t.w->resolvePowerOffer(1);
    CHECK(t.w->player().hasPower("shield"));
    t.w->entity("o90")->onPlayerEnter(*t.w);
    CHECK(t.w->checkpointId() == "o90");
}

TEST_CASE("challenges: without checkpoints a death goes back to the start with the challenge's start state") {
    ChallengeRules rules = sampleRules(true, false);
    ChallengeSim s("glacior_01", &rules);
    for (int i = 0; i < 240; ++i) s.step();
    GameWorld& w = *s.w;
    const Vec2 start = w.respawnPoint();
    // A station taken in exchange of dash, then a wound, then a death.
    auto* station = dynamic_cast<PowerUnlockEntity*>(w.entity("station_2_2"));
    CHECK(station != nullptr);
    station->onPlayerEnter(w);
    CHECK(w.powerOffer().has_value());
    w.resolvePowerOffer(1);  // shield replaces dash; the station now offers dash
    CHECK(w.player().hasPower("shield"));
    CHECK(station->power() == "dash");
    w.player().health = 1;
    w.player().kill(w);
    int guard = 0;
    while ((!w.player().alive() || w.player().respawning()) && guard++ < 1200) s.step();
    CHECK(w.stats().deaths == 1);
    CHECK(w.player().alive());
    CHECK(w.player().health == 2);
    CHECK(s.powers() == (std::vector<std::string>{"dash", "glide", "grapple"}));
    CHECK(std::abs(w.player().feet().x - start.x) < 40.0f);
    // The station offers its own power again.
    auto* again = dynamic_cast<PowerUnlockEntity*>(w.entity("station_2_2"));
    CHECK(again != nullptr);
    CHECK(again->power() == "shield");
}

TEST_CASE("challenges: a challenge run is deterministic and changes nothing for other runs") {
    const auto inputs = playInputs(31, 120 * 30);
    ChallengeRules rules = sampleRules(false, false);
    ChallengeSim a("glacior_01", &rules);
    auto ref = runDigests(a, inputs);
    ChallengeSim b("glacior_01", &rules);
    CHECK(runDigests(b, inputs) == ref);
    // Other rules, another run.
    ChallengeRules more = rules;
    more.hearts = 5;
    ChallengeSim c("glacior_01", &more);
    CHECK(runDigests(c, inputs) != ref);
    // Without a challenge the run is the level's own, as before.
    ChallengeSim d("glacior_01", nullptr);
    CHECK(runDigests(d, inputs) != ref);
}

TEST_CASE("challenges: replay format 4 carries the rules and plays the run again exactly") {
    const auto inputs = playInputs(77, 120 * 25);
    ChallengeRules rules = sampleRules(false, false);
    ChallengeSim live("glacior_01", &rules);
    ReplayRecorder rec;
    rec.begin("glacior_01", std::string(64, 'c'), makeUuidV4(), live.w->gameplaySeed());
    for (const auto& in : inputs) {
        InputFrame f = in;
        if (live.w->cinematic().active() && live.w->cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
        rec.recordInput(f);
        stepWorld(*live.w, f, kSimDt);
        rec.tick(*live.w);
    }
    rec.finish(*live.w, live.w->timer().elapsed());
    const uint64_t liveFinal = simulationStateHash(*live.w);
    ReplayData data = rec.take();
    CHECK(data.challengeRules == rules.canonical());
    auto raw = data.serialize();
    CHECK(raw[4] == ReplayData::kFormatChallenge && raw[5] == 0);
    auto bytes = data.encode();
    std::string err;
    auto back = ReplayData::decode(bytes, &err);
    CHECK(back.has_value());
    CHECK(back->formatVersion == ReplayData::kFormatChallenge);
    CHECK(back->challengeRules == rules.canonical());
    CHECK(back->keysPerSlot);
    CHECK(back->inputs == data.inputs);
    CHECK(back->serialize() == raw);  // written again byte for byte

    // Played again from the file alone: the replay brings its rules to a plain world.
    ChallengeSim view("glacior_01", nullptr);
    ReplaySimulator sim(*back, *view.w);
    CHECK(view.w->challenge() != nullptr);
    CHECK(view.w->challenge()->canonical() == rules.canonical());
    while (sim.step()) view.w->update(1.0f / 50.0f);
    CHECK(sim.finished());
    CHECK(!sim.desynced());
    CHECK(sim.checksPassed() == back->checks.size());
    CHECK(simulationStateHash(*view.w) == liveFinal);

    // Other rules in the section (still canonical): the run is not the same.
    ReplayData other = *back;
    ChallengeRules changed = rules;
    changed.hearts = 9;
    other.challengeRules = changed.canonical();
    ChallengeSim third("glacior_01", nullptr);
    ReplaySimulator bad(other, *third.w);
    while (bad.step()) {
    }
    CHECK(bad.desynced());
}

TEST_CASE("challenges: replay format 4 is only a challenge run, and its section is checked") {
    ReplayData d;
    d.levelId = "x";
    d.levelHash = std::string(64, 'b');
    d.attemptId = makeUuidV4();
    d.frames.push_back({});
    d.keysPerSlot = true;
    d.inputs.assign(120, InputFrame{});
    d.checks.push_back({60, 1});
    // Without rules: version 3, as every run before.
    auto v3 = d.serialize();
    CHECK(v3[4] == 3);
    auto r3 = ReplayData::parse(v3);
    CHECK(r3.has_value());
    CHECK(r3->challengeRules.empty());
    // With rules: version 4, flags 1 | 2, the section last.
    d.challengeRules = sampleRules().canonical();
    auto v4 = d.serialize();
    CHECK(v4[4] == 4);
    CHECK(v4.size() == v3.size() + 2 + d.challengeRules.size());
    auto r4 = ReplayData::parse(v4);
    CHECK(r4.has_value());
    CHECK(r4->challengeRules == d.challengeRules);
    // Truncated, longer, or a section that is not the canonical form: refused.
    auto cut = v4;
    cut.pop_back();
    CHECK(!ReplayData::parse(cut).has_value());
    auto longer = v4;
    longer.push_back('x');
    CHECK(!ReplayData::parse(longer).has_value());
    auto spaced = v4;
    spaced[spaced.size() - d.challengeRules.size() + 1] = ' ';
    CHECK(!ReplayData::parse(spaced).has_value());
    // Version 3 with the challenge flag, or version 4 without it: refused.
    auto flag3 = v3;
    auto flagsAt = v3.size() - (4 + 12) - 4 - 18 - 4 - 2;  // checks, run count, one run, tick count, flags
    CHECK(flag3[flagsAt] == 1);
    flag3[flagsAt] = 3;
    CHECK(!ReplayData::parse(flag3).has_value());
    auto flag4 = v4;
    flag4[flagsAt] = 1;
    CHECK(!ReplayData::parse(flag4).has_value());
}
