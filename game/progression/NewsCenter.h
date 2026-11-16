#pragma once

#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "game/records/Records.h"

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
    /// 2.49.1: a revision of a campaign level downloaded from Cosmic Online. `kept`: published
    /// with « Garder les records » (no major change: the best time stays, the rankings too);
    /// else the best time `old` set on the revision before no longer counts.
    void noteRevision(const std::string& levelId, const std::string& levelName, const std::string& hash, bool kept,
                      const std::optional<LevelRecord>& old);
    /// A newer installer exists on the server (GET /client/latest).
    void offerRelease(const std::string& version, int64_t size, const std::string& sha256, const std::string& download,
                      const std::string& notes);

    const std::vector<Item>& items() const { return items_; }
    int unread() const;
    void markAllRead();
    bool scanned() const { return scanned_; }

    /// The version run before this one ("" on a fresh install), until poll() records the new one.
    const std::string& lastVersion() const { return lastVersion_; }

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
