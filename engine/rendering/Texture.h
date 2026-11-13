#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>

namespace cosmic {

/// RAII owner of an SDL_Texture.
class Texture {
public:
    Texture(SDL_Texture* handle, int w, int h, std::string source = {})
        : handle_(handle), width_(w), height_(h), source_(std::move(source)) {}
    ~Texture() {
        if (handle_) SDL_DestroyTexture(handle_);
    }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    SDL_Texture* sdl() const { return handle_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& source() const { return source_; }

private:
    SDL_Texture* handle_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::string source_;
};

using TexturePtr = std::shared_ptr<Texture>;

}  // namespace cosmic
