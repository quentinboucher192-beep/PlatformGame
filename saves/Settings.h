#pragma once

#include <string>

#include "engine/audio/AudioSystem.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
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
    /// « Mode concentré »: only the elements chosen for it (HUD editor) are shown; toggled in
    /// game (T / Partage, pause menu). Display only.
    bool focusMode = false;
    /// 2.51: « Invincible » in training runs (administrators only: boss training, section
    /// training, practice runs; never a run that counts nor a challenge).
    bool adminInvincible = false;
    /// 2.60: « Touches de ce niveau »: a change of keys also goes to the levels whose keys were
    /// exactly the same (same powers, same keys and buttons asked for).
    bool powerKeysShared = true;
    /// The level's messages (hints, signs, power and boss titles), in each mode.
    bool showMessages = true;
    bool focusMessages = false;
    /// 2.52: arrows on the screen's border towards the enemies already met that are out of
    /// view (one per pack, with their number). Display only.
    bool enemyArrows = true;
    /// 2.60: « Effacer le HUD quand il cache l'action »: a block of the HUD fades (to about
    /// 28 %) while the boss, Nova or an attack of the boss is under it. Display only.
    bool fadeHud = true;
    bool debugMode = false;  // debug overlay available in game (F3)
    bool editorAutosave = true;
    int editorAutosaveSeconds = 60;
    bool unlockAll = true;  // every planet and level playable without progression
    /// 2.54: « Enregistrer mes statistiques » (main menu → Statistiques; kept on this computer).
    bool recordStats = true;
    /// « Histoire des giga-boss » (2.51): 0 told before the first fight against each giga
    /// boss, 1 never (game/content/BossStories.h, StorySetting). Display only.
    int bossStories = 0;
    /// 2.63: the trailer plays on the main menu after a minute without any input (attract mode).
    bool attractTrailer = true;
};

/// Whether a HUD element is shown (the timer and the progress bar have their own settings;
/// in focus mode, only the elements chosen for it).
bool hudVisible(const GameplaySettings& g, HudElement e);
/// Whether the level's messages are shown (in the mode in use).
inline bool messagesVisible(const GameplaySettings& g) { return g.focusMode ? g.focusMessages : g.showMessages; }
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

/// Ghost Racing: ghosts shown during a run (display only, never part of the simulation).
struct GhostSettings {
    bool enabled = true;           // ghosts and gaps shown (quick toggle in game)
    bool personalByDefault = true; // race against my best time when nothing else is chosen
    int maxGhosts = 3;             // 1..8 at once
    int opacity = 1;               // 0 discreet, 1 normal, 2 strong
    bool showNames = true;         // name and reference time above each ghost
    bool showPanel = true;         // segments panel (splits, gaps, best segments)
    bool showDelta = true;         // live gap under the timer
    int cacheMB = 64;              // downloaded replays kept (favourites never removed)
    /// Colour of each kind of ghost: mine, world record, friends, others (palette index).
    int colorMine = 0, colorWorld = 1, colorFriend = 2, colorOther = 3;
};

/// Palette of ghost colours (settings store the index).
constexpr int kGhostPaletteSize = 8;
Color ghostPaletteColor(int index);
const char* ghostPaletteName(int index);
float ghostOpacity(int level);

/// Every user preference (stored in saves/settings.json, versioned).
struct Settings {
    /// Language of the texts ("fr", "en"; empty: never chosen, French unless the installer
    /// says otherwise). Display only.
    std::string language;
    /// Last choice of the Windows installer applied (language.ini), so that a reinstall in
    /// another language switches the game to it, and only then.
    std::string installerLanguage;
    GraphicsSettings graphics;
    AudioVolumes audio;
    GameplaySettings gameplay;
    OnlineSettings online;
    GhostSettings ghosts;
    InputBindings input = InputBindings::defaults();

    Json toJson() const;
    static Settings fromJson(const Json& j);
};

/// The language of `s` (French when none was chosen).
Language settingsLanguage(const Settings& s);
/// The language chosen in the Windows installer (`iniFile`: language.ini next to the game,
/// « Language=english »), applied when it is new: first launch, or the game reinstalled in
/// another language. Returns true when `s` changed (to be saved).
bool applyInstallerLanguage(Settings& s, const fs::path& iniFile);

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
