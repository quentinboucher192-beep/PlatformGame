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

namespace cosmic {

class GameServices;
class TasTraining;

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
};

class AdminTas {
public:
    AdminTas(GameServices& g, std::function<void(const std::string&, bool)> say, std::function<bool(const ApiResult&)> lostRole);
    ~AdminTas();

    /// The tab is shown: the pairs read on a worker thread, the server's list asked for.
    void enter(bool force = false);
    void draw(const Rect& inner);
    const AdminTasLayout& lastLayout() const { return layout_; }

    /// The pairs as scanLocal() gives them (tests), and the server's list (GET /admin/tas → data).
    void setLocal(std::vector<tasadmin::Local> local);
    void applyServer(const Json& data);
    std::vector<tasadmin::Row> rows() const;

private:
    void loadServer();
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

    AdminTasLayout layout_;
};

}  // namespace cosmic
