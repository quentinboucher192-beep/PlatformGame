#include "game/levels/DecorAtlas.h"

#include <cmath>

#include "engine/core/Log.h"
#include "engine/resources/ResourceManager.h"

namespace cosmic {

Rect DecorSprite::frameRect(float time, float speed) const {
    int f = 0;
    if (frames > 1 && fps > 0.0f) f = static_cast<int>(std::floor(time * fps * speed)) % frames;
    if (f < 0) f += frames;
    return {rect.x + rect.w * f, rect.y, rect.w, rect.h};
}

bool DecorAtlas::load(ResourceManager& res, const std::string& jsonPath) {
    sprites_.clear();
    order_.clear();
    texture_.reset();
    texturePath_.clear();
    return append(res, jsonPath);
}

bool DecorAtlas::append(ResourceManager& res, const std::string& jsonPath, bool quiet) {
    auto j = res.json(jsonPath);
    if (!j) {
        if (!quiet) Log::error("Decor atlas not found: {}", jsonPath);
        return false;
    }
    std::string texPath = jstr(*j, "texture");
    TexturePtr tex = res.texture(texPath);
    if (!texture_) {
        texture_ = tex;
        texturePath_ = texPath;
    }
    if (j->contains("sprites") && (*j)["sprites"].is_object()) {
        for (auto& [name, sj] : (*j)["sprites"].items()) {
            if (sprites_.count(name)) continue;
            DecorSprite s;
            s.name = name;
            s.rect = rectFromJson(sj.value("rect", Json()), {0, 0, 16, 16});
            s.pivot = vec2FromJson(sj.value("pivot", Json()), {s.rect.w * 0.5f, s.rect.h});
            s.frames = std::max(1, jget<int>(sj, "frames", 1));
            s.fps = jget<float>(sj, "fps", 0.0f);
            if (sj.contains("glow") && sj["glow"].is_string()) s.glow = colorFromJson(sj["glow"], Color::transparent());
            s.glowRadius = jget<float>(sj, "glowRadius", 0.0f);
            s.glowOffset = vec2FromJson(sj.value("glowOffset", Json()), {});
            s.texture = tex;
            s.atlas = jsonPath;
            s.texturePath = texPath;
            sprites_[name] = s;
            order_.push_back(name);
        }
    }
    return true;
}

const DecorSprite* DecorAtlas::find(const std::string& name) const {
    auto it = sprites_.find(name);
    return it == sprites_.end() ? nullptr : &it->second;
}

const std::vector<std::string>& DecorAtlas::builtinAtlases() {
    static const std::vector<std::string> a = {"sprites/decor/sylvanox_decor.json", "sprites/decor/pyralis_decor.json",
                                               "sprites/decor/glacior_decor.json"};
    return a;
}

DecorAtlas& DecorAtlas::shared(ResourceManager& res) {
    static DecorAtlas atlas;
    static std::string mountKey;
    std::string key;
    for (const auto& m : res.mounts()) key += m.string() + ";";
    if (key != mountKey) {
        mountKey = key;
        atlas.load(res, builtinAtlases().front());
        for (size_t i = 1; i < builtinAtlases().size(); ++i) atlas.append(res, builtinAtlases()[i], true);
    }
    return atlas;
}

}  // namespace cosmic
