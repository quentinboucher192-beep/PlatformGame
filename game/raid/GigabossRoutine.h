#pragma once

// 2.69 (phase 4B): the fights of Asterion's other gigaboss (docs/RAIDS.md §13), each its own
// routine around the rules of raid::Gigaboss (its health scaled by the seats, its phases, the marks
// under every player in play, its weak points, its fall, the reports):
//
// * VOIDWORM (the ABDOMEN): the worm bursts out under every player (its « Fouissement »), swallows
//   an ally the others must free by bursting its stomach (a rescue), lets its parasites out (fewer
//   once the Abdomen's nest is destroyed), its acid rises over the arena in its 3rd phase; in its
//   chitin, its three rings must be pinned together (three hooks held 3 s each) to open its maw;
// * GRAVITON (the LEFT LEG): it bends space sector by sector (gravity turns over: the players fall
//   toward the vault, whose field hurts, unless a slab shelters them), half the arena at once in its
//   3rd phase, opens a well under each player, rains debris on each; its gravity generators, in
//   separate sectors, must be switched off ALL AT THE SAME TIME (one player on each, held together)
//   for it to collapse (one less once the leg's two gravity generators are cut);
// * TITANUS (the RIGHT LEG): its stomps send shockwaves along the floor (jump over them), its heel
//   falls on each player, boulders fall from the vault; its two ankle restraints, broken within a
//   second and a half of each other (a restraint broken alone forges again), bring it to its knee
//   (longer once the leg's two reactors are cut).
//
// A routine reads only the world, the frames' effects and the branch's state given with them: the
// same on every computer of the instance (its state is in the gigaboss's digest). What it shows
// (marks over the arena, the lines of the objectives) is display only.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/raid/GigabossTunables.h"

namespace cosmic {
class GameWorld;
class Enemy;
class Player;
}  // namespace cosmic

namespace cosmic::raid {

class Gigaboss;
class Contributions;
struct GigabossRules;
struct HeartShared;  // 2.74 (phase 6): game/raid/RaidBranch.h

/// Something the combat screen draws over the arena for a gigaboss's routine (display only).
struct BossMark {
    enum class Kind : uint8_t {
        Zone,    // an area (a sector turning over, the acid coming, the stomach): tinted, outlined
        Plate,   // a plate of a mechanism: its clip of the effects sheet, a ring of its progress
        Target,  // a mark on the ground (a ring that closes as the blow comes)
        Wave,    // a shockwave's crest along the floor (its clip)
        Arrow,   // an arrow up (gravity turning over)
        // 2.69 (Skullmaw, Dreadfang, Heartbreaker):
        Beam,    // a line from area's corner to `to` (a gaze: thin while it follows, bright locked, thick firing; a chain)
        Cone,    // a light from area's corner toward `to`, `width` its half-angle: the platforms it touches show
    };
    Kind kind = Kind::Zone;
    Rect area;                       // world
    float progress = 0.0f;           // 0..1 (a hold, a warning)
    Color color{255, 255, 255, 255};
    std::string clip;                // a clip of the gigaboss's effects sheet (rules().fx), "" none
    std::string label;               // a short French label over it, "" none
    bool strong = false;             // the danger is on now (not only announced)
    Vec2 to;                         // Beam / Cone: where it goes
    float width = 0.0f;              // Beam: its thickness (world px); Cone: its half-angle (radians)
    bool flip = false;               // the clip mirrored (a blade or a wave going west)
};

/// A line of the objectives panel (its mark, its text, its colour).
struct BossLine {
    std::string mark, text;
    Color color{255, 210, 122, 255};
};

class GigabossRoutine {
public:
    virtual ~GigabossRoutine() = default;
    /// The routine named in metadata.raid.gigaboss.routine ("voidworm", "graviton", "titanus"); null
    /// for any other (Pyroclast runs the rules of raid::Gigaboss itself).
    static std::unique_ptr<GigabossRoutine> make(const std::string& name);

    /// A zone was loaded (its gigaboss's rules: GigabossRules::extra holds what is its own).
    virtual void begin(GameWorld& w, const GigabossRules& rules) = 0;
    /// After the world's step, once its fight is engaged (its phase known, its fists stepped).
    virtual void step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& boss) = 0;
    /// It fell: what it holds is let go (the players it swallowed, its hazards).
    virtual void fell(GameWorld& w, Gigaboss& g) = 0;
    virtual uint64_t hash() const = 0;

    // ------------------------------------------------------------------ display only
    virtual void marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const = 0;
    /// Its lines of the objectives panel (its stage, its mechanism, what threatens).
    virtual void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const = 0;
    /// Where it threatens the players (the arena's camera keeps them in view).
    virtual void threats(const Gigaboss& g, std::vector<Vec2>& out) const = 0;
    /// 2.69: the enemies of its mechanisms the arrows point at when they are off screen (plates,
    /// claws, a stomach), besides its weak points.
    virtual void targets(std::vector<uint32_t>& out) const { (void)out; }
    /// 2.69: a player it holds now (swallowed, nailed): no mark of a blow on them (Gigaboss's fists).
    virtual bool spared(int seat) const {
        (void)seat;
        return false;
    }

protected:
    // The gigaboss's own state (Gigaboss befriends this class; the routines run its rules their way).
    enum AlertTone { Group = 0, Branch = 1, Danger = 2 };
    static void alert(Gigaboss& g, std::string text, AlertTone tone);
    static int phase(const Gigaboss& g);
    static int inPlay(const Gigaboss& g);
    static int scaledFor(const Gigaboss& g);
    static uint32_t ticks(const Gigaboss& g);
    static bool exposed(const Gigaboss& g);
    static bool armoured(const Gigaboss& g);
    /// Its armour (nothing hurts it) on or off.
    static void setArmour(Gigaboss& g, bool on);
    static void setFight(Gigaboss& g);
    /// Its weak points bare for `seconds` (the stage Exposed), with this alert.
    static void expose(Gigaboss& g, GameWorld& w, Contributions& c, Enemy& b, const std::string& text);
    /// One step of the exposure: the weak points broken (their share of its health, who broke them),
    /// stuck to it; false once it is over (its weak points gone).
    static bool exposedStep(Gigaboss& g, GameWorld& w, Contributions& c, Enemy& b);
    /// Whether an objective named in its rules (`links`) is done in the branch's state now.
    static bool linkDone(const Gigaboss& g, const std::string& name);
    /// 2.74 (phase 6): whether a link of the titan acting on its branch (its id in titan.json) is on in
    /// the branch's state now (the Cœur d'Asterion: a part of the titan fallen).
    static bool titanLinkOn(const Gigaboss& g, const std::string& id);
    /// 2.72: the shared state of the Cœur d'Asterion given at this step (zero elsewhere).
    static const HeartShared& shared(const Gigaboss& g);
    /// 2.72: the seconds its weak points stay bare now (the Cœur's cage shortens its exposures).
    static void setExposedSeconds(Gigaboss& g, float seconds);
    /// The plates of its rules (Gigaboss::vents) held by players: Pyroclast's vents under other
    /// names (each held `hold` seconds closes it for `reopen` seconds; all closed at once: true). An
    /// open one throws its jet in turn (the rules' jet), announced.
    struct PlateWords {
        std::string closed;    // « GROUPE · Anneau épinglé »
        std::string reopened;  // « GIGABOSS · Un anneau s'arrache : épinglez-le de nouveau ! »
    };
    static bool holdPlates(Gigaboss& g, GameWorld& w, Contributions& c, Enemy& b, const PlateWords& words);
    static void resetPlates(Gigaboss& g);

    // Helpers.
    static int steps(float seconds);
    /// The seats in play, alive, not reappearing, in order (at most 8).
    static std::vector<int> playing(GameWorld& w);
    /// Fewer players, more time (two must still hurry, one can do it): × 2 alone, × 1.35 at two.
    static float slack(int inPlay) { return tunables::slack(inPlay); }  // 2.73: GigabossTunables.h
    /// A deterministic number in [0, 1) from a seed (no random generator of the world is used).
    static float noise(uint32_t seed);
    /// A blow of the gigaboss on the players in play inside `area` (its name for the statistics).
    static void strike(GameWorld& w, Enemy& b, const Rect& area, float damage, float knockback, const std::string& name);
    static uint64_t mix(uint64_t h, uint64_t v);

    const GigabossRules* rules_ = nullptr;
};

}  // namespace cosmic::raid
