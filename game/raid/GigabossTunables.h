#pragma once

// 2.73: the few numbers of the gigaboss routines that are not in their lair's data (zone 8,
// metadata.raid.gigaboss), named once so the routines (game/raid/*Routine.cpp) and the raid sheets
// (game/raid/RaidSheets, « code: », « slack: », « plateslack: » values of
// data/raids/asterion_sheets.json) read the same values. Changing one changes the fights: it is a
// rule of the simulation (kSimulationVersion, tests/test_determinism.cpp).

namespace cosmic::raid::tunables {

/// Fewer players, more time (two must still hurry, one can do it): a held plate stays held, a
/// window stays open, × 2 alone, × 1.35 at two (GigabossRoutine::slack, Pyroclast's vents).
inline float slack(int inPlay) { return inPlay <= 1 ? 2.0f : inPlay == 2 ? 1.35f : 1.0f; }

/// Heartbreaker's four plates: the window after the first broken, × 2 alone, × 1.5 at two, × 1.2
/// at three or four.
inline float plateSlack(int players) { return players <= 1 ? 2.0f : players == 2 ? 1.5f : players <= 4 ? 1.2f : 1.0f; }

/// Dreadfang's « Griffe clouante »: only with this many players in play (one must stay free to
/// break the claw); `kPinMany` players aimed at once from `kPinTwoFrom` players in play (one before).
inline constexpr int kPinMinPlayers = 2;
inline constexpr int kPinTwoFrom = 5;
inline constexpr int kPinMany = 2;

/// Voidworm's maw: `kGulpMany` players aimed at once from `kGulpTwoFrom` in play (one before); it
/// holds `kGulpMany + 1` swallowed players at most from then (`kGulpMany` before).
inline constexpr int kGulpTwoFrom = 6;
inline constexpr int kGulpMany = 2;

}  // namespace cosmic::raid::tunables
