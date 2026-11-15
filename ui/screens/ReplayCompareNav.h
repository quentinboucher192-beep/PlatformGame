#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Scene.h"
#include "game/ghost/Ghost.h"
#include "game/online/Replay.h"

namespace cosmic {

class GameServices;

/// A replay chosen for a comparison, already read and checked, with what it represents.
struct ReplayChoice {
    std::shared_ptr<const ReplayData> replay;
    GhostMeta meta;
    bool valid() const { return replay != nullptr; }
};

/// « Comparer deux replays » (choice of A and B): the same sources as the pre-race screen
/// of Ghost Racing (my runs of this level, downloads kept in the cache, imported files,
/// online records), plus the replay being watched. Read only: no record, no progression,
/// nothing sent; online replays are downloaded here, before the playback.
struct ReplayCompareRequest {
    std::string levelId;                         // installed level
    /// The replay being watched (offered first, A by default).
    std::shared_ptr<const ReplayData> current;
    GhostMeta currentMeta;
    /// Chosen at once when offered: local sources ("last", "pb", "prev") or online record ids,
    /// A first, then B.
    std::vector<std::string> preselect;
    /// Online records to offer even when they are not in the first places of the board
    /// (ranking entries: record_id, player, time_ms, achieved_at).
    std::vector<Json> onlineEntries;
    /// Called with A and B; empty: the comparison opens in place of the choice.
    std::function<void(ReplayChoice a, ReplayChoice b)> onChosen;
};

std::unique_ptr<Scene> makeReplayCompareSetup(GameServices& g, ReplayCompareRequest req);
/// The replay viewer comparing A (played again exactly when possible) and B (ghost).
std::unique_ptr<Scene> makeReplayCompare(GameServices& g, const std::string& levelId, ReplayChoice a, ReplayChoice b);

}  // namespace cosmic
