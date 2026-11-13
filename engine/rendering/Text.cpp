#include "engine/rendering/Text.h"

#include <cmath>

#include "engine/core/Log.h"

namespace cosmic {

bool TextRenderer::init(Renderer& renderer, const std::string& titleFont, const std::string& bodyFont,
                        const std::string& boldFont) {
    renderer_ = &renderer;
    if (!TTF_WasInit() && !TTF_Init()) {
        Log::error("TTF_Init failed: {}", SDL_GetError());
        return false;
    }
    files_[static_cast<int>(FontFace::Title)] = titleFont;
    files_[static_cast<int>(FontFace::Body)] = bodyFont;
    files_[static_cast<int>(FontFace::Bold)] = boldFont;
    ready_ = font(FontFace::Body, 24) != nullptr;
    if (!ready_) Log::error("UI font could not be loaded ({})", bodyFont);
    return ready_;
}

void TextRenderer::shutdown() {
    cache_.clear();
    for (auto& [k, f] : fonts_)
        if (f) TTF_CloseFont(f);
    fonts_.clear();
    ready_ = false;
}

TTF_Font* TextRenderer::font(FontFace face, int px) {
    px = std::clamp(px, 6, 400);
    auto key = std::make_pair(static_cast<int>(face), px);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    const std::string& file = files_[static_cast<int>(face)];
    TTF_Font* f = TTF_OpenFont(file.c_str(), static_cast<float>(px));
    if (!f && face != FontFace::Body) {
        f = TTF_OpenFont(files_[static_cast<int>(FontFace::Body)].c_str(), static_cast<float>(px));
    }
    if (!f) Log::error("TTF_OpenFont({}) failed: {}", file, SDL_GetError());
    fonts_[key] = f;
    return f;
}

const TextRenderer::Cached* TextRenderer::get(std::string_view text, FontFace face, int px, int wrap) {
    if (text.empty()) return nullptr;
    std::string key;
    key.reserve(text.size() + 24);
    key.append(std::to_string(static_cast<int>(face))).push_back('|');
    key.append(std::to_string(px)).push_back('|');
    key.append(std::to_string(wrap)).push_back('|');
    key.append(text);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.lastUsed = frame_;
        return &it->second;
    }
    TTF_Font* f = font(face, px);
    if (!f) return nullptr;
    SDL_Color white{255, 255, 255, 255};
    SDL_Surface* s = wrap > 0 ? TTF_RenderText_Blended_Wrapped(f, text.data(), text.size(), white, wrap)
                              : TTF_RenderText_Blended(f, text.data(), text.size(), white);
    if (!s) return nullptr;
    Cached c;
    c.texture = renderer_->textureFromSurface(s, "<text>", false);
    c.lastUsed = frame_;
    SDL_DestroySurface(s);
    if (!c.texture) return nullptr;
    auto [ins, ok] = cache_.emplace(std::move(key), std::move(c));
    return &ins->second;
}

void TextRenderer::endFrame() {
    ++frame_;
    if (frame_ % 60 != 0) return;
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (frame_ - it->second.lastUsed > 180)
            it = cache_.erase(it);
        else
            ++it;
    }
}

Vec2 TextRenderer::measure(std::string_view text, FontFace face, float size) {
    TTF_Font* f = font(face, static_cast<int>(std::lround(size)));
    if (!f || text.empty()) return {0.0f, size};
    int w = 0, h = 0;
    TTF_GetStringSize(f, text.data(), text.size(), &w, &h);
    return {static_cast<float>(w), static_cast<float>(h)};
}

Vec2 TextRenderer::measureWrapped(std::string_view text, FontFace face, float size, float maxWidth) {
    const Cached* c = get(text, face, static_cast<int>(std::lround(size)), std::max(1, static_cast<int>(maxWidth)));
    if (!c) return {0.0f, 0.0f};
    return {static_cast<float>(c->texture->width()), static_cast<float>(c->texture->height())};
}

Vec2 TextRenderer::place(Vec2 pos, Vec2 size, const TextStyle& style) const {
    Vec2 p = pos;
    if (style.align == Align::Center) p.x -= size.x * 0.5f;
    if (style.align == Align::Right) p.x -= size.x;
    if (style.valign == VAlign::Middle) p.y -= size.y * 0.5f;
    if (style.valign == VAlign::Bottom) p.y -= size.y;
    return {std::round(p.x), std::round(p.y)};
}

Vec2 TextRenderer::draw(std::string_view text, Vec2 pos, const TextStyle& style) {
    const Cached* c = get(text, style.face, static_cast<int>(std::lround(style.size)), 0);
    if (!c) return {0.0f, style.size};
    Vec2 size{static_cast<float>(c->texture->width()), static_cast<float>(c->texture->height())};
    Vec2 p = place(pos, size, style);
    if (style.glow > 0.0f) {
        Rect g{p.x - style.glow, p.y - style.glow * 0.5f, size.x + style.glow * 2, size.y + style.glow};
        renderer_->drawTexture(*renderer_->glowTexture(), nullptr, g, style.glowColor, BlendMode::Additive);
    }
    if (style.shadow) {
        Color sc = style.shadowColor;
        sc.a = static_cast<uint8_t>(sc.a * (style.color.a / 255.0f));
        renderer_->drawTexture(*c->texture, nullptr,
                               {p.x + style.shadowOffset, p.y + style.shadowOffset, size.x, size.y}, sc);
    }
    renderer_->drawTexture(*c->texture, nullptr, {p.x, p.y, size.x, size.y}, style.color);
    return size;
}

Vec2 TextRenderer::drawWrapped(std::string_view text, Vec2 pos, float maxWidth, const TextStyle& style) {
    const Cached* c =
        get(text, style.face, static_cast<int>(std::lround(style.size)), std::max(1, static_cast<int>(maxWidth)));
    if (!c) return {0.0f, 0.0f};
    Vec2 size{static_cast<float>(c->texture->width()), static_cast<float>(c->texture->height())};
    Vec2 p = place(pos, size, style);
    if (style.shadow)
        renderer_->drawTexture(*c->texture, nullptr,
                               {p.x + style.shadowOffset, p.y + style.shadowOffset, size.x, size.y},
                               style.shadowColor);
    renderer_->drawTexture(*c->texture, nullptr, {p.x, p.y, size.x, size.y}, style.color);
    return size;
}

}  // namespace cosmic
