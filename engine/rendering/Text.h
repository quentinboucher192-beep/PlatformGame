#pragma once

#include <SDL3_ttf/SDL_ttf.h>

#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

#include "engine/core/Math.h"
#include "engine/rendering/Renderer.h"

namespace cosmic {

enum class FontFace { Title, Body, Bold, Count };
enum class Align { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

struct TextStyle {
    FontFace face = FontFace::Body;
    float size = 24.0f;  // pixel height
    Color color = Color::white();
    Align align = Align::Left;
    VAlign valign = VAlign::Top;
    bool shadow = false;
    Color shadowColor = {0, 0, 0, 160};
    float shadowOffset = 2.0f;
    float glow = 0.0f;  // >0 draws an additive halo of this radius (pixels)
    Color glowColor = {57, 240, 216, 90};
};

/// TTF text drawing with a texture cache (white glyph textures tinted per draw).
class TextRenderer {
public:
    bool init(Renderer& renderer, const std::string& titleFont, const std::string& bodyFont,
              const std::string& boldFont);
    void shutdown();

    /// Call once per frame: evicts textures not used recently.
    void endFrame();

    Vec2 measure(std::string_view text, FontFace face, float size);
    /// Draws single-line text; returns its size.
    Vec2 draw(std::string_view text, Vec2 pos, const TextStyle& style);
    /// Draws word-wrapped text within `maxWidth`; returns its size.
    Vec2 drawWrapped(std::string_view text, Vec2 pos, float maxWidth, const TextStyle& style);
    Vec2 measureWrapped(std::string_view text, FontFace face, float size, float maxWidth);

    bool ready() const { return ready_; }

private:
    struct Cached {
        TexturePtr texture;
        uint64_t lastUsed = 0;
    };
    TTF_Font* font(FontFace face, int px);
    const Cached* get(std::string_view text, FontFace face, int px, int wrap);
    Vec2 place(Vec2 pos, Vec2 size, const TextStyle& style) const;

    Renderer* renderer_ = nullptr;
    std::string files_[static_cast<int>(FontFace::Count)];
    std::map<std::pair<int, int>, TTF_Font*> fonts_;
    std::unordered_map<std::string, Cached> cache_;
    uint64_t frame_ = 0;
    bool ready_ = false;
};

}  // namespace cosmic
