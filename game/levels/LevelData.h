#pragma once

#include <algorithm>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/rendering/Parallax.h"

namespace cosmic {

/// An object placed in a level (platform, enemy, decor, trigger...). Its behaviour is
/// defined by `type` (see ObjectTypes) and its `props`.
struct LevelObject {
    std::string id;
    std::string type;
    std::string layer;
    Rect rect;
    float rotation = 0.0f;  // degrees
    bool flipX = false;
    int z = 0;
    Json props = Json::object();

    template <typename T>
    T prop(const char* key, const T& def) const {
        return jget<T>(props, key, def);
    }
    std::string propStr(const char* key, const std::string& def = {}) const { return jstr(props, key, def); }

    Json toJson() const;
    static LevelObject fromJson(const Json& j);
};

struct LevelLayer {
    std::string id;
    std::string name;
    int order = 0;
    bool visible = true;
    bool locked = false;
    bool foreground = false;  // drawn over the player
    Color tint = Color::white();

    Json toJson() const;
    static LevelLayer fromJson(const Json& j);
};

enum class TimerMode { Limited, Unlimited, TimeAttack, Challenge };
const char* timerModeId(TimerMode m);
const char* timerModeLabel(TimerMode m);
TimerMode timerModeFromId(const std::string& s);

/// What happens when a limited timer reaches zero.
enum class ExpireAction { GameOver, RestartCheckpoint, Continue };
const char* expireActionId(ExpireAction a);
ExpireAction expireActionFromId(const std::string& s);

enum class Medal { None = 0, Bronze, Silver, Gold, Platinum };
const char* medalName(Medal m);

/// Condition for a medal tier. Unset constraints are ignored.
struct MedalRule {
    float maxTime = 0.0f;     // seconds, 0 = no time constraint
    int maxDeaths = -1;       // -1 = no constraint
    int minCollectiblesPercent = 0;
    Json toJson() const;
    static MedalRule fromJson(const Json& j);
};

struct EventAction {
    std::string type;    // open, close, toggle, enable, disable, spawnEnemy, showText, ...
    std::string target;  // object id (or comma-separated ids)
    Json params = Json::object();
    Json toJson() const;
    static EventAction fromJson(const Json& j);
};

/// "When <source> <trigger> then <actions>".
struct LevelEvent {
    std::string id;
    std::string name;
    std::string source;   // object id, power id, "level" ...
    std::string trigger;  // enter, exit, activate, cleared, killed, levelStart, time, powerUsed, collected, bossPhase, checkpoint
    Json params = Json::object();  // e.g. {"seconds": 30} or {"phase": 2}
    bool once = true;
    float delay = 0.0f;
    std::vector<EventAction> actions;
    Json toJson() const;
    static LevelEvent fromJson(const Json& j);
};

struct CinematicStep {
    std::string type;  // camera, zoom, wait, text, dialogue, spawnPlayer, playerAnim, playerWalk, shake, flash, fade, sound, music, letterbox, focus
    float duration = 1.0f;
    bool wait = true;  // block until finished
    Json params = Json::object();
    Json toJson() const;
    static CinematicStep fromJson(const Json& j);
};

struct CinematicDef {
    std::vector<CinematicStep> steps;
    bool skippable = true;
    bool letterbox = true;
    bool empty() const { return steps.empty(); }
    Json toJson() const;
    static CinematicDef fromJson(const Json& j);
};

/// Complete description of a level (data/levels/<id>.json or user / package levels).
struct LevelData {
    static constexpr int kFormatVersion = 1;

    std::string id;
    std::string name;
    std::string description;
    std::string planet;
    std::string author;
    std::string version = "1.0";
    int difficulty = 1;  // 1..5

    std::string music;
    std::string bossMusic;
    std::string ambience;

    Rect bounds{0, 0, 3200, 1200};
    float killY = 0.0f;  // 0 = bounds bottom + 64
    Color ambientLight{110, 120, 160, 255};
    std::vector<ParallaxLayerDef> background;
    Color backgroundColor{11, 15, 30, 255};

    TimerMode timerMode = TimerMode::Limited;
    float timeLimit = 600.0f;
    ExpireAction onExpire = ExpireAction::GameOver;
    float referenceTime = 180.0f;  // developer reference time ("par")
    int challengeMaxDeaths = 0;    // challenge mode: deaths allowed
    /// Damage the player receives is multiplied by this (1 = normal), shown before playing.
    float damageFactor = 1.0f;
    /// Lives for the whole level (0 = unlimited): the run ends when they are all lost.
    int lives = 0;
    /// Lives the player really has: `lives`, or what the challenge mode allows (0 = unlimited).
    int livesAllowed() const { return lives > 0 ? lives : (timerMode == TimerMode::Challenge ? challengeMaxDeaths + 1 : 0); }
    std::array<MedalRule, 4> medals;  // bronze, silver, gold, platinum

    std::vector<std::string> startPowers;
    /// Powers the player can carry at once (0 = no limit). Picking up a tablet with every
    /// slot taken asks to replace one of them or to leave the new power where it is.
    static constexpr int kDefaultMaxPowers = 4;
    static constexpr int kMaxPowerSlots = 8;
    int maxPowers = kDefaultMaxPowers;
    /// Slots actually available: never fewer than the start powers (older levels).
    int powerSlots() const {
        if (maxPowers <= 0) return 0;
        return std::max(maxPowers, static_cast<int>(startPowers.size()));
    }
    float cameraZoom = 1.0f;

    std::vector<LevelLayer> layers;
    std::vector<LevelObject> objects;
    std::vector<LevelEvent> events;
    CinematicDef intro;
    CinematicDef outro;
    Json metadata = Json::object();  // free-form (editor info, thumbnails...)

    // ---------------------------------------------------------------- helpers
    LevelObject* findObject(const std::string& objId);
    const LevelObject* findObject(const std::string& objId) const;
    const LevelLayer* findLayer(const std::string& layerId) const;
    std::string nextObjectId() const;
    std::string nextEventId() const;
    int countObjects(const std::string& type) const;
    void ensureDefaultLayers();
    void sortLayers();
    float effectiveKillY() const { return killY != 0.0f ? killY : bounds.bottom() + 64.0f; }

    Json toJson() const;
    static std::optional<LevelData> fromJson(const Json& j, std::string* error = nullptr);
    static LevelData makeEmpty(const std::string& id, const std::string& name, const std::string& planet);
};

}  // namespace cosmic
