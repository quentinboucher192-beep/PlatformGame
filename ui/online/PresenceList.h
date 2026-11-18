#pragma once
// 2.72 « Qui est en ligne » in the lists of a clan: CLANS › Membres (ClanScene) and the
// registrants of a raid's sheet (RaidEventScene). Each player has the pill of SOCIAL
// (drawPresenceDot: en ligne, absent, hors ligne) and the list's header says « X/Y en ligne ».
//
// Everything comes from the server (server/src/friends.ts: listPresenceOf, countOnline): a
// player's presence goes only to the members of their clan, from the heartbeat of their game
// (never what a client says); a hidden presence (« Montrer ma présence » unticked) reads and
// counts « hors ligne »; « X » is the server's count (`members_online`, `roster_online`) of the
// players « en ligne » (« absent » is not counted). Nothing is guessed here: without that answer
// (no account, another clan, an older server) or with an answer of the offline cache (no longer
// true), the list shows no pill and no counter.

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/rendering/Text.h"

namespace cosmic {

class UIContext;

namespace presencelist {

/// The presence of a list's entry as it may be drawn: « online », « away » or « offline » when
/// the server sent one in a fresh answer (« hidden », from a server before 2.72, reads
/// « offline »); "" otherwise: no pill.
std::string shown(const Json& entry, bool fromCache);

/// The « X » the server counted under `key` of `data`, or -1 (no counter): missing, not a whole
/// number between 0 and `total`, or an answer of the offline cache.
int onlineCount(const Json& data, const char* key, int total, bool fromCache);

/// « X/Y en ligne » (English « X/Y online »); "" when `online` is unknown (< 0).
std::string counterText(int online, int total);

/// The order in which `list` is shown: « en ligne » first, then « absent », « hors ligne », then
/// the entries without a presence; the server's order is kept inside each group.
std::vector<size_t> orderByPresence(const Json& list, bool fromCache);

/// A list's header on one line centred on `cy`: the title from `x`, the counter right-aligned
/// at `right` (measured first, never more than `counterShare` of the line). Each text keeps its
/// size when it fits its room, else gets smaller (down to 75 %), then shortened with « … »: no
/// text overlaps the other nor leaves the line.
struct HeaderLayout {
    Rect title, counter;                         // the room of each text (counter empty: none)
    float titleSize = 0.0f, counterSize = 0.0f;  // sizes to draw at (unscaled, as ui.label)
    std::string titleText, counterText;          // as drawn (shortened when needed)
};
HeaderLayout layoutHeader(UIContext& ui, float x, float right, float cy, const std::string& title, float titleSize, FontFace titleFace,
                          const std::string& counter, float counterSize, FontFace counterFace, float counterShare = 0.45f);
void drawHeader(UIContext& ui, const HeaderLayout& L, FontFace titleFace, Color titleColor, FontFace counterFace, Color counterColor);
/// The counter's colour: the green of « en ligne » when someone is, else `none`.
Color counterColor(int online, Color none);

}  // namespace presencelist
}  // namespace cosmic
