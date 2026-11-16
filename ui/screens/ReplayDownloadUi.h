#pragma once

// « Télécharger » next to the online replays (2.45): the replay and what the screen knows of
// it are kept on this computer (ReplayLibrary), to be watched again without a connection in
// Collection › Replays. The button says « Téléchargement… » while it downloads and
// « Téléchargé » once kept (it then opens that list). The leaderboards' rows take a small
// icon instead (an arrow; a tick once downloaded) whose tooltip the screen draws last.

#include <cstdint>
#include <optional>
#include <string>

#include "game/online/ReplayDownloads.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

class GameServices;

namespace ReplayDownloadUi {

/// A request for an online replay: `source` ("record", "community", "race", "challenge"…) and
/// the server's id make its local id; `shownName` is the pseudonym as the screen shows it.
ReplayDownloadRequest request(const std::string& source, const std::string& remoteId, const std::string& path, const std::string& player,
                              const std::string& shownName, const std::string& levelId, const std::string& levelName,
                              const std::string& category, int64_t timeMs, const std::string& achievedAt);

/// Its text button. `st` gives the size and font; the label follows the state.
void button(GameServices& g, const std::string& name, const Rect& r, const ReplayDownloadRequest& req, UIContext::ButtonStyle st);
/// The longest label button() may show (to measure a button that must fit it).
const char* longestLabel();

/// The small icon of a leaderboard's row (focusable, clickable), with its tooltip.
void icon(GameServices& g, const std::string& name, const Rect& r, const ReplayDownloadRequest& req, bool enabled = true);
/// The tooltip of the icon hovered or focused this frame, over the rest: to call once the
/// screen is drawn.
void drawTip(GameServices& g);

/// The copy kept on this computer of an online replay (its id: ReplayLibrary::makeId), ready
/// for the viewer (its level is the one noted at download); nullopt when it is not kept, or
/// unreadable (`error` then says why).
std::optional<ReplayData> localCopy(GameServices& g, const std::string& id, std::string* error = nullptr);

}  // namespace ReplayDownloadUi

}  // namespace cosmic
