#pragma once

// « Histoire des giga-boss » (2.51): the story of a giga boss (game/content/BossStories.h), told
// over the screen it was opened from: a run (paused below it, as under the pause menu: the scene
// below is neither simulated nor updated), the boss's sheet or the Collection's gallery
// (« Revoir l'histoire »). Each shot shows its painting with a slow zoom and live particles,
// between letterbox bands; its line of narration is typed at the bottom, then stays until the
// shot ends; a progress bar and « Espace · Ⓐ Passer » sit at the bottom right. The title card
// reveals the boss (its sprite, « GIGA-BOSS · 8 PHASES », its name, its subtitle, its difficulty).
// Space, Enter, the gamepad's A or a click skips it (a short fade, then the screen below again).
// Display only.

#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Scene.h"

namespace cosmic {

class GameServices;
class UIContext;

/// The bottom band of a story: its progress bar, the room of the line of narration, left of the
/// measured « Espace · Ⓐ Passer » hint, and the line wrapped there (at most two lines, at the
/// largest size that fits: a smaller size rather than a third line).
constexpr size_t kStoryNarrationLines = 2;
struct StoryNarrationLayout {
    Rect bar;    // the progress bar
    Rect hint;   // « Espace · Ⓐ Passer » (the right part of the band)
    Rect area;   // the room of the narration, left of the hint
    float px = 0.0f;     // font size (pixels)
    float lineH = 0.0f;  // line height (pixels)
    float top = 0.0f;    // top of the first line
    std::vector<std::string> lines;  // the line as displayed, wrapped
};
/// `shown`: the line as displayed (translated). Uses the size of the screen of `ui` (begun).
StoryNarrationLayout layoutStoryNarration(UIContext& ui, const std::string& shown);

/// The story of `bossId`; nullptr when it has none. `startAt` (automated screenshots): the shot
/// to start from (0 = the first), its line already typed (the number of shots: the title card
/// revealed); -1: from its beginning, faded in.
std::unique_ptr<Scene> makeBossStory(GameServices& g, const std::string& bossId, int startAt = -1);

}  // namespace cosmic
