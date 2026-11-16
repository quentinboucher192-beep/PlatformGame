#pragma once

#include <future>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "game/ghost/ReplayLibrary.h"

namespace cosmic {

class OnlineService;

/// What « Télécharger » next to an online replay asks for: where Cosmic Online serves it and
/// what the screen knows of it (DownloadedReplay: id, source, player, level, category…).
struct ReplayDownloadRequest {
    std::string path;  // GET path of the replay ("/api/v1/records/<id>/replay"…)
    DownloadedReplay meta;
    /// How the confirmation names it, as the screen shows it (a pseudonym hidden in
    /// streaming mode stays hidden): « Replay de Joueur n°3 (Noyau 1) ».
    std::string title;
    size_t maxBytes = 4u << 20;
};

/// Downloads replays into the ReplayLibrary (2.45): the bytes come from the network thread,
/// are checked and written on a worker thread, then listed on the main thread (update()).
/// The result is told by a notification of the online service (a toast): « Replay
/// téléchargé », or why it failed.
class ReplayDownloads {
public:
    ReplayDownloads(OnlineService& online, ReplayLibrary& library) : online_(online), library_(library) {}
    ~ReplayDownloads();

    enum class State { None, Busy, Done };
    State state(const std::string& id) const;
    /// Starts a download (nothing when it is already downloaded or downloading). Returns
    /// false, with a notification saying why, when it cannot start (limits, unknown id).
    bool start(ReplayDownloadRequest req);
    /// Main thread, every frame: lists what was written, notifies the results.
    void update();

private:
    struct Job {
        std::string id, title;
        std::future<std::optional<DownloadedReplay>> result;
        std::shared_ptr<std::string> error;
    };
    void fail(const std::string& id, const std::string& why);

    OnlineService& online_;
    ReplayLibrary& library_;
    std::set<std::string> busy_;
    std::vector<Job> jobs_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace cosmic
