#include "game/GameServices.h"

#include "engine/core/Log.h"

namespace cosmic {

void GameServices::loadAll() {
    settings = SettingsStore::load();
    content.reload();
    progression.load();
    records.load();
    applySettings();
}

void GameServices::applySettings() {
    app.input().setBindings(settings.input);
    app.audio().setVolumes(settings.audio);
    if (!app.headless()) {
        if (app.fullscreen() != settings.graphics.fullscreen) app.setFullscreen(settings.graphics.fullscreen);
        app.setVSync(settings.graphics.vsync);
    }
    app.debugOverlay = app.debugOverlay && settings.gameplay.debugMode;
}

void GameServices::playMusic(const std::string& rel) {
    std::string p = app.resources().soundPath(rel);
    if (!p.empty()) app.audio().playMusic(p, 1200);
}

}  // namespace cosmic
