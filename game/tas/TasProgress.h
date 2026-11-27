#pragma once

// The progress of a training of the machine's runs (docs/TAS.md « Entraînement depuis
// ADMINISTRATION »): `cosmic_tas --progress <file>` rewrites this small JSON file (atomically)
// about every half second; the game, which launched it as a child process on the computer of an
// administrator, reads it to show the training live. Nothing in it is trusted beyond the screen
// of that administrator: what is published is checked again by the server and by every game.
//
//   {
//     "format": "cosmic.tas.progress",
//     "phase": "searching",          // starting, searching, verifying, stopped, failed
//     "level": "sylvanox_01", "category": "ANY_PERCENT",
//     "elapsedMs": 61250,            // this session: since the process started
//     "explored": 30144,             // attempts simulated this session
//     "seedMs": 86692,               // the run it started from, verified (0: none)
//     "bestMs": 86650,               // the best run verified this session: the seed's or a faster one written (0: none)
//     "candidateMs": 86601,          // a faster run the search holds, not verified yet (0: none)
//     "improvements": 1,             // runs faster than the seed, verified and written
//     "round": 2, "threads": 4,
//     "sequence": 123,               // +1 at every write
//     "message": "..."               // the last line of the search's log (French, as the tool writes it)
//   }

#include <cstdint>
#include <optional>
#include <string>

#include "engine/core/Json.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

inline constexpr const char* kTasProgressFormat = "cosmic.tas.progress";

enum class TasPhase { Starting, Searching, Verifying, Stopped, Failed };

const char* tasPhaseId(TasPhase p);
bool tasPhaseFromId(const std::string& id, TasPhase& out);

struct TasProgress {
    TasPhase phase = TasPhase::Starting;
    std::string levelId;
    RunCategory category = RunCategory::AnyPercent;
    int64_t elapsedMs = 0;
    int64_t explored = 0;
    int64_t seedMs = 0;
    int64_t bestMs = 0;
    int64_t candidateMs = 0;
    int improvements = 0;
    int round = 0;
    int threads = 0;
    int64_t sequence = 0;
    std::string message;

    /// The process is done (stopped or failed): nothing more will be written.
    bool finished() const { return phase == TasPhase::Stopped || phase == TasPhase::Failed; }
    /// The best time known this session (a run not verified yet counts only once verified).
    int64_t bestKnownMs() const { return bestMs; }

    Json toJson() const;
    /// Nullopt (and why) for anything but a progress file of this format: numbers are clamped to
    /// be non-negative, an unknown phase or category refuses it.
    static std::optional<TasProgress> fromJson(const Json& j, std::string* error = nullptr);
    static std::optional<TasProgress> parse(const std::string& text, std::string* error = nullptr);
};

}  // namespace cosmic
