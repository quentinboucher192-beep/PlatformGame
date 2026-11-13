#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/core/Random.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

class ResourceManager;

/// Data-driven particle effect description (data/effects/*.json).
struct ParticlePreset {
    std::string name;
    std::string texture;     // relative path; empty = built-in soft glow
    int frames = 1;          // horizontal animation frames in the texture
    int countMin = 8, countMax = 12;
    float lifeMin = 0.4f, lifeMax = 0.8f;
    float speedMin = 20.0f, speedMax = 60.0f;
    float angle = -90.0f;    // degrees, 0 = right, -90 = up
    float spread = 360.0f;   // degrees
    float gravity = 0.0f;
    float drag = 0.0f;
    float sizeStart = 4.0f, sizeEnd = 0.0f;
    Color colorStart = Color::white();
    Color colorEnd = Color::white().withAlpha(0);
    bool additive = true;
    float spinMin = 0.0f, spinMax = 0.0f;
    float stretch = 0.0f;    // velocity-aligned elongation factor
    Vec2 emitBox;            // random offset half-extents
    float emitRadius = 0.0f;
    bool front = true;       // drawn in front of entities

    static ParticlePreset fromJson(const std::string& name, const Json& j);
    Json toJson() const;
};

/// Pooled CPU particles rendered with the 2D renderer.
class ParticleSystem {
public:
    static constexpr size_t kMaxParticles = 6000;

    void init(ResourceManager* res);
    bool loadPresets(const Json& j);  // { "name": {preset}, ... }
    const ParticlePreset* preset(const std::string& name) const;
    const std::unordered_map<std::string, ParticlePreset>& presets() const { return presets_; }

    /// Emits a preset at `pos`. `dir` (optional, non-zero) rotates the emission angle.
    void emit(const std::string& name, Vec2 pos, float scale = 1.0f, Vec2 dir = {}, const Color* tint = nullptr);
    void emit(const ParticlePreset& p, Vec2 pos, float scale = 1.0f, Vec2 dir = {}, const Color* tint = nullptr);
    void update(float dt);
    void render(Renderer& r, bool front, const Rect& visible);
    void clear() { particles_.clear(); }
    size_t count() const { return particles_.size(); }
    void setQuality(float q) { quality_ = std::clamp(q, 0.1f, 1.5f); }

private:
    struct Particle {
        Vec2 pos, vel;
        float life = 0, maxLife = 1;
        float size0 = 1, size1 = 0;
        Color c0, c1;
        float rot = 0, spin = 0, drag = 0, gravity = 0, stretch = 0;
        const Texture* tex = nullptr;
        int frames = 1;
        bool additive = true;
        bool front = true;
    };
    ResourceManager* res_ = nullptr;
    std::unordered_map<std::string, ParticlePreset> presets_;
    std::unordered_map<std::string, TexturePtr> textures_;
    std::vector<Particle> particles_;
    Random rng_{0x5EED};
    float quality_ = 1.0f;
};

}  // namespace cosmic
