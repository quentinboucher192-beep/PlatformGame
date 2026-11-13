#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Scene.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/Text.h"
#include "engine/resources/ResourceManager.h"

namespace cosmic {

struct AppConfig {
    std::string title = "Cosmic Platformer";
    int width = 1600;
    int height = 900;
    bool fullscreen = false;
    bool vsync = true;
    bool headless = false;  // automated runs: hidden window, no audio device needed
};

/// Owns the window and every engine service, runs the main loop with a fixed-step
/// simulation (120 Hz) and variable-rate rendering, and manages the scene stack with
/// fade transitions.
class Application {
public:
    static constexpr float kFixedHz = 120.0f;
    static constexpr float kFixedDt = 1.0f / kFixedHz;

    bool init(const AppConfig& config);
    void run();
    void shutdown();
    void quit() { running_ = false; }

    // Services
    Renderer& renderer() { return renderer_; }
    TextRenderer& text() { return text_; }
    InputManager& input() { return input_; }
    AudioSystem& audio() { return audio_; }
    ResourceManager& resources() { return resources_; }
    SDL_Window* window() const { return window_; }

    // Scene stack (operations are deferred to the start of the next frame)
    void pushScene(std::unique_ptr<Scene> scene);
    void popScene();
    /// Replaces the whole stack with `scene` after a fade to black.
    void switchScene(std::unique_ptr<Scene> scene, float fade = 0.35f);
    /// Replaces the top scene after a fade.
    void replaceTop(std::unique_ptr<Scene> scene, float fade = 0.35f);
    Scene* topScene() const { return scenes_.empty() ? nullptr : scenes_.back().get(); }
    bool transitioning() const { return fadeState_ != Fade::None; }

    // Window
    void setFullscreen(bool on);
    bool fullscreen() const { return fullscreen_; }
    void setVSync(bool on);
    void setWindowSize(int w, int h);

    // Diagnostics
    bool debugOverlay = false;
    float fps() const { return fps_; }
    float frameTime() const { return frameMs_; }
    void requestScreenshot(const std::string& path) { screenshotPath_ = path; }

    // Automated runs (tests / headless verification): quit after N frames and save
    // screenshots at chosen frames.
    int maxFrames = 0;
    std::vector<std::pair<int, std::string>> scheduledScreenshots;
    uint64_t frameIndex() const { return frame_; }
    /// Simulated frame delta used in headless mode (deterministic).
    float headlessDt = 1.0f / 60.0f;
    bool headless() const { return config_.headless; }

    /// Hook called every frame after the scenes rendered (debug overlay, toasts).
    std::function<void()> onOverlay;
    /// Hook for global hotkeys (returns true if consumed).
    std::function<bool(const SDL_Event&)> onGlobalEvent;

private:
    enum class Fade { None, Out, In };
    struct PendingOp {
        enum Kind { Push, Pop, Switch, ReplaceTop } kind;
        std::unique_ptr<Scene> scene;
        float fade = 0.35f;
    };
    void applyPending();
    void processEvents();
    void renderFrame(float alpha);
    void saveScreenshot(const std::string& path);

    AppConfig config_;
    SDL_Window* window_ = nullptr;
    Renderer renderer_;
    TextRenderer text_;
    InputManager input_;
    AudioSystem audio_;
    ResourceManager resources_;
    std::vector<std::unique_ptr<Scene>> scenes_;
    std::vector<PendingOp> pending_;
    std::unique_ptr<Scene> fadeTarget_;
    PendingOp::Kind fadeKind_ = PendingOp::Switch;
    Fade fadeState_ = Fade::None;
    float fadeT_ = 0.0f, fadeDur_ = 0.35f;
    bool running_ = false;
    bool fullscreen_ = false;
    float fps_ = 0.0f, frameMs_ = 0.0f;
    uint64_t frame_ = 0;
    std::string screenshotPath_;
};

}  // namespace cosmic
