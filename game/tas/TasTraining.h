#pragma once

// The trainings of the machine's runs on an administrator's computer (docs/TAS.md
// « Entraînement depuis ADMINISTRATION », ADMINISTRATION › TAS). Each training of a level and a
// category is ONE `cosmic_tas` child process (tools/tas, installed next to the game; the game
// never links the search): it searches from the best run this computer has (seeded with its
// replay) until it is stopped, rewrites its progress file every half second, and writes each
// faster run it verifies to <saves>/tas_local/. Nothing runs on Cosmic Online but a lease:
//
//   * a training starts only once the server gave this account the LEASE of its level and
//     category (POST /admin/tas/{level}/{category}/lease): two administrators never train the
//     same pair; the server checks the role of the session on every request;
//   * a heartbeat every minute keeps it (the server counts the training time between two of
//     them, and the attempts reported, bounded); without one for three minutes it expires: a
//     computer turned off or crashed frees its pairs by itself;
//   * « Arrêter » asks the child to stop (it verifies and writes its best run, then exits) and
//     releases the lease as soon as it has exited (kReleaseGrace at most: its last best time
//     and attempts go with the release); closing the game releases every lease at once.
//
// The training curve (game/tas/TasCurve.h): each new best time read in the progress file is a
// point, each window of attempts is kept; both go to the server with the next heartbeat or the
// release of their lease (pending until it took them).
//
// Several pairs train at once (« Entraînements en parallèle », « Threads par entraînement »); a
// queue (« Tout entraîner ») keeps that many running, a pair held by another administrator
// skipped; with a budget per pair, a pair stops after it and goes back to the end of the queue
// (round-robin). The queue lives as long as the game, not the page.

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/online/OnlineTypes.h"
#include "game/tas/TasCurve.h"
#include "game/tas/TasProgress.h"

struct SDL_Process;

namespace cosmic {

class OnlineService;

struct TasPair {
    std::string levelId;
    RunCategory category = RunCategory::AnyPercent;
    std::string key() const { return levelId + "/" + categoryId(category); }
    bool operator==(const TasPair& o) const { return levelId == o.levelId && category == o.category; }
};

struct TasTrainingSettings {
    static constexpr int kMaxParallel = 16;
    static constexpr int kMaxThreads = 16;
    int threadsPerJob = 2;
    int parallel = 1;        // trainings at once
    int budgetMinutes = 30;  // per pair, then the next pair of the queue (0: until stopped)

    /// The cores of this computer shared between the trainings: cores / threads per training,
    /// at least 1, at most kMaxParallel.
    static int defaultParallel(int threadsPerJob, int cores);
    Json toJson() const;
    static TasTrainingSettings fromJson(const Json& j, int cores);
};

class TasTraining {
public:
    enum class JobState { Acquiring, Running, Stopping, Finished, Failed };
    struct Job {
        TasPair pair;
        JobState state = JobState::Acquiring;
        std::string leaseId;
        SDL_Process* process = nullptr;
        std::optional<TasProgress> progress;
        std::chrono::steady_clock::time_point started{}, lastBeatOk{}, lastBeatSent{}, stopAsked{};
        bool hasSeed = false;
        bool budgetReached = false;  // stopped by its budget: back to the end of the queue
        bool releasing = false;
        std::string levelHash;                         // the version trained (with kSimulationVersion)
        int64_t serverTotalMs = 0, serverExplored = 0;  // the version's totals at the server's last answer
        int64_t exploredReported = 0;                   // this session's attempts at that answer
        int session = 0;                                // the version's session number of this lease (server)
        int64_t lastBest = 0;                           // the best time last put on the curve by this training
        int64_t lastWindow = 0;                         // the progress file's last window taken
        std::string error;
        fs::path progressFile, stopFile;
        /// This session's time (since its lease), as this computer counts it.
        int64_t sessionMs() const;
        /// The version's training time now (the total the page shows): the server's at its last
        /// answer, plus the time since while the lease is held.
        int64_t trainingTotalMs() const;
        /// The version's attempts when the session had explored `sessionExplored`.
        int64_t exploredAt(int64_t sessionExplored) const;
    };

    explicit TasTraining(OnlineService& online);
    ~TasTraining();
    TasTraining(const TasTraining&) = delete;
    TasTraining& operator=(const TasTraining&) = delete;

    /// `cosmic_tas` next to the game (installed), else in the build folder; empty when missing.
    static fs::path toolPath();
    /// <saves>/tas_local: the runs the trainings of this computer verified (layout of data/tas).
    static fs::path localRoot();
    static int cores();
    /// A child process as a training launches it: `args[0]` the program, its input a pipe of
    /// this game (SDL_GetProcessInput: closing it asks the child to stop), no window, no output.
    static SDL_Process* spawn(const std::vector<std::string>& args);

    void loadSettings();
    void saveSettings() const;
    TasTrainingSettings& settings() { return settings_; }
    const TasTrainingSettings& settings() const { return settings_; }

    /// « Continuer l'entraînement »: the pair first in the queue, started as soon as a place is free.
    void train(const TasPair& p);
    /// « Tout entraîner »: these pairs at the end of the queue (those already queued or training are skipped).
    int enqueue(const std::vector<TasPair>& pairs);
    void unqueue(const TasPair& p);
    void clearQueue() { queue_.clear(); }
    /// « Arrêter l'entraînement »: the child asked to stop, the lease released now (or the pair left out of the queue).
    void stop(const TasPair& p);
    void stopAll();
    /// Game closing: every child stopped, every lease released (the answers waited for at most `waitMs`).
    void shutdown(int waitMs = 3000);

    /// Every frame: the queue, the progress files, the heartbeats, the children that ended.
    void update(float dt);

    const std::vector<std::unique_ptr<Job>>& jobs() const { return jobs_; }
    const std::deque<TasPair>& queue() const { return queue_; }
    const Job* jobFor(const TasPair& p) const;
    bool queued(const TasPair& p) const;
    int running() const;
    /// Why the last training of the pair failed ("" none, or since it started again).
    std::string failureOf(const TasPair& p) const;

    /// The replay of the best valid run this computer has for the pair (its own trainings', the
    /// downloaded ones, the shipped one): the seed of a training. Empty: from scratch.
    std::function<fs::path(const TasPair&)> seedFor;
    /// The level's ranked fingerprint as this game plays it (the version of the level the lease
    /// and the training are for, with kSimulationVersion). Empty: not trainable.
    std::function<std::string(const std::string& levelId)> hashFor;
    /// Something to tell the page (a pair skipped, a lease lost, a training that failed).
    std::function<void(const std::string& message, bool error)> onMessage;
    /// A training ended (its pair may have a new local run).
    std::function<void(const TasPair&)> onEnded;

    /// The lease route of a pair: /api/v1/admin/tas/<level>/<CATEGORY>/lease.
    static std::string leasePath(const TasPair& p);

    /// The training curves of this computer (shared ones taken in by the page: AdminTas).
    TasCurveBook& curves() { return curves_; }
    const TasCurveBook& curves() const { return curves_; }
    /// What a heartbeat or a release of the lease carries for the curve: its pending points
    /// (`curve`, at most kSendPoints) and its pending windows downsampled to kSendWindows
    /// (`attempts`); the ids of what was sent. Public for the tests.
    static constexpr size_t kSendPoints = 16, kSendWindows = 4;
    /// The clouds' points a window sent carries (2.98): its finished attempts, each checkpoint's sample.
    static constexpr size_t kSendFinishes = 96, kSendCheckpointSample = 8;
    struct CurveSend {
        Json points = Json::array(), windows = Json::array();
        std::vector<int64_t> pointIds, windowIds;
        bool cloud = false;  // the windows carry their clouds' points
    };
    static CurveSend curveSend(const TasCurveVersion& v, const std::string& leaseId, int64_t nowMs, bool cloud = true);
    /// A new best time and the new windows of the progress file of a training, on the curve.
    void noteCurve(Job& j);

private:
    void startNext();
    void acquire(Job& j);
    void launch(Job& j);
    void heartbeat(Job& j);
    void requestStop(Job& j);
    void release(Job& j);
    void readProgress(Job& j);
    void ended(Job& j);
    void say(const std::string& m, bool error);
    /// The server's answer to a heartbeat or a release that carried `sent`.
    void curveAnswer(const TasPair& p, const std::string& hash, const std::string& lease, const CurveSend& sent, const ApiResult& r);
    /// The lease's pending points and windows (none once the server refused them).
    CurveSend pendingCurve(const TasPair& p, const std::string& hash, const std::string& lease) const;
    /// A server that does not know the clouds (then the curve) refused the call because of them
    /// (400): they are no longer sent (the call goes again without them: the lease is never lost
    /// for them).
    enum class Refusal { None, Cloud, Curve };
    Refusal curveRefused(const CurveSend& sent, const ApiResult& r);
    void sendRelease(const TasPair& p, const std::string& lease, const std::string& hash, int64_t explored, bool withCurve);

    OnlineService& online_;
    TasCurveBook curves_;
    bool curveUnsupported_ = false, cloudUnsupported_ = false;
    TasTrainingSettings settings_;
    std::vector<std::unique_ptr<Job>> jobs_;
    std::deque<TasPair> queue_;
    float pollTimer_ = 0.0f;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    int releasesPending_ = 0;
    std::map<std::string, std::string> failures_;  // pair key -> why
};

const char* tasJobStateLabel(TasTraining::JobState s);

}  // namespace cosmic
