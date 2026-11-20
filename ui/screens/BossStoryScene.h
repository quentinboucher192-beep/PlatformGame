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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Scene.h"

namespace cosmic {

class GameServices;
class UIContext;
struct BossStory;
struct StoryRoster;
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
/// `skipHint` false (2.68, a film's first viewing): no hint, the line has the band's whole width.
StoryNarrationLayout layoutStoryNarration(UIContext& ui, const std::string& shown, bool skipHint = true);

/// The story of `bossId`; nullptr when it has none. `startAt` (automated screenshots): the shot
/// to start from (0 = the first), its line already typed (the number of shots: the title card
/// revealed); -1: from its beginning, faded in. `atSeconds` (automated screenshots, 2.60): the
/// story that many seconds in, as it plays then (instead of `startAt`).
std::unique_ptr<Scene> makeBossStory(GameServices& g, const std::string& bossId, int startAt = -1, float atSeconds = -1.0f);

/// 2.63: the game's trailer (GameServices::trailer, data/trailer/trailer.json), played by the same
/// scene as the stories: its shots with their own lengths, their titles large in the picture, the
/// game's name on its last card, its own music. `attract`: the main menu's attract mode (it starts
/// again at its end, through black, as long as nobody touches anything; any input — a key, a
/// button, a click, the mouse or a stick moving — leaves it at once, without acting on the menu).
/// nullptr when there is no trailer. `atSeconds` (automated screenshots): that moment of it.
std::unique_ptr<Scene> makeTrailer(GameServices& g, bool attract, float atSeconds = -1.0f);
/// 2.63: the shot a story (or the trailer) `s` is playing (its number of shots: the title card); -1
/// when `s` is not one. « Passer » goes on to the next shot, « Quitter » leaves the whole story.
int storyShot(const Scene* s);
/// The passes of the trailer `s` plays in attract mode (0 during the first); -1 when `s` is not one.
int trailerPasses(const Scene* s);
/// 2.68: a mini-film (a BossStory of kind « film », Asterion's: data/raids/asterion_film.json), played
/// by the same scene: its shots with their own lengths, their captions typed at the bottom (none
/// when empty), their titles large in the picture, its own music (the music before it comes back
/// after it), no title card; it rises from black and ends with a slow fade to the screen below.
/// `locked` (a first viewing): nothing skips it or leaves it — no key, button or click — and no
/// hint says otherwise; only closing the game's window stops it. Else « Passer » and « Quitter »,
/// as a story. `onEnd`: called once when it plays to its end (never when it is left or
/// interrupted). `atSeconds` (automated screenshots): that moment of it. nullptr without a film.
/// 2.74 (phase 6): `keepBelow`: the screen below keeps running under it (the raid's victory film over its
/// combat: the instance's lockstep never stops; the screen below sends neutral inputs meanwhile).
std::unique_ptr<Scene> makeFilm(GameServices& g, std::shared_ptr<const BossStory> film, bool locked, std::function<void()> onEnd = {},
                                float atSeconds = -1.0f, bool keepBelow = false);
/// 2.68: whether `s` is a film played locked (a first viewing).
bool filmLocked(const Scene* s);

/// 2.81 (« Après Asterion »): how a film is played beyond makeFilm.
struct FilmPlay {
    /// The names its roster shot writes (StoryRoster: the clans present at the fall, as given by the server, each
    /// « [TAG] Nom »), none: the roster's generic text (offline, a test, an administrator's preview).
    std::vector<std::string> names;
    int own = -1;  // the viewer's clan among them (highlighted), -1: none
    /// Where it starts (seconds of the film). With `synced` (played with others at the server's time), a negative
    /// value is a countdown before its first image; a late start (more than kFilmLateJoin) offers « Depuis le début ».
    float startAt = -1.0f;
    bool synced = false;
    bool keepBelow = false;  // over a raid's combat (which keeps running)
    /// Once it is closed (its scene gone): whether it played to its end. A next film is pushed from here.
    std::function<void(bool ended)> onClose;
};
/// A late joiner is offered « Depuis le début » when the film is already this far (seconds).
constexpr float kFilmLateJoin = 2.0f;
std::unique_ptr<Scene> makeFilmPlay(GameServices& g, std::shared_ptr<const BossStory> film, FilmPlay play);
/// Whether `s` is a film played at the server's time with others (and still in step with them).
bool filmSynced(const Scene* s);
/// The time a film `s` is at (seconds; negative during a countdown); NaN when `s` is not one.
float filmTime(const Scene* s);
/// The story or film `s` plays (nullptr when `s` is not one).
const BossStory* storyOf(const Scene* s);

/// The names of a roster shot, measured: centred rows of pills (the viewer's clan in its colour, « · votre clan »
/// after its name), as large as the area allows down to a floor, then « et N autres clans »; or the generic text.
/// `top`: nothing above it (the bottom of the shot's title). Every rect inside `area`, none overlapping.
struct FilmRosterLayout {
    Rect area;
    float px = 0.0f;
    bool generic = false;
    std::vector<std::string> texts;  // as displayed (a name, « … · votre clan », « et N autres clans »)
    std::vector<Rect> pills;
    std::vector<int> index;  // the name of each pill (-1: « et N autres clans » or the generic text)
};
FilmRosterLayout layoutFilmRoster(UIContext& ui, const Rect& img, const StoryRoster& roster, const std::vector<std::string>& names, int own,
                                  float top);

/// The top band of a film played with others: « EN DIRECT · avec les clans de la chute » at the left, the
/// button « Depuis le début » (« Commencer maintenant » during the countdown) at the right, its keys in it; measured.
struct FilmLiveLayout {
    Rect band, label, button;
    float labelPx = 0.0f, buttonPx = 0.0f, keyH = 0.0f;
    std::string labelText, buttonText;
    bool showButton = false;
};
/// 2.82: `reserveRight` pixels kept free at the band's right (the « Mode Film » button).
FilmLiveLayout layoutFilmLive(UIContext& ui, bool countdown, bool canRestart, float reserveRight = 0.0f);
/// 2.82: « Mode Film » (Settings › gameplay.filmMode): the button that turns it on, at the right of the top band of
/// every film and story (« F · Ⓨ  Mode Film »), measured; in Film mode, the discreet button that turns it off (shown
/// while the mouse moves) at the same place, and the notice shown when it is turned on.
struct FilmModeLayout {
    Rect button;
    float px = 0.0f, keyH = 0.0f;
    std::string text;
    Rect notice;
    float noticePx = 0.0f;
    std::string noticeText;
};
FilmModeLayout layoutFilmMode(UIContext& ui, bool on);
/// Whether a story or film scene `s` is shown in Film mode now.
bool filmModeShown(const Scene* s);
/// The main menu's attract mode: after this long without any input, the trailer plays.
constexpr float kAttractDelay = 60.0f;
/// Whether the attract mode may start now: its setting on (Paramètres › Jeu), a trailer, nothing
/// being typed, the tutorial done, no update being downloaded.
bool attractAllowed(GameServices& g);

}  // namespace cosmic
