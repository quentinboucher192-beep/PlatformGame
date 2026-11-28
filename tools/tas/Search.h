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
//   beat and it GUIDES the whole search (docs/TAS.md « La meilleure course guide la recherche »):
//   every section is offered its own frames for that section (by the section's index, and
//   realigned on Nova's position), played from the state the search is in — even after the run
//   found has left it; they join the beam. At each section's end, every good arrival is played on
//   with the rest of the seed run (realigned by a few steps): a faster finish is a faster run at
//   once, and the arrivals are ranked by the finish they give (estimated when they cannot re-join).
// * The planner (docs/TAS.md « Le planificateur », tools/tas/Planner.h): plans made in a light copy
//   of Nova's movement (BodySim: dashes, long jumps, wall jumps, energy) toward the section's end,
//   at its start and every few generations, played in the real world like the guide's frames;
//   their states join the beam only on their merit. --no-planner turns it off.
// * Rounds (a training, docs/TAS.md « Les tours »): the first searches the whole level; the next
//   ones search a WINDOW of one or two sections (WindowPlanner) — the seed run's sections before
//   kept exactly, after the window the run re-joins the seed run — in several passes, deeper each
//   time, so that every section gets deep searches over the rounds.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/OnlineTypes.h"
#include "tools/tas/Planner.h"

namespace cosmic {
class ContentDatabase;
class GameWorld;
struct LevelData;
}  // namespace cosmic

namespace cosmic::tas {

class Portfolio;
class Director;

/// One attempt of the search, for the counts of a training (game/tas/TasProgress.h windows).
struct AttemptNote {
    bool finished = false;  // the exit reached, the category kept
    float clock = 0.0f;     // the level's clock then (s)
    int section = 0;        // did not finish: the section (0-based) of its furthest point
    float route = 0.0f;     // ... how far along the route (0..1)
    bool broke = false;     // ... it broke the category (a death, a blow in « Sans dégât »)
};

/// A checkpoint of the level reached by an attempt (the course's checkpoints in the order of the
/// route, CP1 first: the level's checkpoints a player's splits count), and the level's clock then.
struct Crossing {
    int checkpoint = 0;  // 0-based
    float clock = 0.0f;
};

// RouteHint (tools/tas/RouteHint.h): a point of a player's ROUTE (docs/TAS.md « Les guides », « Le tracé
// d'une ancienne version »): where Nova's feet were, at `clock` s, in a run of an older version of the
// level — its inputs no longer play the same, its path still says where a player went.

struct SearchOptions {
    RunCategory category = RunCategory::AnyPercent;
    double budgetSeconds = 30.0 * 60.0;
    int threads = 2;
    int beam = 24;              // states per thread and generation (it widens when stalled)
    uint64_t seed = 1;          // the random choices of the search
    std::vector<InputFrame> seedRun;  // --seed-replay: its inputs from the load
    int keepSeedSections = 0;         // the seed run's first sections taken as they are
    /// A round of a training after the first: only the sections windowFirst..windowLast (1-based)
    /// are searched, the seed run's sections before them kept exactly, the rest re-joining the
    /// seed run. 0: the whole level. Only with a seed run that finishes the category.
    int windowFirst = 0, windowLast = 0;
    bool refine = true;  // the refinement of the run found (a window round: from the window on)
    /// The planner (docs/TAS.md « Le planificateur »): plans in BodySim offered to the beams as one
    /// more source of candidates, at each section's start and every `plannerEvery` generations
    /// (--no-planner: off, for A/B tests).
    bool planner = true;
    int plannerEvery = 8;
    /// Phase leads (docs/TAS.md « Le réalignement de phase »): when the run is early on the best run
    /// (or a guide) at a section's start, that source's frames for the section are also offered
    /// after the wait that takes them furthest along their path (--no-phase-leads: off).
    bool phaseLeads = true;
    PlanBudget plannerBudget;
    /// The whole level only: the run's first frames taken as they are (it goes on from the section
    /// they end in) — a run that has already left the seed run (the tests of the guidance).
    std::vector<InputFrame> startRun;
    std::function<void(const std::vector<InputFrame>&)> onSection;  // the run so far, after each section
    std::function<void(const std::string&)> log;
    // A training from the game (docs/TAS.md « Entraînement depuis ADMINISTRATION »): stopped from
    // outside (the search ends as soon as it can, with the best run it has), its attempts counted
    // as they go, and the clock of each better run that finishes the category (before its
    // verification), from the search's threads.
    const std::atomic<bool>* stopRequested = nullptr;
    std::atomic<int64_t>* exploredTotal = nullptr;
    std::function<void(float clock)> onBest;
    /// The attempts as they go, by batches (one generation of a beam, one refinement, the re-joins
    /// of a section), from the search's threads, with the checkpoints they reached; and each
    /// stage: a section starts (1-based, of `sections`), or 0: whole runs refined.
    std::function<void(const AttemptNote* notes, size_t n, const Crossing* crossings, size_t m)> onAttempts;
    std::function<void(int section, int sections)> onStage;
    /// The portfolio of strategies (tools/tas/Portfolio.h, docs/TAS.md « Le portefeuille »): with a
    /// best run, the round is cut into slices that its bandit gives to the beam's passes, Go-Explore,
    /// the timing polish, the refinement or an arm registered; branch-and-bound in the beam. None:
    /// the search as before. Lives as long as the process (like WindowPlanner).
    Portfolio* portfolio = nullptr;
    /// The mind of the TAS (tools/tas/Director.h, docs/TAS.md « L'IA du TAS »): with a portfolio, it
    /// plans the slices after the beam's part of a round — strategies side by side, each on its
    /// own cores — and says why. None: the bandit alone, one strategy on every core.
    Director* director = nullptr;
    /// An early arrival that does not carry to the exit (Slice::onLead): a branch may commit to it.
    std::function<void(const std::vector<InputFrame>& prefix, size_t through, float lead, const std::string& how)> onLead;
    /// Each faster whole run the search holds (played from the load, before its verification), from
    /// the search's threads: cosmic_tas verifies it at once on a thread of its own (docs/TAS.md
    /// « Vérification au fil de l'eau »). Keep it short: copy and return.
    /// `source`: where the gain came from ("beam", "guide", "splice", "polish": the refinement).
    std::function<void(const std::vector<InputFrame>& run, float clock, const std::string& source)> onRun;
    /// Other runs of this level that GUIDE the search (docs/TAS.md « Les guides »): a player's
    /// runs (the PB of this computer, records of Cosmic Online), exact replays of this version of
    /// the level, their inputs from the load. Like the best run, each one's frames are offered to
    /// every section (by the section's index and realigned on Nova's position), every arrival is
    /// re-joined into each of them, and — `splice` — a run is first assembled section by section
    /// from the fastest way among the best run and the guides (a « somme des meilleurs segments »
    /// that really plays), kept when it finishes faster.
    std::vector<std::vector<InputFrame>> guides;
    bool splice = true;
    /// Routes of players' runs of older versions of the level (never their inputs): at every
    /// generation of a section, besides its own selection, the beam keeps the states that follow
    /// one of them the furthest (within kRouteRadius px of a point of it), so that today's inputs
    /// that take the player's way are searched under today's rules. The planner aims along them too.
    std::vector<RouteHint> routeHints;
    static constexpr float kRouteRadius = 24.0f;
};

struct SearchOutcome {
    bool found = false;
    std::vector<InputFrame> inputs;  // from the load to the exit
    float clock = 0.0f;              // the level's clock at the exit (s)
    int64_t explored = 0;            // attempts simulated (moves tried from a savestate)
    std::string why;                 // when nothing was found
    // What the search did (its log, the tests).
    int windowFirst = 0, windowLast = 0;  // the window searched (1-based); 0: the whole level
    size_t keptFrames = 0;                // the seed run's first frames kept exactly (a window round)
    int passes = 0;                       // a window round: its passes over the window
    int guidedSections = 0;               // sections searched with the seed run's frames offered while the run differed from it
    int guideArrivals = 0;                // ... and where they reached the section's end
    int rejoins = 0;                      // arrivals played on with the rest of the seed run that finished the category
    int guideSources = 0;                 // the guides studied (besides the best run)
    int splicedSections = 0;              // the splice: its sections taken from a guide (not the best run)
    int spliceWays = 0, spliceArrivals = 0, spliceJoined = 0;  // the splice: ways played, those that reached their section's end, those re-joined (measured)
    float spliceClock = -1.0f;            // the splice's run at the exit (negative: it did not finish)
    int64_t routeKept = 0;                // states kept in the beam because they follow a route hint
    std::string bestSource;               // where the best run kept came from ("beam", "guide", "splice", "polish"; empty: the seed run)
    /// Before the refinement, the run kept ended with the seed run's rest: inputs[rejoinAt..] are
    /// seedRun[rejoinSeedAt..] (the first one's presses and releases recomputed).
    bool rejoined = false;
    size_t rejoinAt = 0, rejoinSeedAt = 0;
    std::vector<InputFrame> beforeRefinement;  // the run kept before the refinement
    PlanStats planner;                         // what the planner did, all threads
    /// Arrivals EARLIER than the best run at a section's end that did not re-join it (their world
    /// out of phase for its next sections): the run from the load through that section, for the
    /// phase realignment (tools/tas/Phase.h, docs/TAS.md « Le réalignement de phase »). The earliest
    /// two of each section (as healthy or more), at least `kEarlyLead` s ahead.
    struct EarlyArrival {
        std::vector<InputFrame> run;
        size_t sections = 0;  // the sections it went through
        float clock = 0.0f, lead = 0.0f;
        int health = 0;
    };
    std::vector<EarlyArrival> early;
    static constexpr float kEarlyLead = 0.25f;
};

/// The windows of a training's rounds (docs/TAS.md « Les tours »): from the second round, each
/// round searches one or two consecutive sections (one for a level of one or two sections). They
/// rotate over the whole level: the sections searched the longest ago first, those where a round
/// found a gain sooner again (a gain counts less each round that finds nothing there). Lives as
/// long as the process (cosmic_tas --until-stopped).
class WindowPlanner {
public:
    explicit WindowPlanner(int sections = 0);
    int sections() const { return static_cast<int>(last_.size()); }
    int width() const { return sections() <= 2 ? 1 : 2; }
    /// The window of `round` (1-based sections, first <= last); {0, 0} without sections.
    std::pair<int, int> next(int round) const;
    /// What a round did: the sections it searched (1-based; {0, 0}: the whole level), the time it
    /// gained (ms; 0: nothing faster).
    void done(int round, int first, int last, int64_t gainMs);
    /// The weight of past gains of a section (0-based; the tests, the log).
    double gainOf(int section) const { return section >= 0 && section < sections() ? gain_[static_cast<size_t>(section)] : 0.0; }

private:
    std::vector<int> last_;      // per section: the last round that searched it
    std::vector<double> gain_;   // per section: its recent gains (+1 per round that gained, halved per round that did not)
};

/// The rest of `best` after its section `section` (0-based) played after `prefix`, realigned by a
/// few steps (the search's re-join, docs/TAS.md): the fastest that finishes the category.
struct Rejoin {
    bool found = false;
    float clock = 0.0f;
    std::vector<InputFrame> inputs;  // prefix, then best's rest from seedAt
    size_t seedAt = 0;
    int tried = 0;
};
Rejoin rejoinBest(const ContentDatabase& content, const LevelData& level, RunCategory category, const std::vector<InputFrame>& prefix,
                  const std::vector<InputFrame>& best, int section);
/// The frame index of `run` just after each section's end it reaches (the search's measure).
std::vector<size_t> sectionEndsOf(const ContentDatabase& content, const LevelData& level, RunCategory category, const std::vector<InputFrame>& run);

/// The steps a player has to hold « confirm » before a cinematic is skipped (GameScene: 0.8 s).
constexpr int kSkipHoldSteps = 97;

/// The frame to send at a step: `want` unless a skippable cinematic plays — then nothing but
/// the skip, once it has been « held » kSkipHoldSteps steps (`cinematicSteps` counts them).
InputFrame playerFrame(const GameWorld& w, InputFrame want, int& cinematicSteps);

SearchOutcome search(const ContentDatabase& content, const LevelData& level, const SearchOptions& opt);

}  // namespace cosmic::tas
