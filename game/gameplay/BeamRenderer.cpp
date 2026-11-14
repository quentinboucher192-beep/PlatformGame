#include "game/gameplay/BeamRenderer.h"

#include <algorithm>
#include <cmath>

#include "engine/resources/ResourceManager.h"

namespace cosmic {

namespace BeamRenderer {

const std::vector<std::string>& styles() {
    static const std::vector<std::string> s = {"", "flame", "frost", "plasma", "thorn", "star"};
    return s;
}

const std::vector<std::string>& styleLabels() {
    static const std::vector<std::string> s = {"Trait lumineux", "Flamme", "Givre", "Plasma", "Sève et ronces", "Lumière d'étoile"};
    return s;
}

Color styleColor(const std::string& style, Color fallback) {
    if (style == "flame") return {255, 138, 61, 255};
    if (style == "frost") return {127, 246, 255, 255};
    if (style == "plasma") return {57, 240, 216, 255};
    if (style == "thorn") return {93, 255, 176, 255};
    if (style == "star") return {255, 217, 138, 255};
    return fallback;
}

bool draw(Renderer& r, ResourceManager& res, const std::string& style, Vec2 origin, float angle, float length, float width,
          float time, Color tint) {
    if (style.empty() || length <= 1.0f || width <= 0.5f) return false;
    AnimationSetPtr set = res.animation("animations/beams.json");
    if (!set || !set->texture) return false;
    const AnimationClip* clip = set->clip(style);
    if (!clip || clip->frameCount() == 0) return false;
    float fd = clip->durations.empty() || clip->durations[0] <= 0.0f ? 0.07f : clip->durations[0];
    int frame = static_cast<int>(std::floor(time / fd)) % clip->frameCount();
    Rect f = set->frameRect(*clip, frame);
    // Strip layout (beams.json "beam"): start cap, body, end cap, in texture pixels.
    const float fw = f.w, fh = f.h;
    const float cap = 16.0f, body0 = 16.0f, body1 = std::max(body0 + 1.0f, fw - 16.0f);
    const float bodyW = body1 - body0;
    // Everything in screen pixels: the pieces turn around the beam's origin.
    const WorldView& v = r.view();
    Vec2 o = v.toScreen(origin);
    float sc = v.scale;
    float sy = width * sc / fh;  // texture pixel -> screen pixel
    float L = length * sc;
    float capL = cap * sy, capR = cap * sy;
    if (capL + capR > L) capL = capR = L * 0.5f;
    float deg = angle * 57.29578f;
    float h = fh * sy;
    auto piece = [&](float along, float len, float srcX, float srcW) {
        if (len <= 0.0f || srcW <= 0.0f) return;
        Rect src{f.x + srcX, f.y, srcW, fh};
        Rect dst{o.x + along, o.y - h * 0.5f, len + 1.0f, h};  // +1: no seam between pieces
        Vec2 pivot{-along, h * 0.5f};
        r.drawTexture(*set->texture, &src, dst, tint, BlendMode::Alpha, false, false, deg, &pivot);
    };
    piece(0.0f, capL, 0.0f, capL / sy);
    float x = capL, end = L - capR;
    float bodyLen = bodyW * sy;
    while (x < end - 0.01f) {
        float len = std::min(bodyLen, end - x);
        piece(x, len, body0, bodyW * len / bodyLen);
        x += len;
    }
    piece(end, capR, fw - capR / sy, capR / sy);
    return true;
}

}  // namespace BeamRenderer

}  // namespace cosmic
