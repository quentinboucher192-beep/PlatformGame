#include "engine/core/Application.h"

#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "engine/core/Log.h"
#include "engine/core/Paths.h"

namespace cosmic {

bool Application::init(const AppConfig& config) {
    config_ = config;
    SDL_SetAppMetadata("Cosmic Platformer", COSMIC_VERSION_STRING, "com.cosmicstudio.cosmicplatformer");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, config.vsync ? "1" : "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        Log::error("SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    if (!config.headless && !SDL_InitSubSystem(SDL_INIT_AUDIO)) Log::warn("Audio subsystem unavailable: {}", SDL_GetError());

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (config.headless) flags |= SDL_WINDOW_HIDDEN;
    window_ = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
    if (!window_) {
        Log::error("SDL_CreateWindow failed: {}", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window_, 960, 540);
    if (!renderer_.init(window_, config.vsync && !config.headless)) return false;
    setFullscreen(config.fullscreen && !config.headless);

    if (!TTF_Init()) Log::error("TTF_Init failed: {}", SDL_GetError());
    auto font = [](const char* f) { return Paths::toUtf8(Paths::assetsDir() / "fonts" / f); };
    text_.init(renderer_, font("Orbitron.ttf"), font("ChakraPetch-Regular.ttf"), font("ChakraPetch-SemiBold.ttf"));

    if (!config.headless) audio_.init();
    resources_.init(&renderer_, &audio_);
    input_.init();

    // Window icon
    if (auto p = resources_.resolve("ui/app_icon.png")) {
        if (SDL_Surface* icon = IMG_Load(Paths::toUtf8(*p).c_str())) {
            SDL_SetWindowIcon(window_, icon);
            SDL_DestroySurface(icon);
        }
    }
    running_ = true;
    return true;
}

void Application::shutdown() {
    for (auto it = scenes_.rbegin(); it != scenes_.rend(); ++it) (*it)->onExit();
    scenes_.clear();
    pending_.clear();
    fadeTarget_.reset();
    resources_.shutdown();
    text_.shutdown();
    input_.shutdown();
    audio_.shutdown();
    renderer_.shutdown();
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    TTF_Quit();
    SDL_Quit();
}

void Application::pushScene(std::unique_ptr<Scene> scene) { pending_.push_back({PendingOp::Push, std::move(scene)}); }
void Application::popScene() { pending_.push_back({PendingOp::Pop, nullptr}); }
void Application::switchScene(std::unique_ptr<Scene> scene, float fade) {
    pending_.push_back({PendingOp::Switch, std::move(scene), fade});
}
void Application::replaceTop(std::unique_ptr<Scene> scene, float fade) {
    pending_.push_back({PendingOp::ReplaceTop, std::move(scene), fade});
}

void Application::applyPending() {
    // Fades delay switch / replace operations: the swap happens at full black.
    if (fadeState_ != Fade::None) return;
    while (!pending_.empty()) {
        PendingOp op = std::move(pending_.front());
        pending_.erase(pending_.begin());
        switch (op.kind) {
            case PendingOp::Push:
                if (op.scene) {
                    op.scene->attach(this);
                    scenes_.push_back(std::move(op.scene));
                    scenes_.back()->onEnter();
                }
                break;
            case PendingOp::Pop:
                if (!scenes_.empty()) {
                    scenes_.back()->onExit();
                    scenes_.pop_back();
                    if (!scenes_.empty()) scenes_.back()->onResume();
                }
                break;
            case PendingOp::Switch:
            case PendingOp::ReplaceTop:
                if (op.fade > 0.0f && !scenes_.empty()) {
                    fadeTarget_ = std::move(op.scene);
                    fadeKind_ = op.kind;
                    fadeState_ = Fade::Out;
                    fadeT_ = 0.0f;
                    fadeDur_ = op.fade;
                    return;
                }
                if (op.kind == PendingOp::Switch) {
                    for (auto it = scenes_.rbegin(); it != scenes_.rend(); ++it) (*it)->onExit();
                    scenes_.clear();
                } else if (!scenes_.empty()) {
                    scenes_.back()->onExit();
                    scenes_.pop_back();
                }
                if (op.scene) {
                    op.scene->attach(this);
                    scenes_.push_back(std::move(op.scene));
                    scenes_.back()->onEnter();
                }
                break;
        }
    }
}

void Application::processEvents() {
    input_.beginFrame();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        SDL_ConvertEventToRenderCoordinates(renderer_.sdl(), &e);
        if (e.type == SDL_EVENT_QUIT) {
            running_ = false;
            continue;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.scancode == SDL_SCANCODE_F11 ||
                (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT))) {
                setFullscreen(!fullscreen_);
                continue;
            }
            if (e.key.scancode == SDL_SCANCODE_F3) {
                debugOverlay = !debugOverlay;
                continue;
            }
            if (e.key.scancode == SDL_SCANCODE_F12) {
                screenshotPath_ = Paths::toUtf8(Paths::screenshotsDir() /
                                                ("cosmic_" + std::to_string(SDL_GetTicks()) + ".png"));
                continue;
            }
        }
        if (onGlobalEvent && onGlobalEvent(e)) continue;
        input_.handleEvent(e);
        if (!scenes_.empty() && fadeState_ == Fade::None) scenes_.back()->handleEvent(e);
    }
}

void Application::run() {
    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    double accumulator = 0.0;
    float fpsTimer = 0.0f;
    int fpsFrames = 0;

    while (running_) {
        applyPending();
        if (scenes_.empty() && fadeState_ == Fade::None && pending_.empty()) break;

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = static_cast<double>(now - last) / freq;
        last = now;
        if (config_.headless) dt = headlessDt;
        dt = std::min(dt, 0.1);  // breakpoint / window drag protection
        frameMs_ = static_cast<float>(dt * 1000.0);
        fpsTimer += static_cast<float>(dt);
        ++fpsFrames;
        if (fpsTimer >= 0.5f) {
            fps_ = fpsFrames / fpsTimer;
            fpsTimer = 0.0f;
            fpsFrames = 0;
        }

        processEvents();
        input_.update(static_cast<float>(dt));

        // Transition fade progression
        if (fadeState_ != Fade::None) {
            fadeT_ += static_cast<float>(dt);
            if (fadeState_ == Fade::Out && fadeT_ >= fadeDur_) {
                if (fadeKind_ == PendingOp::Switch) {
                    for (auto it = scenes_.rbegin(); it != scenes_.rend(); ++it) (*it)->onExit();
                    scenes_.clear();
                } else if (!scenes_.empty()) {
                    scenes_.back()->onExit();
                    scenes_.pop_back();
                }
                if (fadeTarget_) {
                    fadeTarget_->attach(this);
                    scenes_.push_back(std::move(fadeTarget_));
                    scenes_.back()->onEnter();
                }
                fadeState_ = Fade::In;
                fadeT_ = 0.0f;
                // Do not count the loading time of the new scene in the next frame.
                last = SDL_GetPerformanceCounter();
                accumulator = 0.0;
            } else if (fadeState_ == Fade::In && fadeT_ >= fadeDur_) {
                fadeState_ = Fade::None;
            }
        }

        // Fixed-step simulation of the top scene (and the scenes below an overlay that asks for it).
        accumulator += dt;
        int steps = 0;
        while (accumulator >= kFixedDt && steps < 8) {
            for (size_t i = 0; i < scenes_.size(); ++i) {
                bool top = i + 1 == scenes_.size();
                bool belowUpdates = !top && scenes_[i + 1]->isOverlay() && scenes_[i + 1]->updatesSceneBelow();
                if (top || belowUpdates) scenes_[i]->fixedUpdate(kFixedDt);
            }
            accumulator -= kFixedDt;
            ++steps;
        }
        if (steps == 8) accumulator = 0.0;
        float alpha = static_cast<float>(accumulator / kFixedDt);

        for (size_t i = 0; i < scenes_.size(); ++i) {
            bool top = i + 1 == scenes_.size();
            bool belowUpdates = !top && scenes_[i + 1]->isOverlay() && scenes_[i + 1]->updatesSceneBelow();
            if (top || belowUpdates) scenes_[i]->update(static_cast<float>(dt));
        }
        audio_.updateLoops();

        renderFrame(alpha);
        ++frame_;
        if (maxFrames > 0 && frame_ >= static_cast<uint64_t>(maxFrames)) running_ = false;
    }
}

void Application::renderFrame(float alpha) {
    renderer_.beginFrame({8, 10, 22, 255});
    // Render from the deepest non-covered scene up.
    size_t first = scenes_.empty() ? 0 : scenes_.size() - 1;
    while (first > 0 && scenes_[first]->isOverlay()) --first;
    for (size_t i = first; i < scenes_.size(); ++i) scenes_[i]->render(i + 1 == scenes_.size() ? alpha : 1.0f);

    if (debugOverlay && !scenes_.empty()) scenes_.back()->renderDebug();
    if (onOverlay) onOverlay();

    if (fadeState_ != Fade::None) {
        float a = fadeState_ == Fade::Out ? fadeT_ / fadeDur_ : 1.0f - fadeT_ / fadeDur_;
        renderer_.fillRect({0, 0, static_cast<float>(renderer_.width()), static_cast<float>(renderer_.height())},
                           Color{4, 6, 14, 255}.withAlphaF(std::clamp(a, 0.0f, 1.0f)));
    }

    for (auto it = scheduledScreenshots.begin(); it != scheduledScreenshots.end();) {
        if (static_cast<uint64_t>(it->first) == frame_) {
            saveScreenshot(it->second);
            it = scheduledScreenshots.erase(it);
        } else {
            ++it;
        }
    }
    if (!screenshotPath_.empty()) {
        saveScreenshot(screenshotPath_);
        screenshotPath_.clear();
    }
    text_.endFrame();
    renderer_.present();
}

void Application::saveScreenshot(const std::string& path) {
    SDL_Surface* s = SDL_RenderReadPixels(renderer_.sdl(), nullptr);
    if (!s) {
        Log::error("Screenshot failed: {}", SDL_GetError());
        return;
    }
    if (IMG_SavePNG(s, path.c_str()))
        Log::info("Screenshot saved: {}", path);
    else
        Log::error("Cannot save screenshot {}: {}", path, SDL_GetError());
    SDL_DestroySurface(s);
}

void Application::setFullscreen(bool on) {
    fullscreen_ = on;
    SDL_SetWindowFullscreen(window_, on);
}

void Application::setVSync(bool on) { renderer_.setVSync(on); }

void Application::setWindowSize(int w, int h) {
    if (!fullscreen_) {
        SDL_SetWindowSize(window_, w, h);
        SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

}  // namespace cosmic
