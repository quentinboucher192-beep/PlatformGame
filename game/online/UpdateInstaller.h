#pragma once

#include <filesystem>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "game/progression/NewsCenter.h"

namespace cosmic {

class OnlineService;

/// The installer of a newer version offered by Cosmic Online (NewsCenter item "update"):
/// downloaded on demand, checked against its announced SHA-256 off the main thread, then
/// launched. Shared by the main menu's alert and the Notifications card, so that both show
/// the same state.
class UpdateInstaller {
public:
    enum class State { Idle, Downloading, Checking, Ready, Failed };

    void init(OnlineService& online, NewsCenter& news, std::string currentVersion);

    /// The newest installer offered that is newer than this game, or nullptr.
    const NewsCenter::Item* offer() const;

    State state(const std::string& version) const;
    std::string error(const std::string& version) const;
    /// 2.63.8: bytes received so far and the expected size (0: unknown) of a download in progress.
    std::pair<size_t, int64_t> progress(const std::string& version) const;
    std::filesystem::path file(const std::string& version) const;

    /// Downloads the installer of `it` (no-op while one is under way). `launchWhenReady`: the
    /// caller wants it run as soon as it is checked (see takeLaunch).
    void download(const NewsCenter::Item& it, bool launchWhenReady);

    /// Every frame: picks up the result of the SHA-256 check.
    void update();

    /// True once (then false) when an installer asked with `launchWhenReady` is ready.
    bool takeLaunch(std::string* version);

    /// Opens the installer (Windows: runs it). False when it cannot be opened (then error()
    /// says where the file is).
    bool launch(const std::string& version);

private:
    struct Check {
        std::mutex m;
        bool done = false;
        bool ok = false;
        std::string error;
    };
    OnlineService* online_ = nullptr;
    NewsCenter* news_ = nullptr;
    std::string current_;
    std::map<std::string, State> states_;
    std::map<std::string, std::string> errors_;
    std::map<std::string, std::shared_ptr<Check>> checks_;
    std::map<std::string, std::shared_ptr<std::atomic<size_t>>> received_;
    std::map<std::string, int64_t> sizes_;
    std::string launchWhenReady_, launchNow_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace cosmic
