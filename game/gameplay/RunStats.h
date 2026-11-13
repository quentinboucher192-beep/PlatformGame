#pragma once

#include <set>
#include <string>

namespace cosmic {

/// Statistics of the current attempt (feeds records, medals and the results screen).
struct RunStats {
    int deaths = 0;
    int shards = 0;
    int totalShards = 0;
    int relics = 0;
    int totalRelics = 0;
    int secrets = 0;
    int totalSecrets = 0;
    int kills = 0;
    int totalEnemies = 0;
    int scoreBonus = 0;  // collectible / enemy values
    int damageTaken = 0;
    std::set<std::string> powersUsed;
    std::set<std::string> secretIds;

    int collectibles() const { return shards + relics; }
    int totalCollectibles() const { return totalShards + totalRelics; }
    int collectiblePercent() const {
        int t = totalCollectibles();
        return t > 0 ? (collectibles() * 100) / t : 100;
    }
};

}  // namespace cosmic
