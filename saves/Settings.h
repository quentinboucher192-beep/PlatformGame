#pragma once

#include <string>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Json.h"
#include "engine/input/InputTypes.h"
#include "saves/HudLayout.h"

namespace cosmic {

struct GraphicsSettings {
    bool fullscreen = false;
    bool vsync = true;
    int windowWidth = 1600;
    int windowHeight = 900;
    bool dynamicLighting = true;
    int particleQuality = 2;  // 0 low, 1 medium, 2 high
    float screenShake = 1.0f;
    bool showFps = false;
};

struct GameplaySettings {
    bool showTimer = true;
    bool showInputHints = true;
    /// Progress bar of the level: 0 hidden, 1 at the top, 2 at the bottom, 3 where the
    /// player put it in the HUD editor.
    int progressBar = 1;
    /// Place and size of each HUD element (HUD editor).
    HudLayout hud;
    bool debugMode = false;  // debug overlay available in game (F3)
    bool editorAutosave = true;
    int editorAutosaveSeconds = 60;
    bool unlockAll = true;  // every planet and level playable without progression
};

/// Whether a HUD element is shown (the timer and the progress bar have their own settings).
bool hudVisible(const GameplaySettings& g, HudElement e);
void setHudVisible(GameplaySettings& g, HudElement e, bool visible);

/// Client-side preferences of Cosmic Online. Privacy settings that other players depend on
/// (profile / country / globe visibility, friend requests) live on the server, with the
/// account, and are edited from the same screen.
struct OnlineSettings {
    bool enabled = true;           // false: the game never contacts the server
    bool autoLogin = true;         // reconnect with the saved session at startup
    bool autoSync = true;          // send finished runs automatically when connected
    bool showPseudonyms = true;    // false: other players appear as "Joueur n°X" (streaming)
    int bandwidth = 1;             // 0 economical (fewer markers, no custom avatars), 1 normal
    bool onboardingDone = false;   // first-visit screen of Cosmic Online already shown
    std::string serverOverride;    // advanced: replaces the server of data/online/config.json
};

/// Every user preference (stored in saves/settings.json, versioned).
struct Settings {
    GraphicsSettings graphics;
    AudioVolumes audio;
    GameplaySettings gameplay;
    OnlineSettings online;
    InputBindings input = InputBindings::defaults();

    Json toJson() const;
    static Settings fromJson(const Json& j);
};

/// Loads / saves settings and controls with SaveFile versioning.
class SettingsStore {
public:
    static constexpr int kVersion = 2;
    static Settings load();
    static bool save(const Settings& s);
    /// Exposed for tests: migration v1 -> v2 (v1 stored volumes as 0-100 integers
    /// and had no gamepad settings block).
    static Json migrateV1ToV2(const Json& v1);
};

}  // namespace cosmic
