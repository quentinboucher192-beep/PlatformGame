#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

class AudioSystem;

/// Loads and caches textures, animations, sounds and JSON from a stack of mount points.
/// Relative asset paths ("sprites/player/nova.png") are resolved against the mounts from
/// the most recent to the oldest; the base game assets folder is always the last mount.
/// Imported level packages push their own assets folder so a level always finds the
/// exact files it was exported with.
class ResourceManager {
public:
    void init(Renderer* renderer, AudioSystem* audio);
    void shutdown();

    void pushMount(const fs::path& root);
    void popMount();
    const std::vector<fs::path>& mounts() const { return mounts_; }

    std::optional<fs::path> resolve(const std::string& rel) const;
    bool exists(const std::string& rel) const { return resolve(rel).has_value(); }

    /// Texture (pixel-art filtering by default). Missing files yield a visible
    /// checkerboard so problems are obvious, and an error is logged.
    TexturePtr texture(const std::string& rel, bool pixelArt = true);
    AnimationSetPtr animation(const std::string& rel);
    /// Drops a cached animation so the next request reloads it (editor changes).
    void invalidateAnimation(const std::string& rel);
    std::optional<Json> json(const std::string& rel);
    /// Absolute path of a sound to hand to AudioSystem (empty if missing).
    std::string soundPath(const std::string& rel);
    void playSound(const std::string& rel, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f);
    /// Sustained looping effect, see AudioSystem::sustainLoop().
    void sustainLoop(const std::string& key, const std::string& rel, float volume);

    void clearCaches();
    size_t textureCount() const { return textures_.size(); }

private:
    TexturePtr missingTexture();

    Renderer* renderer_ = nullptr;
    AudioSystem* audio_ = nullptr;
    std::vector<fs::path> mounts_;
    std::unordered_map<std::string, TexturePtr> textures_;
    std::unordered_map<std::string, AnimationSetPtr> animations_;
    std::unordered_map<std::string, std::string> soundPaths_;
    TexturePtr missing_;
};

}  // namespace cosmic
