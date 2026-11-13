#include "engine/rendering/Parallax.h"

#include <cmath>

#include "engine/resources/ResourceManager.h"

namespace cosmic {

ParallaxLayerDef ParallaxLayerDef::fromJson(const Json& j) {
    ParallaxLayerDef d;
    d.texture = jstr(j, "texture");
    d.factor = vec2FromJson(j.value("factor", Json()), d.factor);
    d.offset = vec2FromJson(j.value("offset", Json()), {});
    d.scroll = vec2FromJson(j.value("scroll", Json()), {});
    d.repeatX = jget<bool>(j, "repeatX", true);
    d.fill = jget<bool>(j, "fill", false);
    d.foreground = jget<bool>(j, "foreground", false);
    d.tint = colorFromJson(j.value("tint", Json()), Color::white());
    d.fillBelow = colorFromJson(j.value("fillBelow", Json()), Color::transparent());
    d.fillAbove = colorFromJson(j.value("fillAbove", Json()), Color::transparent());
    return d;
}

Json ParallaxLayerDef::toJson() const {
    Json j;
    j["texture"] = texture;
    j["factor"] = cosmic::toJson(factor);
    j["offset"] = cosmic::toJson(offset);
    if (scroll.lengthSq() > 0) j["scroll"] = cosmic::toJson(scroll);
    j["repeatX"] = repeatX;
    if (fill) j["fill"] = true;
    if (foreground) j["foreground"] = true;
    if (!(tint == Color::white())) j["tint"] = tint.toHex();
    if (fillBelow.a) j["fillBelow"] = fillBelow.toHex();
    if (fillAbove.a) j["fillAbove"] = fillAbove.toHex();
    return j;
}

void ParallaxBackground::load(ResourceManager& res, const std::vector<ParallaxLayerDef>& layers, float referenceY) {
    layers_.clear();
    referenceY_ = referenceY;
    for (const auto& d : layers) {
        Layer l;
        l.def = d;
        l.tex = res.texture(d.texture);
        if (l.tex) layers_.push_back(std::move(l));
    }
}

void ParallaxBackground::render(Renderer& r, const WorldView& view, bool foreground) const {
    const float sw = static_cast<float>(view.screenW), sh = static_cast<float>(view.screenH);
    const Vec2 origin = view.origin;  // viewport position (editors draw in sub-rectangles)
    Vec2 camCenter = view.toWorld(origin + Vec2{sw * 0.5f, sh * 0.5f});
    for (const Layer& l : layers_) {
        if (l.def.foreground != foreground || !l.tex) continue;
        const float tw = static_cast<float>(l.tex->width()), th = static_cast<float>(l.tex->height());
        if (l.def.fill) {
            // Cover the screen while keeping aspect; slow drift with the camera.
            float s = std::max(sw / tw, sh / th) * 1.08f;
            float w = tw * s, h = th * s;
            float dx = -std::fmod(camCenter.x * l.def.factor.x * view.scale, std::max(1.0f, w - sw));
            float x = origin.x + (sw - w) * 0.5f + std::clamp(dx * 0.02f, -(w - sw) * 0.5f, (w - sw) * 0.5f);
            float y = origin.y + (sh - h) * 0.5f;
            r.drawTexture(*l.tex, nullptr, {x, y, w, h}, l.def.tint);
            continue;
        }
        float scale = view.scale;
        float w = tw * scale, h = th * scale;
        float ox = (l.def.offset.x + l.def.scroll.x * time_ - camCenter.x * l.def.factor.x) * scale + sw * 0.5f;
        float oy = origin.y + (l.def.offset.y - (camCenter.y - referenceY_) * l.def.factor.y) * scale + sh * 0.5f - h * 0.5f;
        if (l.def.fillAbove.a && oy > origin.y) r.fillRect({origin.x, origin.y, sw, oy - origin.y + 1}, l.def.fillAbove);
        if (l.def.fillBelow.a && oy + h < origin.y + sh)
            r.fillRect({origin.x, oy + h - 1, sw, origin.y + sh - (oy + h) + 2}, l.def.fillBelow);
        if (l.def.repeatX) {
            float start = std::fmod(ox, w);
            if (start > 0) start -= w;
            start += origin.x;
            for (float x = start; x < origin.x + sw; x += w) {
                float x0 = std::floor(x), x1 = std::floor(x + w);
                r.drawTexture(*l.tex, nullptr, {x0, std::round(oy), x1 - x0 + 1, std::round(h)}, l.def.tint);
            }
        } else {
            r.drawTexture(*l.tex, nullptr, {std::round(origin.x + ox - w * 0.5f), std::round(oy), w, h}, l.def.tint);
        }
    }
}

void LightMap::apply(Renderer& r, const WorldView& view, Color ambient) {
    const int tw = std::max(1, r.width() / 2), th = std::max(1, r.height() / 2);
    if (!target_ || target_->width() != tw || target_->height() != th) target_ = r.createTarget(tw, th, true);
    if (!target_) return;
    r.setTarget(target_);
    SDL_SetRenderDrawColor(r.sdl(), ambient.r, ambient.g, ambient.b, 255);
    SDL_RenderClear(r.sdl());
    const float k = static_cast<float>(tw) / static_cast<float>(r.width());
    Rect vis = view.visibleWorld().expanded(200);
    for (const Light& l : lights_) {
        if (!vis.contains(l.pos)) continue;
        Vec2 s = view.toScreen(l.pos) * k;
        float rad = l.radius * view.scale * k;
        r.drawGlow(s, rad, l.color, BlendMode::Additive);
    }
    r.setTarget(nullptr);
    r.drawTexture(*target_, nullptr, {0, 0, static_cast<float>(r.width()), static_cast<float>(r.height())},
                  Color::white(), BlendMode::Multiply);
}

}  // namespace cosmic
