#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "game/levels/DifficultyRating.h"

namespace cosmic {

struct LevelEntry;

/// Difficulty ratings for the screens. Campaign levels use data/levels/ratings.json
/// (computed with `CosmicPlatformer --rate-levels data/levels/ratings.json`, checked by the
/// tests); the player's and imported levels are rated on a worker thread, which owns its
/// own copy of the content, so the main thread never waits.
class RatingService {
public:
    RatingService() = default;
    ~RatingService();
    RatingService(const RatingService&) = delete;
    RatingService& operator=(const RatingService&) = delete;

    /// Reads the campaign ratings. Call after the content is loaded.
    void init(const ContentDatabase& content);
    /// Main thread, every frame: collects finished ratings.
    void poll();

    /// Rating of a level of the content database; nullptr while it is being computed.
    const DifficultyRating* get(const LevelEntry& level);
    /// Rates a level being edited (the newest request replaces an older one not started).
    void requestDraft(const LevelData& level);
    const std::optional<DifficultyRating>& draft() const { return draft_; }
    bool draftPending() const { return draftPending_; }
    /// The content changed (enemies saved, package imported): the worker reloads it and
    /// the levels of the player are rated again.
    void contentChanged();

private:
    struct Job {
        std::string key;  // cache key ("" = draft)
        std::string levelId;
        std::shared_ptr<LevelData> draft;
        int generation = 0;
    };
    struct Done {
        std::string key;
        DifficultyRating rating;
        int generation = 0;
    };
    void start();
    void run();

    std::map<std::string, DifficultyRating> campaign_;
    std::map<std::string, DifficultyRating> cache_;
    std::map<std::string, bool> queued_;
    std::optional<DifficultyRating> draft_;
    bool draftPending_ = false;
    int generation_ = 0;

    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::deque<Done> done_;
    bool stop_ = false;
    bool reload_ = true;
};

}  // namespace cosmic
