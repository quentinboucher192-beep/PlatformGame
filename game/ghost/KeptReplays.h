#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Paths.h"

namespace cosmic::keptreplays {

// 2.60.3: the exact replays kept with the best segments (SegmentHistory: the full runs' gold
// segments, « ghosts/golds »; TrainingRecords: the best section-training attempts,
// « ghosts/training »). One folder per level version (GhostStore::levelKey), one file per run,
// kept while a best segment refers to it. Local only, never sent.

constexpr uint64_t kMaxFileBytes = 4ull << 20;

/// Letters, digits, '-' and '_' only, at most `max` characters (file and folder names).
std::string safeName(const std::string& s, size_t max);
/// A kept replay's name as read back: "<safe name>.cprz", never a path.
bool validName(const std::string& name);
/// The folder of a level version's replays under `root` (empty when `root` is).
fs::path levelDir(const fs::path& root, const std::string& levelKey);
/// Writes `bytes` in a new file of the level's folder ("<date's digits>-<n>.cprz"): its name,
/// empty when nothing was written (no folder, too big, disk).
std::string write(const fs::path& root, const std::string& levelKey, const std::vector<uint8_t>& bytes, const std::string& date);
/// Deletes under `root` every file `used` (folder → names) does not name, and the folders it
/// does not list. Returns the bytes kept per folder.
std::vector<std::pair<std::string, uint64_t>> collect(const fs::path& root, const std::map<std::string, std::set<std::string>>& used);
uint64_t fileSize(const fs::path& p);

}  // namespace cosmic::keptreplays
