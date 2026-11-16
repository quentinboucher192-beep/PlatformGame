#pragma once

// Collection › Replays (2.45): « Replays téléchargés », the online replays kept on this
// computer (ReplayLibrary), newest first, filtered by level. « Regarder » opens the replay
// viewer on the local file (no connection, no account needed), « Comparer » the comparison
// of replays with it as A, « Supprimer » removes it after a confirmation. A replay made on
// another version of its level, or whose level is missing, says so and is not played (the
// same versions as the viewer checks: ranked and gameplay fingerprints, computed off the
// main thread).

#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/ghost/ReplayLibrary.h"

namespace cosmic {

class GameServices;

class DownloadedReplaysPanel {
public:
    explicit DownloadedReplaysPanel(GameServices& g) : g_(g) {}
    ~DownloadedReplaysPanel();
    /// Every frame (the tab shown or not): results of the worker threads.
    void update();
    void draw(const Rect& area);

private:
    enum class Action { Watch, Compare };
    struct Opening {
        std::string id;
        Action action = Action::Watch;
        std::future<std::optional<ReplayData>> replay;
        std::shared_ptr<std::string> error;
    };
    using Versions = std::map<std::string, std::optional<std::vector<std::string>>>;

    void checkVersions();
    void open(const DownloadedReplay& e, Action a);
    void opened(const DownloadedReplay& e, Action a, ReplayData rep);
    void drawRow(const DownloadedReplay& e, const Rect& row, float bw, float fs, bool visible);
    std::string shownPlayer(const DownloadedReplay& e) const;
    std::string levelName(const DownloadedReplay& e) const;
    void say(const std::string& m, bool error);

    GameServices& g_;
    Versions versions_;                 // installed versions of each level (nullopt: not installed)
    std::future<Versions> checking_;    // levels being read
    std::vector<std::string> checkingIds_;
    std::optional<Opening> opening_;
    std::string filterLevel_;           // "" : every level
    std::string confirmDelete_;         // the replay whose « Supprimer » waits for its confirmation
    std::string deleteNow_;             // confirmed: removed once the list is drawn
    std::string msg_;
    bool msgError_ = false;
    float msgTime_ = -1.0f;
};

}  // namespace cosmic
