#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "engine/core/Json.h"

namespace cosmic {

/// Players' rating of an official level (Cosmic Online 2.19): the average and the number of
/// ratings are computed by the server; `myStars` is the player's own rating (0: none).
/// Not to be confused with the difficulty /5 computed by the game from the level itself.
struct LevelRatingInfo {
    double average = 0.0;  // meaningful when count > 0
    int count = 0;
    int myStars = 0;
};

/// Last averages received from the server for the official levels, kept on disk so that the
/// campaign can still show them without network — always presented as received earlier
/// (fresh() is true only once they were read during this session). Nothing is ever invented:
/// without anything received, get() has nothing.
class LevelRatingCache {
public:
    /// Data of GET /levels/ratings, read with the account `player` on `server`.
    bool applyList(const Json& data, const std::string& server, const std::string& player, int64_t nowUnix);
    /// A rating view (GET / PUT / DELETE /levels/{id}/rating) of `player` on `server`.
    bool applyOne(const Json& view, const std::string& server, const std::string& player);
    /// Rating of a level as `player` sees it. Once a list was received, a level missing from
    /// it has no rating yet (count 0); before that, nothing is known (nullopt).
    std::optional<LevelRatingInfo> get(const std::string& levelId, const std::string& player) const;
    bool known() const { return receivedAt_ > 0; }
    bool fresh() const { return fresh_; }
    /// When the list was received (Unix seconds, 0 = never).
    int64_t receivedAt() const { return receivedAt_; }
    const std::string& server() const { return server_; }

    Json toJson() const;
    /// A saved cache (never fresh). False when unreadable or saved for another server.
    bool fromJson(const Json& j, const std::string& server);
    void clear();

    /// "4,3": one decimal, French comma.
    static std::string averageText(double average);
    /// "aucune note", "1 note", "128 notes".
    static std::string countText(int count);

private:
    struct Entry {
        double average = 0.0;
        int count = 0;
    };
    void resetFor(const std::string& server, const std::string& player);

    std::string server_, player_;
    int64_t receivedAt_ = 0;
    bool fresh_ = false;
    std::map<std::string, Entry> levels_;
    std::map<std::string, int> mine_;
};

}  // namespace cosmic
