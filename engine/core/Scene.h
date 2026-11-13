#pragma once

#include <SDL3/SDL.h>

namespace cosmic {

class Application;

/// A screen of the game (menu, gameplay, editor...). Scenes form a stack managed by
/// the Application; overlays (pause menu, dialogs) render on top of the scene below.
class Scene {
public:
    virtual ~Scene() = default;

    virtual void onEnter() {}
    virtual void onExit() {}
    /// Called when the scene above this one was removed.
    virtual void onResume() {}
    virtual void handleEvent(const SDL_Event& /*e*/) {}
    /// Fixed-rate simulation step (Application::kFixedDt).
    virtual void fixedUpdate(float /*dt*/) {}
    /// Variable-rate update (visual effects, UI).
    virtual void update(float /*dt*/) {}
    /// `alpha` = interpolation factor between the two last fixed steps.
    virtual void render(float alpha) = 0;
    /// Extra lines for the debug overlay (F3).
    virtual void renderDebug() {}

    /// Overlays let the scene below render (and optionally update).
    virtual bool isOverlay() const { return false; }
    virtual bool updatesSceneBelow() const { return false; }
    /// While this scene is on top, non-essential popups (online notifications) wait.
    virtual bool holdsNotifications() const { return false; }

    void attach(Application* app) { app_ = app; }
    Application& app() { return *app_; }

protected:
    Application* app_ = nullptr;
};

}  // namespace cosmic
