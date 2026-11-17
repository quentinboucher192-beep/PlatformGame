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
    if (auto file = resolve(set->texturePath)) set->textureFile = Paths::toUtf8(*file);
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

Rect ResourceManager::visibleBox(const AnimationSet& set, const Rect& src) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (set.textureFile.empty() || set.frameWidth <= 0 || set.frameHeight <= 0) return {};
    auto it = visibleCells_.find(set.textureFile);  // read each frame: no string built for it
    if (it == visibleCells_.end() || it->second.frameW != set.frameWidth || it->second.frameH != set.frameHeight) {
        it = visibleCells_.insert_or_assign(set.textureFile, VisibleCells{}).first;
        VisibleCells& vc = it->second;
        vc.frameW = set.frameWidth;
        vc.frameH = set.frameHeight;
        SDL_Surface* loaded = IMG_Load(set.textureFile.c_str());
        SDL_Surface* s = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
        if (loaded) SDL_DestroySurface(loaded);
        if (s) {
            const int cols = s->w / set.frameWidth, rows = s->h / set.frameHeight;
            vc.cols = std::max(0, cols);
            vc.cells.assign(static_cast<size_t>(std::max(0, cols * rows)), Rect{});
            const auto* px = static_cast<const uint8_t*>(s->pixels);
            for (int cy = 0; cy < rows; ++cy)
                for (int cx = 0; cx < cols; ++cx) {
                    int x0 = set.frameWidth, y0 = set.frameHeight, x1 = -1, y1 = -1;
                    for (int y = 0; y < set.frameHeight; ++y) {
                        const uint8_t* row = px + static_cast<size_t>(cy * set.frameHeight + y) * static_cast<size_t>(s->pitch) +
                                             static_cast<size_t>(cx * set.frameWidth) * 4;
                        for (int x = 0; x < set.frameWidth; ++x)
                            if (row[x * 4 + 3] > 24) {  // seen (the faintest edges of the art do not count)
                                x0 = std::min(x0, x);
                                x1 = std::max(x1, x);
                                y0 = std::min(y0, y);
                                y1 = std::max(y1, y);
                            }
                    }
                    if (x1 >= x0 && y1 >= y0)
                        vc.cells[static_cast<size_t>(cy * cols + cx)] = {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1 - x0 + 1),
                                                                        static_cast<float>(y1 - y0 + 1)};
                }
            SDL_DestroySurface(s);
        }
    }
    const VisibleCells& vc = it->second;
    if (vc.cols <= 0 || src.x < 0.0f || src.y < 0.0f) return {};
    const int cx = static_cast<int>(src.x) / set.frameWidth, cy = static_cast<int>(src.y) / set.frameHeight;
    if (cx >= vc.cols) return {};
    const size_t i = static_cast<size_t>(cy * vc.cols + cx);
    return i < vc.cells.size() ? vc.cells[i] : Rect{};
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
    visibleCells_.clear();
}

}  // namespace cosmic
