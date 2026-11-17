#pragma once
// 2.64: the tutorial of the first launch, « Le premier envol de Nova » (data/tutorial, made by
// tools/levelgen/tutorial.py): a small soft world outside the campaign (no planet on the map,
// no ranking, nothing sent online, no record, no achievement counted), played once. Bip, a small
// robot, floats by Nova; its explanation cards stop the run until the player has read them
// (« Compris, je continue »). The level asks for them with the event action « tutorial »
// (params.card: one of the ids below), each card once.

#include <optional>
#include <string>
#include <vector>

#include "engine/input/InputTypes.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class Progression;

namespace tutorial {

/// The level file, under the data folder.
inline constexpr const char* kLevelFile = "tutorial/premier_envol.json";
/// The souvenir title given at its end (data/cosmetics/cosmetics.json, `souvenir: "tutorial"`).
inline constexpr const char* kSouvenirTitle = "title_premier_envol";
/// The tutorial's boss: a slow data variant (`variantOf`) of the easiest boss of the game by
/// the bosses' difficulty rating (game/enemies/BossDifficulty.h).
inline constexpr const char* kBoss = "mycelia_tendre";

/// The small animated picture of a card.
enum class Preview { Walk, Jump, Bounce, Shards, Attack, Dash, Bubble, Lantern, Boss };

/// A key shown on a card: an action with the player's own binding (keyboard or gamepad, the
/// device in use), or a power with its key in the level being played.
struct KeyRow {
    std::string label;             // « Marcher », « Sauter »…
    std::vector<Action> actions;   // each drawn with its binding, separated by « / »
    std::string power;             // or this power's key in the level
};

struct Card {
    std::string id;
    std::string title;
    std::string text;
    std::vector<KeyRow> keys;
    Preview preview = Preview::Walk;
};

/// Every card of the tutorial, in the order the level shows them.
const std::vector<Card>& cards();
const Card* card(const std::string& id);
/// The cards a level asks for (event actions « tutorial »), each once, in the order of its
/// events: the « 3 / 9 » of a card.
std::vector<std::string> levelCards(const LevelData& level);

/// What the end card lists: what the player learnt.
const std::vector<std::string>& learnt();

/// Offered at launch: a new player (no progression written by a game of before), never once
/// started, skipped or finished.
bool due(const Progression& p);
/// The level, read from the data folder (nullopt with `error` when it cannot be read).
std::optional<LevelData> loadLevel(std::string* error = nullptr);

}  // namespace tutorial
}  // namespace cosmic
