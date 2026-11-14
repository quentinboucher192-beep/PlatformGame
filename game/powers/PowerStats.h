#pragma once

#include <string>
#include <vector>

namespace cosmic {

struct PowerDefinition;

/// One characteristic of a power, in French, ready to show (guide, power sheet, pickup).
struct PowerStat {
    std::string key;    // "energy", "damage", "radius"... (from the definition)
    std::string label;  // "Énergie", "Dégâts", "Rayon"...
    std::string value;  // "20", "4", "56 px", "0,3 s"...
};

/// Everything the definition says about the power that a player can read: its energy cost
/// first, then damage, reach and the other parameters, its recharge last. Read from the
/// definition itself: what is shown is what the game uses.
std::vector<PowerStat> powerStats(const PowerDefinition& d);
/// A short line of its first `count` characteristics ("20 énergie · Dégâts 4 · Portée 420 px").
std::string powerStatsLine(const PowerDefinition& d, size_t count = 4);

}  // namespace cosmic
