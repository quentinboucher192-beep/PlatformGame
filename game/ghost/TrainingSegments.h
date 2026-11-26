#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class GhostTrack;

// Section training (« Répéter un segment », 2.30.2): which segment of a run a training segment
// is, which checkpoints a run reached. 2.91: the bests themselves, every source together (the ONE
// personal Sum of Best), are kept by PersonalBests (game/ghost/PersonalBests.h).

/// The ends of a segment: checkpoint ids, "" = the level's start (`from`) or its exit (`to`).
/// Index of the segment from `from` to `to` in a run that reached `checkpoints` in this order
/// (segment 0: from the start to the first checkpoint; the last one: from the last checkpoint
/// to the exit). -1 when that run did not go straight from one to the other (a checkpoint
/// skipped, another order, another route).
int segmentIndexIn(const std::vector<std::string>& checkpoints, const std::string& from, const std::string& to);

/// The checkpoints a finished run reached, in order: those recorded with it (as many as its
/// splits); else those of the route when the run has exactly one split per checkpoint of the
/// route (every checkpoint in the route's order: the usual run, and the runs recorded before
/// 2.30.2 without them); else nothing (unknown: its segments cannot be matched). A run without
/// any split went from the start to the exit without a checkpoint: an empty list, known.
std::optional<std::vector<std::string>> resolveRunCheckpoints(const std::vector<std::string>& recorded, size_t splitCount,
                                                              const std::vector<std::string>& route);

/// Which checkpoint each split of a ghost's run was taken at: the one of `checkpoints` (id,
/// feet) where the ghost stood at that moment, within 320 px (as the race matches them).
/// Empty when one split cannot be told.
std::vector<std::string> checkpointsOfTrack(const GhostTrack& track, const std::vector<std::pair<std::string, Vec2>>& checkpoints);

/// Sum of Best: the sum of the best duration of every segment of a route; -1 when one of
/// them is unknown (< 0) or when there is none.
float sumOfBest(const std::vector<float>& bestSegments);

}  // namespace cosmic
