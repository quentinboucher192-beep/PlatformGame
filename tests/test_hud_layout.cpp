// HUD layout (Paramètres › Personnaliser le HUD): saved with the settings, read back safely.

#include "saves/Settings.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("hud layout: places, sizes and visibility survive a save") {
    Settings s;
    s.gameplay.hud[HudElement::Timer] = {true, 0.25f, 0.8f, 0.75f, true};
    s.gameplay.hud[HudElement::Vitals].scale = 1.4f;
    s.gameplay.hud[HudElement::Boss].visible = false;
    s.gameplay.hud[HudElement::Progress] = {true, 0.5f, 0.9f, 1.0f, true};
    s.gameplay.progressBar = 3;
    Settings r = Settings::fromJson(s.toJson());
    CHECK(r.gameplay.hud[HudElement::Timer].custom);
    CHECK_NEAR(r.gameplay.hud[HudElement::Timer].x, 0.25f, 1e-5);
    CHECK_NEAR(r.gameplay.hud[HudElement::Timer].y, 0.8f, 1e-5);
    CHECK_NEAR(r.gameplay.hud[HudElement::Timer].scale, 0.75f, 1e-5);
    CHECK(!r.gameplay.hud[HudElement::Vitals].custom);  // default place, only bigger
    CHECK_NEAR(r.gameplay.hud[HudElement::Vitals].scale, 1.4f, 1e-5);
    CHECK(!hudVisible(r.gameplay, HudElement::Boss));
    CHECK(r.gameplay.progressBar == 3);
}

TEST_CASE("hud layout: bad values are clamped, a custom bar without a place falls back") {
    Json j = {{"gameplay",
               {{"progressBar", 3},
                {"hud",
                 {{"vitals", {{"scale", 50.0}, {"x", -3.0}, {"y", 9.0}}},
                  {"powers", {{"scale", "grand"}}},
                  {"timer", "n'importe quoi"}}}}}};
    Settings s = Settings::fromJson(j);
    const HudPlace& v = s.gameplay.hud[HudElement::Vitals];
    CHECK_NEAR(v.scale, HudLayout::kMaxScale, 1e-5);
    CHECK(v.x >= 0.0f && v.x <= 1.0f && v.y >= 0.0f && v.y <= 1.0f);
    CHECK_NEAR(s.gameplay.hud[HudElement::Powers].scale, 1.0f, 1e-5);
    CHECK(!s.gameplay.hud[HudElement::Timer].custom);
    CHECK(s.gameplay.progressBar == 1);  // "where the player put it" needs a place
}

TEST_CASE("hud layout: the timer and the progress bar keep their own settings") {
    GameplaySettings g;
    setHudVisible(g, HudElement::Timer, false);
    CHECK(!g.showTimer);
    g.progressBar = 2;
    setHudVisible(g, HudElement::Progress, false);
    CHECK(g.progressBar == 0);
    setHudVisible(g, HudElement::Progress, true);
    CHECK(g.progressBar == 1);
    g.hud[HudElement::Progress].custom = true;
    setHudVisible(g, HudElement::Progress, true);
    CHECK(g.progressBar == 3);
}

TEST_CASE("hud layout: focus mode keeps only its elements, and its own level messages choice") {
    GameplaySettings g;
    // Defaults: health, powers and the boss stay; the rest goes in focus mode.
    CHECK(g.hud[HudElement::Vitals].focus);
    CHECK(g.hud[HudElement::Powers].focus);
    CHECK(g.hud[HudElement::Boss].focus);
    CHECK(!g.hud[HudElement::Timer].focus);
    CHECK(!g.hud[HudElement::Splits].focus);
    CHECK(hudVisible(g, HudElement::Timer));
    CHECK(messagesVisible(g));
    g.focusMode = true;
    CHECK(!hudVisible(g, HudElement::Timer));
    CHECK(!hudVisible(g, HudElement::Collectibles));
    CHECK(hudVisible(g, HudElement::Vitals));
    CHECK(!messagesVisible(g));  // hidden by default in focus mode
    g.focusMessages = true;
    CHECK(messagesVisible(g));
    // An element hidden in the full HUD stays hidden in focus mode.
    g.hud[HudElement::Powers].visible = false;
    CHECK(!hudVisible(g, HudElement::Powers));
    // Saved and read back.
    Settings s;
    s.gameplay = g;
    s.gameplay.hud[HudElement::Timer].focus = true;
    s.gameplay.showMessages = false;
    Settings r = Settings::fromJson(s.toJson());
    CHECK(r.gameplay.focusMode);
    CHECK(r.gameplay.focusMessages);
    CHECK(!r.gameplay.showMessages);
    CHECK(r.gameplay.hud[HudElement::Timer].focus);
    CHECK(!r.gameplay.hud[HudElement::Collectibles].focus);
    // A file from before 2.18: the defaults.
    Settings old = Settings::fromJson(Json::parse(R"({"gameplay":{"hud":{"timer":{"scale":1.0,"visible":true}}}})"));
    CHECK(!old.gameplay.focusMode);
    CHECK(old.gameplay.showMessages);
    CHECK(!old.gameplay.hud[HudElement::Timer].focus);
    CHECK(old.gameplay.hud[HudElement::Vitals].focus);
}
