#include "game/enemies/BossDifficulty.h"

#include <algorithm>
#include <cmath>

namespace cosmic {

namespace {

/// Pressure of a set of attacks with a phase's multipliers (see the header).
double pressureOf(const EnemyDefinition& d, const std::vector<std::string>& ids, double speed, double cooldown, double telegraph, int combo) {
    double dmg = 0.0, cycle = 0.0, wind = 0.0;
    int n = 0;
    for (const auto& id : ids) {
        const AttackDef* a = d.attack(id);
        if (!a) continue;
        dmg += a->damage;
        cycle += a->cooldown + a->telegraph + a->active + a->recover;
        wind += a->telegraph;
        ++n;
    }
    if (n == 0) return 0.0;
    dmg /= n;
    cycle /= n;
    wind = wind / n * telegraph;
    double perSecond = std::max(1, combo) / std::max(0.05, cycle * cooldown / std::max(0.05, speed));
    return dmg * perSecond / std::max(0.15, wind);
}

}  // namespace

BossDifficulty bossDifficulty(const EnemyDefinition& d) {
    BossDifficulty out;
    if (d.phases.empty()) {
        std::vector<std::string> all;
        for (const auto& a : d.attacks) all.push_back(a.id);
        out.pressure = pressureOf(d, all, 1.0, 1.0, 1.0, 1);
    } else {
        for (size_t i = 0; i < d.phases.size(); ++i) {
            const BossPhase& p = d.phases[i];
            double next = i + 1 < d.phases.size() ? d.phases[i + 1].hpThreshold : 0.0;
            double share = std::max(0.0, static_cast<double>(p.hpThreshold) - next);
            out.pressure += share * pressureOf(d, p.attacks, p.speedMultiplier, p.cooldownMultiplier, p.telegraphMultiplier, p.combo);
        }
    }
    bool limit = d.enrageTime > 0.0f;
    for (const auto& p : d.phases) limit = limit || p.enrageTime > 0.0f;
    int phases = static_cast<int>(std::max<size_t>(1, d.phases.size()));
    out.score = std::max(0.0f, d.hp) * out.pressure * (1.0 + 0.04 * static_cast<double>(d.attacks.size())) *
                std::max(0.5, 1.0 + 0.1 * (phases - 3)) * (limit ? 1.15 : 1.0) * (1.0 + 0.05 * static_cast<double>(d.finalAttacks.size()));
    if (out.score <= 0.0) {
        out.stars = 1;
        return out;
    }
    double t = std::log(out.score / BossDifficulty::kOneStar) / std::log(BossDifficulty::kTenStars / BossDifficulty::kOneStar);
    out.stars = std::clamp(static_cast<int>(std::lround(1.0 + 9.0 * t)), 1, 10);
    return out;
}

}  // namespace cosmic
