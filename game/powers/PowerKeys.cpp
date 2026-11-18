#include "game/powers/PowerKeys.h"

#include <algorithm>
#include <numeric>

#include "engine/input/InputManager.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/LevelData.h"
#include "game/player/Player.h"
#include "game/powers/Power.h"
#include "saves/SaveFile.h"

namespace cosmic {

const ResolvedPowerKey* PowerKeyMap::find(const std::string& power) const {
    for (const auto& p : powers)
        if (p.power == power) return &p;
    return nullptr;
}

bool powerUsesKey(const PowerDefinition& def) { return powerTriggerInfo(def).kind == PowerTrigger::SlotKey; }

std::vector<std::string> levelKeyPowers(const LevelData& level, const ContentDatabase& content) {
    std::vector<std::string> out;
    auto add = [&](const std::string& id) {
        if (id.empty() || std::find(out.begin(), out.end(), id) != out.end()) return;
        if (const PowerDefinition* d = content.power(id); d && powerUsesKey(*d)) out.push_back(id);
    };
    for (const auto& p : level.startPowers) add(p);
    for (const auto& o : level.objects)
        if (o.type == "power_unlock") add(o.propStr("power", "dash"));
    for (const auto& e : level.events)
        for (const auto& a : e.actions)
            if (a.type == "givePower") add(jstr(a.params, "power"));
    return out;
}

namespace {

/// The commands whose keys stay theirs in a level: every command of the settings (moves with
/// their fixed alternates, jump, attack, interact, pause, focus mode, ghosts).
/// 2.69: the raid's large map is not one of them (it exists in the raids only: the powers of the other
/// levels keep their keys, M for the decoy among them).
bool isBaseCommand(int a) {
    return a < kFirstMenuAction && actionIsRebindable(static_cast<Action>(a)) && static_cast<Action>(a) != Action::RaidMap;
}

const char* const kRestart = "Recommencer";

/// The free keys a power gets when the one it asks for is taken by another power: the
/// suggestions of the game's powers first (the right hand around the attack), then others.
const SDL_Scancode kKeyFallback[] = {SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_O, SDL_SCANCODE_I,     SDL_SCANCODE_U,
                                     SDL_SCANCODE_F, SDL_SCANCODE_H, SDL_SCANCODE_P, SDL_SCANCODE_N,     SDL_SCANCODE_M,
                                     SDL_SCANCODE_Y, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_B,     SDL_SCANCODE_X,
                                     SDL_SCANCODE_Z, SDL_SCANCODE_Q, SDL_SCANCODE_1, SDL_SCANCODE_2,     SDL_SCANCODE_3,
                                     SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7,     SDL_SCANCODE_8,
                                     SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SEMICOLON};
const PadBinding kPadFallback[] = {PadBinding::button(SDL_GAMEPAD_BUTTON_EAST),           PadBinding::axisPos(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER),
                                   PadBinding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),  PadBinding::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
                                   PadBinding::axisPos(SDL_GAMEPAD_AXIS_LEFT_TRIGGER),    PadBinding::button(SDL_GAMEPAD_BUTTON_RIGHT_STICK),
                                   PadBinding::axisPos(SDL_GAMEPAD_AXIS_RIGHTY),          PadBinding::axisNeg(SDL_GAMEPAD_AXIS_RIGHTY),
                                   PadBinding::axisNeg(SDL_GAMEPAD_AXIS_RIGHTX),          PadBinding::axisPos(SDL_GAMEPAD_AXIS_RIGHTX)};

int sourceRank(PowerKeySource s) {
    switch (s) {
        case PowerKeySource::Player: return 0;
        case PowerKeySource::Level: return 1;
        case PowerKeySource::Suggestion: return 2;
        case PowerKeySource::None: break;
    }
    return 3;
}

bool isNone(SDL_Scancode k) { return k == SDL_SCANCODE_UNKNOWN; }
bool isNone(const PadBinding& p) { return p.kind == PadBinding::Kind::None; }

/// One device of every power: what it asks for, where it goes.
template <typename T>
struct Claim {
    const T* wanted;
    PowerKeySource source;
    T* out;
    std::string* command;
    std::string* power;
};

template <typename T, typename CommandOf, size_t N>
void assign(std::vector<ResolvedPowerKey>& rs, std::vector<Claim<T>> claims, CommandOf commandOf, const T (&fallback)[N]) {
    std::vector<size_t> order(claims.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return sourceRank(claims[a].source) < sourceRank(claims[b].source); });
    std::vector<std::pair<T, size_t>> taken;
    std::vector<size_t> moved;
    for (size_t i : order) {
        const T& w = *claims[i].wanted;
        if (isNone(w)) continue;
        if (std::string c = commandOf(w); !c.empty()) {
            *claims[i].command = c;  // the command wins: no key for the power
            continue;
        }
        auto it = std::find_if(taken.begin(), taken.end(), [&](const auto& t) { return t.first == w; });
        if (it != taken.end()) {
            *claims[i].power = rs[it->second].power;
            moved.push_back(i);
            continue;
        }
        *claims[i].out = w;
        taken.push_back({w, i});
    }
    // Then, in the same order, a free key for each power whose key another one kept.
    for (size_t i : moved)
        for (const T& f : fallback) {
            if (!commandOf(f).empty()) continue;
            if (std::find_if(taken.begin(), taken.end(), [&](const auto& t) { return t.first == f; }) != taken.end()) continue;
            *claims[i].out = f;
            taken.push_back({f, i});
            break;
        }
}

}  // namespace

std::string baseCommandOfKey(const InputBindings& base, SDL_Scancode key) {
    if (key == SDL_SCANCODE_UNKNOWN) return {};
    for (int a = 0; a < kFirstMenuAction; ++a)
        if (isBaseCommand(a))
            for (auto k : base.keys[a])
                if (k == key) return actionLabel(static_cast<Action>(a));
    if (key == SDL_SCANCODE_R) return kRestart;  // quick restart (contre-la-montre, trainings)
    return {};
}

std::string baseCommandOfPad(const InputBindings& base, const PadBinding& pad) {
    if (pad.kind == PadBinding::Kind::None) return {};
    for (int a = 0; a < kFirstMenuAction; ++a)
        if (isBaseCommand(a))
            for (const auto& p : base.pad[a])
                if (p == pad) return actionLabel(static_cast<Action>(a));
    if (pad == PadBinding::button(SDL_GAMEPAD_BUTTON_BACK)) return kRestart;
    return {};
}

Json powerKeyJson(SDL_Scancode key, const PadBinding& pad) {
    Json e = Json::object();
    if (std::string k = bindingName(key); !k.empty()) e["key"] = k;
    if (pad.kind != PadBinding::Kind::None) e["pad"] = padBindingToJson(pad);
    return e;
}

SDL_Scancode powerKeyEntryKey(const Json& keys, const std::string& power) {
    if (!keys.is_object() || !keys.contains(power) || !keys[power].is_object()) return SDL_SCANCODE_UNKNOWN;
    return bindingFromName(jstr(keys[power], "key"));
}

PadBinding powerKeyEntryPad(const Json& keys, const std::string& power) {
    if (!keys.is_object() || !keys.contains(power) || !keys[power].is_object() || !keys[power].contains("pad")) return {};
    return padBindingFromJson(keys[power]["pad"]);
}

PowerKeyMap resolvePowerKeys(const std::vector<std::string>& powers, const ContentDatabase& content, const Json& levelKeys,
                             const Json* playerKeys, const InputBindings& base) {
    PowerKeyMap m;
    for (const auto& id : powers) {
        if (id.empty() || m.find(id)) continue;
        ResolvedPowerKey r;
        r.power = id;
        const PowerDefinition* d = content.power(id);
        const SDL_Scancode sk = d ? d->suggestedKey : SDL_SCANCODE_UNKNOWN;
        const PadBinding sp = d ? d->suggestedPad : PadBinding{};
        const SDL_Scancode lk = powerKeyEntryKey(levelKeys, id);
        const PadBinding lp = powerKeyEntryPad(levelKeys, id);
        r.levelKey = lk != SDL_SCANCODE_UNKNOWN ? lk : sk;
        r.levelKeySource = lk != SDL_SCANCODE_UNKNOWN ? PowerKeySource::Level : sk != SDL_SCANCODE_UNKNOWN ? PowerKeySource::Suggestion : PowerKeySource::None;
        r.levelPad = lp.kind != PadBinding::Kind::None ? lp : sp;
        r.levelPadSource = lp.kind != PadBinding::Kind::None ? PowerKeySource::Level
                           : sp.kind != PadBinding::Kind::None ? PowerKeySource::Suggestion
                                                               : PowerKeySource::None;
        const SDL_Scancode pk = playerKeys ? powerKeyEntryKey(*playerKeys, id) : SDL_SCANCODE_UNKNOWN;
        const PadBinding pp = playerKeys ? powerKeyEntryPad(*playerKeys, id) : PadBinding{};
        r.wantedKey = pk != SDL_SCANCODE_UNKNOWN ? pk : r.levelKey;
        r.keySource = pk != SDL_SCANCODE_UNKNOWN ? PowerKeySource::Player : r.levelKeySource;
        r.wantedPad = pp.kind != PadBinding::Kind::None ? pp : r.levelPad;
        r.padSource = pp.kind != PadBinding::Kind::None ? PowerKeySource::Player : r.levelPadSource;
        m.powers.push_back(std::move(r));
    }
    std::vector<Claim<SDL_Scancode>> keys;
    std::vector<Claim<PadBinding>> pads;
    for (auto& r : m.powers) {
        keys.push_back({&r.wantedKey, r.keySource, &r.key, &r.keyCommand, &r.keyPower});
        pads.push_back({&r.wantedPad, r.padSource, &r.pad, &r.padCommand, &r.padPower});
    }
    assign(m.powers, std::move(keys), [&](SDL_Scancode k) { return baseCommandOfKey(base, k); }, kKeyFallback);
    m.sharedPad = padsShared(m.powers.size());
    if (!m.sharedPad) {
        assign(m.powers, std::move(pads), [&](const PadBinding& p) { return baseCommandOfPad(base, p); }, kPadFallback);
    } else {
        // 2.50: more powers than free gamepad inputs (a giga boss arena offers every power): each
        // power keeps its own button, shared with another one; only four are carried at once,
        // and applyPowerSlotKeys() moves a carried power whose button another carried power has.
        for (auto& c : pads) {
            if (isNone(*c.wanted)) continue;
            if (std::string cmd = baseCommandOfPad(base, *c.wanted); !cmd.empty()) *c.command = cmd;
            else *c.out = *c.wanted;
        }
    }
    return m;
}

// ----------------------------------------------------------------- the player's own keys

namespace {
SaveFile overridesFile() { return SaveFile("power_keys", PowerKeyOverrides::kVersion); }
fs::path overridesPath() { return Paths::savesDir() / "power_keys.json"; }
}  // namespace

void PowerKeyOverrides::load() { loadFile(overridesPath()); }
bool PowerKeyOverrides::save() const { return saveFile(overridesPath()); }

void PowerKeyOverrides::loadFile(const fs::path& file) {
    levels_ = Json::object();
    if (auto data = overridesFile().load(file)) fromJson(*data);
}

bool PowerKeyOverrides::saveFile(const fs::path& file) const { return overridesFile().save(file, toJson()); }

Json PowerKeyOverrides::toJson() const { return {{"levels", levels_}}; }

void PowerKeyOverrides::fromJson(const Json& data) {
    levels_ = Json::object();
    if (!data.is_object() || !data.contains("levels") || !data["levels"].is_object()) return;
    for (const auto& [id, keys] : data["levels"].items()) {
        if (levels_.size() >= 4000) break;
        if (id.empty() || id.size() > 128) continue;
        Json k = sanitizePowerKeys(keys);
        if (!k.empty()) levels_[id] = std::move(k);
    }
}

const Json& PowerKeyOverrides::level(const std::string& levelId) const {
    static const Json kNone = Json::object();
    auto it = levels_.find(levelId);
    return it != levels_.end() && it->is_object() ? *it : kNone;
}

Json& PowerKeyOverrides::entry(const std::string& levelId, const std::string& power) {
    Json& l = levels_[levelId];
    if (!l.is_object()) l = Json::object();
    Json& e = l[power];
    if (!e.is_object()) e = Json::object();
    return e;
}

void PowerKeyOverrides::tidy(const std::string& levelId, const std::string& power) {
    auto it = levels_.find(levelId);
    if (it == levels_.end()) return;
    if (it->contains(power) && (*it)[power].empty()) it->erase(power);
    if (it->empty()) levels_.erase(levelId);
}

void PowerKeyOverrides::setKey(const std::string& levelId, const std::string& power, SDL_Scancode key) {
    if (levelId.empty() || power.empty()) return;
    Json& e = entry(levelId, power);
    if (std::string k = bindingName(key); !k.empty()) e["key"] = k;
    else e.erase("key");
    tidy(levelId, power);
}

void PowerKeyOverrides::setPad(const std::string& levelId, const std::string& power, const PadBinding& pad) {
    if (levelId.empty() || power.empty()) return;
    Json& e = entry(levelId, power);
    if (pad.kind != PadBinding::Kind::None) e["pad"] = padBindingToJson(pad);
    else e.erase("pad");
    tidy(levelId, power);
}

void PowerKeyOverrides::reset(const std::string& levelId, const std::string& power) {
    auto it = levels_.find(levelId);
    if (it == levels_.end()) return;
    it->erase(power);
    if (it->empty()) levels_.erase(levelId);
}

bool PowerKeyOverrides::has(const std::string& levelId, const std::string& power) const { return level(levelId).contains(power); }

// ------------------------------------------------------------------------------ helpers

PowerKeyMap levelPowerKeys(const LevelData& level, const ContentDatabase& content, const InputBindings& base,
                           const PowerKeyOverrides* overrides, const std::vector<std::string>& extra) {
    std::vector<std::string> powers = levelKeyPowers(level, content);
    for (const auto& id : extra)
        if (const PowerDefinition* d = content.power(id); d && powerUsesKey(*d)) powers.push_back(id);
    return resolvePowerKeys(powers, content, level.powerKeys, overrides ? &overrides->level(level.id) : nullptr, base);
}

ResolvedPowerKey levelPowerKey(const std::string& power, const LevelData& level, const ContentDatabase& content, const InputBindings& base,
                               const PowerKeyOverrides* overrides, const std::vector<std::string>& extra) {
    std::vector<std::string> more = extra;
    more.push_back(power);
    PowerKeyMap m = levelPowerKeys(level, content, base, overrides, more);
    if (const ResolvedPowerKey* r = m.find(power)) return *r;
    ResolvedPowerKey none;
    none.power = power;
    return none;
}

bool padsShared(size_t keyPowers) { return keyPowers > std::size(kPadFallback); }

std::string powerKeySignature(const PowerKeyMap& keys) {
    std::vector<std::string> parts;
    for (const auto& r : keys.powers)
        parts.push_back(r.power + "=" + std::to_string(static_cast<int>(r.wantedKey)) + "/" + std::to_string(static_cast<int>(r.wantedPad.kind)) +
                        ":" + std::to_string(r.wantedPad.code));
    std::sort(parts.begin(), parts.end());
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : ";") + p;
    return out;
}

bool matchPowerKeys(PowerKeyOverrides& overrides, const std::string& toLevelId, const PowerKeyMap& toKeys, const PowerKeyMap& fromKeys) {
    bool changed = false;
    for (const auto& r : toKeys.powers) {
        const ResolvedPowerKey* f = fromKeys.find(r.power);
        if (!f) continue;
        const Json before = overrides.level(toLevelId).contains(r.power) ? overrides.level(toLevelId)[r.power] : Json();
        overrides.setKey(toLevelId, r.power, f->wantedKey == r.levelKey ? SDL_SCANCODE_UNKNOWN : f->wantedKey);
        overrides.setPad(toLevelId, r.power, f->wantedPad == r.levelPad ? PadBinding{} : f->wantedPad);
        const Json after = overrides.level(toLevelId).contains(r.power) ? overrides.level(toLevelId)[r.power] : Json();
        changed = changed || before != after;
    }
    return changed;
}

std::vector<std::string> applyPowerSlotKeys(InputManager& in, const Player& player, const PowerKeyMap& keys) {
    std::vector<std::string> missing;
    const auto& powers = player.powers();
    SDL_Scancode slotKey[kPowerSlotKeys] = {};
    PadBinding slotPad[kPowerSlotKeys];
    bool movable[kPowerSlotKeys] = {};
    for (int s = 0; s < kPowerSlotKeys; ++s) {
        slotKey[s] = SDL_SCANCODE_UNKNOWN;
        if (static_cast<size_t>(s) >= powers.size()) continue;
        const Power& p = *powers[static_cast<size_t>(s)];
        if (p.trigger() != PowerTrigger::SlotKey || p.key() != powerSlotAction(s)) continue;
        if (const ResolvedPowerKey* r = keys.find(p.id())) {
            slotKey[s] = r->key;
            slotPad[s] = r->pad;
            movable[s] = r->padCommand.empty();
        } else {
            missing.push_back(p.id());
        }
    }
    // 2.50: in a level whose powers share their gamepad buttons (every power offered), two
    // carried powers never share one: the later slot takes a free button, never the button of
    // another carried power nor a command's (display and input only, outside the simulation).
    if (keys.sharedPad) {
        for (int s = 0; s < kPowerSlotKeys; ++s) {
            if (!movable[s]) continue;
            bool clash = isNone(slotPad[s]) && slotKey[s] != SDL_SCANCODE_UNKNOWN;
            for (int o = 0; o < s && !clash; ++o) clash = !isNone(slotPad[s]) && slotPad[o] == slotPad[s];
            if (!clash) continue;
            PadBinding chosen;
            for (const PadBinding& f : kPadFallback) {
                bool used = !baseCommandOfPad(in.bindings(), f).empty();
                for (int o = 0; o < kPowerSlotKeys && !used; ++o) used = o != s && slotPad[o] == f;
                if (!used) {
                    chosen = f;
                    break;
                }
            }
            slotPad[s] = chosen;
        }
    }
    for (int s = 0; s < kPowerSlotKeys; ++s) in.setPowerSlotKey(s, slotKey[s], slotPad[s]);
    return missing;
}

Json planLevelPowerKeys(const LevelData& level, const ContentDatabase& content) {
    PowerKeyMap m = resolvePowerKeys(levelKeyPowers(level, content), content, Json::object(), nullptr, InputBindings::defaults());
    Json out = Json::object();
    for (const auto& r : m.powers) {
        Json e = powerKeyJson(r.key, r.pad);
        if (!e.empty()) out[r.power] = e;
    }
    return out;
}

}  // namespace cosmic
