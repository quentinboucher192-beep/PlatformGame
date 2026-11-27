#pragma once
// ADMINISTRATION › TAS: the machine's runs of every level shipped with the game (raids excluded)
// and every category, improved ON THIS COMPUTER (docs/TAS.md « Entraînement depuis
// ADMINISTRATION »). For each level and category: the run shipped with the game, the run
// published online (its version, when and by whom), the training time and attempts of every
// administrator for this version of the level, and where it stands: free, trained here (live),
// trained by another administrator (his lease), waiting in the queue, a faster run of this
// computer not published yet, or « À recalculer » (the level changed: the old runs are not
// comparable). « Entraîner » / « Arrêter » per pair, « Tout entraîner » (the pairs shown, those
// held by another administrator skipped), the trainings in parallel and their threads, a budget
// per pair (round-robin), « Publier » per pair and « Publier toutes les améliorations ». The
// trainings themselves live in GameServices::tasTraining (they go on when the page is left).
//
// The training curve (docs/TAS.md « Courbe d'entraînement », game/tas/TasCurve.h): each row shows
// a small step line of its best time and its attempts in short (finished, their share, the best
// time); a row selected (click, Entrée / A) opens its curve under the table (AdminTasCurve.cpp):
// the best time against the cumulative training time with its improvements, the runs published,
// the shipped and online times as reference lines, the attempts of each window (finished:
// best–worst and median; the sections reached), the rounds of cosmic_tas, and the counts of the
// whole training and of the round (finished, their times, where the others stopped). Échap / B or
// the row selected again closes it.

#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "engine/core/Paths.h"
#include "engine/rendering/Text.h"
#include "game/online/OnlineTypes.h"
#include "ui/widgets/Charts.h"

namespace cosmic {

class GameServices;
class TasTraining;
struct TasCurveVersion;
struct TasCurveWindow;

namespace tasadmin {

/// What this computer has for a level and a category (read on a worker thread).
struct Local {
    std::string levelId, levelName, planetId, planetName;
    RunCategory category = RunCategory::AnyPercent;
    std::string hash;           // the level's ranked fingerprint here ("" unknown: not trainable)
    int64_t shippedMs = 0;      // data/tas, valid for this version (0: none)
    int64_t localMs = 0;        // this computer's trainings (<saves>/tas_local), valid for this version
    int64_t downloadedMs = 0;   // published, downloaded and verified here
    int64_t oldShippedMs = 0;   // data/tas for an older version of the level (not comparable)
    int64_t oldLocalMs = 0;     // this computer's run of an older version
};

/// The server's list (GET /admin/tas) for a pair, read for this game's version of the level.
struct Server {
    int64_t publishedMs = 0;  // the run published for this version (0: none)
    int revision = 0;
    std::string publishedAt, publishedBy;
    int64_t archiveMs = 0;  // the last run published for another version (not comparable)
    int64_t trainingMs = 0, explored = 0;
    int sessions = 0;
    std::string holder, holderSince;  // an active lease on this version
    bool mine = false;
};

struct Row {
    Local local;
    Server server;
};

enum class Status { Free, Recompute, Acquiring, Training, Stopping, Queued, HeldByOther, HeldByMe, Unpublished, Failed };

/// The pair's entry of GET /admin/tas → its runs and training for the version `hash` (with this
/// game's simulation); `pair` null: nothing on the server.
Server serverFor(const Json& pair, const std::string& hash);
/// A run of this computer faster than the one published for this version (or none published).
bool unpublished(const Row& r);
/// No run for this version of the level anywhere, but one of an older version: it must be
/// computed again (« À recalculer (niveau modifié) »), as if never trained.
bool recompute(const Row& r);
Status statusOf(const Row& r, const TasTraining& t);
/// « Libre », « À recalculer (niveau modifié) », « En entraînement par … » (French source).
std::string statusLabel(Status s);
/// The pairs of every level shipped with the game (campaign order, then the others; raids are
/// not levels of the content), the four categories each. Its own content, read with this
/// computer's online revisions (`userDir`, `server`): for a worker thread.
std::vector<Local> scanLocal(const fs::path& userDir, const std::string& server);
/// « 2 h 05 min », « 12 min 30 s », « 45 s ».
std::string durationText(int64_t ms);
/// "27/11 · 14:05" (local time) of an ISO date of the server, « — » when it is not one.
std::string stamp(const std::string& iso);
/// A tick of the training time: « 0 », « 30 s », « 12 min », « 2 h », « 1 h 30 ».
std::string durationTick(int64_t ms);
/// « 1 234 567 » (French digit groups), « 12,3 M » / « 456 k » when compact.
std::string countText(int64_t n);
std::string compactCount(int64_t n);
/// « 0,04 % » (a share of `total`, at least two significant digits), « — » without a total.
std::string percentText(int64_t part, int64_t total);
/// The ticks of an axis: round steps (`steps`, increasing) from `lo` to `hi`, at most `maxCount`.
std::vector<int64_t> axisTicks(int64_t lo, int64_t hi, int maxCount, const std::vector<int64_t>& steps);
/// The steps of the training time (1 s … 7 days) and of a run's time (1 ms … 10 min).
const std::vector<int64_t>& durationSteps();
const std::vector<int64_t>& runTimeSteps();
/// Where the rounds of cosmic_tas start along the training time (a new session or a new round of
/// the windows, in order): {training time, round}.
std::vector<std::pair<int64_t, int>> roundStarts(const std::vector<TasCurveWindow>& windows);

}  // namespace tasadmin

/// What draw() placed (the fit tests): every text with its box and size, every button.
struct AdminTasLayout {
    struct Text {
        Rect r;
        std::string text;
        float size = 0.0f;
        FontFace face = FontFace::Body;
        bool verbatim = false;
    };
    Rect inner, list;
    std::vector<Text> texts;
    std::vector<Rect> buttons;
    int rowsDrawn = 0;
    int sparklines = 0;  // rows with a curve drawn
    // The curve of the row selected (empty when closed): its panel, the best time's plot, the
    // strip of the sections reached, the counts.
    Rect detail, plot, strip, stats;
    int pointsDrawn = 0, windowsDrawn = 0, roundsDrawn = 0, referenceLines = 0;
    bool emptyCurve = false;  // « jamais entraînée » / no point yet
};

class AdminTas {
public:
    AdminTas(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);
    ~AdminTas();

    /// The tab is shown: the pairs read on a worker thread, the server's list asked for.
    void enter(bool force = false);
    void draw(const Rect& inner);
    const AdminTasLayout& lastLayout() const { return layout_; }

    /// The pairs as scanLocal() gives them (tests), and the server's list (GET /admin/tas → data:
    /// its curves taken into this computer's, GameServices::tasTraining.curves()).
    void setLocal(std::vector<tasadmin::Local> local);
    void applyServer(const Json& data);
    /// GET /admin/tas/{level}/{category}/curve → data: the windows of attempts of every version too.
    void applyCurve(const Json& data);
    std::vector<tasadmin::Row> rows() const;

    /// The curve of a pair opened under the table (`levelId` empty: closed).
    void select(const std::string& levelId, RunCategory c);
    bool selected(const std::string& levelId, RunCategory c) const { return !selLevel_.empty() && selLevel_ == levelId && selCat_ == c; }
    /// Échap / B closes the curve first (ADMINISTRATION does not leave).
    bool capturesBack() const { return !selLevel_.empty(); }
    /// The tooltip of the curve (a point, a window of attempts), over everything: drawn last.
    void drawTooltip();
    const charts::Tip& tip() const { return tip_; }

private:
    void loadServer();
    void loadCurve();
    void drawDetail(const tasadmin::Row& row, const Rect& area);
    void drawStats(const tasadmin::Row& row, const TasCurveVersion* v, const Rect& r, bool training, int round);
    void publish(const tasadmin::Row& r, std::function<void(bool)> done);
    void publishNext();
    std::vector<const tasadmin::Local*> shown() const;
    void drawToolbar(float& y);
    void drawRow(const tasadmin::Row& row, const Rect& r, float actionsW);
    void text(const std::string& t, const Rect& box, float size, Color c, FontFace face = FontFace::Body, Align a = Align::Left, bool verbatim = false);
    bool button(const std::string& id, const Rect& r, const std::string& label, bool enabled, bool primary = false, bool danger = false);

    GameServices& g_;
    std::function<void(const std::string&, bool)> say_;
    std::function<bool(const ApiResult&)> lostRole_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);

    std::vector<tasadmin::Local> local_;
    std::future<std::vector<tasadmin::Local>> scan_;
    bool scanned_ = false, rescan_ = false;
    Json server_ = Json::object();  // GET /admin/tas
    bool serverLoaded_ = false, serverBusy_ = false;
    std::string serverError_;
    float serverT_ = -100.0f;
    bool toolFound_ = false;

    int category_ = -1;  // filter: -1 all, else the index in kAllCategories
    int planet_ = -1;    // filter: -1 all, else the index in planets()
    bool activeOnly_ = false;
    std::vector<std::pair<std::string, std::string>> planets() const;  // id, name

    std::vector<std::pair<std::string, RunCategory>> publishQueue_;
    bool publishing_ = false;
    int published_ = 0, publishFailed_ = 0;

    // The curve opened: its pair, the version looked at (0: this game's, then the older ones),
    // the keyboard cursors (a point, a window), the tooltip, its windows asked for every 30 s.
    std::string selLevel_;
    RunCategory selCat_ = RunCategory::AnyPercent;
    int selVersion_ = 0;
    int pointCursor_ = -1, windowCursor_ = -1;
    bool scrollToSel_ = false;
    charts::Tip tip_;
    bool curveBusy_ = false;
    float curveT_ = -100.0f;
    bool inList_ = false;  // drawing the rows (their scroll region cuts what is out of it)

    AdminTasLayout layout_;
};

}  // namespace cosmic
