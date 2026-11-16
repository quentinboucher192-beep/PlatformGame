#pragma once
// 2.50 « Migrer vers un nouveau compte » (docs/online/DEVICES.md, server/src/accountMigration.ts):
// what the game reads of the server's answers, kept apart from the screens (ui/screens/
// OnlineMigration) so that it is tested against the server's own shapes.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic {

class OnlineService;
struct HttpResponse;

/// Whether the server offers the migration, and the progression of an old account waiting to be
/// restored on this computer. Main thread only; every request goes through OnlineService.
class MigrationState {
public:
    /// Every frame. For the account logged in (and its server), asks GET /players/me/migration
    /// once: an answer means the server offers the migration; 404 or 405 (a server older than
    /// 2.50) means it does not, for this account and this server. A network failure, a server
    /// error or a refused session asks again a minute later; until an answer nothing about the
    /// migration is shown. Once it is offered, reads the progression waiting to be restored
    /// (GET /players/me/progress-restore). Logged out, or another account: everything forgotten.
    void update(OnlineService& online, float dt);
    bool available() const { return available_; }
    /// The `restore` of GET /players/me/progress-restore ({from_username, created_at,
    /// postponed_at, copy: {created_at, game_version, size, device_name, summary}}); null when
    /// nothing waits (or not known yet).
    const Json& pendingRestore() const { return restore_; }
    /// What the server answered since (a migration's `restore`, « Plus tard », « Restaurer »).
    void setPendingRestore(const Json& restore);
    /// Reads it again (the « Cet ordinateur » tab is shown, a notification is opened); `then`
    /// once the answer is in (also on a failure: the last known one is kept).
    void refreshRestore(OnlineService& online, std::function<void()> then = {});

private:
    std::string key_;  // "<server>|<player>": the account this state is about
    bool available_ = false, answered_ = false, busy_ = false;
    float wait_ = 0.0f;
    Json restore_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    int generation_ = 0;  // answers to an earlier account are dropped
};

/// The French sentence of a code of `problems` (GET /admin/players/{id}/migration) or of a
/// refusal of POST /admin/players/{id}/migrate.
std::string migrationProblemText(const std::string& code);
/// What forbids the migration among the preview's `problems`: every code, but LAST_ADMIN only
/// when the source is to be closed (the server refuses it in that case only).
std::vector<std::string> migrationBlockingProblems(const Json& problems, bool closeSource);

/// A download of GET /players/me/progress-restore/file, checked.
struct RestoreDownload {
    bool ok = false;
    bool gone = false;    // 404: nothing waits any more (restored elsewhere, replaced)
    std::string error;    // French: the server's message, or what is wrong with the file
    std::string sha256;   // of the file as received (the same as the server's header)
    std::string gameVersion;
    Json files;           // the copy's `files` (progress, records, boss_stats, achievements, collection)
};
/// The copy exactly as the old game sent it ({format: "cosmic.progress_copy", game_version,
/// player_id, created_at, summary, files}), its SHA-256 checked against the server's
/// `x-progress-sha256` header. An error answer (JSON envelope) gives the server's French message.
RestoreDownload readRestoreDownload(const HttpResponse& r);

}  // namespace cosmic
