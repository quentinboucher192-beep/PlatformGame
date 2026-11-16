#pragma once
// Difficulty of a boss from 1 to 10 stars (2.37, the « Fiche » of Statistiques des boss),
// computed from its definition only (never chosen by hand, never from the player's results):
//
//   pressure of a phase = mean damage × attacks per second / mean wind-up (seconds, ≥ 0.15),
//                         attacks per second = combo / (mean cycle × cooldown ÷ speed), the
//                         cycle being wind-up + active + recovery + cooldown of its attacks;
//   pressure            = the phases' pressures weighted by the share of health each lasts;
//   score               = health × pressure × (1 + 0.04 × attacks) × (1 + 0.1 × (phases − 3))
//                         × 1.15 with a time limit × (1 + 0.05 × attacks of its last stand);
//   stars               = 1 + 9 × ln(score / kOneStar) / ln(kTenStars / kOneStar), rounded and
//                         kept between 1 and 10.
//
// The two references are fixed (the easiest guardian of 2.34, Mycelia, and Singularis): a boss
// made in the enemy editor is rated on the same scale.
#include "game/enemies/EnemyDefinition.h"

namespace cosmic {

struct BossDifficulty {
    static constexpr double kOneStar = 56.0;     // score of 1 star
    static constexpr double kTenStars = 4400.0;  // score of 10 stars

    double pressure = 0.0;  // weighted by the phases' shares of health
    double score = 0.0;
    int stars = 1;          // 1 … 10
};

BossDifficulty bossDifficulty(const EnemyDefinition& d);

}  // namespace cosmic
