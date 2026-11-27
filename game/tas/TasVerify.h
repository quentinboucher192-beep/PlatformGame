#pragma once

// The check of a machine's run against its replay (docs/TAS.md « Vérification »): its replay,
// as it is written, is played by the normal replay playback (ReplaySimulator on a fresh world on
// the normal heap, as the replay viewer plays it): it must not desynchronise, must finish the
// level with the time the record claims, satisfy the record's category by the rules of the
// records (eligibleCategories) and give the deaths, damage and collectibles of the record.
//
// Used by `cosmic_tas` before it writes a run, and by the game before it ever shows a run
// downloaded from Cosmic Online (game/tas/TasOnline.h): a run published there is played again
// on each computer, never trusted.

#include <atomic>
#include <cstdint>
#include <string>

#include "engine/core/Paths.h"

namespace cosmic {

class ContentDatabase;
struct LevelData;
struct ReplayData;
struct TasRecord;

struct TasCheck {
    bool ok = false;
    std::string why;  // French, when refused
    int64_t timeMs = 0;
    int deaths = 0, damageTaken = 0, collectibles = 0, totalCollectibles = 0;
};

/// The longest run a machine's run may be (30 min of the level's clock).
inline constexpr uint32_t kTasMaxSteps = 120u * 60u * 30u;

/// Plays `replay` (its bytes decoded) in a fresh world of `level` and checks it against `record`
/// (see above). `level` must be the level the record is for (its levelHash is the caller's check).
/// `cancel` (optional): set from another thread, the check stops (refused, « interrompue »).
TasCheck verifyTasReplay(const ContentDatabase& content, const LevelData& level, const TasRecord& record, const ReplayData& replay,
                         const std::atomic<bool>* cancel = nullptr);

/// The content as the game plays it on this computer: the enemy and campaign revisions that
/// Cosmic Online `server` published and the game downloaded into `userDir`/online (their
/// files, checked when they were installed) given to `content` (just reloaded). Without a
/// server, nothing. For `cosmic_tas --server` and the check of downloaded runs: the level
/// played is the one the game shows, with its fingerprint.
void applyOnlineRevisions(ContentDatabase& content, const fs::path& userDir, const std::string& server);

}  // namespace cosmic
