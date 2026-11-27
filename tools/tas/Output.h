#pragma once

// The run found, made into what the game keeps of a run (docs/TAS.md « Vérification »): the
// inputs are played in a world on the normal heap set up as the game sets up a run (GameScene),
// recorded by the game's own ReplayRecorder (the outro played to its end as a player would), and
// the replay obtained is played again by the normal replay playback (ReplaySimulator on a fresh
// world, as the replay viewer does). It must not desynchronise, must finish the level with the
// time claimed and satisfy the category by the rules of the records (eligibleCategories).
// Otherwise nothing is written.

#include <cstdint>
#include <string>
#include <vector>

#include "game/gameplay/InputFrame.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/tas/TasFile.h"

namespace cosmic {
class ContentDatabase;
struct LevelData;
}  // namespace cosmic

namespace cosmic::tas {

struct Verified {
    bool ok = false;
    std::string why;
    ReplayData replay;
    std::vector<uint8_t> bytes;  // the replay file (CPRZ), as verified
    TasRecord record;
};

/// The tool's version, written in the files.
std::string toolVersion();

Verified recordAndVerify(const ContentDatabase& content, const LevelData& level, const LevelFingerprint& fp, RunCategory category,
                         const std::vector<InputFrame>& inputs, int64_t explored);

/// Why `r` cannot GUIDE a search of this level (--guide, docs/TAS.md « Les guides »), in French;
/// empty when it can: an exact replay of a run alone of this level, of this very version (its
/// level hash) and simulation (kSimulationVersion), recorded with the keys per slot.
std::string guideRefusal(const ReplayData& r, const std::string& levelId, const std::string& levelHash);
/// A guide's file read and checked (guideRefusal): its inputs and its replay's id.
bool readGuideReplay(const std::string& path, const std::string& levelId, const std::string& levelHash, std::vector<InputFrame>& out, std::string& replayId,
                     std::string& why);

/// The inputs of a replay file (an exact replay of this level), for --seed-replay.
bool readSeedReplay(const std::string& path, const std::string& levelId, std::vector<InputFrame>& out, std::string& why);

}  // namespace cosmic::tas
