#pragma once

// The search of the machine's runs (docs/TAS.md « La recherche »). One generic algorithm for every
// level and category: it plays the deterministic engine from savestates and keeps the fastest run
// it finds — never a proven optimum.
//
// * Sections: the run is searched section by section (checkpoint to checkpoint, then to the exit;
//   for 100 % the collectibles of the section first). Each thread runs its own beam (its own arena
//   world, its own random choices) from the same state; the best arrival of all of them (the
//   earliest, a lost heart counting as time lost) becomes the start of the next section.
// * The beam is synchronous in time: every state of a generation has played the same number of
//   steps; they are ranked by their progress (Course: the distance along the route, what the world
//   did on the way) and kept diverse (one per place and situation). When the progress stalls,
//   the beam widens, the moves get longer and more of them are tried.
// * Moves: a few steps of the inputs of a player — a direction, a jump held for a while, a second
//   jump, an attack, an interaction, the key of a power carried, Down or Up — and, when a power is
//   offered, each answer (leave it, or replace each slot).
// * Refinement: the run found is played again with each jump, dash, power or attack moved by one
//   or two steps, or dropped; a change is kept when the run still finishes the category, earlier
//   (or as early with fewer inputs).
// * A seed run (--seed-replay) is played first: when it finishes the category it is the run to
//   beat, its states join the beams, and the refinement starts from it.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/gameplay/InputFrame.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {
class ContentDatabase;
class GameWorld;
struct LevelData;
}  // namespace cosmic

namespace cosmic::tas {

struct SearchOptions {
    RunCategory category = RunCategory::AnyPercent;
    double budgetSeconds = 30.0 * 60.0;
    int threads = 2;
    int beam = 24;              // states per thread and generation (it widens when stalled)
    uint64_t seed = 1;          // the random choices of the search
    std::vector<InputFrame> seedRun;  // --seed-replay: its inputs from the load
    int keepSeedSections = 0;         // the seed run's first sections taken as they are
    std::function<void(const std::vector<InputFrame>&)> onSection;  // the run so far, after each section
    std::function<void(const std::string&)> log;
};

struct SearchOutcome {
    bool found = false;
    std::vector<InputFrame> inputs;  // from the load to the exit
    float clock = 0.0f;              // the level's clock at the exit (s)
    int64_t explored = 0;            // attempts simulated (moves tried from a savestate)
    std::string why;                 // when nothing was found
};

/// The steps a player has to hold « confirm » before a cinematic is skipped (GameScene: 0.8 s).
constexpr int kSkipHoldSteps = 97;

/// The frame to send at a step: `want` unless a skippable cinematic plays — then nothing but
/// the skip, once it has been « held » kSkipHoldSteps steps (`cinematicSteps` counts them).
InputFrame playerFrame(const GameWorld& w, InputFrame want, int& cinematicSteps);

SearchOutcome search(const ContentDatabase& content, const LevelData& level, const SearchOptions& opt);

}  // namespace cosmic::tas
