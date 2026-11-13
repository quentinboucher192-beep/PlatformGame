// TEMPORARY: placeholders replaced as each screen is implemented.
#include "game/GameServices.h"
#include "ui/screens/Navigation.h"

namespace cosmic {
namespace {
class Placeholder : public Scene {
public:
    Placeholder(GameServices& g, const char* name) : g_(g), name_(name) {}
    void render(float) override {
        g_.ui.begin(g_.app, 0.016f);
        g_.ui.r().fillRect({0, 0, g_.ui.screenW(), g_.ui.screenH()}, {10, 12, 24, 255});
        g_.ui.label(name_, {g_.ui.screenW() * 0.5f, g_.ui.screenH() * 0.5f}, 40, {}, Align::Center);
        if (g_.ui.backPressed()) g_.app.switchScene(makeMainMenu(g_));
        g_.ui.end();
    }
private:
    GameServices& g_;
    const char* name_;
};
}  // namespace
std::unique_ptr<Scene> makeSettingsScene(GameServices& g, bool) { return std::make_unique<Placeholder>(g, "settings"); }
std::unique_ptr<Scene> makeLevelEditor(GameServices& g, const std::string&) { return std::make_unique<Placeholder>(g, "level editor"); }
std::unique_ptr<Scene> makeEnemyEditor(GameServices& g) { return std::make_unique<Placeholder>(g, "enemy editor"); }
std::unique_ptr<Scene> makeContentManager(GameServices& g) { return std::make_unique<Placeholder>(g, "content"); }
}  // namespace cosmic
