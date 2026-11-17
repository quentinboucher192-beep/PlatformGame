#pragma once

// 2.65: « MANETTE DÉBRANCHÉE » (docs/COOP.md § 3, mockup « Débranché »): a player's gamepad went
// away during a run; the run and its chrono are paused (no step at all, outside the simulation)
// until the same pad comes back, another free pad takes the place (A), the keyboard does
// (Entrée), or, in cooperation, the team goes on without that player. Then 3, 2, 1.

#include <memory>
#include <string>

#include "engine/core/Math.h"
#include "engine/core/Scene.h"

namespace cosmic {

class GameScene;
class GameServices;
class UIContext;

struct DeviceLostView {
    bool coop = false;          // cooperation on one PC: « Continuer sans … », the player's number
    std::string seat;           // « J2 » (cooperation)
    std::string name;           // the player's name (cooperation)
    std::string device;         // « Manette Xbox (n° 1) »
    Color color{124, 240, 240, 255};
    bool keyboardFree = true;   // Entrée gives the player the keyboard
    int countdown = 0;          // 3, 2, 1 once every player has a device again; 0 while waiting
    std::string message;        // under the buttons (« toujours débranchée… »), "" for none
    bool confirmQuit = false;   // « Quitter la partie » asked once: the button asks to confirm
};

enum class DeviceLostChoice { None, Resume, ContinueWithout, Quit };

/// Draws the panel over the paused run (every text measured and placed one after the other);
/// returns the button chosen this frame. Its buttons are « unplug/resume », « unplug/without »
/// and « unplug/quit » (focus « unplug/resume » when it opens).
DeviceLostChoice drawDeviceLost(UIContext& ui, const DeviceLostView& v);

/// The solo game's pause (GameScene): `padNumber`, the pad lost (DeviceRegistry).
std::unique_ptr<Scene> makeDeviceLost(GameServices& g, GameScene& game, int padNumber);

}  // namespace cosmic
