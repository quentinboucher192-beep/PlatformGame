#include "game/gameplay/BossBadge.h"

#include <cmath>

#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/enemies/EnemyDefinition.h"

namespace cosmic {

namespace BossBadge {

const std::vector<std::string>& icons() {
    static const std::vector<std::string> v = {"portrait", "crown", "skull", "flame", "snowflake", "eye", "star"};
    return v;
}

const std::vector<std::string>& iconLabels() {
    static const std::vector<std::string> v = {"Portrait du boss", "Couronne", "Tête de mort", "Flamme", "Flocon", "Œil", "Étoile"};
    return v;
}

Color color(const EnemyDefinition& d) {
    Color gold{255, 217, 138, 255};
    if (d.barColor.empty()) return gold;
    Color c = Color::fromHex(d.barColor, gold);
    c.a = 255;
    return c;
}

namespace {
constexpr float kPi = 3.14159265f;

void symbol(Renderer& r, const std::string& icon, Vec2 c, float s, Color col) {
    Color hole{18, 8, 12, 255};
    if (icon == "skull") {
        Vec2 hc = c - Vec2{0, 0.18f * s};
        r.fillCircle(hc, 0.62f * s, col);
        r.fillRect({c.x - 0.4f * s, c.y + 0.18f * s, 0.8f * s, 0.42f * s}, col);
        r.fillCircle(hc + Vec2{-0.24f * s, 0.06f * s}, 0.19f * s, hole);
        r.fillCircle(hc + Vec2{0.24f * s, 0.06f * s}, 0.19f * s, hole);
        r.fillTriangle({c.x, c.y + 0.02f * s}, {c.x - 0.09f * s, c.y + 0.2f * s}, {c.x + 0.09f * s, c.y + 0.2f * s}, hole);
        for (int t = -1; t <= 1; ++t) r.fillRect({c.x + t * 0.18f * s - 0.03f * s, c.y + 0.34f * s, 0.06f * s, 0.26f * s}, hole);
    } else if (icon == "flame") {
        r.fillTriangle({c.x - 0.5f * s, c.y + 0.45f * s}, {c.x + 0.5f * s, c.y + 0.45f * s}, {c.x + 0.05f * s, c.y - 0.75f * s}, col);
        r.fillCircle({c.x, c.y + 0.2f * s}, 0.48f * s, col);
        Color in = Color::lerp(col, Color::white(), 0.6f);
        r.fillTriangle({c.x - 0.22f * s, c.y + 0.45f * s}, {c.x + 0.22f * s, c.y + 0.45f * s}, {c.x - 0.02f * s, c.y - 0.25f * s}, in);
    } else if (icon == "snowflake") {
        for (int k = 0; k < 3; ++k) {
            float a = kPi / 3.0f * static_cast<float>(k) + kPi / 2.0f;
            Vec2 d{std::cos(a), std::sin(a)};
            r.drawLine(c - d * (0.7f * s), c + d * (0.7f * s), col, 0.13f * s);
            for (int sgn : {-1, 1}) {
                Vec2 b = c + d * (0.42f * s * static_cast<float>(sgn));
                Vec2 n{-d.y, d.x};
                r.drawLine(b, b + (d * static_cast<float>(sgn) + n) * (0.2f * s), col, 0.09f * s);
                r.drawLine(b, b + (d * static_cast<float>(sgn) - n) * (0.2f * s), col, 0.09f * s);
            }
        }
        r.fillCircle(c, 0.14f * s, col);
    } else if (icon == "eye") {
        std::vector<Vec2> lens;
        for (int k = 0; k <= 16; ++k) {
            float a = kPi * static_cast<float>(k) / 16.0f;
            lens.push_back({c.x - std::cos(a) * 0.72f * s, c.y - std::sin(a) * 0.36f * s});
        }
        for (int k = 15; k >= 1; --k) {
            float a = kPi * static_cast<float>(k) / 16.0f;
            lens.push_back({c.x - std::cos(a) * 0.72f * s, c.y + std::sin(a) * 0.36f * s});
        }
        r.fillConvexPolygon(lens, col);
        r.fillCircle(c, 0.3f * s, hole);
        r.fillCircle(c, 0.14f * s, col);
    } else if (icon == "star") {
        std::vector<Vec2> pts;
        for (int k = 0; k < 10; ++k) {
            float a = -kPi / 2.0f + kPi / 5.0f * static_cast<float>(k);
            float rr = (k % 2 == 0 ? 0.78f : 0.34f) * s;
            pts.push_back({c.x + std::cos(a) * rr, c.y + std::sin(a) * rr});
        }
        for (int k = 0; k < 10; k += 2) r.fillTriangle(c, pts[static_cast<size_t>(k)], pts[static_cast<size_t>((k + 1) % 10)], col);
        for (int k = 0; k < 10; k += 2) r.fillTriangle(c, pts[static_cast<size_t>(k)], pts[static_cast<size_t>((k + 9) % 10)], col);
    } else {  // crown
        float cw = 1.2f * s, ch = 0.62f * s, bh = 0.24f * s;
        Vec2 base{c.x - cw * 0.5f, c.y + ch * 0.55f};
        r.fillRect({base.x, base.y - bh, cw, bh}, col);
        r.fillTriangle({base.x, base.y - bh}, {base.x + cw * 0.17f, base.y - ch}, {base.x + cw * 0.36f, base.y - bh}, col);
        r.fillTriangle({base.x + cw * 0.32f, base.y - bh}, {base.x + cw * 0.5f, base.y - ch - 0.2f * s}, {base.x + cw * 0.68f, base.y - bh}, col);
        r.fillTriangle({base.x + cw * 0.64f, base.y - bh}, {base.x + cw * 0.83f, base.y - ch}, {base.x + cw, base.y - bh}, col);
    }
}
}  // namespace

void draw(Renderer& r, ResourceManager& res, const EnemyDefinition& d, Vec2 c, float radius, bool defeated, float time) {
    Color col = defeated ? Color{140, 150, 175, 255} : color(d);
    std::vector<Vec2> hex;
    for (int k = 0; k < 6; ++k) {
        float a = kPi / 3.0f * static_cast<float>(k) + kPi / 6.0f;
        hex.push_back({c.x + std::cos(a) * radius * 1.16f, c.y + std::sin(a) * radius * 1.16f});
    }
    r.fillConvexPolygon(hex, col);
    for (auto& v : hex) v = c + (v - c) * (1.0f / 1.16f);
    r.fillConvexPolygon(hex, {22, 12, 14, 250});
    if (d.barIcon == "portrait" || d.barIcon.empty()) {
        AnimationSetPtr anim = res.animation(d.animation);
        const AnimationClip* clip = anim ? anim->clip("idle") : nullptr;
        if (anim && !clip && !anim->clips.empty()) clip = &anim->clips.begin()->second;
        if (anim && anim->texture && clip) {
            Rect src = anim->frameRect(*clip, 0);
            float k = radius * 1.7f / std::max(src.w, src.h);
            Rect dst = Rect::fromCenter(c, {src.w * k, src.h * k});
            r.pushClip(Rect::fromCenter(c, {radius * 1.55f, radius * 1.7f}));
            r.drawTexture(*anim->texture, &src, dst, defeated ? Color{120, 125, 140, 255} : Color::white());
            r.popClip();
        } else {
            symbol(r, "crown", c, radius, col);
        }
    } else {
        symbol(r, d.barIcon, c, radius, col);
    }
    (void)time;
    if (defeated) r.drawLine({c.x - radius, c.y + radius * 0.6f}, {c.x + radius, c.y - radius * 0.6f}, Color{255, 77, 109, 230}, radius * 0.16f);
}

}  // namespace BossBadge

}  // namespace cosmic
