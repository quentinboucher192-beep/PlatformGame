#pragma once

#include <future>
#include <map>
#include <optional>
#include <set>
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
        /// 2.76.2: the text already in English, when the server gave it (the notes of an installer:
        /// written from the dictionary of its version, which this game may not know yet).
        std::string textEn;
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
        /// 2.50: gameplay fingerprints of earlier versions whose records stay valid
        /// (data/levels/kept_records.json: no major change).
        std::map<std::string, std::set<std::string>> keptLevels;
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
                      const std::string& notes, const std::string& notesEn = {});

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

/// When to ask Cosmic Online for a newer installer (GameServices::updateReleaseCheck),
/// so that a version published while the game runs is announced at once: when Cosmic Online
/// becomes usable, when the connection comes back, after a login (wake), then every 10 minutes
/// (2 after a failure). Each new version gets its toast once per session. No clock, no network:
/// the caller says what happened (tested in tests/test_news.cpp).
class ReleaseWatch {
public:
    static constexpr float kPeriodSeconds = 600.0f;
    static constexpr float kRetrySeconds = 120.0f;
    /// Every frame: true when a check should be sent now. `usable`: Cosmic Online on and
    /// configured; `offline`: its last request failed (the server could not be reached).
    bool tick(float dt, bool usable, bool offline);
    /// A login (or anything that makes a check worth it): checked at the next tick.
    void wake() { due_ = true; }
    /// A check was sent / answered (`ok`: the server answered; a failure tries again sooner).
    void sent();
    void answered(bool ok);
    bool busy() const { return busy_; }
    /// True the first time this session that `version` (a plausible « x.y.z », with a SHA-256
    /// of 64 hexadecimal digits for its installer) is offered: its toast is shown.
    bool announce(const std::string& version, const std::string& sha256);

private:
    float sinceCheck_ = 0.0f;  // seconds since the last check was sent
    float period_ = kPeriodSeconds;
    bool due_ = false;
    bool busy_ = false;
    bool usable_ = false;
    bool offline_ = false;
    std::set<std::string> announced_;
};

}  // namespace cosmic
