// Keys of the powers per level (2.31): each power used with a key of its own keeps the same key
// wherever it sits in the HUD. The level chooses it (editor, `powerKeys`), else the power's
// suggestion; each player may change it for a level (saves/power_keys.json); the base commands
// always win. Nothing of it reaches the simulation, the replays or the fingerprints.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/input/InputManager.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "game/powers/Power.h"
#include "game/powers/PowerKeys.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

int at(Action a) { return static_cast<int>(a); }

LevelData levelWith(std::vector<std::string> start, std::vector<std::string> tablets = {}) {
    LevelData l = LevelData::makeEmpty("test_level_power_keys", "Test", "sylvanox");
    l.intro.steps.clear();
    l.startPowers = std::move(start);
    l.maxPowers = 8;
    int n = 0;
    for (const auto& p : tablets) {
        LevelObject o;
        o.id = "tab" + std::to_string(++n);
        o.type = "power_unlock";
        o.layer = "gameplay";
        o.rect = {400.0f + 60.0f * n, 760, 24, 24};
        o.props = {{"power", p}};
        l.objects.push_back(o);
    }
    return l;
}

SDL_Event mouseButton(int button, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = static_cast<Uint8>(button);
    e.button.down = down;
    return e;
}

SDL_Event keyDown(SDL_Scancode sc) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = sc;
    e.key.down = true;
    return e;
}

/// One frame of the application: events, then the sampling of the devices.
PlayerInput frame(InputManager& in, std::vector<SDL_Event> events = {}) {
    in.beginFrame();
    for (const auto& e : events) in.handleEvent(e);
    in.update(1.0f / 60.0f);
    return in.consumePlayerInput();
}

}  // namespace

TEST_CASE("level power keys: the fingerprints of a level never change with its power keys") {
    ContentDatabase content;
    content.reload();
    LevelData l = levelWith({"dash", "wave"}, {"shield"});
    Json before = l.toJson();
    auto fp0 = fingerprintLevelJson(before);
    auto rk0 = fingerprintRankedLevel(content, before);
    std::string gp0 = fingerprintGameplay(content, l);
    CHECK(fp0 && rk0 && !gp0.empty());
    CHECK(!before.contains("powerKeys"));  // a level without keys is written as before
    // Keys chosen, then changed: the same prints.
    for (const char* key : {"C", "Mouse4"}) {
        l.powerKeys = Json::object();
        l.powerKeys["dash"] = {{"key", key}, {"pad", {{"button", "y"}}}};
        l.powerKeys["shield"] = {{"key", "V"}};
        Json after = l.toJson();
        CHECK(after.contains("powerKeys"));
        CHECK(fingerprintLevelJson(after)->hash == fp0->hash);
        CHECK(fingerprintRankedLevel(content, after)->hash == rk0->hash);
        CHECK(fingerprintGameplay(content, l) == gp0);
        // Read back: the same keys, the same prints.
        auto back = LevelData::fromJson(after);
        CHECK(back.has_value());
        CHECK(back->powerKeys == l.powerKeys);
        CHECK(fingerprintGameplay(content, *back) == gp0);
    }
    // Every campaign level: its file with or without its keys has the same ranked print.
    int withKeys = 0;
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto text = FileUtil::readText(e->path);
        CHECK(text.has_value());
        Json j = Json::parse(*text);
        Json bare = j;
        bare.erase("powerKeys");
        withKeys += j.contains("powerKeys") ? 1 : 0;
        CHECK(fingerprintRankedLevel(content, j)->hash == fingerprintRankedLevel(content, bare)->hash);
        CHECK(fingerprintLevelJson(j)->hash == fingerprintLevelJson(bare)->hash);
    }
    CHECK(withKeys > 0);
}

TEST_CASE("level power keys: suggestions never collide in a campaign level nor take a command's key") {
    ContentDatabase content;
    content.reload();
    const InputBindings d = InputBindings::defaults();
    std::set<int> keys;
    for (const PowerDefinition* p : content.powers()) {
        if (!powerUsesKey(*p)) {
            CHECK(p->suggestedKey == SDL_SCANCODE_UNKNOWN);  // no key for a power without one
            continue;
        }
        CHECK(p->suggestedKey != SDL_SCANCODE_UNKNOWN);
        CHECK(p->suggestedPad.kind != PadBinding::Kind::None);
        CHECK(baseCommandOfKey(d, p->suggestedKey).empty());
        CHECK(baseCommandOfPad(d, p->suggestedPad).empty());
        CHECK(keys.count(p->suggestedKey) == 0);  // on the keyboard, every power has its own key
        keys.insert(p->suggestedKey);
        // Saved and read back the same (files of packages).
        PowerDefinition again = PowerDefinition::fromJson(p->toFileJson());
        CHECK(again.suggestedKey == p->suggestedKey);
        CHECK(again.suggestedPad == p->suggestedPad);
        // Never part of what the power plays like (fingerprints).
        CHECK(!p->toJson().contains("suggestedKey"));
    }
    CHECK(keys.size() == 12);
    // The powers of each campaign level: their suggestions never collide, on either device.
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(e->id);
        CHECK(level.has_value());
        std::vector<std::string> powers = levelKeyPowers(*level, content);
        // 2.50: a giga boss arena offers every power: more than a gamepad's free inputs, so
        // their buttons are shared (two carried powers never share one: see below).
        const bool shared = padsShared(powers.size());
        for (size_t a = 0; a < powers.size(); ++a)
            for (size_t b = a + 1; b < powers.size(); ++b) {
                const PowerDefinition* pa = content.power(powers[a]);
                const PowerDefinition* pb = content.power(powers[b]);
                if (!shared && pa->suggestedPad == pb->suggestedPad) std::printf("  %s : %s / %s\n", e->id.c_str(), pa->id.c_str(), pb->id.c_str());
                CHECK(shared || !(pa->suggestedPad == pb->suggestedPad));
                CHECK(pa->suggestedKey != pb->suggestedKey);
            }
    }
}

TEST_CASE("level power keys: the player's choice, else the level's, else the suggestion") {
    ContentDatabase content;
    content.reload();
    LevelData l = levelWith({"dash", "wave"}, {"shield", "glide"});
    CHECK(levelKeyPowers(l, content) == std::vector<std::string>({"dash", "wave", "shield"}));  // the glider has no key
    l.powerKeys["dash"] = {{"key", "C"}, {"pad", {{"button", "y"}}}};
    l.powerKeys["dash"]["pad"] = {{"axis", "righttrigger"}, {"dir", 1}};
    PowerKeyOverrides mine;
    mine.setKey(l.id, "wave", SDL_SCANCODE_V);
    const InputBindings base = InputBindings::defaults();
    PowerKeyMap m = levelPowerKeys(l, content, base, &mine);
    CHECK(m.powers.size() == 3);
    const ResolvedPowerKey* dash = m.find("dash");
    CHECK(dash->key == SDL_SCANCODE_C);
    CHECK(dash->keySource == PowerKeySource::Level);
    CHECK(dash->pad == PadBinding::axisPos(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    const ResolvedPowerKey* wave = m.find("wave");
    CHECK(wave->key == SDL_SCANCODE_V);
    CHECK(wave->keySource == PowerKeySource::Player);
    CHECK(wave->levelKey == SDL_SCANCODE_L);  // « Par défaut » gives the suggestion back
    CHECK(wave->levelKeySource == PowerKeySource::Suggestion);
    // The wave's suggested button is the right trigger, now the dash's: the level's choice keeps
    // it, the wave gets a free button.
    CHECK(wave->wantedPad == PadBinding::axisPos(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    CHECK(wave->padPower == "dash");
    CHECK(wave->pad.kind != PadBinding::Kind::None);
    CHECK(!(wave->pad == dash->pad));
    const ResolvedPowerKey* shield = m.find("shield");
    CHECK(shield->key == SDL_SCANCODE_O);
    CHECK(shield->keySource == PowerKeySource::Suggestion);
    CHECK(shield->pad == PadBinding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    // « Par défaut »: the level's key again.
    mine.reset(l.id, "wave");
    CHECK(!mine.has(l.id, "wave"));
    CHECK(levelPowerKeys(l, content, base, &mine).find("wave")->key == SDL_SCANCODE_L);
    // One power alone (trainings, the power picker): the same answer as in the whole map.
    CHECK(levelPowerKey("shield", l, content, base, &mine).key == SDL_SCANCODE_O);
    ResolvedPowerKey boomerang = levelPowerKey("boomerang", l, content, base, &mine);  // not in the level: added after
    CHECK(boomerang.key == SDL_SCANCODE_P);
    CHECK(boomerang.padPower == "shield");  // its suggested button is the shield's here
    CHECK(levelPowerKey("glide", l, content, base, &mine).key == SDL_SCANCODE_UNKNOWN);  // no key of its own
}

TEST_CASE("level power keys: a base command keeps its key, the power then has none") {
    ContentDatabase content;
    content.reload();
    LevelData l = levelWith({"dash", "wave", "shield"});
    l.powerKeys["shield"] = {{"key", "Space"}, {"pad", {{"button", "a"}}}};  // jump's key and button
    InputBindings base = InputBindings::defaults();
    base.keys[at(Action::Attack)] = {SDL_SCANCODE_K, SDL_SCANCODE_UNKNOWN};  // the player put Attack on K
    PowerKeyMap m = levelPowerKeys(l, content, base, nullptr);
    const ResolvedPowerKey* dash = m.find("dash");
    CHECK(dash->key == SDL_SCANCODE_UNKNOWN);
    CHECK(dash->wantedKey == SDL_SCANCODE_K);
    CHECK(dash->keyCommand == actionLabel(Action::Attack));
    CHECK(dash->pad == PadBinding::button(SDL_GAMEPAD_BUTTON_EAST));
    const ResolvedPowerKey* shield = m.find("shield");
    CHECK(shield->key == SDL_SCANCODE_UNKNOWN);
    CHECK(shield->keyCommand == actionLabel(Action::Jump));
    CHECK(shield->pad.kind == PadBinding::Kind::None);
    CHECK(shield->padCommand == actionLabel(Action::Jump));
    // The quick restart (R / Vue) is a command too; the arrows are the moves' keys.
    CHECK(baseCommandOfKey(base, SDL_SCANCODE_R) == "Recommencer");
    CHECK(baseCommandOfPad(base, PadBinding::button(SDL_GAMEPAD_BUTTON_BACK)) == "Recommencer");
    CHECK(baseCommandOfKey(base, SDL_SCANCODE_LEFT) == actionLabel(Action::MoveLeft));
    CHECK(baseCommandOfPad(base, PadBinding::button(SDL_GAMEPAD_BUTTON_DPAD_UP)) == actionLabel(Action::MoveUp));
    // The keys of the power slots never count as commands.
    base.keys[at(Action::PowerSlot1)] = {SDL_SCANCODE_U, SDL_SCANCODE_UNKNOWN};
    CHECK(baseCommandOfKey(base, SDL_SCANCODE_U).empty());
}

TEST_CASE("level power keys: two powers on one key are resolved the same way every time") {
    ContentDatabase content;
    content.reload();
    LevelData l = levelWith({"dash", "wave"}, {"shield", "boomerang"});
    l.powerKeys["wave"] = {{"key", "K"}};  // the dash's suggestion
    const InputBindings base = InputBindings::defaults();
    PowerKeyMap a = levelPowerKeys(l, content, base, nullptr);
    // Both on K: the level's choice (the wave) keeps it, the dash takes the first free key.
    CHECK(a.find("wave")->key == SDL_SCANCODE_K);
    CHECK(a.find("dash")->keyPower == "wave");
    CHECK(a.find("dash")->key == SDL_SCANCODE_L);  // K is taken, L (the wave's suggestion) is free
    // The shield and the boomerang share a suggested button (never in the same campaign level):
    // the first one keeps it.
    CHECK(a.find("shield")->pad == PadBinding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    CHECK(a.find("boomerang")->padPower == "shield");
    std::set<int> used;
    for (const auto& r : a.powers) {
        CHECK(r.key != SDL_SCANCODE_UNKNOWN);
        CHECK(used.insert(r.key).second);  // never two powers on one key
    }
    for (size_t i = 0; i < a.powers.size(); ++i)
        for (size_t j = i + 1; j < a.powers.size(); ++j) CHECK(!(a.powers[i].pad == a.powers[j].pad));
    // The same answer every time.
    PowerKeyMap b = levelPowerKeys(l, content, base, nullptr);
    for (size_t i = 0; i < a.powers.size(); ++i) {
        CHECK(a.powers[i].key == b.powers[i].key);
        CHECK(a.powers[i].pad == b.powers[i].pad);
    }
    // The player's choice comes first: the dash back on K, the wave moved.
    PowerKeyOverrides mine;
    mine.setKey(l.id, "dash", SDL_SCANCODE_K);
    PowerKeyMap c = levelPowerKeys(l, content, base, &mine);
    CHECK(c.find("dash")->key == SDL_SCANCODE_K);
    CHECK(c.find("wave")->keyPower == "dash");
    CHECK(c.find("wave")->key == SDL_SCANCODE_L);
}

TEST_CASE("level power keys: a slot takes the key of the power it holds, never a spurious press") {
    ContentDatabase content;
    content.reload();
    InputManager in;
    in.setBindings(InputBindings::defaults());
    // The settings give the power slots no key any more.
    for (int s = 0; s < kPowerSlotKeys; ++s) CHECK(in.bindings().keys[at(powerSlotAction(s))][0] == SDL_SCANCODE_UNKNOWN);
    // Mouse 4 held, then given to slot 1 (a power picked up while it is held): neither held nor
    // pressed until released and pressed again.
    frame(in, {mouseButton(4, true)});
    in.setPowerSlotKey(0, mouseButtonBinding(4), PadBinding{});
    PlayerInput p = frame(in);
    CHECK(!p.isPressed(Action::PowerSlot1));
    CHECK(!p.isHeld(Action::PowerSlot1));
    p = frame(in, {mouseButton(4, false)});
    CHECK(!p.isPressed(Action::PowerSlot1));
    p = frame(in, {mouseButton(4, true)});
    CHECK(p.isPressed(Action::PowerSlot1));
    CHECK(p.isHeld(Action::PowerSlot1));
    frame(in, {mouseButton(4, false)});
    // A press not consumed yet when the slot changes power: dropped.
    in.beginFrame();
    in.handleEvent(keyDown(SDL_SCANCODE_K));
    CHECK(!in.consumePlayerInput().isPressed(Action::PowerSlot2));  // K is no slot's key yet
    in.setPowerSlotKey(1, SDL_SCANCODE_K, PadBinding{});
    in.beginFrame();
    in.handleEvent(keyDown(SDL_SCANCODE_K));
    in.setPowerSlotKey(1, SDL_SCANCODE_L, PadBinding{});
    CHECK(!in.consumePlayerInput().isPressed(Action::PowerSlot2));
    // A fresh press of its key: the slot's action (the bits recorded in the replays).
    in.beginFrame();
    in.handleEvent(keyDown(SDL_SCANCODE_L));
    CHECK(in.consumePlayerInput().isPressed(Action::PowerSlot2));
    // The settings applied again keep the keys of the slots (they belong to the level).
    in.setBindings(InputBindings::defaults());
    CHECK(in.bindings().keys[at(Action::PowerSlot2)][0] == SDL_SCANCODE_L);
    in.clearPowerSlotKeys();
    CHECK(in.bindings().keys[at(Action::PowerSlot2)][0] == SDL_SCANCODE_UNKNOWN);

    // In a level: whatever the order the powers are picked up in, each keeps its key.
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    LevelData l = levelWith({}, {"dash", "wave", "shield"});
    PowerKeyMap keys = levelPowerKeys(l, content, InputBindings::defaults(), nullptr);
    std::map<std::string, SDL_Scancode> seen;
    for (const auto& order : {std::vector<std::string>{"dash", "wave", "shield"}, std::vector<std::string>{"shield", "wave", "dash"}}) {
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        GameWorld w(s, WorldMode::Play);
        w.silent = true;
        w.setKeysPerSlot(true);
        w.load(l);
        InputManager im;
        for (const auto& id : order) {
            w.player().grantPower(*content.power(id));
            CHECK(applyPowerSlotKeys(im, w.player(), keys).empty());
        }
        for (size_t slot = 0; slot < order.size(); ++slot) {
            SDL_Scancode k = im.bindings().keys[at(powerSlotAction(static_cast<int>(slot)))][0];
            CHECK(k == keys.find(order[slot])->key);
            CHECK(im.bindings().pad[at(powerSlotAction(static_cast<int>(slot)))][0] == keys.find(order[slot])->pad);
            if (seen.count(order[slot])) CHECK(seen[order[slot]] == k);
            seen[order[slot]] = k;
        }
        // A power that uses no key of its own leaves its slot without one; a power not listed
        // by the level is reported, so that the game gives it its key too.
        w.player().grantPower(*content.power("glide"));
        w.player().grantPower(*content.power("decoy"));
        std::vector<std::string> missing = applyPowerSlotKeys(im, w.player(), keys);
        CHECK(missing == std::vector<std::string>({"decoy"}));
        CHECK(im.bindings().keys[at(Action::PowerSlot4)][0] == SDL_SCANCODE_UNKNOWN);
    }

    // 2.50: a giga boss arena offers every power; their gamepad buttons are shared (shield and
    // the boomerang blade both suggest LB) but two powers carried together never get the same
    // one, whatever the order they are picked up in, and the keyboard keeps a key per power.
    LevelData all = levelWith({}, {"shield", "boomerang", "time_slow", "recall", "frost_nova", "impulse", "decoy", "teleport", "dash", "wave",
                                   "plasma_bomb"});
    PowerKeyMap allKeys = levelPowerKeys(all, content, InputBindings::defaults(), nullptr);
    CHECK(allKeys.sharedPad);
    CHECK(allKeys.find("shield")->pad == allKeys.find("boomerang")->pad);  // its own button, shared
    for (const auto& r : allKeys.powers) {
        CHECK(!r.padConflict());
        CHECK(!r.keyConflict());
    }
    for (const auto& order : {std::vector<std::string>{"shield", "boomerang", "time_slow", "recall"},
                              std::vector<std::string>{"recall", "time_slow", "boomerang", "shield"},
                              std::vector<std::string>{"frost_nova", "impulse", "decoy", "teleport"}}) {
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        GameWorld w(s, WorldMode::Play);
        w.silent = true;
        w.setKeysPerSlot(true);
        w.load(all);
        InputManager im;
        for (const auto& id : order) {
            w.player().grantPower(*content.power(id));
            CHECK(applyPowerSlotKeys(im, w.player(), allKeys).empty());
        }
        std::vector<PadBinding> pads;
        std::set<SDL_Scancode> keysHeld;
        for (size_t slot = 0; slot < order.size(); ++slot) {
            const PadBinding p = im.bindings().pad[at(powerSlotAction(static_cast<int>(slot)))][0];
            CHECK(p.kind != PadBinding::Kind::None);
            CHECK(baseCommandOfPad(InputBindings::defaults(), p).empty());
            for (const auto& q : pads) CHECK(!(q == p));
            pads.push_back(p);
            // The first carried power of a shared button keeps it.
            if (slot == 0) CHECK(p == allKeys.find(order[0])->pad);
            const SDL_Scancode k = im.bindings().keys[at(powerSlotAction(static_cast<int>(slot)))][0];
            CHECK(k == allKeys.find(order[slot])->key);
            CHECK(keysHeld.insert(k).second);
        }
    }
    CHECK(seen["dash"] == SDL_SCANCODE_K);
    CHECK(seen["wave"] == SDL_SCANCODE_L);
    CHECK(seen["shield"] == SDL_SCANCODE_O);
}

TEST_CASE("level power keys: settings with the keys of the power slots still load, those keys ignored") {
    // A file of 2.24-2.31: slot keys chosen by the player, Attack's key taken by slot 1.
    Json old = Json::parse(R"({
        "keyboard": {"jump": ["Space", ""], "attack": ["", ""], "interact": ["R", ""], "power_slot_1": ["J", ""],
                     "power_slot_2": ["Mouse4", ""], "dash": ["Q", ""]},
        "gamepad": {"jump": [{"button": "a"}, null], "power_slot_1": [{"button": "x"}, null], "attack": [null, null]},
        "gamepadSettings": {"stickDeadzone": 0.3}
    })");
    InputBindings b = InputBindings::fromJson(old);
    for (int s = 0; s < kPowerSlotKeys; ++s) {
        CHECK(b.keys[at(powerSlotAction(s))][0] == SDL_SCANCODE_UNKNOWN);
        CHECK(b.pad[at(powerSlotAction(s))][0].kind == PadBinding::Kind::None);
    }
    CHECK(b.keys[at(Action::Dash)][0] == SDL_SCANCODE_UNKNOWN);
    CHECK(b.keys[at(Action::Interact)][0] == SDL_SCANCODE_R);  // the player's own choices stay
    CHECK(b.keys[at(Action::Jump)][0] == SDL_SCANCODE_SPACE);
    CHECK(b.gamepad.stickDeadzone > 0.29f);
    // Attack, left without a key by the slot: its own key back (free again).
    CHECK(b.keys[at(Action::Attack)][0] == SDL_SCANCODE_J);
    CHECK(b.pad[at(Action::Attack)][0] == PadBinding::button(SDL_GAMEPAD_BUTTON_WEST));
    // Saved again: no slot, no button of the powers before 2.24.
    Json saved = b.toJson();
    CHECK(!saved["keyboard"].contains("power_slot_1"));
    CHECK(!saved["gamepad"].contains("power_slot_1"));
    CHECK(!saved["keyboard"].contains("dash"));
    InputBindings again = InputBindings::fromJson(saved);
    for (int a = 0; a < kActionCount; ++a) {
        CHECK(again.keys[a] == b.keys[a]);
        CHECK(again.pad[a] == b.pad[a]);
    }
    // A command the player really emptied in a file without slot keys stays empty.
    Json mine = Json::parse(R"({"keyboard": {"attack": ["", ""], "jump": ["J", ""]}})");
    CHECK(InputBindings::fromJson(mine).keys[at(Action::Attack)][0] == SDL_SCANCODE_UNKNOWN);
}

TEST_CASE("level power keys: the player's keys are saved, read back, and survive a damaged file") {
    const fs::path dir = Paths::userDir() / "power_keys_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path f = dir / "power_keys.json";
    PowerKeyOverrides a;
    a.setKey("sylvanox_01", "dash", SDL_SCANCODE_C);
    a.setPad("sylvanox_01", "dash", PadBinding::button(SDL_GAMEPAD_BUTTON_NORTH));
    a.setKey("sylvanox_01", "wave", mouseButtonBinding(5));
    a.setKey("pyralis_02", "shield", static_cast<SDL_Scancode>(kWheelUpBinding));
    CHECK(a.saveFile(f));
    PowerKeyOverrides b;
    b.loadFile(f);
    CHECK(b.toJson() == a.toJson());
    CHECK(powerKeyEntryKey(b.level("sylvanox_01"), "dash") == SDL_SCANCODE_C);
    CHECK(powerKeyEntryPad(b.level("sylvanox_01"), "dash") == PadBinding::button(SDL_GAMEPAD_BUTTON_NORTH));
    CHECK(powerKeyEntryKey(b.level("sylvanox_01"), "wave") == mouseButtonBinding(5));
    CHECK(powerKeyEntryKey(b.level("pyralis_02"), "shield") == static_cast<SDL_Scancode>(kWheelUpBinding));
    CHECK(b.level("glacior_01").empty());
    // A key set back to nothing leaves no empty entry behind.
    b.setKey("pyralis_02", "shield", SDL_SCANCODE_UNKNOWN);
    CHECK(!b.has("pyralis_02", "shield"));
    CHECK(!b.toJson()["levels"].contains("pyralis_02"));
    // A damaged file: nothing read, nothing lost but its content (put aside), no crash.
    FileUtil::writeTextAtomic(f, "{\"format\": \"cosmic.power_keys\", \"version\": 1, \"data\": {\"levels\": ");
    PowerKeyOverrides c;
    c.loadFile(f);
    CHECK(c.toJson()["levels"].empty());
    CHECK(fs::exists(dir / "power_keys.json.corrupt"));
    // Wrong types everywhere: only what makes sense is kept.
    Json junk = Json::parse(R"({"levels": {"a": {"dash": {"key": 4, "pad": "x"}, "wave": {"key": "V", "pad": {"button": 2}},
                                   "shield": {"pad": {"axis": "lefttrigger", "dir": -3}}}, "b": 7, "": {"dash": {"key": "K"}}}})");
    PowerKeyOverrides d;
    d.fromJson(junk);
    CHECK(d.toJson()["levels"].size() == 1);
    CHECK(!d.has("a", "dash"));
    CHECK(powerKeyEntryKey(d.level("a"), "wave") == SDL_SCANCODE_V);
    CHECK(powerKeyEntryPad(d.level("a"), "wave").kind == PadBinding::Kind::None);
    CHECK(powerKeyEntryPad(d.level("a"), "shield") == PadBinding::axisNeg(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    // A level's own keys go through the same check.
    Json lj = LevelData::makeEmpty("x", "X", "sylvanox").toJson();
    lj["powerKeys"] = {{"dash", {{"key", std::string(200, 'K')}}}, {"wave", "L"}};
    CHECK(LevelData::fromJson(lj)->powerKeys.empty());
    fs::remove_all(dir, ec);
}

TEST_CASE("level power keys: every campaign level carries the keys of its powers (tools/levelgen)") {
    ContentDatabase content;
    content.reload();
    std::map<std::string, Json> keyOf;  // power -> its entry in the first level met
    int levels = 0;
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(e->id);
        CHECK(level.has_value());
        std::vector<std::string> powers = levelKeyPowers(*level, content);
        if (powers.empty()) continue;
        ++levels;
        for (const auto& p : powers) {
            if (!level->powerKeys.contains(p)) std::printf("  %s : pas de touche pour %s\n", e->id.c_str(), p.c_str());
            CHECK(level->powerKeys.contains(p));
            CHECK(level->powerKeys[p].contains("key"));
            CHECK(level->powerKeys[p].contains("pad"));
            // The same key for a power in every campaign level.
            if (keyOf.count(p)) CHECK(keyOf[p] == level->powerKeys[p]);
            else keyOf[p] = level->powerKeys[p];
        }
        // What --plan-power-keys gives: the suggestions, without conflict.
        CHECK(level->powerKeys == planLevelPowerKeys(*level, content));
        PowerKeyMap m = levelPowerKeys(*level, content, InputBindings::defaults(), nullptr);
        for (const auto& r : m.powers) {
            CHECK(!r.keyConflict());
            CHECK(!r.padConflict());
            CHECK(r.levelKeySource == PowerKeySource::Level);
        }
    }
    CHECK(levels == 25);  // 2.33: Le Cœur d'une Étoile Mourante; 2.48: La Gueule du Dragon; 2.49: Le Cœur de Pierre; 2.53: La Fosse des Abysses;
                          // 2.56: Le Nid de Cendres; 2.57: La Crypte des Noms; 2.58: La Fonderie Engloutie
}

TEST_CASE("level power keys (2.60): a change follows in the levels that had exactly the same keys") {
    ContentDatabase content;
    content.reload();
    const InputBindings base = InputBindings::defaults();
    LevelData a = levelWith({"dash", "wave"}, {"shield"});
    a.id = "keys_a";
    LevelData b = levelWith({"wave", "dash"}, {"shield"});  // same powers, another order
    b.id = "keys_b";
    LevelData c = levelWith({"dash", "wave"});  // one power fewer: not the same keys
    c.id = "keys_c";
    LevelData d = levelWith({"dash", "wave"}, {"shield"});
    d.id = "keys_d";
    d.powerKeys["dash"] = {{"key", "C"}};  // the level asks for another key: not the same keys
    PowerKeyOverrides mine;
    auto keys = [&](const LevelData& l) { return levelPowerKeys(l, content, base, &mine); };
    CHECK(!powerKeySignature(keys(a)).empty());
    CHECK(powerKeySignature(keys(a)) == powerKeySignature(keys(b)));
    CHECK(powerKeySignature(keys(a)) != powerKeySignature(keys(c)));
    CHECK(powerKeySignature(keys(a)) != powerKeySignature(keys(d)));
    CHECK(powerKeySignature(PowerKeyMap{}).empty());
    // The player changes the dash in A: B follows, C and D keep theirs.
    const PowerKeyMap bBefore = keys(b);
    mine.setKey(a.id, "dash", SDL_SCANCODE_F);
    mine.setPad(a.id, "dash", PadBinding::button(SDL_GAMEPAD_BUTTON_NORTH));
    CHECK(matchPowerKeys(mine, b.id, bBefore, keys(a)));
    CHECK(keys(b).find("dash")->wantedKey == SDL_SCANCODE_F);
    CHECK(keys(b).find("dash")->wantedPad == PadBinding::button(SDL_GAMEPAD_BUTTON_NORTH));
    CHECK(powerKeySignature(keys(a)) == powerKeySignature(keys(b)));
    CHECK(!mine.has(c.id, "dash"));
    CHECK(!mine.has(d.id, "dash"));
    // Nothing more to do the second time.
    CHECK(!matchPowerKeys(mine, b.id, keys(b), keys(a)));
    // « Par défaut » in A: B goes back to the same keys, stored as no choice (its level's key).
    const PowerKeyMap bNow = keys(b);
    mine.reset(a.id, "dash");
    CHECK(matchPowerKeys(mine, b.id, bNow, keys(a)));
    CHECK(!mine.has(b.id, "dash"));
    CHECK(powerKeySignature(keys(a)) == powerKeySignature(keys(b)));
    // A level whose own key differs follows by a choice of the player's: D asks for C, A for
    // the suggestion; D takes the suggestion as its player's key, and the signatures match.
    const PowerKeyMap dBefore = keys(d);
    CHECK(matchPowerKeys(mine, d.id, dBefore, keys(a)));
    CHECK(mine.has(d.id, "dash"));
    CHECK(powerKeySignature(keys(a)) == powerKeySignature(keys(d)));
}
