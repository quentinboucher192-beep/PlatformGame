#pragma once

// The phase realignment (docs/TAS.md « Le réalignement de phase »): an arrival EARLIER than the
// best run at a section's start no longer plays the best run's next sections — the level's
// elements that move with time (moving platforms, lasers, patrols) are not where they were for
// them. Many are periodic: a short wait may put the world back in phase for the old, highly
// optimised route and keep most of the lead.
//
// From such an arrival, section by section to the exit: the best run's frames for the section,
// played after k idle frames (k from 0 up to the lead, at most one period of the elements met in
// the section, plus a margin), from its own start and from where Nova was nearest; a try that
// leaves the best run's path (out of phase) is realigned again where it left it (a second wait,
// before the last place where the best run had its feet on the ground); the earliest arrivals
// (a lost heart counting as time lost) go on to the next section. Chosen by the measured finish;
// the run is played again from the load before it is given — and like every run, verified by the
// normal replay reader before anything is written. Generic: nothing here knows a level.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/InputFrame.h"
#include "tools/tas/Sim.h"

namespace cosmic {
struct LevelData;
}

namespace cosmic::tas {

/// An element of the level that repeats with time (a moving platform on its route, a laser's
/// cycle): where it can be, and its period.
struct PeriodicElement {
    std::string id;
    Rect sweep;           // everywhere it can be (its route, its beam's reach), in the level's coordinates
    float period = 0.0f;  // s
};
std::vector<PeriodicElement> periodicElements(const LevelData& level);

/// The longest period of the elements that the best run met in its section `s` (its feet within
/// `margin` px of where they can be); 0 when none.
float periodMet(const RunStudy& best, size_t s, const std::vector<PeriodicElement>& elements, float margin = 96.0f);

struct PhaseOptions {
    float maxWait = 8.0f;  // s: the waits tried at a section's start, at most (never more than the lead + `extra`, nor the period met + `extra`)
    float extra = 0.5f;    // s
    float minWait = 0.5f;  // s: tried even without a lead or an element met (patrols)
    int coarse = 4;        // frames between two waits of the sweep...
    int fine = 1;          // ... then every `fine` frames around the best ones
    int keep = 3;          // the arrivals kept at each section's end (a small beam over the waits)
    int inner = 2;         // the tries that left the best run's path realigned again where they left it (one level deep)
    float leave = 96.0f;   // px from the best run's path, `leaveFrames` frames in a row: out of phase
    int leaveFrames = 40;
    float slack = 1.5f;    // s over the best run's own time for the section: given up
    /// Every try told (the measurements, --phase-trace): what it played and how it ended.
    std::function<void(const std::string&)> trace;
};

/// What the realignment did in one section.
struct PhaseSection {
    int source = 0;               // the source whose frames it played (realign's `sources`)
    int wait = 0;                 // idle frames inserted in the section (all its waits)
    int waits = 0;                // how many places they were inserted (1: at its start; 2: and where a try left the path)
    float arrival = -1.0f;        // the clock at its end
    float sourceArrival = -1.0f;  // the source's own
    float periodMet = 0.0f;     // s
    int tried = 0;
};

struct PhaseRun {
    bool found = false;
    float clock = -1.0f;              // at the exit
    std::vector<InputFrame> run;      // from the load to the exit (played again from the load)
    std::vector<PhaseSection> sections;  // from `from` on (the path of the run kept)
    int64_t tried = 0;                // tries simulated (attempts of the search)
    /// The furthest way realigned (its earliest arrival), finished or not: from the load to the end
    /// of section `partialSections` (a branch can start from it when nothing finished).
    std::vector<InputFrame> partial;
    size_t partialSections = 0;
    float partialClock = -1.0f;
};

/// From `prefix` (inputs from the load; the world then at the start of section `from` of the
/// course, 0-based), the best run's sections `from`.. realigned as above: the fastest run that
/// finishes the category, or none. `over`: stop early (the best found so far is returned).
PhaseRun realign(Sim& sim, const RunStudy& best, const std::vector<PeriodicElement>& elements, const std::vector<InputFrame>& prefix, size_t from,
                 const PhaseOptions& opt, const std::function<bool()>& over = {});
/// The same with several SOURCES (the best run, a run that went another way, a guide...): each
/// section tries every source that has it (by the section's index), so that a run can take one
/// source's section after another's arrival — a sum of the best segments that really plays, the
/// world brought back in phase by the waits.
/// `prefixSource`: the source the prefix is the beginning of (-1: none): its own way on (its next
/// sections, no wait) is always kept, so that the realignment never does worse than that source.
PhaseRun realign(Sim& sim, const std::vector<const RunStudy*>& sources, const std::vector<PeriodicElement>& elements, const std::vector<InputFrame>& prefix,
                 size_t from, const PhaseOptions& opt, const std::function<bool()>& over = {}, int prefixSource = -1);

/// A prefix worth realigning: source `source`'s frames through the end of its section `through`
/// (1-based count of sections kept), `lead` s earlier there than another source.
struct PhaseLead {
    size_t source = 0, through = 0;
    float lead = 0.0f;
};
/// The leads among `sources`: each source's arrival at each section end (but the last) that is at
/// least `minLead` s earlier than another source's there (the other's next sections are out of
/// phase for it); the largest leads first.
std::vector<PhaseLead> phaseLeads(const std::vector<const RunStudy*>& sources, size_t sections, float minLead);

/// The phase splice (docs/TAS.md « Le réalignement de phase »): every lead realigned on all the
/// sources, the largest leads first, until `over`; `each` is told of every run found (from the
/// search's thread: verify it, keep it when faster). The fastest run found.
struct PhaseSplice {
    PhaseRun run;       // the fastest (run.found: one finished)
    PhaseRun furthest;  // the realignment that went the furthest (its partial way), for when none finished
    PhaseLead from;
    int leads = 0;    // the leads realigned
    int64_t tried = 0;
};
PhaseSplice phaseSplice(Sim& sim, const std::vector<const RunStudy*>& sources, const std::vector<PeriodicElement>& elements, const std::vector<PhaseLead>& leads,
                        const PhaseOptions& opt, const std::function<bool()>& over,
                        const std::function<void(const PhaseLead&, const PhaseRun&)>& each = {});
/// The same on several threads, one Sim each (a Sim's savestates belong to its own arena): the
/// leads are taken in order by whichever thread is free; `each` is called from those threads.
/// The same leads give the same runs (each realignment is deterministic); which is kept when two
/// finish at the same time does not depend on the threads (the earlier lead in the list).
PhaseSplice phaseSplice(const std::vector<Sim*>& sims, const std::vector<const RunStudy*>& sources, const std::vector<PeriodicElement>& elements,
                        const std::vector<PhaseLead>& leads, const PhaseOptions& opt, const std::function<bool()>& over,
                        const std::function<void(const PhaseLead&, const PhaseRun&)>& each = {});

/// The frames of `run` up to the end of its last section reached (the course's), and how many
/// sections that is: where a realignment can start from.
std::vector<InputFrame> throughSections(Sim& sim, const std::vector<InputFrame>& run, size_t& sections, size_t atMost = static_cast<size_t>(-1));

}  // namespace cosmic::tas
