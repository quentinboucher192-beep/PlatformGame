#pragma once
// 2.50: the earlier versions of a campaign level whose records count on the version shipped now
// (data/levels/kept_records.json, written by `CosmicPlatformer --keep-records <levels>` before
// tools/levelgen changes a level without a major change: a giga boss arena that gets every
// power, for instance). The server links the same rankings (levels migration `--keep`); this
// list lets the game keep its own best times too, offline included.

#include <map>
#include <set>
#include <string>

#include "engine/core/Json.h"

namespace cosmic {

class KeptRecords {
public:
    /// Reads data/levels/kept_records.json (nothing kept when it is missing or damaged).
    void loadDefault();
    void load(const Json& j);
    /// Gameplay fingerprints (local records) of the earlier versions kept for this level.
    const std::set<std::string>& gameplay(const std::string& levelId) const;
    /// Ranked fingerprints (Cosmic Online) of the earlier versions kept for this level.
    const std::set<std::string>& ranked(const std::string& levelId) const;
    bool empty() const { return gameplay_.empty() && ranked_.empty(); }

private:
    std::map<std::string, std::set<std::string>> gameplay_, ranked_;
};

}  // namespace cosmic
