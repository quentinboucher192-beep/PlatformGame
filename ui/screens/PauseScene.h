#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Scene.h"
#include "engine/input/InputManager.h"
#include "game/achievements/Achievements.h"
#include "ui/widgets/CosmicStyle2.h"

namespace cosmic {

class GameServices;
class GameScene;
class UIContext;
struct PauseLayout;
struct PauseSideTracked;

/// The right of the two switch rows of the pause, « Invincible (admin) » and « Mode concentré »: the
/// state word (ACTIVÉ / DÉSACTIVÉ; « NON CLASSÉ » with it for Invincible when on), the switch, then
/// the slot of the « › » (Mode concentré: the button that opens the HUD editor on its tab;
/// Invincible: the plate's « › » of the focus). The same on both rows, so that their switches line
/// up. Measured in the language shown, for plates of unit `pu` (screen pixels per reference pixel:
/// the plate's height / 64); sizes in screen pixels.
struct PauseSwitchRight {
    std::string on, off, unranked;  // the words drawn (capitals, already in the language shown)
    std::string onUnranked;         // Invincible when on: « ACTIVÉ » over « NON CLASSÉ », or on one line
    float font = 0.0f;              // their size, unscaled (UIContext::label)
    float lineH = 0.0f;             // one line of them
    bool twoLines = true;           // « NON CLASSÉ » under « ACTIVÉ » (the plate is high enough)
    float stateW = 0.0f;            // the widest state of the rows shown
    float switchW = 0.0f, switchH = 0.0f;
    float arrowW = 0.0f, arrowH = 0.0f;  // the slot of the « › », the button in it
    float gap = 0.0f;                    // between the state, the switch and the slot
    float width = 0.0f;                  // all of it (cstyle::PlateEx::rightW)
};
/// `unranked`: the row « Invincible (admin) » is shown (its words count in the width).
PauseSwitchRight pauseSwitchRight(UIContext& ui, float pu, bool unranked);

/// Pause menu overlay: resume, restart, settings, back to menu / editor.
class PauseScene : public Scene {
public:
    PauseScene(GameServices& g, GameScene& game) : g_(g), game_(game) {}
    void onEnter() override;
    void onExit() override;
    /// Back from a screen opened over it (Succès…): the focus goes back to where it was.
    void onResume() override;
    void update(float dt) override;
    void render(float alpha) override;
    bool isOverlay() const override { return true; }

private:
    /// The map of what was explored; `tracked`: « Succès suivis » under it (2.60).
    void drawMapPanel(const Rect& panel, const std::vector<const AchievementDef*>* tracked, bool note);
    /// « Succès suivis » (2.52) in the column: each achievement followed, its numbers, a line of
    /// its description when there is room, its bar; then the free places.
    void drawTracked(const Rect& r, const PauseLayout& L, const std::vector<const AchievementDef*>& rows, bool note);
    /// 2.60: under the map, the five places, each description on up to `maxLines` lines.
    void drawTrackedSide(const Rect& r, const PauseSideTracked& M, const std::vector<const AchievementDef*>& rows, bool note, int maxLines);
    /// The lines each description takes under the map at `width` (at most `maxLines`).
    std::vector<int> trackedSideLines(const std::vector<const AchievementDef*>& rows, float width, int maxLines);
    void drawTrackedHeader(const Rect& r, float headH, float fs, int count);
    void drawTrackedRow(const AchievementDef& a, const Rect& row, float nameH, float lineH, float barH, const std::vector<std::string>& desc,
                        float fs, float dfs);

private:
    GameServices& g_;
    GameScene& game_;
    float t_ = 0.0f;
    FreshKeys keys_;  // titan 13: what was held when the pause opened waits for its release
    bool confirmQuit_ = false;
    float mapPan_ = 0.0f;  // world units the map was scrolled by
    bool keysOpened_ = false;  // automated screenshots: « Touches de ce niveau » opened once
    std::string resumeFocus_;  // the focus to give back when a screen opened over it closes
    bool hudKey_ = false;      // → on « Mode concentré » this frame: the HUD editor opens (the map does not pan)
    // The achievements' counters with this run's progress, and what their conditions read.
    AchievementStats liveStats_;
    AchievementInputs achInputs_;
    // 2.84: the Cosmic kit (plates, holographic panel, chrome title).
    cstyle::Anims anims_;
    cstyle::HoloCard card_;
    cstyle::ChromeText title_;
};

}  // namespace cosmic
