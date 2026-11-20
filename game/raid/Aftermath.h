#pragma once
// 2.81: « Après Asterion », the film of what the fall changes in the world (data/raids/asterion_apres.json), watched
// together. The server stamps its start once the Heart's fall is validated (raid_heart_events.film_at: the fall plus
// kDelayMs, the victory film's length and a margin; never a game's claim) and names the clans present at the fall
// (raid_clan_titles « Briseurs d'étoile »). Every viewer plays it at the same server time: the same server time gives
// the same image; a late joiner starts mid-film. Display only: nothing of the simulation, the replays or the records
// depends on it.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic::aftermath {

/// The film starts this long after the fall's validation (the server's FILM_DELAY_MS, raidHeart.ts).
constexpr int64_t kDelayMs = 90'000;

struct Clan {
    std::string id, tag, name;
};

/// What the server says of a fallen Heart (GET /raids/events/{id} › `aftermath`).
struct Party {
    std::string eventId;
    int64_t fallenAtMs = 0;  // server times, milliseconds since the epoch
    int64_t filmAtMs = 0;
    std::vector<Clan> clans;  // present at the fall, in the server's order
    std::string myClanId;     // the viewer's clan (highlighted when it is among them)
    bool tookPart = false;    // registered for the event, or a member of a clan present at the fall
    bool official = false;    // the world outcome « Asterion est tombé » is official (frozen)
    bool unlocked = false;    // tookPart || official: it can be watched again afterwards
};

/// Milliseconds since the epoch of an ISO 8601 UTC time (« 2026-11-20T18:04:05.250Z »); nullopt when unreadable.
std::optional<int64_t> parseIsoMs(const std::string& iso);
/// From the `aftermath` object of an event (`eventId` its id); nullopt when there is none or it is unreadable.
std::optional<Party> partyFromJson(const Json& j, const std::string& eventId);
/// server − this computer's clock (ms), from an answer's `server_time` received at `localNowMs` (0 when unreadable).
int64_t skewMs(const Json& answer, int64_t localNowMs);
/// Seconds into the film at server time `serverNowMs` (negative before it starts).
double positionAt(int64_t serverNowMs, int64_t filmAtMs);
/// Whether it is playing for those who watch it together: from its start (or `aheadSeconds` before it) to its end.
bool liveAt(int64_t serverNowMs, int64_t filmAtMs, double lengthSeconds, double aheadSeconds = 0.0);
/// The names its roster writes (« [TAG] Nom »), the viewer's own clan's index in `own` (-1: not among them).
std::vector<std::string> rosterNames(const Party& p, int* own);

}  // namespace cosmic::aftermath
