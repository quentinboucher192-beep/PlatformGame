#include "ui/screens/Thumbnail.h"

#include <SDL3_image/SDL_image.h>

#include "game/GameServices.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

std::vector<uint8_t> renderLevelThumbnail(GameServices& g, const LevelData& level, const std::string& assetRoot,
                                          const Vec2* focus) {
    Renderer& r = g.app.renderer();
    ResourceManager& res = g.app.resources();
    if (!assetRoot.empty()) res.pushMount(Paths::fromUtf8(assetRoot));
    std::vector<uint8_t> out;
    {
        WorldServices ws{res, g.app.audio(), g.content, nullptr, 0.0f, 1.0f};
        GameWorld world(ws, WorldMode::Editor);
        world.load(level);
        GameRenderer gr;
        gr.build(r, res, world);
        const int W = 480, H = 270;
        TexturePtr target = r.createTarget(W, H, true);
        if (target) {
            Vec2 c = focus ? *focus : world.respawnPoint() - Vec2{0, 40};
            WorldView view;
            view.scale = 1.0f;
            view.screenW = W;
            view.screenH = H;
            c.x = std::clamp(c.x, level.bounds.x + W * 0.5f, std::max(level.bounds.x + W * 0.5f, level.bounds.right() - W * 0.5f));
            c.y = std::clamp(c.y, level.bounds.y + H * 0.5f, std::max(level.bounds.y + H * 0.5f, level.bounds.bottom() - H * 0.5f));
            view.topLeft = {c.x - W * 0.5f, c.y - H * 0.5f};
            r.setTarget(target);
            // The renderer measures the screen through width()/height(); draw at target size.
            RenderOptions opt;
            opt.lighting = false;
            opt.editor = false;
            SDL_SetRenderDrawColor(r.sdl(), 0, 0, 0, 255);
            SDL_RenderClear(r.sdl());
            gr.render(r, res, world, view, 1.0f, opt);
            SDL_Surface* s = SDL_RenderReadPixels(r.sdl(), nullptr);
            r.setTarget(nullptr);
            if (s) {
                SDL_IOStream* io = SDL_IOFromDynamicMem();
                if (io && IMG_SavePNG_IO(s, io, false)) {
                    Sint64 size = SDL_GetIOSize(io);
                    SDL_PropertiesID props = SDL_GetIOProperties(io);
                    auto* mem = static_cast<uint8_t*>(SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
                    if (mem && size > 0) out.assign(mem, mem + size);
                }
                if (io) SDL_CloseIO(io);
                SDL_DestroySurface(s);
            }
        }
    }
    if (!assetRoot.empty()) res.popMount();
    return out;
}

TexturePtr textureFromPng(GameServices& g, const std::vector<uint8_t>& png) {
    if (png.empty()) return nullptr;
    SDL_IOStream* io = SDL_IOFromConstMem(png.data(), png.size());
    if (!io) return nullptr;
    SDL_Surface* s = IMG_Load_IO(io, true);
    if (!s) return nullptr;
    TexturePtr t = g.app.renderer().textureFromSurface(s, "<thumbnail>", true);
    SDL_DestroySurface(s);
    return t;
}

}  // namespace cosmic
