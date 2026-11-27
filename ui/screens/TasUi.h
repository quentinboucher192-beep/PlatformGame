#pragma once

// The TAS in the screens (« meilleur temps trouvé par la machine », ui/screens/TasCatalog.h): its own
// colour (lime, outside the ghosts' palette), its diamond, its « MACHINE · HORS CLASSEMENT » pill, and
// the ways to it — its sheet (« Fiche du TAS »), its replay, the race against it, one of its segments.

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/core/Math.h"
#include "engine/core/Scene.h"
#include "engine/rendering/Texture.h"
#include "game/ghost/GhostRace.h"
#include "game/online/OnlineTypes.h"
#include "game/online/Replay.h"
#include "ui/screens/TasCatalog.h"

namespace cosmic {

class GameServices;
class Renderer;
class UIContext;

namespace tasui {

/// The machine's colour: lime, reserved to it (never one of the ghosts' colours).
inline constexpr Color kTas{200, 255, 60, 255};
inline constexpr Color kTasLine{200, 255, 60, 150};
inline constexpr Color kTasDeep{58, 74, 18, 235};   // its fills (rows, buttons)
inline constexpr Color kTasCard{46, 60, 14, 235};   // its cards (a gradient to the panel)
/// A time behind the TAS (every human time is): the warm text of the gaps.
inline constexpr Color kBehind{255, 176, 138, 255};

/// The machine's diamond: a square on its point, centred on `c`, `size` wide, filled faintly.
void diamond(Renderer& r, Vec2 c, float size, float line, float alpha = 1.0f);
/// The machine's ghost (world space, the view set): its own look — a luminous lime outline, scan lines
/// that run down it, barely filled — never one of the players' tinted silhouettes (shape and tag, not the
/// colour alone). `src` / `dst`: its animation frame; `viewScale`: the view's (the outline's width).
void drawGhost(Renderer& r, const Texture& tex, const Rect& src, const Rect& dst, bool flip, float alpha, float viewScale, float time);
/// A pill of the machine (« MACHINE · HORS CLASSEMENT », « MACHINE »): its left edge at `leftMid.x`,
/// centred on `leftMid.y`, `size` its font; returns its width (`draw` false: only measures).
float pill(UIContext& ui, const std::string& text, Vec2 leftMid, float size, bool draw = true);
/// The key of the device in use (a keycap on the keyboard, the gamepad's button on a pad), then
/// `text`: returns the width (`draw` false: only measures).
float keyHint(UIContext& ui, const std::string& key, SDL_GamepadButton pad, const std::string& text, Vec2 leftMid, float height, float fontSize,
              bool draw = true);
/// What a screen drew of the TAS the last frame (tests: nothing overlaps, everything inside): the frame
/// of the screen or of its part, then each text, button and picture of it, named.
struct Pieces {
    Rect frame;
    std::vector<std::pair<std::string, Rect>> items;
    void clear(const Rect& f) {
        frame = f;
        items.clear();
    }
    void add(const std::string& name, const Rect& r) { items.push_back({name, r}); }
};
/// The record of a screen (« sheet », « setup », « ranking », « stats », « replay », « ghosts »).
Pieces& pieces(const std::string& screen);

/// The category as the screens name it (« ANY% »…), and its rule in a few words.
std::string categoryName(RunCategory c);
std::string categoryShortRule(RunCategory c);

/// « Fiche du TAS » (ui/screens/TasSheetScene.cpp): the TAS of a level in one category (its tabs: the
/// level's other TAS), what it is, how it was found, its route and its splits. `onRace`: what « Course
/// contre le TAS » does when the screen under it is the ghosts' setup (it checks the TAS there); empty:
/// that setup opens with the TAS checked.
struct SheetRequest {
    std::string levelId;
    RunCategory category = RunCategory::AnyPercent;
    std::function<void(RunCategory)> onRace;
};
std::unique_ptr<Scene> makeSheet(GameServices& g, SheetRequest req);
/// Opens the sheet when the level has a TAS in `c` (else in its first category); false when none.
bool openSheet(GameServices& g, const std::string& levelId, RunCategory c, std::function<void(RunCategory)> onRace = {});
/// The TAS's replay in the replay viewer, from `at` seconds (its gap, its references as for any run).
bool watch(GameServices& g, const tas::TasRun& t, double at = 0.0);
/// « Course contre des fantômes » with the TAS of `c` checked.
void race(GameServices& g, const std::string& levelId, RunCategory c);
/// One segment of the TAS (`from` → `to`, checkpoint ids, "" = the start / the finish), from its replay.
bool watchSegment(GameServices& g, const tas::TasRun& t, const std::string& from, const std::string& to, const std::string& name);

}  // namespace tasui

/// The replay viewer on the TAS's replay (ui/screens/ReplayScene.cpp), from `at` seconds.
std::unique_ptr<Scene> makeTasReplayViewer(GameServices& g, ReplayData replay, const tas::TasRun& t, double at);

}  // namespace cosmic
