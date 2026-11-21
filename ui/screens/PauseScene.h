#pragma once

#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Scene.h"
#include "game/achievements/Achievements.h"
#include "ui/widgets/CosmicStyle2.h"

namespace cosmic {

class GameServices;
class GameScene;
struct PauseLayout;
struct PauseSideTracked;

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
    bool confirmQuit_ = false;
    float mapPan_ = 0.0f;  // world units the map was scrolled by
    bool keysOpened_ = false;  // automated screenshots: « Touches de ce niveau » opened once
    std::string resumeFocus_;  // the focus to give back when a screen opened over it closes
    // The achievements' counters with this run's progress, and what their conditions read.
    AchievementStats liveStats_;
    AchievementInputs achInputs_;
    // 2.84: the Cosmic kit (plates, holographic panel, chrome title).
    cstyle::Anims anims_;
    cstyle::HoloCard card_;
    cstyle::ChromeText title_;
};

}  // namespace cosmic
