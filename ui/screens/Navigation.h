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
/// `importFile`: a .cplanet file to analyse immediately (double-click / command line).
std::unique_ptr<Scene> makeContentManager(GameServices& g, const std::string& importFile = {});

}  // namespace cosmic
