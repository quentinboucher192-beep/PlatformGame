#include "game/levels/RatingService.h"

#include <algorithm>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/content/ContentDatabase.h"

namespace cosmic {

namespace {
std::string fileKey(const LevelEntry& e) {
    std::error_code ec;
    auto t = fs::last_write_time(e.path, ec);
    auto size = fs::file_size(e.path, ec);
    return e.id + "|" + Paths::toUtf8(e.path) + "|" + std::to_string(t.time_since_epoch().count()) + "|" + std::to_string(size);
}
}  // namespace

RatingService::~RatingService() {
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
        jobs_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void RatingService::init(const ContentDatabase&) {
    campaign_.clear();
    auto j = FileUtil::loadJson(Paths::dataDir() / "levels" / "ratings.json");
    if (!j || jstr(*j, "format") != "cosmic.level_ratings") return;
    if (jget<int>(*j, "version", 0) != DifficultyRating::kRatingVersion) {
        Log::warn("ratings.json : version de formule différente, notes recalculées");
        return;
    }
    if (auto it = j->find("levels"); it != j->end() && it->is_object())
        for (const auto& [id, r] : it->items()) campaign_[id] = DifficultyRating::fromJson(r);
}

void RatingService::start() {
    if (thread_.joinable()) return;
    thread_ = std::thread([this] { run(); });
}

void RatingService::run() {
    std::unique_ptr<ContentDatabase> content;
    for (;;) {
        Job job;
        bool reload = false;
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            reload = reload_;
            reload_ = false;
        }
        if (!content || reload) {
            content = std::make_unique<ContentDatabase>();
            content->reload();
        }
        Done d;
        d.key = job.key;
        d.generation = job.generation;
        if (job.draft) {
            d.rating = rateDifficulty(*job.draft, *content);
        } else if (auto level = content->loadLevel(job.levelId)) {
            d.rating = rateDifficulty(*level, *content);
        } else {
            d.rating.reachable = false;
        }
        std::lock_guard<std::mutex> lock(m_);
        done_.push_back(std::move(d));
    }
}

void RatingService::poll() {
    std::lock_guard<std::mutex> lock(m_);
    while (!done_.empty()) {
        Done d = std::move(done_.front());
        done_.pop_front();
        if (d.key.empty()) {
            draft_ = d.rating;
            if (std::none_of(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.draft != nullptr; })) draftPending_ = false;
            continue;
        }
        queued_.erase(d.key);
        if (d.generation == generation_) cache_[d.key] = d.rating;
    }
}

const DifficultyRating* RatingService::get(const LevelEntry& level) {
    if (level.source == ContentSource::Builtin) {
        auto it = campaign_.find(level.id);
        if (it != campaign_.end()) return &it->second;
    }
    std::string key = fileKey(level);
    if (auto it = cache_.find(key); it != cache_.end()) return &it->second;
    if (!queued_.count(key)) {
        queued_[key] = true;
        start();
        {
            std::lock_guard<std::mutex> lock(m_);
            jobs_.push_back({key, level.id, nullptr, generation_});
        }
        cv_.notify_one();
    }
    return nullptr;
}

void RatingService::requestDraft(const LevelData& level) {
    start();
    draftPending_ = true;
    {
        std::lock_guard<std::mutex> lock(m_);
        jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.draft != nullptr; }), jobs_.end());
        jobs_.push_back({"", level.id, std::make_shared<LevelData>(level), generation_});
    }
    cv_.notify_one();
}

void RatingService::contentChanged() {
    std::lock_guard<std::mutex> lock(m_);
    reload_ = true;
    ++generation_;
    cache_.clear();
    queued_.clear();
}

}  // namespace cosmic
