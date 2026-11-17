#include "engine/resources/ResourceManager.h"

#include <SDL3_image/SDL_image.h>

#include <iterator>
#include <system_error>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"

namespace cosmic {

void ResourceManager::init(Renderer* renderer, AudioSystem* audio) {
    renderer_ = renderer;
    audio_ = audio;
    owner_ = std::this_thread::get_id();
    // Installed (native) assets always win; the user folder and packages only add new files.
    mounts_ = {Paths::assetsDir(), Paths::userDir() / "assets"};
}

void ResourceManager::shutdown() {
    clearCaches();
    missing_.reset();
}

void ResourceManager::pushMount(const fs::path& root) { mounts_.push_back(root); }

void ResourceManager::popMount() {
    if (mounts_.size() > 2) mounts_.pop_back();
}

void ResourceManager::invalidateAnimation(const std::string& rel) {
    if (auto p = resolve(rel)) animations_.erase(Paths::toUtf8(*p));
}

std::optional<fs::path> ResourceManager::resolve(const std::string& rel) const {
    if (rel.empty() || !Paths::isSafeRelative(rel)) return std::nullopt;
    fs::path r = Paths::fromUtf8(Paths::normalizeSlashes(rel));
    std::error_code ec;
    // Native assets cannot be replaced: the installation folder is searched first, then the
    // most recently mounted roots (package being played, then the user folder).
    if (!mounts_.empty()) {
        fs::path p = mounts_.front() / r;
        if (fs::is_regular_file(p, ec)) return p;
    }
    for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
        if (it == std::prev(mounts_.rend())) break;
        fs::path p = *it / r;
        if (fs::is_regular_file(p, ec)) return p;
    }
    return std::nullopt;
}

TexturePtr ResourceManager::missingTexture() {
    if (missing_ || !renderer_) return missing_;
    SDL_Surface* s = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            uint8_t* p = static_cast<uint8_t*>(s->pixels) + y * s->pitch + x * 4;
            bool on = ((x / 4) + (y / 4)) % 2 == 0;
            p[0] = on ? 255 : 30;
            p[1] = 0;
            p[2] = on ? 255 : 30;
            p[3] = 255;
        }
    missing_ = renderer_->textureFromSurface(s, "<missing>", true);
    SDL_DestroySurface(s);
    return missing_;
}

TexturePtr ResourceManager::texture(const std::string& rel, bool pixelArt) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = resolve(rel);
    if (!path) {
        static std::unordered_map<std::string, bool> warned;
        if (!warned[rel]) {
            Log::error("Texture not found: {}", rel);
            warned[rel] = true;
        }
        return missingTexture();
    }
    std::string key = Paths::toUtf8(*path) + (pixelArt ? "|p" : "|l");
    auto it = textures_.find(key);
    if (it != textures_.end()) return it->second;
    if (!renderer_ || offThread()) return nullptr;  // 2.62: created by the owner's thread only
    SDL_Surface* s = IMG_Load(Paths::toUtf8(*path).c_str());
    if (!s) {
        Log::error("IMG_Load({}) failed: {}", rel, SDL_GetError());
        return missingTexture();
    }
    TexturePtr t = renderer_->textureFromSurface(s, rel, pixelArt);
    SDL_DestroySurface(s);
    if (!t) return missingTexture();
    textures_[key] = t;
    return t;
}

AnimationSetPtr ResourceManager::animation(const std::string& rel) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = resolve(rel);
    if (!path) {
        Log::error("Animation not found: {}", rel);
        return nullptr;
    }
    std::string key = Paths::toUtf8(*path);
    auto it = animations_.find(key);
    if (it != animations_.end()) return it->second;
    auto j = FileUtil::loadJson(*path);
    if (!j) return nullptr;
    auto set = AnimationSet::fromJson(*j, rel);
    set->texture = texture(set->texturePath);
    if (!set->texture && offThread()) waitingTexture_.push_back(set);  // 2.62: finishDeferred()
    animations_[key] = set;
    return set;
}

void ResourceManager::finishDeferred() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (offThread()) return;
    for (auto& set : waitingTexture_)
        if (set && !set->texture) set->texture = texture(set->texturePath);
    waitingTexture_.clear();
}

std::optional<Json> ResourceManager::json(const std::string& rel) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = resolve(rel);
    if (!path) return std::nullopt;
    return FileUtil::loadJson(*path);
}

std::string ResourceManager::soundPath(const std::string& rel) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string key;
    for (const auto& m : mounts_) key += Paths::toUtf8(m) + ";";
    key += rel;
    auto it = soundPaths_.find(key);
    if (it != soundPaths_.end()) return it->second;
    auto p = resolve(rel);
    std::string out = p ? Paths::toUtf8(*p) : std::string();
    if (!p) Log::warn("Sound not found: {}", rel);
    soundPaths_[key] = out;
    return out;
}

std::string ResourceManager::relativeSoundPath(const std::string& absolute) const {
    if (absolute.empty()) return {};
    for (const auto& [key, abs] : soundPaths_)
        if (abs == absolute) {
            size_t semi = key.rfind(';');
            return semi == std::string::npos ? key : key.substr(semi + 1);
        }
    return {};
}

void ResourceManager::playSound(const std::string& rel, float volume, float pitch, float pan) {
    if (!audio_ || rel.empty()) return;
    std::string p = soundPath(rel);
    if (!p.empty()) audio_->playSound(p, volume, pitch, pan);
}

void ResourceManager::sustainLoop(const std::string& key, const std::string& rel, float volume) {
    if (!audio_ || rel.empty()) return;
    std::string p = soundPath(rel);
    if (!p.empty()) audio_->sustainLoop(key, p, volume);
}

void ResourceManager::clearCaches() {
    textures_.clear();
    animations_.clear();
    soundPaths_.clear();
}

}  // namespace cosmic
