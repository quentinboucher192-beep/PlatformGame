#pragma once

// The training curve of a level and a category (docs/TAS.md « Courbe d'entraînement »), per
// VERSION of the level (its ranked fingerprint and the simulation): a changed level starts a new
// curve, the old ones stay (read-only: nothing trains them any more).
//
//   * the POINTS: one each time the best time of the training improves (the run it starts from,
//     verified: « start »; a faster run verified and written by cosmic_tas: « improvement »), at
//     the cumulative training time of the version then (the server's measure once shared) with
//     the attempts explored so far; and one per run PUBLISHED (« publish », from the server's
//     list of runs). Only real points: read from cosmic_tas's progress file (bestMs,
//     bestElapsedMs, bestExplored) or given by the server — never computed in between;
//   * the WINDOWS: the attempts of the search counted every few seconds (TasAttemptWindow: how
//     many, how many finished and their times, where the others stopped), placed on the same
//     training time; with their CLOUDS (2.98): each finished attempt a point (400 a window at
//     most, then a sample with its best), and a sample of the times at which the attempts reached
//     each checkpoint of the level (CP1, CP2…: 32 a window and a checkpoint, with their best).
//
// Kept on this computer (<saves>/tas_local/.training/<level>_<CATEGORY>.curve.json, TasCurveBook:
// read and written on a thread of their own) and shared: the new points and a downsampled summary
// of the windows go with the lease's heartbeat and release (server/src/tas.ts, which places them
// on its own measure of the training time and keeps at most 1 000 points per version); GET
// /admin/tas gives every administrator the points, GET /admin/tas/{level}/{category}/curve the
// windows too. This computer keeps its own finer windows; the server's are shown for the sessions
// of the other computers.

#include <condition_variable>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/online/OnlineTypes.h"
#include "game/tas/TasProgress.h"

namespace cosmic {

enum class TasCurveKind { Start, Improvement, Publish };
const char* tasCurveKindId(TasCurveKind k);  // "start", "improvement", "publish"
bool tasCurveKindFromId(const std::string& id, TasCurveKind& out);

/// Where a point or a window of this computer stands with the server: shared (its list has it),
/// pending (goes with its lease's next heartbeat or release), unshared (its lease ended first:
/// kept here only).
enum class TasShare { Shared, Pending, Unshared };

/// "2026-11-27T14:05:09Z" (UTC) of a time in ms since the epoch; and now.
std::string tasIsoUtc(int64_t epochMs);
int64_t tasNowMs();

struct TasCurvePoint {
    TasCurveKind kind = TasCurveKind::Improvement;
    int64_t timeMs = 0;      // the best time (a publication: the run published)
    int64_t trainingMs = 0;  // the cumulative training time of the version then
    int64_t explored = 0;    // the attempts explored so far (cumulative)
    std::string at;          // ISO 8601 UTC
    std::string by;          // the administrator: whose computer found it, who published it
    int revision = 0;        // a publication: its revision
    int session = 0;         // the session of the version (the server's count) that found it
    /// 2.99 (docs/TAS.md « Où est le gain »): the run's splits at the level's checkpoints (CP1
    /// first; tasSplitsFromJson) — compared with the best point before it, they say in which
    /// segment the gain is — and where the gain came from (TasProgress::bestSource). Empty
    /// before 2.99, and for a publication.
    std::vector<int64_t> splits;
    std::string source;
    TasShare share = TasShare::Shared;
    // Pending (this game only, never written): its lease, when it was found (its age when sent),
    // the session's attempts then.
    std::string leaseId;
    int64_t foundAtMs = 0;
    int64_t sessionExplored = 0;

    bool best() const { return kind != TasCurveKind::Publish; }
    bool sameAs(const TasCurvePoint& o) const;
    Json toJson() const;
    static std::optional<TasCurvePoint> fromJson(const Json& j);
    /// A point of the server (GET /admin/tas, /curve, the answer to a heartbeat): nullopt when malformed.
    static std::optional<TasCurvePoint> fromServer(const Json& j);
};

struct TasCurveWindow {
    int64_t fromMs = 0, toMs = 0;  // the cumulative training time it covers
    int session = 0;
    std::string by, at;  // whose computer; its end (ISO)
    bool local = false;  // counted on this computer (the finer windows of its own sessions)
    TasShare share = TasShare::Shared;
    std::string leaseId;  // pending: its lease
    int64_t endAtMs = 0;  // pending: when it ended (ms since the epoch)
    TasAttemptWindow stats;

    Json toJson() const;
    static std::optional<TasCurveWindow> fromJson(const Json& j);
    static std::optional<TasCurveWindow> fromServer(const Json& j);
};

struct TasCurveVersion {
    std::string levelHash;
    int simVersion = 0;
    std::vector<TasCurvePoint> points;    // by training time; the best ones each strictly faster
    std::vector<TasCurveWindow> windows;  // by training time
    /// The level's checkpoints the windows count (CP1 first), as this computer's trainings named
    /// them (empty: never trained here; the tabs then say « CP n » only).
    std::vector<TasCheckpoint> checkpoints;
    /// The players' runs that guided this computer's last training of the version (cosmic_tas
    /// --guide): their splits, the references drawn at each checkpoint (empty: none).
    std::vector<TasProgress::Reference> references;
    /// The best point before `points[i]` that has splits comparable with it (nullptr: none).
    const TasCurvePoint* previousBest(size_t i) const;
    /// The fastest best time (0: none), the number of best points.
    int64_t bestMs() const;
    size_t bests() const;
    /// The training time the curve reaches (its last point or window).
    int64_t lastTrainingMs() const;
};

/// The sum of the best segments (docs/TAS.md « Les guides »): segment by segment, the fastest of
/// the version's references (the players' runs that guide it) and of its best run (its splits),
/// added up to the checkpoint `cp` (1-based; 0: to the exit). 0 without a reference with splits.
int64_t tasSumOfBest(const TasCurveVersion& v, int cp);
/// Where a gain came from (TasCurvePoint::source), in French: « la recherche », « un guide »…
std::string tasSourceLabel(const std::string& source);

/// Every window's counts added (TasAttemptWindow::merged): the whole training, a round...
TasAttemptWindow tasAttemptTotal(const std::vector<const TasCurveWindow*>& windows);

class TasCurve {
public:
    static constexpr size_t kMaxPoints = 1000;   // best points per version (the server keeps as many)
    static constexpr size_t kMaxWindows = 1500;  // windows per version kept here (neighbours merged beyond)
    /// The clouds' points per version kept here: the finished attempts, and as many checkpoint
    /// times; beyond, the oldest windows keep only their best (the curve says how many are shown).
    static constexpr size_t kMaxCloudPoints = 30000;
    std::string levelId;
    RunCategory category = RunCategory::AnyPercent;
    std::vector<TasCurveVersion> versions;

    const TasCurveVersion* find(const std::string& hash, int sim) const;
    TasCurveVersion& version(const std::string& hash, int sim);

    /// A new best time of a training: kept only when faster than every best of its version, never
    /// before the last point on the training time; at kMaxPoints the last best point gives way
    /// (the curve keeps its start and its best). True when kept.
    bool addBest(const std::string& hash, int sim, TasCurvePoint p);
    /// A window of this computer, in order; beyond kMaxWindows neighbours of the same session,
    /// round and section are merged (the oldest first).
    void addWindow(const std::string& hash, int sim, TasCurveWindow w);
    /// The server's points of a version: they replace the shared ones; this computer's pending and
    /// unshared ones stay while they are still the fastest. True when anything changed.
    bool mergeServerPoints(const std::string& hash, int sim, std::vector<TasCurvePoint> pts);
    /// The server's windows of a version: those of the sessions counted here are left out (this
    /// computer's own finer windows are kept). True when anything changed.
    bool mergeServerWindows(const std::string& hash, int sim, std::vector<TasCurveWindow> ws);
    /// The server took what a heartbeat or a release of `leaseId` carried: the points sent
    /// (by foundAtMs) give way to those it kept (`added`, placed on its own training time; the
    /// others were not faster than its curve); the windows sent (by stats.id) are shared.
    void acknowledge(const std::string& hash, int sim, const std::string& leaseId, const std::vector<int64_t>& sentPoints,
                     const std::vector<TasCurvePoint>& added, const std::vector<int64_t>& sentWindows);
    /// The lease ended without taking them: its pending points and windows stay here only.
    void abandon(const std::string& leaseId);

    Json toJson() const;
    /// Nullopt for anything but a curve file; the pending points and windows of a file come back
    /// unshared (their lease ended with the game that wrote them).
    static std::optional<TasCurve> fromJson(const Json& j);
    /// Sorted by training time, the best points strictly faster each time, at most kMaxPoints.
    static void normalize(TasCurveVersion& v);
};

/// Every training curve of this computer: read on a thread when opened, written on a thread
/// (each changed curve a few seconds later, or at once for a new point).
class TasCurveBook {
public:
    TasCurveBook() = default;
    ~TasCurveBook();
    TasCurveBook(const TasCurveBook&) = delete;
    TasCurveBook& operator=(const TasCurveBook&) = delete;

    /// Reads the curves of `dir` on a thread; until then (and without a folder) they are only in memory.
    void open(const fs::path& dir);
    bool loaded() const { return loaded_; }
    /// Every frame: the curves read are taken in, those changed are written when due.
    void update(float dt);
    /// Everything changed written now (waits for the writer).
    void flush();

    const TasCurve* find(const std::string& levelId, RunCategory c) const;
    TasCurve& at(const std::string& levelId, RunCategory c);
    /// The curve changed: written within a few seconds (`now`: at the next update).
    void changed(const std::string& levelId, RunCategory c, bool now = false);
    static fs::path fileOf(const fs::path& dir, const std::string& levelId, RunCategory c);

private:
    static std::string key(const std::string& levelId, RunCategory c);
    void writer();
    std::map<std::string, TasCurve> curves_;
    fs::path dir_;
    std::future<std::vector<TasCurve>> reading_;
    bool loaded_ = false;
    std::map<std::string, float> dirty_;  // key -> seconds left before it is written
    // The writer thread: copies of the curves to write.
    std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::string, std::pair<fs::path, TasCurve>> pending_;
    bool writing_ = false, stop_ = false;
    std::thread thread_;
};

}  // namespace cosmic
