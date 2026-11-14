#include "game/GameServices.h"

#include "engine/core/Log.h"

namespace cosmic {

void GameServices::loadAll() {
    settings = SettingsStore::load();
    content.reload();
    ratings.init(content);
    progression.load();
    records.load();
    news.load();
    news.startScan(content);
    applySettings();
}

void GameServices::checkForUpdate() {
    if (!online.usable()) return;
    online.get("/api/v1/client/latest?platform=windows", [this](const ApiResult& r) {
        if (!r.ok || !r.data.is_object() || !r.data.contains("release") || !r.data["release"].is_object()) return;
        const Json& rel = r.data["release"];
        std::string v = jstr(rel, "version");
        if (v.empty() || !versionNewer(v, COSMIC_VERSION_STRING)) return;
        news.offerRelease(v, jget<int64_t>(rel, "size", 0), jstr(rel, "sha256"), jstr(rel, "download"), jstr(rel, "notes"));
    }, false);
}

void GameServices::applySettings() {
    app.input().setBindings(settings.input);
    app.audio().setVolumes(settings.audio);
    if (!app.headless()) {
        if (app.fullscreen() != settings.graphics.fullscreen) app.setFullscreen(settings.graphics.fullscreen);
        app.setVSync(settings.graphics.vsync);
    }
    app.debugOverlay = app.debugOverlay && settings.gameplay.debugMode;
    progression.unlockAll = settings.gameplay.unlockAll;
}

void GameServices::playMusic(const std::string& rel) {
    std::string p = app.resources().soundPath(rel);
    if (!p.empty()) app.audio().playMusic(p, 1200);
}

}  // namespace cosmic
