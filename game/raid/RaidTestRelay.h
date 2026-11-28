#pragma once

// 2.80 (docs/RAIDS.md « Tester avec des joueurs invités »): an administrator's test of Asterion played with invited players.
//
// « Tester Asterion » stays local (TestSession, RaidSimulation) while nobody is invited. With invited
// players, the test goes through a relay of its own on the server (server/src/raidTests.ts, a CoopRoom
// marked as a test): the administrator's computer and every invited one build the SAME test from its
// settings (RelayedTestConfig, the server's copy) and play the same steps in lockstep:
//
// * the relay gives the real players' frames (8 relay seats: the administrator on 0, the invited
//   players on the others as they come; vacant before, standby while away);
// * at a step (TEST_SEATS, coopnet::TestSeats), it says where each relay seat's player plays — their
//   group, their seat of the test, « Invincible » ticked by the administrator — the same for everyone;
// * the simulated players are computed by EVERY computer alike (TestBots is deterministic): a real
//   player's seat is theirs while they play it, their simulated player's again when they are away or
//   gone. Nothing else travels: no frame of a simulated player, no state.
//
// So every computer of the test gets the same digest, and a late player catches up from the history.
// A test never reports anything (RaidRun::Test), wherever it runs.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "game/coop/CoopProtocol.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Difficulty.h"

namespace cosmic {
struct WorldServices;
}

namespace cosmic::raid {

class TestSession;
class RaidSimulation;

/// The settings of a relayed test as the server keeps them (raidTests.ts readConfig): every computer
/// builds the same test from them.
struct RelayedTestConfig {
    bool fullRaid = false;  // « Simuler un raid complet » (else one zone)
    // One zone (RaidTestSetup).
    std::string branch;
    int zone = 1;
    int players = 2;  // the seats: the administrator and the simulated players
    int startPhase = 1;
    uint32_t parts = 0x7Fu;
    int relics = 0;
    // The whole raid (RaidSimSetup). §41 (titan 16): its players (14 to 56) spread over seven groups.
    int raidPlayers = 35;
    bool fullGroups = false;  // 2.77.3: « Joueurs par groupe »: the administrator's place played where they are not
    // Both.
    int skill = 1;
    int difficulty = kDifficultyDefault;  // titan 13: « Difficulté » of the test (1 to 5)
    bool invincible = true;  // the administrator's seat
    bool botsInvincible = false;

    std::string mode() const { return fullRaid ? "raid" : "zone"; }
    /// The `config` object of POST /admin/raid-tests (its keys only).
    Json toJson() const;
    /// From the server's copy (`mode`, `config`); nothing when a field is missing or out of bounds.
    static std::optional<RelayedTestConfig> fromJson(const std::string& mode, const Json& config);
    /// The seats of a group (the administrator's place included) and the simulated players an invited
    /// player can replace in one group.
    int seatsPerGroup() const { return fullRaid ? raidPlayers / 7 : players; }  // §41: the smallest group's places
};

/// The test of one zone as every computer builds it from the same settings (the heart's parts given as
/// fallen and its relics included), begun at its zone; nothing when its branch or zone is unknown here.
std::unique_ptr<TestSession> buildZoneTest(const WorldServices& services, const RelayedTestConfig& c);
/// The whole raid's simulation as every computer builds it (relayed: every group at the same speed,
/// ×1, whoever watches which), begun; nothing when the branches are missing.
std::unique_ptr<RaidSimulation> buildRaidSimulation(const WorldServices& services, const RelayedTestConfig& c, int background = 1);

/// The fingerprint a relayed test needs (the game's version, the rules of the simulation and of the
/// cooperation, the titan of every branch): an invited game with another one cannot join it.
std::string testFingerprint();

/// One step of a relayed test of one zone: the relay's frames (one per relay seat), the places at
/// this step. The administrator's frame on seat 0 (neutral while away: their autopilot after 15 s),
/// each invited player's on their seat (away: their simulated player's), the invincible seats.
/// 2.80 (2.79's orders): `order`, the administrator's order given at this step by the relay (TEST_ORDER, packed as
/// raid::bots::withOrder packs it; 0: none), on their frame as a test alone carries it.
void stepRelayed(TestSession& s, const std::vector<InputFrame>& frames, const coopnet::TestSeats& seats, uint32_t order = 0);
/// The same for the whole raid's simulation: the administrator's place (a group's free place, or
/// watching) and the invited players in their groups.
void stepRelayed(RaidSimulation& sim, const std::vector<InputFrame>& frames, const coopnet::TestSeats& seats, uint32_t order = 0);
/// The frame `f` carrying the packed order (0: as it is).
InputFrame withPackedOrder(const InputFrame& f, uint32_t order);

/// Whether a relay seat's frame is a player's (not vacant, not away).
bool playedFrame(const InputFrame& f);

}  // namespace cosmic::raid
