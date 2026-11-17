#pragma once
// Pieces shared by the screens of the clan raids (CLANS › RAIDS, the sheet of an event,
// ADMINISTRATION › Raids): the colours of the maquette, texts measured into their room
// (smaller, then shortened, or wrapped on a counted number of lines), pills, sections, and the
// times shown in the computer's own zone (the titan: ui/widgets/AsterionFigure).

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/online/Raids.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::raidui {

inline const Color kOrange{255, 138, 76, 255};
inline const Color kGold{255, 210, 122, 255};
inline const Color kPeach{255, 207, 168, 255};
inline const Color kSky{159, 216, 255, 255};
inline const Color kDim{185, 191, 230, 255};
inline const Color kText{238, 240, 255, 255};
inline const Color kPanel{18, 24, 54, 238};
inline const Color kCard{26, 33, 72, 245};
inline const Color kBorder{44, 53, 102, 255};
inline const Color kInk{26, 15, 46, 255};
inline const Color kGreen{124, 240, 168, 255};

/// `t` at `size` when it fits `room`, else smaller (down to 75 %), then shortened with « … ».
/// `verbatim`: a player's words (names, instructions) are never translated.
Vec2 fit(UIContext& ui, const std::string& t, Vec2 pos, float size, float room, Color c, FontFace face = FontFace::Body, Align a = Align::Left,
         bool verbatim = false);
/// Width of `t` at `size` (unscaled), translated as it will be drawn.
float textW(UIContext& ui, const std::string& t, float size, FontFace face = FontFace::Body, bool verbatim = false);
/// A filled pill whose left edge is `x`, centred on `cy`: its width, 0 when it does not fit before `maxRight`.
float pill(UIContext& ui, float x, float cy, const std::string& t, Color fill, Color text, float maxRight, float size = 14);
/// The lines of `t` (translated first) wrapped to `width`, `maxLines` at most (the last one
/// shortened with « … » when the text goes on).
std::vector<std::string> wrap(UIContext& ui, const std::string& t, FontFace face, float size, float width, int maxLines, bool verbatim = false);
/// Draws lines made by wrap() from `pos` (top left), `lineH` apart; the height used.
float drawLines(UIContext& ui, const std::vector<std::string>& lines, Vec2 pos, float size, float lineH, Color c, FontFace face = FontFace::Body);
/// A panel of the maquette with its title in small capitals (fitted to the panel); the y under the title.
float section(UIContext& ui, const Rect& r, const std::string& title, Color titleColor);
/// The colours of a state's pill.
Color stateFill(raids::State s);
Color stateText(raids::State s);
/// Toggle chips that wrap from (x, y) to `right`: each measured to its label. Returns the index
/// clicked (-1 when none) and sets `bottom` under the last row.
int chips(UIContext& ui, const std::string& id, float x, float y, float right, const std::vector<std::string>& labels, const std::vector<bool>& on,
          float& bottom, bool enabled = true);

/// The UTC now (seconds) of this computer, corrected by the server's clock (`skew`).
int64_t nowUtc(int64_t skew);
/// server_time − this computer's clock, from an answer of the server (0 without one).
int64_t skewOf(const Json& data);
/// « samedi 21 novembre · 21:00 à votre heure (UTC+1) · 20:00 UTC ».
std::string whenLine(int64_t utc);
/// « sam. 21 nov. 21:00 » (the computer's zone).
std::string whenShort(int64_t utc);

}  // namespace cosmic::raidui
