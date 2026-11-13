#pragma once

#include <filesystem>
#include <string>

namespace cosmic {

namespace fs = std::filesystem;

/// Locates the installation folders (assets/, data/) and the per-user writable folder.
/// Nothing is ever written next to the executable: the installed game lives in
/// Program Files and every save / user creation goes to the user folder.
class Paths {
public:
    /// Must be called once at startup. `userDirOverride` (or the COSMIC_USER_DIR
    /// environment variable) replaces the default per-user folder (used by tests).
    static void init(const std::string& userDirOverride = {});

    static const fs::path& installDir();  // folder containing assets/ and data/
    static fs::path assetsDir() { return installDir() / "assets"; }
    static fs::path dataDir() { return installDir() / "data"; }

    static const fs::path& userDir();  // %APPDATA%/CosmicStudio/CosmicPlatformer on Windows
    static fs::path savesDir() { return userDir() / "saves"; }
    static fs::path userLevelsDir() { return userDir() / "levels"; }
    static fs::path userEnemiesDir() { return userDir() / "enemies"; }
    static fs::path packagesDir() { return userDir() / "packages"; }
    static fs::path exportsDir() { return userDir() / "exports"; }
    static fs::path screenshotsDir() { return userDir() / "screenshots"; }
    static fs::path autosaveDir() { return userDir() / "levels" / "autosave"; }
    static fs::path logFile() { return userDir() / "cosmic.log"; }

    /// True if `rel` is a safe relative path: not absolute, no drive letter, no "..",
    /// no empty components, no control characters. Used everywhere user / package data
    /// provides a path.
    static bool isSafeRelative(const std::string& rel);

    /// Converts to forward slashes and removes "./" components.
    static std::string normalizeSlashes(const std::string& p);

    /// UTF-8 string of a path (portable).
    static std::string toUtf8(const fs::path& p);
    static fs::path fromUtf8(const std::string& s);
};

}  // namespace cosmic
