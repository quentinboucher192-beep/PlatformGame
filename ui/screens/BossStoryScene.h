#pragma once

// « Histoire des giga-boss » (2.51): the story of a giga boss (game/content/BossStories.h), told
// over the screen it was opened from: a run (paused below it, as under the pause menu: the scene
// below is neither simulated nor updated), the boss's sheet or the Collection's gallery
// (« Revoir l'histoire »). Each shot shows its painting with a slow zoom and live particles,
// between letterbox bands; its line of narration is typed at the bottom, then stays until the
// shot ends; a progress bar and « Espace · Ⓐ Passer » sit at the bottom right. The title card
// reveals the boss (its sprite, « GIGA-BOSS · 8 PHASES », its name, its subtitle, its difficulty).
// Space, Enter, the gamepad's A or a click skips it (a short fade, then the screen below again).
// 2.60: a shot of version 2 is played as its animated scene instead (parallax layers, sprites
// moved by keyframes, glows, particles, a camera, shakes, flashes; with the same slow zoom and
// cross-fades); every file of the story is loaded when it opens, none while it plays.
// Display only.

#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Scene.h"

namespace cosmic {

class GameServices;
class UIContext;
struct AnimationSet;
struct AnimationClip;

/// Version 2's scenes: units of a stage per pixel of its sheets and layers (at scale 1).
constexpr float kStoryPixel = 2.0f;
/// The frame (source rectangle) of a scene's sprite: its clip `clip` (its idle clip, else its
/// first, when the sheet has no such clip) `t` seconds in; a looping clip wraps, another one holds
/// its last frame or goes on with the clip it names as its next.
Rect storySpriteFrame(const AnimationSet& set, const std::string& clip, float t);
/// The same with the clip already found (nullptr: the sheet's idle clip, else its first).
Rect storySpriteFrame(const AnimationSet& set, const AnimationClip* clip, float t);

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
/// revealed); -1: from its beginning, faded in. `atSeconds` (automated screenshots, 2.60): the
/// story that many seconds in, as it plays then (instead of `startAt`).
std::unique_ptr<Scene> makeBossStory(GameServices& g, const std::string& bossId, int startAt = -1, float atSeconds = -1.0f);

}  // namespace cosmic
