#pragma once

#include "engine/audio/AudioSystem.h"
#include "engine/core/Json.h"
#include "engine/input/InputTypes.h"

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
    bool debugMode = false;  // debug overlay available in game (F3)
    bool editorAutosave = true;
    int editorAutosaveSeconds = 60;
};

/// Every user preference (stored in saves/settings.json, versioned).
struct Settings {
    GraphicsSettings graphics;
    AudioVolumes audio;
    GameplaySettings gameplay;
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
