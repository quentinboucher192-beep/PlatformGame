#include "ui/screens/CreationImageFx.h"

#include <algorithm>
#include <cmath>

#include "engine/rendering/Renderer.h"
#include "ui/screens/CosmeticFx.h"

namespace cosmic {

namespace {

constexpr float kImagePi = 3.14159265f;

/// A number in [0, 1) from a seed (the same every frame: nothing random is kept).
float noise(uint32_t s) {
    s ^= s >> 16;
    s *= 0x7feb352dU;
    s ^= s >> 15;
    s *= 0x846ca68bU;
    s ^= s >> 16;
    return static_cast<float>(s & 0xFFFFFF) / 16777216.0f;
}

Color tintAt(Rgb8 a, Rgb8 b, float k, float alpha) {
    auto l = [k](uint8_t p, uint8_t q) { return static_cast<uint8_t>(std::clamp(p + (q - p) * k, 0.0f, 255.0f)); };
    return {l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)};
}

float fadeOf(const ImageEmitter& e, float k) {
    if (e.fade == 2) return 1.0f - k;
    if (e.fade == 1) return k < 0.7f ? 1.0f : (1.0f - k) / 0.3f;
    return 1.0f;
}

/// One particle at `p` (world or screen), `k` of its life gone.
void particle(Renderer& r, const ImageSprite& s, const ImageEmitter& e, Rgb8 a, Rgb8 b, Vec2 p, float k, float angleDeg, float pxScale, bool world,
              float alpha, uint32_t seed) {
    if (!s.texture || s.frames <= 0) return;
    const int frame = std::clamp(static_cast<int>(k * static_cast<float>(s.frames)), 0, s.frames - 1);
    const Rect src{static_cast<float>(frame * s.frameW), 0.0f, static_cast<float>(s.frameW), static_cast<float>(s.frameH)};
    const float w = static_cast<float>(s.frameW) * static_cast<float>(e.size) / 100.0f * pxScale;
    const float h = static_cast<float>(s.frameH) * static_cast<float>(e.size) / 100.0f * pxScale;
    const Color tint = tintAt(a, b, k, alpha * fadeOf(e, k));
    if (tint.a == 0) return;
    const Rect dst{p.x - w * 0.5f, p.y - h * 0.5f, w, h};
    if (world) r.drawWorldTexture(*s.texture, &src, dst, tint, BlendMode::Alpha, false, angleDeg);
    else r.drawTexture(*s.texture, &src, dst, tint, BlendMode::Alpha, false, false, angleDeg);
    // Sparkles: now and then a small star of light near the particle.
    if (e.sparkles && seed % 4 == 0 && k < 0.85f) {
        const Vec2 q{p.x + (noise(seed * 11u + 3u) - 0.5f) * w * 1.6f, p.y + (noise(seed * 13u + 7u) - 0.5f) * h * 1.6f};
        const float twinkle = 0.5f + 0.5f * std::sin(k * 18.0f + static_cast<float>(seed));
        const Color c = tintAt(a, b, 0.5f, alpha * fadeOf(e, k) * twinkle);
        const float rad = std::max(1.0f, 2.2f * pxScale * static_cast<float>(e.size) / 100.0f);
        if (world) r.drawWorldGlow(q, rad * 2.5f, c, BlendMode::Additive);
        else r.drawGlow(q, rad * 2.5f, c, BlendMode::Additive);
    }
}

}  // namespace

ImageSprite spriteOf(const CreationImageDraw& d) {
    ImageSprite s;
    s.texture = d.texture.get();
    s.frameW = d.info.frameW;
    s.frameH = d.info.frameH;
    s.frames = d.info.frames;
    s.fps = d.info.fps;
    return s;
}

void drawImageTrail(Renderer& r, const ImageSprite& s, const ImageEmitter& e, Rgb8 a, Rgb8 b, const std::vector<TrailSample>& samples, bool world,
                    float scale, float fade) {
    if (!s.texture || samples.size() < 2) return;
    const float life = static_cast<float>(e.lifeMs) / 1000.0f;
    const float k = world ? 1.0f : scale;
    for (size_t i = 0; i + 1 < samples.size(); ++i) {
        const TrailSample& sm = samples[i];
        if (sm.age > life) continue;
        // Its particles: `rate` per second of samples (60 a second while Nova moves).
        const uint32_t n = static_cast<uint32_t>(std::floor((sm.seed + 1.0) * e.rate / 60.0) - std::floor(static_cast<double>(sm.seed) * e.rate / 60.0));
        if (n == 0) continue;
        // Thrown backwards (away from where Nova goes), spread around it.
        const Vec2 dir = samples[i + 1].p - sm.p;
        const float base = dir.lengthSq() > 0.01f ? std::atan2(-dir.y, -dir.x) : kImagePi;
        for (uint32_t j = 0; j < n; ++j) {
            const uint32_t seed = sm.seed * 7919u + j * 104729u + 17u;
            const float ang = base + (noise(seed) - 0.5f) * static_cast<float>(e.spread) * kImagePi / 180.0f;
            const float speed = static_cast<float>(e.speed) * (0.55f + 0.45f * noise(seed * 3u + 1u));
            const float t = sm.age;
            const Vec2 p{sm.p.x + std::cos(ang) * speed * t * k, sm.p.y + (std::sin(ang) * speed * t + 0.5f * static_cast<float>(e.gravity) * t * t) * k};
            const float angle = (e.spin != 0 ? noise(seed * 5u + 2u) * 360.0f : 0.0f) + static_cast<float>(e.spin) * t;
            particle(r, s, e, a, b, p, t / life, angle, k, world, fade, seed);
        }
    }
}

void drawImageVictory(Renderer& r, const ImageSprite& s, const ImageEmitter& e, Rgb8 a, Rgb8 b, Vec2 at, float t, bool world, float scale) {
    if (!s.texture || t < 0.0f || t > kVictoryFxTime) return;
    const float life = static_cast<float>(e.lifeMs) / 1000.0f;
    const float k = world ? 1.0f : scale;
    const float emitFor = std::max(0.2f, kVictoryFxTime - life - 0.2f);
    const int count = std::max(1, static_cast<int>(static_cast<float>(e.rate) * emitFor));
    const float end = std::clamp((kVictoryFxTime - t) / 0.6f, 0.0f, 1.0f);
    // From Nova's middle, upwards, spread around; a burst at once then the rest at `rate`.
    const Vec2 origin{at.x, at.y - 28.0f * k};
    const int burst = std::min(count, std::max(1, e.rate / 5));
    for (int j = 0; j < count; ++j) {
        const uint32_t seed = static_cast<uint32_t>(j) * 2654435761u + 99u;
        const float born = j < burst ? 0.0f : static_cast<float>(j - burst) / static_cast<float>(std::max(1, e.rate));
        const float age = t - born;
        if (age < 0.0f || age > life) continue;
        const float ang = -kImagePi * 0.5f + (noise(seed) - 0.5f) * static_cast<float>(e.spread) * kImagePi / 180.0f;
        const float speed = static_cast<float>(e.speed) * (0.6f + 0.4f * noise(seed * 3u + 1u)) * (j < burst ? 1.5f : 1.0f);
        const Vec2 p{origin.x + std::cos(ang) * speed * age * k, origin.y + (std::sin(ang) * speed * age + 0.5f * static_cast<float>(e.gravity) * age * age) * k};
        const float angle = (e.spin != 0 ? noise(seed * 5u + 2u) * 360.0f : 0.0f) + static_cast<float>(e.spin) * age;
        particle(r, s, e, a, b, p, age / life, angle, k, world, end, seed);
    }
}

void drawImageFrame(Renderer& r, const ImageSprite& s, Vec2 center, float radius, float scale, float time) {
    if (!s.texture || s.frames <= 0) return;
    const int frame = s.frames > 1 ? static_cast<int>(std::floor(time * static_cast<float>(std::max(1, s.fps)))) % s.frames : 0;
    const Rect src{static_cast<float>(frame * s.frameW), 0.0f, static_cast<float>(s.frameW), static_cast<float>(s.frameH)};
    const float side = 2.0f * (radius + kAvatarFrameMargin * scale);
    r.drawTexture(*s.texture, &src, Rect::fromCenter(center, {side, side}));
}

}  // namespace cosmic
