#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Texture.h"

namespace cosmic {

enum class BlendMode { Alpha, Additive, Multiply, None };

/// Mapping between world coordinates (pixels of the art) and screen pixels.
struct WorldView {
    Vec2 topLeft;       // world point shown at screen pixel (0, 0)
    float scale = 1.0f; // screen pixels per world pixel
    int screenW = 1;    // size of the viewport in pixels
    int screenH = 1;
    Vec2 origin;        // screen position of the viewport's top-left (editors' sub-views)

    Vec2 toScreen(Vec2 w) const { return (w - topLeft) * scale; }
    Vec2 toWorld(Vec2 s) const { return s / scale + topLeft; }
    Rect toScreen(const Rect& r) const {
        Vec2 p = toScreen(r.pos());
        return {p.x, p.y, r.w * scale, r.h * scale};
    }
    Rect visibleWorld() const {
        Vec2 tl = toWorld(origin);
        return {tl.x, tl.y, screenW / scale, screenH / scale};
    }
};

/// Thin, stateful wrapper around SDL_Renderer with 2D primitives used by the game,
/// the UI and the editors. Screen-space functions take pixels; world-space functions
/// go through the current WorldView.
class Renderer {
public:
    bool init(SDL_Window* window, bool vsync);
    void shutdown();

    SDL_Renderer* sdl() const { return renderer_; }
    int width() const { return width_; }
    int height() const { return height_; }
    void refreshOutputSize();
    void setVSync(bool on);

    void beginFrame(Color clear);
    void present();

    TexturePtr createTarget(int w, int h, bool linearFilter);
    TexturePtr textureFromSurface(SDL_Surface* surface, const std::string& source, bool pixelArt);
    void setTarget(const TexturePtr& target);  // nullptr = window
    const TexturePtr& glowTexture() const { return glow_; }

    // ------------------------------------------------------------- screen space
    void drawTexture(const Texture& tex, const Rect* src, const Rect& dst, Color tint = Color::white(),
                     BlendMode blend = BlendMode::Alpha, bool flipX = false, bool flipY = false,
                     float angleDeg = 0.0f, const Vec2* pivot = nullptr);
    /// Draws a texture with 9-slice scaling (border in source pixels, scaled by `borderScale`).
    void drawNineSlice(const Texture& tex, const Rect& src, float border, const Rect& dst, float borderScale,
                       Color tint = Color::white());
    void fillRect(const Rect& r, Color c, BlendMode blend = BlendMode::Alpha);
    void drawRect(const Rect& r, Color c, float thickness = 1.0f);
    void drawLine(Vec2 a, Vec2 b, Color c, float thickness = 1.0f, BlendMode blend = BlendMode::Alpha);
    void fillCircle(Vec2 center, float radius, Color c, BlendMode blend = BlendMode::Alpha);
    void drawCircle(Vec2 center, float radius, Color c, float thickness = 1.0f);
    void fillGradientV(const Rect& r, Color top, Color bottom, BlendMode blend = BlendMode::Alpha);
    void fillGradientH(const Rect& r, Color left, Color right, BlendMode blend = BlendMode::Alpha);
    void fillRoundedRect(const Rect& r, float radius, Color c);
    void fillRoundedRectGradient(const Rect& r, float radius, Color top, Color bottom);
    void drawRoundedRect(const Rect& r, float radius, Color c, float thickness = 1.0f);
    void fillConvexPolygon(const std::vector<Vec2>& pts, Color c, BlendMode blend = BlendMode::Alpha);
    void fillTriangle(Vec2 a, Vec2 b, Vec2 c, Color col, BlendMode blend = BlendMode::Alpha);
    void drawGlow(Vec2 center, float radius, Color c, BlendMode blend = BlendMode::Additive);
    void drawRing(Vec2 center, float radius, float thickness, Color c, BlendMode blend = BlendMode::Alpha);
    void drawArc(Vec2 center, float radius, float thickness, float startRad, float endRad, Color c);

    void pushClip(const Rect& r);
    void popClip();

    // -------------------------------------------------------------- world space
    void setView(const WorldView& v) { view_ = v; }
    const WorldView& view() const { return view_; }
    void drawWorldTexture(const Texture& tex, const Rect* src, const Rect& worldDst, Color tint = Color::white(),
                          BlendMode blend = BlendMode::Alpha, bool flipX = false, float angleDeg = 0.0f,
                          const Vec2* worldPivotOffset = nullptr, bool flipY = false);
    void fillWorldRect(const Rect& r, Color c, BlendMode blend = BlendMode::Alpha);
    void drawWorldRect(const Rect& r, Color c, float thicknessPx = 1.0f);
    void drawWorldLine(Vec2 a, Vec2 b, Color c, float thicknessPx = 1.0f, BlendMode blend = BlendMode::Alpha);
    void drawWorldGlow(Vec2 center, float radius, Color c, BlendMode blend = BlendMode::Additive);
    void drawWorldCircle(Vec2 center, float radius, Color c, float thicknessPx = 1.0f);
    void fillWorldCircle(Vec2 center, float radius, Color c, BlendMode blend = BlendMode::Alpha);

    /// Statistics for the debug overlay.
    int drawCallsLastFrame() const { return lastDrawCalls_; }

    static SDL_BlendMode toSdl(BlendMode b);

private:
    void geometry(const std::vector<SDL_Vertex>& verts, const std::vector<int>& idx, BlendMode blend,
                  SDL_Texture* tex = nullptr);
    Rect snap(const Rect& r) const;

    SDL_Renderer* renderer_ = nullptr;
    int width_ = 1;
    int height_ = 1;
    WorldView view_;
    TexturePtr glow_;
    std::vector<Rect> clipStack_;
    std::vector<SDL_Vertex> scratchVerts_;
    std::vector<int> scratchIdx_;
    int drawCalls_ = 0;
    int lastDrawCalls_ = 0;
};

}  // namespace cosmic
