#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "engine/core/Scene.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class GameServices;

/// Options for starting a level.
struct PlayOptions {
    /// Play this level data directly (editor test) instead of loading `levelId`.
    std::optional<LevelData> level;
    bool editorTest = false;
    /// Asset root to mount (package levels being edited).
    std::string assetRoot;
    /// Start at this position (feet, world pixels) and skip the intro.
    std::optional<Vec2> spawn;
    /// Launched from ONLINE ("Jouer ce niveau"): leaving the level goes back to ONLINE on
    /// this level, never to the campaign; no "next level".
    bool fromOnline = false;
};

// Factories of every screen (implemented next to each scene).
std::unique_ptr<Scene> makeSplashScene(GameServices& g);
std::unique_ptr<Scene> makeMainMenu(GameServices& g);
std::unique_ptr<Scene> makePlanetMap(GameServices& g);
std::unique_ptr<Scene> makeLevelSelect(GameServices& g, const std::string& planetId);
std::unique_ptr<Scene> makeGameScene(GameServices& g, const std::string& levelId, PlayOptions opt = {});
std::unique_ptr<Scene> makeSettingsScene(GameServices& g, bool overlay);
std::unique_ptr<Scene> makeLevelEditor(GameServices& g, const std::string& levelId);
std::unique_ptr<Scene> makeEnemyEditor(GameServices& g);
/// The WORKSHOP (reached from ONLINE): the player's levels, imported levels, .cplanet
/// export/import, enemy editor.
/// `importFile`: a .cplanet file to analyse immediately (double-click / command line).
/// `tab`: 0 Mes niveaux, 1 Niveaux importés, 2 Exporter, 3 Importer, 4 Éditeur d'ennemis.
std::unique_ptr<Scene> makeContentManager(GameServices& g, const std::string& importFile = {}, int tab = 0);
// Cosmic Online
std::unique_ptr<Scene> makeOnlineRanking(GameServices& g, const std::string& levelId = {});
/// Account overlay: create an account or log in. `onboarding` shows the first-visit
/// explanation (what is shared, location consent). `required`: the ONLINE section needs
/// an account, so leaving without one returns to the main menu (the game itself never
/// needs an account).
std::unique_ptr<Scene> makeOnlineAccount(GameServices& g, bool onboarding, bool required = false);
/// `tab`: 0 Préférences, 1 Compte et confidentialité, 2 Amis.
std::unique_ptr<Scene> makeOnlineSettings(GameServices& g, int tab = 0);
/// Downloads and plays back the replay of a verified record.
std::unique_ptr<Scene> makeReplayViewer(GameServices& g, const std::string& recordId);

}  // namespace cosmic
