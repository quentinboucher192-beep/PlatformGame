#pragma once

// Keys of the powers (2.31). A power used with a key of its own (PowerTrigger::SlotKey) has
// the same key wherever it sits in the HUD: the key the level chose for it in the editor
// (LevelData::powerKeys), else the key suggested by the power (data/powers: suggestedKey,
// suggestedPad); each player may change it for a level (saves/power_keys.json). The keys of
// the base commands (moves, jump, attack, interact, pause, focus mode, ghosts, quick restart)
// always win. Nothing here touches the simulation: the scene that plays binds the slot keys of
// the InputManager (PowerSlot1..8, the bits of InputFrame and of the replays) to the keys of
// the powers carried in those slots (applyPowerSlotKeys).

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "engine/input/InputTypes.h"

namespace cosmic {

class ContentDatabase;
class InputManager;
class Player;
struct LevelData;
struct PowerDefinition;

/// Where the key asked for a power comes from.
enum class PowerKeySource { None, Suggestion, Level, Player };

/// The key of one power in a level, for one player, on both devices.
struct ResolvedPowerKey {
    std::string power;
    // Keyboard / mouse.
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;        // what it is used with (UNKNOWN: none, « — »)
    SDL_Scancode wantedKey = SDL_SCANCODE_UNKNOWN;  // asked: the player's, else the level's, else the suggestion
    SDL_Scancode levelKey = SDL_SCANCODE_UNKNOWN;   // the level's, else the suggestion (what « Par défaut » gives back)
    PowerKeySource keySource = PowerKeySource::None;  // of wantedKey
    PowerKeySource levelKeySource = PowerKeySource::None;  // of levelKey (Level or Suggestion)
    std::string keyCommand;  // the base command that has wantedKey (it wins: `key` is none), "" if none
    std::string keyPower;    // the power that had wantedKey first (this one got a free key), "" if none
    // Gamepad.
    PadBinding pad, wantedPad, levelPad;
    PowerKeySource padSource = PowerKeySource::None;
    PowerKeySource levelPadSource = PowerKeySource::None;
    std::string padCommand, padPower;

    bool keyConflict() const { return !keyCommand.empty() || !keyPower.empty(); }
    bool padConflict() const { return !padCommand.empty() || !padPower.empty(); }
};

/// The keys of the powers of a level (in its order).
struct PowerKeyMap {
    std::vector<ResolvedPowerKey> powers;
    const ResolvedPowerKey* find(const std::string& power) const;
};

/// Whether a power is used with a key of its own (not passive, not on a move of the character).
bool powerUsesKey(const PowerDefinition& def);

/// The powers of a level that use a key of their own, in its order: start powers, tablets
/// and stations (power_unlock), powers given by its events (givePower); each once. A power
/// this game does not know is left out.
std::vector<std::string> levelKeyPowers(const LevelData& level, const ContentDatabase& content);

/// The base command that uses `key` / `pad` in `base` (its French name), "" if none: moves,
/// jump, attack, interact, pause, focus mode, ghosts (with the arrows and the d-pad) and the
/// quick restart (R / Vue). The keys of the power slots never count.
std::string baseCommandOfKey(const InputBindings& base, SDL_Scancode key);
std::string baseCommandOfPad(const InputBindings& base, const PadBinding& pad);

/// The keys of `powers` (each once, in that order): the player's choice (`playerKeys`, may be
/// null), else the level's (`levelKeys`), else the power's suggestion. A key of a base command
/// of `base` stays with it (the power then has none). Of two powers asking for the same key,
/// the player's choice, then the level's, then the first one in the order keeps it; the other
/// one gets the first free key of a fixed list. Deterministic.
PowerKeyMap resolvePowerKeys(const std::vector<std::string>& powers, const ContentDatabase& content, const Json& levelKeys,
                             const Json* playerKeys, const InputBindings& base);

/// One entry of a `powerKeys` object: {"key": …, "pad": …} with what is given.
Json powerKeyJson(SDL_Scancode key, const PadBinding& pad);
/// What an entry of a `powerKeys` object gives (UNKNOWN / None for what it does not).
SDL_Scancode powerKeyEntryKey(const Json& keys, const std::string& power);
PadBinding powerKeyEntryPad(const Json& keys, const std::string& power);

/// The player's own keys, per level (saves/power_keys.json, versioned; a damaged file is put
/// aside and nothing is lost but its content).
class PowerKeyOverrides {
public:
    static constexpr int kVersion = 1;
    void load();
    bool save() const;
    /// Same, with another file (tests).
    void loadFile(const fs::path& file);
    bool saveFile(const fs::path& file) const;

    /// The player's keys for a level, shaped as LevelData::powerKeys ({} if none).
    const Json& level(const std::string& levelId) const;
    void setKey(const std::string& levelId, const std::string& power, SDL_Scancode key);
    void setPad(const std::string& levelId, const std::string& power, const PadBinding& pad);
    /// « Par défaut »: that power uses the level's key (and button) again.
    void reset(const std::string& levelId, const std::string& power);
    bool has(const std::string& levelId, const std::string& power) const;

    Json toJson() const;
    void fromJson(const Json& data);

private:
    Json& entry(const std::string& levelId, const std::string& power);
    void tidy(const std::string& levelId, const std::string& power);
    Json levels_ = Json::object();
};

/// The keys of the key powers of `level`, and of `extra` (powers given besides the level's:
/// trainings), for this player: `overrides` (may be null) for this level, the level's
/// powerKeys, the suggestions; see resolvePowerKeys.
PowerKeyMap levelPowerKeys(const LevelData& level, const ContentDatabase& content, const InputBindings& base,
                           const PowerKeyOverrides* overrides, const std::vector<std::string>& extra = {});
/// The key of one power in `level` for this player, as levelPowerKeys gives it (the power is
/// added after the level's own and `extra` when it is none of them). For a power that uses no
/// key of its own, the result has no key (see powerUsesKey).
ResolvedPowerKey levelPowerKey(const std::string& power, const LevelData& level, const ContentDatabase& content, const InputBindings& base,
                               const PowerKeyOverrides* overrides, const std::vector<std::string>& extra = {});

/// Binds the slot keys of `in` (PowerSlot1..8) to the keys of the powers `player` carries:
/// slot N gets the key of the power in slot N when it is used with its slot's key, nothing
/// otherwise (empty slot, passive power, power on a move). Returns the carried key powers
/// missing from `keys` (normally none: the caller resolves again with them as `extra`).
std::vector<std::string> applyPowerSlotKeys(InputManager& in, const Player& player, const PowerKeyMap& keys);

/// tools/levelgen (CosmicPlatformer --plan-power-keys): the keys written into a campaign
/// level, as a powerKeys object: each key power's suggestion, conflicts resolved as for a
/// level without its own choice (against the default commands).
Json planLevelPowerKeys(const LevelData& level, const ContentDatabase& content);

}  // namespace cosmic
