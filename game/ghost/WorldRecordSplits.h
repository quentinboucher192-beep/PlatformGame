#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "game/online/Replay.h"

namespace cosmic {

// « Segments » (2.60.3), online only: the splits of the verified world record of the same
// level version, read from its replay as the server keeps it (never a split a client sends,
// never a run still waiting for its verification).

struct WorldRecordEntry {
    std::string recordId;
    std::string player;
    int64_t timeMs = 0;  // its time, as the server verified it
};
/// The entries of a leaderboard (its « entries ») that can give the world record's splits, best
/// first: the board lists verified records only; of those, the ones whose replay was verified
/// too (`has_replay`). Entries without an id or a time are left out.
std::vector<WorldRecordEntry> worldRecordCandidates(const Json& entries);
/// The checkpoint splits (seconds) of a record's replay when it was made on `levelHash` (this
/// exact version of the level); nullopt otherwise (another version: its splits do not compare).
std::optional<std::vector<float>> worldRecordSplits(const ReplayData& replay, const std::string& levelHash);
/// Mine minus the other run at the end of segment `i` (cumulative; the exit after the last
/// split), when both went through as many checkpoints; nullopt otherwise.
std::optional<float> cumulativeGap(const std::vector<float>& mySplits, float myTime, const std::vector<float>& otherSplits, float otherTime, size_t i);

}  // namespace cosmic
