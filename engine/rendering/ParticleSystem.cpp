#include "engine/rendering/ParticleSystem.h"

#include <cmath>

#include "engine/core/Log.h"
#include "engine/resources/ResourceManager.h"

namespace cosmic {

ParticlePreset ParticlePreset::fromJson(const std::string& name, const Json& j) {
    ParticlePreset p;
    p.name = name;
    p.texture = jstr(j, "texture");
    p.frames = std::max(1, jget<int>(j, "frames", 1));
    if (j.contains("count")) {
        Vec2 c = vec2FromJson(j["count"], {8, 12});
        p.countMin = static_cast<int>(c.x);
        p.countMax = static_cast<int>(c.y);
    }
    Vec2 life = vec2FromJson(j.value("life", Json()), {p.lifeMin, p.lifeMax});
    p.lifeMin = life.x;
    p.lifeMax = life.y;
    Vec2 speed = vec2FromJson(j.value("speed", Json()), {p.speedMin, p.speedMax});
    p.speedMin = speed.x;
    p.speedMax = speed.y;
    p.angle = jget<float>(j, "angle", p.angle);
    p.spread = jget<float>(j, "spread", p.spread);
    p.gravity = jget<float>(j, "gravity", 0.0f);
    p.drag = jget<float>(j, "drag", 0.0f);
    Vec2 size = vec2FromJson(j.value("size", Json()), {p.sizeStart, p.sizeEnd});
    p.sizeStart = size.x;
    p.sizeEnd = size.y;
    p.colorStart = colorFromJson(j.value("colorStart", Json()), p.colorStart);
    p.colorEnd = colorFromJson(j.value("colorEnd", Json()), p.colorStart.withAlpha(0));
    p.additive = jget<bool>(j, "additive", true);
    Vec2 spin = vec2FromJson(j.value("spin", Json()), {0, 0});
    p.spinMin = spin.x;
    p.spinMax = spin.y;
    p.stretch = jget<float>(j, "stretch", 0.0f);
    p.emitBox = vec2FromJson(j.value("emitBox", Json()), {});
    p.emitRadius = jget<float>(j, "emitRadius", 0.0f);
    p.front = jget<bool>(j, "front", true);
    return p;
}

Json ParticlePreset::toJson() const {
    Json j;
    if (!texture.empty()) j["texture"] = texture;
    if (frames > 1) j["frames"] = frames;
    j["count"] = {countMin, countMax};
    j["life"] = {lifeMin, lifeMax};
    j["speed"] = {speedMin, speedMax};
    j["angle"] = angle;
    j["spread"] = spread;
    j["gravity"] = gravity;
    j["drag"] = drag;
    j["size"] = {sizeStart, sizeEnd};
    j["colorStart"] = colorStart.toHex();
    j["colorEnd"] = colorEnd.toHex();
    j["additive"] = additive;
    j["spin"] = {spinMin, spinMax};
    j["stretch"] = stretch;
    j["emitBox"] = cosmic::toJson(emitBox);
    j["emitRadius"] = emitRadius;
    j["front"] = front;
    return j;
}

void ParticleSystem::init(ResourceManager* res) {
    res_ = res;
    particles_.reserve(1024);
}

bool ParticleSystem::loadPresets(const Json& j) {
    if (!j.is_object()) return false;
    for (auto& [name, pj] : j.items()) {
        if (!pj.is_object()) continue;
        presets_[name] = ParticlePreset::fromJson(name, pj);
        const std::string& tex = presets_[name].texture;
        if (!tex.empty() && res_ && !textures_.count(tex)) textures_[tex] = res_->texture(tex, false);
    }
    return true;
}

const ParticlePreset* ParticleSystem::preset(const std::string& name) const {
    auto it = presets_.find(name);
    return it == presets_.end() ? nullptr : &it->second;
}

void ParticleSystem::emit(const std::string& name, Vec2 pos, float scale, Vec2 dir, const Color* tint) {
    const ParticlePreset* p = preset(name);
    if (!p) {
        static std::unordered_map<std::string, bool> warned;
        if (!warned[name]) Log::warn("Unknown particle preset '{}'", name);
        warned[name] = true;
        return;
    }
    emit(*p, pos, scale, dir, tint);
}

void ParticleSystem::emit(const ParticlePreset& p, Vec2 pos, float scale, Vec2 dir, const Color* tint) {
    int count = static_cast<int>(std::round(rng_.rangeInt(p.countMin, std::max(p.countMin, p.countMax)) * quality_));
    float baseAngle = p.angle;
    if (dir.lengthSq() > 1e-6f) baseAngle = radToDeg(std::atan2(dir.y, dir.x));
    const Texture* tex = nullptr;
    if (!p.texture.empty()) {
        auto it = textures_.find(p.texture);
        if (it == textures_.end() && res_) it = textures_.emplace(p.texture, res_->texture(p.texture, false)).first;
        if (it != textures_.end() && it->second) tex = it->second.get();
    }
    for (int i = 0; i < count && particles_.size() < kMaxParticles; ++i) {
        Particle q;
        float a = degToRad(baseAngle + rng_.range(-p.spread * 0.5f, p.spread * 0.5f));
        float sp = rng_.range(p.speedMin, p.speedMax) * scale;
        q.vel = Vec2{std::cos(a), std::sin(a)} * sp;
        Vec2 off{rng_.range(-p.emitBox.x, p.emitBox.x), rng_.range(-p.emitBox.y, p.emitBox.y)};
        if (p.emitRadius > 0) off += Vec2::fromAngle(rng_.range(0, 2 * kPi)) * (p.emitRadius * std::sqrt(rng_.unit()));
        q.pos = pos + off * scale;
        q.maxLife = q.life = rng_.range(p.lifeMin, p.lifeMax);
        q.size0 = p.sizeStart * scale;
        q.size1 = p.sizeEnd * scale;
        q.c0 = tint ? Color{tint->r, tint->g, tint->b, p.colorStart.a} : p.colorStart;
        q.c1 = tint ? Color{tint->r, tint->g, tint->b, p.colorEnd.a} : p.colorEnd;
        q.rot = rng_.range(0, 360);
        q.spin = rng_.range(p.spinMin, p.spinMax);
        q.drag = p.drag;
        q.gravity = p.gravity;
        q.stretch = p.stretch;
        q.tex = tex;
        q.frames = p.frames;
        q.additive = p.additive;
        q.front = p.front;
        particles_.push_back(q);
    }
}

void ParticleSystem::update(float dt) {
    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            p = particles_.back();
            particles_.pop_back();
            continue;
        }
        p.vel.y += p.gravity * dt;
        if (p.drag > 0.0f) p.vel *= std::max(0.0f, 1.0f - p.drag * dt);
        p.pos += p.vel * dt;
        p.rot += p.spin * dt;
        ++i;
    }
}

void ParticleSystem::render(Renderer& r, bool front, const Rect& visible) {
    const Texture* glow = r.glowTexture().get();
    Rect vis = visible.expanded(32);
    for (const Particle& p : particles_) {
        if (p.front != front || !vis.contains(p.pos)) continue;
        float t = 1.0f - p.life / p.maxLife;
        float size = lerpf(p.size0, p.size1, t);
        if (size <= 0.05f) continue;
        Color c = Color::lerp(p.c0, p.c1, t);
        // Quick fade-in on the first 10% to avoid popping.
        if (t < 0.1f) c.a = static_cast<uint8_t>(c.a * (t / 0.1f));
        const Texture* tex = p.tex ? p.tex : glow;
        BlendMode bm = p.additive ? BlendMode::Additive : BlendMode::Alpha;
        float w = size, h = size;
        float angle = p.rot;
        if (p.stretch > 0.0f) {
            float sp = p.vel.length();
            w = size + sp * p.stretch;
            angle = radToDeg(std::atan2(p.vel.y, p.vel.x));
        }
        Rect src;
        const Rect* srcPtr = nullptr;
        if (p.tex && p.frames > 1) {
            int fw = p.tex->width() / p.frames;
            int f = std::min(p.frames - 1, static_cast<int>(t * p.frames));
            src = {static_cast<float>(f * fw), 0, static_cast<float>(fw), static_cast<float>(p.tex->height())};
            srcPtr = &src;
        }
        r.drawWorldTexture(*tex, srcPtr, {p.pos.x - w * 0.5f, p.pos.y - h * 0.5f, w, h}, c, bm, false, angle);
    }
}

}  // namespace cosmic
