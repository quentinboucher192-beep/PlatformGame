#pragma once

#include <map>
#include <optional>
#include <string>

#include "game/levels/LevelData.h"

#include "engine/core/Json.h"
#include "engine/core/Paths.h"

namespace cosmic {

class ContentDatabase;
struct EnemyDefinition;
struct LevelData;

/// Identity of a level revision for online rankings: id, integer revision
/// (`metadata.revision` of the level file, 1 by default) and SHA-256 of the canonical
/// JSON of the file (parsed then re-serialised: independent of whitespace and line
/// endings, so Windows and Linux builds agree).
/// Version of the simulation rules. Bump it whenever a code change can alter how a run
/// plays out (physics, AI, damage, timers...), with the reference digests of
/// tests/test_determinism.cpp.
constexpr int kSimulationVersion = 17;  // 6: energy 150, time slow, magnet, air dash, plasma bomb (special button),
                                       //    arena bosses keep their wounds when the player dies
                                       // 7: crouch in place (Down on the ground)
                                       // 8: energy costs by strength (charge shot = reference),
                                       //    a boss's creatures vanish with it
                                       // 9: guardians of Aethra (12 attack types, arena hazards:
                                       //    pull, ice, lava tide, thorn walls) and 4 powers
                                       //    (boomerang, frost nova, decoy, recall); earlier
                                       //    levels play exactly as before (same digests)
                                       // 10: keys per power slot (InputFrame bits 13-20,
                                       //    replays version 3): the key of a slot uses the
                                       //    power carried there, and two powers of the old
                                       //    special button can be carried together. A run
                                       //    with one button per power (replays version 2)
                                       //    plays exactly as before (same digests)
                                       // 11: daily challenge rules (game/gameplay/ChallengeRules.h,
                                       //    replays version 4): hearts, power slots, imposed
                                       //    powers, stations off, checkpoints off (a death goes
                                       //    back to the start with the challenge's start state).
                                       //    A run without a challenge plays exactly as before
                                       //    (same digests)
                                       // 12: the giga boss of the Noyau (2.33): rage counted in a
                                       //    phase, a last stand before dying, prisms with two
                                       //    beams, platforms in orbit, crust floors broken by an
                                       //    event, damage zones switched on by an event (and
                                       //    throwing Nova up), pickups shown by an event, the
                                       //    rage heard by the level. Earlier levels play exactly
                                       //    as before (same digests)
                                       // 13: (2.47.1) the clock starts when Nova can move: a
                                       //    skipped intro that shows her appearing no longer
                                       //    counts her materialisation (watching it gave ~1 s).
                                       //    The rankings are kept: no run became better.
                                       // 14: (2.62.2) an ambush (an arena without a boss) is not
                                       //    reset by a fall: it waits as Nova left it, its doors
                                       //    open to let her back in and closing behind her; an
                                       //    enemy of an ambush being fought that leaves its arena
                                       //    is brought back into it. The rankings are kept: a run
                                       //    without a fall in an ambush plays as before.
                                       // 15: (2.71) the attack strikes upwards (Up alone) or,
                                       //    in the air, downwards (Down alone), with the aim
                                       //    held when it started (the bounce of a strike downwards
                                       //    too). A replay recorded before (no version in the
                                       //    file) plays with its rules (GameWorld::
                                       //    simulationVersion): same digests. The rankings are kept.
                                       // 16: (2.76) a boss whose rules say so (Pyrrhax) fights by its
                                       //    hit parts (game/enemies/HitParts: its pose in the fixed
                                       //    step, the blows on the part they touch, its contact by its
                                       //    parts that hurt); an enemy that flees when weakened stays
                                       //    within fleeMaxDistance of where its flight began, cornered
                                       //    there. A replay of before plays with its box and its old
                                       //    flight (same digests); the rankings are kept, as the player
                                       //    asked.
                                       // 17: (2.76.2) every other boss fights by its hit parts
                                       //    (partsFromVersion 17 in data/enemies). The rankings are kept.
                                       //    Any part of a boss by parts hurts on contact, its red
                                       //    parts strike during their attacks (the player's rule,
                                       //    kWholeBodyContactSimulation); Pyrrhax's 16 is unchanged.
/// 2.62: version of the rules of cooperation (docs/COOP.md): a team of 2 or 3 players in one
/// world. It changes nothing for a solo run (kSimulationVersion and its digests are the same);
/// a team's attempts carry it, and any change of the team rules (solid players, revivals,
/// standby, the team's frame...) bumps it with the reference digests of tests/test_coop.cpp.
constexpr int kCoopSimulationVersion = 2;  // 2 (2.75): the players pass through each other (no
                                           // body, no ally's head to stand on). A team replay of
                                           // version 1 plays with its solid players (GameWorld::
                                           // setCoopVersion): same digests.
/// Generation of the rankings, part of every ranked fingerprint. It follows
/// kSimulationVersion when a change makes earlier runs incomparable (records and replays of
/// different generations are never mixed). A change that only adds a possibility and
/// leaves every earlier run valid keeps it, so the records are kept:
///   6 -> simulation 6 and 7 (7 adds crouching in place: earlier runs never crouched).
///   8 -> simulation 8 (2.17: energy costs and minions changed: the rankings start again),
///        9 (2.20: only additions — new guardians, attacks and powers — the records stay)
///        10 (2.24: keys per power slot; the moves and powers are the same, earlier runs
///        stay valid and comparable), 11 (2.32: the challenge rules only apply to a
///        challenge run; every other run plays as before, the records stay) and 12 (2.33:
///        only additions for the giga boss's level; every earlier run plays as before),
///        13 (2.47.1), 14 (2.62.2: ambushes kept after a fall; the records stay, as the
///        player asked) and 15 (2.71: the attack upwards and downwards; the records stay, as
///        the player asked, and the replays recorded before play with their own rules), 16 (2.76:
///        Pyrrhax by its hit parts, the flight of the weakened enemies bounded; the records stay, as
///        the player asked, and the replays recorded before play with their own rules).
constexpr int kRankingGeneration = 8;

struct LevelFingerprint {
    std::string levelId;
    int version = 1;
    std::string hash;
};

std::optional<LevelFingerprint> fingerprintLevelFile(const fs::path& file, std::string* error = nullptr);
std::optional<LevelFingerprint> fingerprintLevelJson(const Json& level);
/// Fingerprint of a ranked level: the level, every enemy and power definition it uses
/// (recursively), the player configuration and kRankingGeneration. Two runs with the same
/// fingerprint, seed and inputs play out identically. A campaign level bound to enemy
/// revisions (2.45, `metadata.enemyRevisions`) is printed with those definitions; nullopt when
/// this game does not have one of them.
std::optional<LevelFingerprint> fingerprintRankedLevel(const ContentDatabase& content, const Json& level);

/// Fingerprint of what a community level plays like: geometry, objects, events, cinematics,
/// timer, start powers, the definitions of its enemies and powers, the player and the
/// simulation version - but not its id, name, description, author, music or metadata, and
/// enemy / power references are replaced by a digest of their definition. So a copy of the
/// level installed on another computer (where the importer may rename the level or its
/// definitions) has the same fingerprint as the author's: the author's completion replay and
/// the players' replays are matched with it (Cosmic Online, community levels).
/// An enemy as it plays, for the fingerprints: appearance-only settings added after 2.7
/// (beam textures and thickness) are left out, so that changing the look of an attack
/// never changes a level's fingerprint (nor drops its records).
Json gameplayEnemyJson(const EnemyDefinition& d);

std::string fingerprintGameplay(const ContentDatabase& content, const LevelData& level);

/// Fingerprints of the built-in levels (cached after the first computation).
class LevelFingerprints {
public:
    const LevelFingerprint* get(const ContentDatabase& content, const std::string& levelId);
    void clear() { cache_.clear(); }

private:
    std::map<std::string, std::optional<LevelFingerprint>> cache_;
};

/// Builds the manifest of every built-in level (fingerprint, totals, spawn and exit, time
/// limits) used to register level revisions on the server (`--online-manifest`). The
/// spawn is measured by running the intro of the level in a headless world.
Json buildOnlineManifest(ContentDatabase& content, std::string* error = nullptr);
/// Manifest entry of one level (also used by the integration tests with their own level).
Json onlineManifestEntry(const ContentDatabase& content, const LevelData& level, const LevelFingerprint& fp);

}  // namespace cosmic
