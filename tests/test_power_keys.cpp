// Keys of the power slots (2.24): the player binds a key to each slot of the HUD
// (« Pouvoir 1 » … « Pouvoir 8 », Paramètres › Clavier / souris, Manette) and that key uses
// the power equipped there, whatever it is. Passive powers and powers used with a move of
// the character (glider, triple jump, ground pound, charged shot, grapple) do not use the
// key of their slot. Replays recorded before (format 2, one button per power) still load and
// play exactly as before.

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/Replay.h"
#include "game/powers/Powers.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

struct World {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    World() {
        content.reload();
        res.init(nullptr, &audio);
    }
    GameWorld& make(const LevelData& level, bool keysPerSlot) {
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->silent = true;
        w->setKeysPerSlot(keysPerSlot);
        w->load(level);
        w->seedGameplay(levelSeed(level.id));
        w->startAt({100, 800});
        return *w;
    }
};

LevelData flat(std::vector<std::string> powers, int maxPowers = 4) {
    LevelData l = LevelData::makeEmpty("test_power_keys", "Test", "sylvanox");
    l.intro.steps.clear();
    l.startPowers = std::move(powers);
    l.maxPowers = maxPowers;
    return l;
}

void addObject(LevelData& l, const std::string& id, const std::string& type, Rect r, Json props) {
    LevelObject o;
    o.id = id;
    o.type = type;
    o.layer = "gameplay";
    o.rect = r;
    o.props = std::move(props);
    l.objects.push_back(o);
}

uint32_t bit(Action a) { return 1u << static_cast<int>(a); }

InputFrame press(Action a) {
    InputFrame f;
    f.held = f.pressed = bit(a);
    return f;
}

InputFrame walk(int dir) {
    InputFrame f;
    f.held = bit(dir > 0 ? Action::MoveRight : Action::MoveLeft);
    f.moveX = static_cast<int8_t>(dir > 0 ? 127 : -127);
    return f;
}

void idle(GameWorld& w, int n) {
    for (int i = 0; i < n; ++i) stepWorld(w, InputFrame{}, kSimDt);
}

template <typename T>
T* powerOf(GameWorld& w) {
    for (const auto& p : w.player().powers())
        if (auto* t = dynamic_cast<T*>(p.get())) return t;
    return nullptr;
}

/// The scripted play of the determinism tests (runs right, jumps, attacks, dashes with the
/// old Dash button).
std::vector<InputFrame> scripted(uint64_t seed, int ticks) {
    Random r(seed);
    std::vector<InputFrame> out;
    InputFrame prev, cur;
    int hold = 0;
    for (int i = 0; i < ticks; ++i) {
        if (hold-- <= 0) {
            cur = InputFrame{};
            float k = r.unit();
            int dir = k < 0.75f ? 1 : (k < 0.88f ? -1 : 0);
            if (dir > 0) cur.held |= bit(Action::MoveRight);
            if (dir < 0) cur.held |= bit(Action::MoveLeft);
            cur.moveX = static_cast<int8_t>(dir * 127);
            if (r.chance(0.45f)) cur.held |= bit(Action::Jump);
            if (r.chance(0.2f)) cur.held |= bit(Action::Attack);
            if (r.chance(0.08f)) cur.held |= bit(Action::Dash);
            if (r.chance(0.05f)) {
                cur.held |= bit(Action::MoveDown);
                cur.moveY = 127;
            }
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

/// What the game does when it samples the keys since 2.24: the key of the slot where a
/// power sits instead of the power's own button (here a frame with the old buttons is
/// converted, with the powers carried at this step).
InputFrame toSlotKeys(const GameWorld& w, const InputFrame& f) {
    InputFrame out = f;
    for (int i = 0; i < InputFrame::kActions; ++i)
        if (actionIsLegacyPowerKey(static_cast<Action>(i))) {
            out.held &= ~(1u << i);
            out.pressed &= ~(1u << i);
            out.released &= ~(1u << i);
        }
    const auto& powers = w.player().powers();
    for (size_t s = 0; s < powers.size() && s < static_cast<size_t>(kPowerSlotKeys); ++s) {
        if (powers[s]->trigger() != PowerTrigger::SlotKey) continue;
        int from = static_cast<int>(powers[s]->def().input);
        int to = static_cast<int>(powerSlotAction(static_cast<int>(s)));
        if ((f.held >> from) & 1u) out.held |= 1u << to;
        if ((f.pressed >> from) & 1u) out.pressed |= 1u << to;
        if ((f.released >> from) & 1u) out.released |= 1u << to;
    }
    return out;
}

/// The same file in format 2 (what the game wrote until 2.23): actions on 16 bits, 12-byte
/// runs, a reserved field instead of the input flags.
std::vector<uint8_t> asFormat2(const ReplayData& d) {
    std::vector<uint8_t> v3 = d.serialize();
    size_t at = 68;
    at += 1 + v3[at];  // level id
    at += 1 + v3[at];  // game version
    at += d.frames.size() * 12 + d.events.size() * 8;
    std::vector<uint8_t> out(v3.begin(), v3.begin() + static_cast<std::ptrdiff_t>(at));
    out[4] = 2;
    out[5] = 0;
    const uint8_t* in = v3.data() + at;
    out.insert(out.end(), in, in + 20);
    out[at + 10] = out[at + 11] = 0;  // reserved
    uint32_t runs = static_cast<uint32_t>(in[16] | (in[17] << 8) | (in[18] << 16) | (in[19] << 24));
    in += 20;
    for (uint32_t r = 0; r < runs; ++r, in += 18) {
        out.insert(out.end(), in, in + 2);       // repeat
        out.insert(out.end(), in + 2, in + 4);   // held, low 16 bits
        out.insert(out.end(), in + 6, in + 8);   // pressed
        out.insert(out.end(), in + 10, in + 12); // released
        out.insert(out.end(), in + 14, in + 18); // move x, y, flags, choice
    }
    out.insert(out.end(), in, static_cast<const uint8_t*>(v3.data() + v3.size()));  // digests
    return out;
}

}  // namespace

TEST_CASE("power keys: every power is classified (slot key, move of the character, passive)") {
    ContentDatabase content;
    content.reload();
    const std::set<std::string> slot = {"dash",      "wave",       "shield", "impulse", "teleport", "time_slow",
                                        "plasma_bomb", "boomerang", "frost_nova", "decoy", "recall"};
    const std::set<std::string> moves = {"glide", "triple_jump", "ground_pound", "charge_shot", "grapple"};
    const std::set<std::string> passive = {"magnet", "air_dash"};
    int seen = 0;
    for (const PowerDefinition* d : content.powers()) {
        PowerTriggerInfo t = powerTriggerInfo(*d);
        ++seen;
        if (slot.count(d->id)) {
            CHECK(t.kind == PowerTrigger::SlotKey);
            CHECK(t.keys.empty());
        } else if (moves.count(d->id)) {
            CHECK(t.kind == PowerTrigger::StandardKeys);
            CHECK(!t.keys.empty());
            CHECK(!t.how.empty());
            for (Action a : t.keys) CHECK(actionIsRebindable(a) && powerSlotOf(a) < 0);  // a move, never a slot
        } else {
            if (!passive.count(d->id)) std::printf("  pouvoir non classé : %s\n", d->id.c_str());
            CHECK(passive.count(d->id) == 1);
            CHECK(t.kind == PowerTrigger::Passive);
        }
    }
    CHECK(seen == static_cast<int>(slot.size() + moves.size() + passive.size()));
    auto keysOf = [&](const char* id) { return powerTriggerInfo(*content.power(id)).keys; };
    CHECK(keysOf("ground_pound") == std::vector<Action>({Action::MoveDown, Action::Attack}));
    CHECK(keysOf("glide") == std::vector<Action>({Action::Jump}));
    CHECK(keysOf("triple_jump") == std::vector<Action>({Action::Jump}));
    CHECK(keysOf("charge_shot") == std::vector<Action>({Action::Attack}));
    CHECK(keysOf("grapple") == std::vector<Action>({Action::Interact}));
}

TEST_CASE("power keys: the key of slot N uses the power equipped in slot N") {
    World env;
    GameWorld& w = env.make(flat({"wave", "dash", "shield"}), true);
    CHECK(w.player().powers()[0]->key() == Action::PowerSlot1);
    CHECK(w.player().powers()[1]->key() == Action::PowerSlot2);
    idle(w, 30);
    // Slot 2: the dash.
    stepWorld(w, press(powerSlotAction(1)), kSimDt);
    CHECK(w.player().dashing());
    idle(w, 60);
    // Slot 1: the wave (a projectile of the player).
    size_t before = w.projectiles().size();
    stepWorld(w, press(powerSlotAction(0)), kSimDt);
    CHECK(w.projectiles().size() == before + 1);
    CHECK(!w.player().dashing());
    idle(w, 60);
    // Slot 3: the shield.
    stepWorld(w, press(powerSlotAction(2)), kSimDt);
    CHECK(w.player().shieldActive());
    idle(w, 700);
    CHECK(!w.player().shieldActive());
    // The old buttons of the powers do nothing in a run with keys per slot.
    stepWorld(w, press(Action::Shield), kSimDt);
    stepWorld(w, press(Action::Dash), kSimDt);
    CHECK(!w.player().shieldActive());
    CHECK(!w.player().dashing());
    // An empty slot does nothing.
    float x = w.player().box.x;
    stepWorld(w, press(powerSlotAction(3)), kSimDt);
    CHECK(w.player().box.x == x);
    // Replacing a power gives the new one the key of that slot.
    w.player().replacePower(1, *env.content.power("teleport"));
    CHECK(w.player().powers()[1]->id() == "teleport");
    CHECK(w.player().powers()[1]->key() == Action::PowerSlot2);
    // Removing one moves the next ones down a slot, with that slot's key.
    CHECK(w.player().removePower("wave"));
    CHECK(w.player().powers()[0]->key() == Action::PowerSlot1);
    CHECK(w.player().powers()[1]->id() == "shield");
    CHECK(w.player().powers()[1]->key() == Action::PowerSlot2);
}

TEST_CASE("power keys: with the old rules each power keeps its own button") {
    World env;
    GameWorld& w = env.make(flat({"wave", "dash"}), false);
    CHECK(w.player().powers()[1]->key() == Action::Dash);
    idle(w, 30);
    stepWorld(w, press(powerSlotAction(1)), kSimDt);  // no slot keys in an old run
    CHECK(!w.player().dashing());
    stepWorld(w, press(Action::Dash), kSimDt);
    CHECK(w.player().dashing());
}

TEST_CASE("power keys: two powers of the old special button are carried together, each on its slot") {
    World env;
    // With one button per power the frost nova replaced the boomerang (same button).
    {
        GameWorld& legacy = env.make(flat({"boomerang", "frost_nova"}), false);
        CHECK(legacy.player().powers().size() == 1);
        CHECK(legacy.player().hasPower("frost_nova"));
    }
    GameWorld& w = env.make(flat({"boomerang", "frost_nova"}), true);
    CHECK(w.player().powers().size() == 2);
    idle(w, 30);
    stepWorld(w, press(powerSlotAction(0)), kSimDt);
    CHECK(powerOf<BoomerangPower>(w) && powerOf<BoomerangPower>(w)->flying());
    CHECK(powerOf<FrostNovaPower>(w) && powerOf<FrostNovaPower>(w)->cooldownRemaining() == 0.0f);
    stepWorld(w, InputFrame{}, kSimDt);
    stepWorld(w, press(powerSlotAction(1)), kSimDt);
    CHECK(powerOf<FrostNovaPower>(w)->cooldownRemaining() > 0.0f);
    // A tablet of a third one: no « same button » any more, a free slot takes it ...
    LevelData l = flat({"boomerang", "frost_nova"});
    addObject(l, "tablet", "power_unlock", {260, 776, 24, 24}, {{"power", "decoy"}});
    GameWorld& t = env.make(l, true);
    for (int i = 0; i < 300 && !t.player().hasPower("decoy") && !t.powerOffer(); ++i) stepWorld(t, walk(+1), kSimDt);
    CHECK(!t.powerOffer());
    CHECK(t.player().powers().size() == 3);
    // ... while the rules of the old replays still ask to replace the boomerang.
    LevelData l2 = flat({"boomerang"});
    addObject(l2, "tablet", "power_unlock", {260, 776, 24, 24}, {{"power", "decoy"}});
    GameWorld& old = env.make(l2, false);
    for (int i = 0; i < 300 && !old.powerOffer(); ++i) stepWorld(old, walk(+1), kSimDt);
    CHECK(old.powerOffer().has_value());
    if (old.powerOffer()) CHECK(old.powerOffer()->conflictSlot == 0);
}

TEST_CASE("power keys: the rule set after loading, before the start, grants the start powers again") {
    World env;
    LevelData l = flat({"boomerang", "frost_nova"});
    WorldServices s{env.res, env.audio, env.content, nullptr, 0.0f, 1.0f};
    GameWorld w(s, WorldMode::Play);
    w.load(l);  // old rules: the frost nova replaced the boomerang
    CHECK(w.player().powers().size() == 1);
    w.setKeysPerSlot(true);
    CHECK(w.player().powers().size() == 2);
    CHECK(w.player().powers()[1]->key() == Action::PowerSlot2);
}

TEST_CASE("power keys: a passive power or a power on a move ignores the key of its slot") {
    // Glider, magnet, triple jump, charged shot in slots 1 to 4: their keys pressed or not,
    // the same run.
    std::vector<uint64_t> digests[2];
    for (int run = 0; run < 2; ++run) {
        World env;
        GameWorld& w = env.make(flat({"glide", "magnet", "triple_jump", "charge_shot"}), true);
        Random rng(0x224);
        InputFrame prev;
        for (int i = 0; i < 1200; ++i) {
            InputFrame f;
            int dir = (i / 120) % 3 == 2 ? -1 : 1;
            f.held = bit(dir > 0 ? Action::MoveRight : Action::MoveLeft);
            f.moveX = static_cast<int8_t>(dir * 127);
            if (rng.chance(0.05f)) f.held |= bit(Action::Jump);
            if (rng.chance(0.02f)) f.held |= bit(Action::Attack);
            bool slots = rng.chance(0.2f);
            if (run == 1 && slots)
                for (int s = 0; s < 4; ++s) f.held |= bit(powerSlotAction(s));
            f.pressed = f.held & ~prev.held;
            f.released = prev.held & ~f.held;
            prev = f;
            stepWorld(w, f, kSimDt);
            if (i % 60 == 59) digests[run].push_back(simulationStateHash(w));
        }
        for (const char* id : {"glide", "magnet", "triple_jump", "charge_shot"}) CHECK(w.stats().powersUsed.count(id) == 0);
    }
    CHECK(digests[0] == digests[1]);
}

TEST_CASE("power keys: keys per slot play a run exactly as the buttons of the powers did") {
    // The scripted play of the golden digests, once with the old Dash button (old rules) and
    // once with the key of the dash's slot (new rules): the same run, step for step.
    const auto inputs = scripted(7, 120 * 40);
    std::vector<uint64_t> digests[2];
    int dashes[2] = {0, 0};
    for (int mode = 0; mode < 2; ++mode) {
        World env;
        auto level = env.content.loadLevel("sylvanox_01");
        CHECK(level.has_value());
        if (!level) return;
        WorldServices s{env.res, env.audio, env.content, nullptr, 0.0f, 1.0f};
        GameWorld w(s, WorldMode::Play);
        w.setKeysPerSlot(mode == 1);
        w.load(*level);
        w.seedGameplay(levelSeed("sylvanox_01"));
        w.autoPowerChoice = InputFrame::kChoiceLeave;
        w.startAt({2990.0f, 800.0f});  // just before the tablet of the dash
        bool wasDashing = false;
        for (size_t i = 0; i < inputs.size(); ++i) {
            InputFrame f = mode == 1 ? toSlotKeys(w, inputs[i]) : inputs[i];
            if (w.cinematic().active() && w.cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
            stepWorld(w, f, kSimDt);
            if (w.player().dashing() && !wasDashing) ++dashes[mode];
            wasDashing = w.player().dashing();
            if ((i + 1) % 60 == 0) digests[mode].push_back(simulationStateHash(w));
        }
    }
    CHECK(dashes[0] > 0);  // the run really uses the power
    CHECK(dashes[0] == dashes[1]);
    CHECK(digests[0] == digests[1]);
}

TEST_CASE("power keys: the slot keys survive the frame and the replay file (format 3)") {
    PlayerInput in;
    in.held[static_cast<int>(Action::PowerSlot8)] = in.pressed[static_cast<int>(Action::PowerSlot8)] = true;
    in.released[static_cast<int>(Action::PowerSlot1)] = true;
    in.held[static_cast<int>(Action::Pause)] = true;  // never reaches the simulation
    InputFrame f = InputFrame::fromPlayerInput(in);
    CHECK(f.held == bit(Action::PowerSlot8));
    CHECK(f.released == bit(Action::PowerSlot1));
    PlayerInput back = f.toPlayerInput();
    CHECK(back.isPressed(Action::PowerSlot8) && back.isReleased(Action::PowerSlot1) && !back.isHeld(Action::Pause));

    ReplayData d;
    d.levelId = "x";
    d.levelHash = std::string(64, 'c');
    d.attemptId = makeUuidV4();
    d.frames.push_back({});
    d.keysPerSlot = true;
    d.inputs.assign(200, InputFrame{});
    d.inputs[10] = f;
    d.inputs[11].held = bit(Action::PowerSlot4) | bit(Action::Jump);
    d.checks.push_back({60, 7});
    std::string err;
    auto parsed = ReplayData::parse(d.serialize(), &err);
    CHECK(parsed.has_value());
    if (!parsed) return;
    CHECK(parsed->formatVersion == 3);
    CHECK(parsed->keysPerSlot);
    CHECK(parsed->inputs == d.inputs);
    // A file without the flag: the old rules (a run of a world with one button per power).
    d.keysPerSlot = false;
    CHECK(!ReplayData::parse(d.serialize())->keysPerSlot);
    // Unknown input flags are refused.
    auto raw = d.serialize();
    size_t at = 68 + 1 + raw[68];
    at += 1 + raw[at] + d.frames.size() * 12 + d.events.size() * 8;
    raw[at + 10] = 0x04;
    CHECK(!ReplayData::parse(raw).has_value());
}

TEST_CASE("power keys: an old replay (format 2, one button per power) loads and plays exactly as before") {
    // A run recorded with the rules before 2.24, in the file format of the time.
    const auto inputs = scripted(99, 120 * 30);
    World live;
    auto level = live.content.loadLevel("sylvanox_01");
    CHECK(level.has_value());
    if (!level) return;
    WorldServices s{live.res, live.audio, live.content, nullptr, 0.0f, 1.0f};
    GameWorld w(s, WorldMode::Play);
    w.load(*level);  // old rules: the default of a world
    w.seedGameplay(levelSeed("sylvanox_01"));
    ReplayRecorder rec;
    rec.begin("sylvanox_01", std::string(64, 'a'), makeUuidV4(), w.gameplaySeed());
    for (const auto& in : inputs) {
        InputFrame f = in;
        if (w.cinematic().active() && w.cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
        rec.recordInput(f);
        stepWorld(w, f, kSimDt);
        rec.tick(w);
    }
    rec.finish(w, w.timer().elapsed());
    const uint64_t liveFinal = simulationStateHash(w);
    ReplayData data = rec.take();
    CHECK(!data.keysPerSlot);
    std::string err;
    auto old = ReplayData::parse(asFormat2(data), &err);
    if (!old) std::printf("  format 2 : %s\n", err.c_str());
    CHECK(old.has_value());
    if (!old) return;
    CHECK(old->formatVersion == 2);
    CHECK(!old->keysPerSlot);
    CHECK(old->inputs == data.inputs);
    CHECK(old->checks.size() == data.checks.size());
    // Played back by today's viewer (ReplaySimulator gives the world the replay's rules,
    // even a world that would have the new ones).
    World view;
    GameWorld v(WorldServices{view.res, view.audio, view.content, nullptr, 0.0f, 1.0f}, WorldMode::Play);
    v.setKeysPerSlot(true);
    v.load(*level);
    ReplaySimulator sim(*old, v);
    CHECK(!v.keysPerSlot());
    while (sim.step()) {
    }
    CHECK(sim.finished());
    CHECK(!sim.desynced());
    CHECK(sim.checksPassed() == old->checks.size());
    CHECK(simulationStateHash(v) == liveFinal);
}

TEST_CASE("power keys: a run with slot keys plays back identically from its replay") {
    // Two powers of the old special button and a dash, used with their slot keys.
    LevelData l = flat({"boomerang", "frost_nova", "dash"}, 4);
    l.id = "test_power_keys_replay";
    addObject(l, "e1", "enemy", {320, 788, 18, 12}, {{"enemy", "glowmite"}, {"patrol", 40}});
    addObject(l, "e2", "enemy", {460, 776, 16, 24}, {{"enemy", "bulwark"}, {"patrol", 30}});
    World live;
    GameWorld& w = live.make(l, true);
    ReplayRecorder rec;
    rec.begin(l.id, std::string(64, 'd'), makeUuidV4(), w.gameplaySeed());
    Random rng(0x2240);
    InputFrame prev;
    for (int i = 0; i < 120 * 20; ++i) {
        InputFrame f;
        int dir = (i / 150) % 3 == 2 ? -1 : 1;
        f.held = bit(dir > 0 ? Action::MoveRight : Action::MoveLeft);
        f.moveX = static_cast<int8_t>(dir * 127);
        if (rng.chance(0.04f)) f.held |= bit(Action::Jump);
        for (int s = 0; s < 3; ++s)
            if (rng.chance(0.02f)) f.held |= bit(powerSlotAction(s));
        f.pressed = f.held & ~prev.held;
        f.released = prev.held & ~f.held;
        prev = f;
        rec.recordInput(f);
        stepWorld(w, f, kSimDt);
        rec.tick(w);
    }
    rec.finishAttempt(w, w.timer().elapsed());
    for (const char* id : {"boomerang", "frost_nova", "dash"}) CHECK(w.stats().powersUsed.count(id) == 1);
    const uint64_t liveFinal = simulationStateHash(w);
    ReplayData data = rec.take();
    CHECK(data.keysPerSlot);
    std::string err;
    auto back = ReplayData::decode(data.encode(), &err);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->formatVersion == ReplayData::kFormatVersion);
    CHECK(back->keysPerSlot);
    CHECK(back->inputs == data.inputs);
    // The viewer's world starts with the old rules: the replay gives it its own.
    World view;
    WorldServices s{view.res, view.audio, view.content, nullptr, 0.0f, 1.0f};
    GameWorld v(s, WorldMode::Play);
    v.load(l);
    v.startAt({100, 800});
    ReplaySimulator sim(*back, v);
    CHECK(v.keysPerSlot());
    CHECK(v.player().powers().size() == 3);  // the start powers granted again under the new rules
    while (sim.step()) {
    }
    CHECK(!sim.desynced());
    CHECK(sim.checksPassed() == back->checks.size());
    CHECK(simulationStateHash(v) == liveFinal);
}

TEST_CASE("power keys: settings saved before 2.24 get slot keys from the buttons of the powers") {
    // Defaults: the six power buttons of before, in their order, on slots 1 to 6.
    InputBindings d = InputBindings::defaults();
    CHECK(d.keys[static_cast<int>(Action::PowerSlot1)][0] == SDL_SCANCODE_K);
    CHECK(d.keys[static_cast<int>(Action::PowerSlot2)][0] == SDL_SCANCODE_L);
    CHECK(d.keys[static_cast<int>(Action::PowerSlot6)][0] == SDL_SCANCODE_F);
    CHECK(d.pad[static_cast<int>(Action::PowerSlot1)][0] == PadBinding::button(SDL_GAMEPAD_BUTTON_EAST));
    CHECK(d.pad[static_cast<int>(Action::PowerSlot2)][0] == PadBinding::axisPos(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    // No default key used twice by gameplay actions; the old power buttons have none.
    std::set<int> keys;
    std::vector<PadBinding> pads;
    for (int a = 0; a < kFirstMenuAction; ++a) {
        if (actionIsLegacyPowerKey(static_cast<Action>(a))) {
            CHECK(d.keys[a][0] == SDL_SCANCODE_UNKNOWN && d.keys[a][1] == SDL_SCANCODE_UNKNOWN);
            CHECK(d.pad[a][0].kind == PadBinding::Kind::None);
            continue;
        }
        for (auto sc : d.keys[a])
            if (sc != SDL_SCANCODE_UNKNOWN) {
                CHECK(keys.count(sc) == 0);
                keys.insert(sc);
            }
        for (const auto& pb : d.pad[a])
            if (pb.kind != PadBinding::Kind::None) {
                for (const auto& other : pads) CHECK(!(other == pb));
                pads.push_back(pb);
            }
    }
    // A file of 2.23: Dash rebound on Q, Onde on R, the gamepad's Dash on Y (north) after
    // moving Interact to the right stick; the power buttons become the slot keys.
    Json old = Json::parse(R"({
        "keyboard": {"jump": ["Space", "Z"], "attack": ["J", "X"], "dash": ["Q", ""], "wave": ["R", "C"],
                     "shield": ["O", "V"], "impulse": ["I", "B"], "teleport": ["U", "N"], "special": ["F", "H"],
                     "interact": ["E", "Return"]},
        "gamepad": {"interact": [{"button": "rightstick"}, null], "dash": [{"button": "y"}, null],
                    "special": [{"button": "leftshoulder"}, null], "shield": [null, null]}
    })");
    InputBindings b = InputBindings::fromJson(old);
    CHECK(b.keys[static_cast<int>(Action::PowerSlot1)][0] == SDL_SCANCODE_Q);
    CHECK(b.keys[static_cast<int>(Action::PowerSlot1)][1] == SDL_SCANCODE_UNKNOWN);
    CHECK(b.keys[static_cast<int>(Action::PowerSlot2)][0] == SDL_SCANCODE_R);
    // One key per command (2.29): the second key of before is dropped.
    CHECK(b.keys[static_cast<int>(Action::PowerSlot6)][0] == SDL_SCANCODE_F);
    CHECK(b.keys[static_cast<int>(Action::PowerSlot6)][1] == SDL_SCANCODE_UNKNOWN);
    CHECK(b.keys[static_cast<int>(Action::Dash)][0] == SDL_SCANCODE_UNKNOWN);  // bound to nothing now
    CHECK(b.pad[static_cast<int>(Action::PowerSlot1)][0] == PadBinding::button(SDL_GAMEPAD_BUTTON_NORTH));
    CHECK(b.pad[static_cast<int>(Action::Interact)][0] == PadBinding::button(SDL_GAMEPAD_BUTTON_RIGHT_STICK));
    // The old special (left shoulder) goes to slot 6; slot 3 (old shield) was unbound.
    CHECK(b.pad[static_cast<int>(Action::PowerSlot6)][0] == PadBinding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    CHECK(b.pad[static_cast<int>(Action::PowerSlot3)][0].kind == PadBinding::Kind::None);
    // A default slot key the player gave to another action stays with that action.
    Json clash = Json::parse(R"({"keyboard": {"attack": ["K", ""]}, "gamepad": {}})");
    InputBindings c = InputBindings::fromJson(clash);
    CHECK(c.keys[static_cast<int>(Action::Attack)][0] == SDL_SCANCODE_K);
    // One key per command (2.29): its only key taken, the slot waits for the player's choice.
    CHECK(c.keys[static_cast<int>(Action::PowerSlot1)][0] == SDL_SCANCODE_UNKNOWN);
    CHECK(c.keys[static_cast<int>(Action::PowerSlot1)][1] == SDL_SCANCODE_UNKNOWN);
    // Saved again: the slots are written, the old buttons are not; read back the same.
    Json saved = b.toJson();
    CHECK(saved["keyboard"].contains("power_slot_1"));
    CHECK(!saved["keyboard"].contains("dash"));
    InputBindings again = InputBindings::fromJson(saved);
    for (int a = 0; a < kActionCount; ++a) {
        CHECK(again.keys[a] == b.keys[a]);
        CHECK(again.pad[a] == b.pad[a]);
    }
    // Once there are slot keys, an old button left in the file is ignored.
    saved["keyboard"]["dash"] = Json::array({"M", ""});
    CHECK(InputBindings::fromJson(saved).keys[static_cast<int>(Action::PowerSlot1)][0] == SDL_SCANCODE_Q);
}

TEST_CASE("power keys: a key pressed gives the slot's action, never the old power button") {
    InputManager in;
    in.setBindings(InputBindings::defaults());
    in.beginFrame();
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = SDL_SCANCODE_K;  // « Pouvoir 1 » by default (the Dash key before 2.24)
    in.handleEvent(e);
    PlayerInput p = in.consumePlayerInput();
    CHECK(p.isPressed(Action::PowerSlot1));
    CHECK(!p.isPressed(Action::Dash));
    InputFrame f = InputFrame::fromPlayerInput(p);
    CHECK(f.pressed == bit(Action::PowerSlot1));
}
