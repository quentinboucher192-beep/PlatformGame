#pragma once

#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic {

class ContentDatabase;
class RecordsManager;

/// Notifications of the main menu: what changed in the game since the player last saw it.
/// New campaign levels, native powers, monsters and bosses; campaign levels modified (a best
/// time set on the old version no longer counts: it is dropped, and said); a newer
/// installer on the server; the version just installed. Saved in saves/news.json.
class NewsCenter {
public:
    struct Item {
        std::string id;     // unique (kind:target[:version])
        std::string kind;   // level, level_changed, record, power, enemy, boss, update, version
        std::string title;
        std::string text;
        std::string date;
        std::string target;  // level id, power id, enemy id, version
        bool read = false;
        // Installer offered by the server (kind "update").
        int64_t size = 0;
        std::string sha256;
        std::string download;
        Json toJson() const;
        static Item fromJson(const Json& j);
    };
    /// What the game contains now (fingerprints of the campaign levels: off the main thread).
    struct Snapshot {
        std::map<std::string, std::string> levels;  // campaign level id -> gameplay fingerprint
        std::map<std::string, std::string> levelNames;
        std::vector<std::pair<std::string, std::string>> powers, enemies, bosses;  // id, name
    };
    static Snapshot snapshot(const ContentDatabase& content);

    void load();
    bool save() const;
    /// Starts the snapshot on a worker thread; poll() applies it when ready.
    void startScan(const ContentDatabase& content);
    void poll(const ContentDatabase& content, RecordsManager& records, const std::string& gameVersion);
    /// Compares with what was already seen and adds the notifications.
    void apply(const Snapshot& now, RecordsManager& records, const std::string& gameVersion, const Json& changelog);
    /// A newer installer exists on the server (GET /client/latest).
    void offerRelease(const std::string& version, int64_t size, const std::string& sha256, const std::string& download,
                      const std::string& notes);

    const std::vector<Item>& items() const { return items_; }
    int unread() const;
    void markAllRead();
    bool scanned() const { return scanned_; }

private:
    void add(Item it);
    Json known_ = Json::object();
    std::string lastVersion_;
    std::vector<Item> items_;
    std::future<Snapshot> pending_;
    bool scanned_ = false;
};

/// "2.10.0" > "2.4.1" (numeric parts).
bool versionNewer(const std::string& a, const std::string& b);

}  // namespace cosmic
