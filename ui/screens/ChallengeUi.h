#pragma once

// Drawing helpers of the challenges shared by « Défis » (ChallengesScene.cpp) and
// ADMINISTRATION › Calendrier des défis (AdminScene.cpp, 2.41): fitted labels and chips, the
// colours of the tiers, French dates, where a challenge's level is on this computer, the
// conditions of the tiers (« Vitesse » or « Finir le niveau »), and the calendar's cells (the
// level's preview behind them, the tags of what makes a challenge what it is).

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "engine/rendering/Text.h"
#include "engine/rendering/Texture.h"
#include "game/online/DailyChallenges.h"

namespace cosmic {

class GameServices;
class UIContext;

namespace challenge_ui {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size);
/// The size (from `size`, never below 11.5 pixels) at which `t` fits `room`.
float fit(UIContext& ui, const std::string& t, FontFace face, float size, float room, float minScale = 0.75f);
/// `t` fitted (then shortened with « … » if needed) in `room` pixels, drawn; returns its size.
Vec2 fitLabel(UIContext& ui, const std::string& t, Vec2 pos, float size, float room, Color c, Align a = Align::Left, FontFace face = FontFace::Body,
              VAlign va = VAlign::Middle, float minScale = 0.75f);

Color tierColor(ChallengeTier t);

/// A rounded chip whose left edge is at `x`, centred on `cy`; returns its width.
float chip(UIContext& ui, const std::string& text, float x, float cy, float h, Color c, float maxW = 1e9f, bool filled = false);
float chipWidth(UIContext& ui, const std::string& text, float h);

/// "63,4 %", "100 %" (how far an attempt went).
std::string percentText(float p);
/// "16 novembre 2026" (`year` false: "16 novembre").
std::string frenchDate(const std::string& ymd, bool year = true);
/// "novembre 2026" of "2026-11".
std::string frenchMonth(const std::string& ym);
/// "Novembre" of 1..12.
const char* monthTitle(int month);

/// Where the challenge's level is on this computer.
struct LevelPlace {
    std::string localId;       // "" when it is not installed
    bool sameVersion = false;  // the version of the challenge (its fingerprint)
    std::string problem;       // why it cannot be played ("" = it can)
};
LevelPlace levelPlace(GameServices& g, const DailyChallenge& c);

/// "Glacior · niveau 2" of a campaign level ("" when unknown).
std::string planetLineOf(GameServices& g, const std::string& levelId);
/// "Glacior · niveau 2" (campaign) or "" (community: its author is drawn verbatim by the caller).
std::string planetLine(GameServices& g, const DailyChallenge& c);
/// The level's name as shown (campaign: the game's name, translated; community: verbatim).
std::string levelTitle(GameServices& g, const DailyChallenge& c);
/// "Dash, Grappin" (the powers' names).
std::string powerList(GameServices& g, const std::vector<std::string>& ids);

/// The condition of a tier: « Vitesse »: "Arriver au bout", "En 1:45 ou moins", "En 1:20 ou
/// moins, sans dégât"; « Finir le niveau »: "Atteindre 50 % du parcours", "Finir le niveau,
/// sans dégât".
std::string tierCondition(const DailyChallenge& c, ChallengeTier t);

/// The calendar's cells (2.38): each challenge's level behind its cell (its preview, cropped
/// and darkened under the text), and the tags of what makes it what it is.
class CalendarArt {
public:
    explicit CalendarArt(GameServices& g) : g_(g) {}
    /// Once per frame: one missing preview rendered per frame at most (the calendar fills in
    /// over a few frames, never one long wait).
    void beginFrame() { budget_ = 1; }
    /// The preview of a challenge's level, one per level version, kept in memory and on disk
    /// (cache/calendar); nullptr until it is ready, or when the level is not installed.
    TexturePtr cellThumb(const DailyChallenge& c);
    /// The preview of an installed level (`localId`), cached under `version` (any text that
    /// changes with the level: its fingerprint, its revision); same budget as cellThumb.
    TexturePtr levelThumb(const std::string& localId, const std::string& version);
    /// The level's preview filling `r` (cropped to its shape), darkened so that the text above
    /// stays readable; nothing while it is not ready.
    void drawCellImage(const Rect& r, const DailyChallenge& c, bool lit);
    /// EXTRÊME, a community level, its hearts, no checkpoint, no power station.
    std::vector<std::pair<std::string, Color>> tagsOf(const DailyChallenge& c);
    /// The tags from `x` to `right` on the line `cy`: each whole (one that does not fit and
    /// those after it are left out, never cut).
    void drawTags(const DailyChallenge& c, float x, float cy, float right, float h);
    /// An EXTRÊME level (one life), read once per level version (a community level: as installed).
    bool extremeLevel(const DailyChallenge& c);

private:
    GameServices& g_;
    std::map<std::string, TexturePtr> thumbs_;
    std::map<std::string, bool> extremeOf_;
    int budget_ = 0;
};

}  // namespace challenge_ui
}  // namespace cosmic
