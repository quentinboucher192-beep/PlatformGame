// VIEW REPLAY (advanced) and « Comparer deux replays ».
//
// Exact replays (format 2) are re-simulated: the level runs with the recorded seed and the
// inputs of every step, so the run looks exactly like it did for the player (animations,
// enemies, attacks, blocks breaking, HUD, camera). The state digests stored every 0.5 s are
// checked on the way; if the local simulation differs (other engine version), the viewer
// says so and offers the recorded trajectory. Old replays (format 1), and replays of
// another version of the level in a comparison, only show that trajectory.
//
// The player: play, pause, step by step forwards and backwards (1/120 s, a simulation
// step), speeds from 0.25x to 4x, a timeline to drag, jumps to the checkpoints, the exact
// chronometer, the player's inputs and the splits. A jump back loads the level again and
// simulates up to that step (ReplayPlayback): what is shown is always the real state.
//
// The comparison: A is played (exactly when it can be), B runs as a ghost on A's clock,
// shifted by an offset the player sets (fine, coarse, typed, or aligned on a checkpoint)
// to make them meet again; every gap shown includes it and says so. Read only: nothing here
// changes a record, the progression or anything online.
//
// The gap (2.91): live at the top of the run (in time, the Ghost Racing estimate exact at the
// checkpoints, and in distance along the route, 1 m = 1 tile) and as a curve over the whole run
// at the bottom, which is the timeline (a click goes there). The viewer compares the run with
// the record (PB), the Sum of Best (continuous when each best segment has its replay here, at
// the checkpoints only otherwise) or another replay; the comparison draws A − B, B's offset
// included. Everything is read from the replays' recorded trajectories and splits, off the main
// thread (ReplayGapSources), and kept until the reference or the offset changes.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <map>

#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/gameplay/LevelProgress.h"
#include "game/ghost/Ghost.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/ReplayCompare.h"
#include "game/ghost/ReplayGap.h"
#include "game/ghost/ReplayPlayback.h"
#include "game/online/AdminView.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "ui/screens/LevelPowerKeysScene.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayCompareNav.h"
#include "ui/screens/ReplayGapSources.h"
#include "ui/screens/ReplayGapView.h"
#include "ui/screens/TasUi.h"
#include "ui/widgets/Glyphs.h"

namespace cosmic {

namespace {

/// Never below 11.5 pixels (small windows): below, the font loses its spaces.
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

const Color kColorA{57, 240, 216, 255};
const Color kColorB{255, 158, 64, 255};
const Color kGold{255, 214, 90, 255};

constexpr std::array<float, 5> kSpeeds{0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
const std::array<const char*, 5> kSpeedLabels{"×0,25", "×0,5", "×1", "×2", "×4"};
constexpr int kSeekBudget = 3000;  // steps simulated per frame while jumping

bool isMine(const GhostMeta& m) { return m.source == "pb" || m.source == "prev" || m.source == "last"; }

/// Mine are named by what they are (best time, last run...), the others by their player.
std::string displayName(const GhostMeta& m) {
    if (m.source.empty() || m.source == "current") return m.label.empty() ? std::string("Replay") : m.label;
    return m.label.empty() || isMine(m) ? m.sourceLabel() : m.label;
}

class ReplayScene : public Scene {
public:
    /// Online record (downloaded).
    ReplayScene(GameServices& g, std::string recordId) : g_(g), recordId_(std::move(recordId)) {}
    /// A replay already in memory (file, local run). 2.75.3: `adminNote`, « Vu en tant qu'admin (…) »
    /// for a replay an administrator watches through a lock (the server's headers, adminview::replayNote).
    ReplayScene(GameServices& g, ReplayData replay, std::string title, std::string adminNote = {}) : g_(g), adminNote_(std::move(adminNote)) {
        local_ = true;
        auto r = std::make_shared<const ReplayData>(std::move(replay));
        a_.replay = r;
        a_.meta.source = "current";
        a_.meta.label = std::move(title);
        a_.meta.levelId = r->levelId;
        a_.meta.levelHash = r->levelHash;
        a_.meta.timeMs = r->durationMs;
        levelId_ = r->levelId;
        playerName_ = a_.meta.label;
        timeMs_ = r->durationMs;
    }
    /// The TAS's own replay (« meilleur temps trouvé par la machine »): its category, never ranked;
    /// the playback starts at `at` seconds (the sheet's route clicked there).
    void watchTas(const tas::TasRun& t, double at) {
        watchingTas_ = true;
        a_.meta.source = "tas";
        a_.meta.category = categoryId(t.category);
        a_.meta.levelVersion = t.levelVersion;
        startAt_ = at > 0.0 ? at : -1.0;
    }
    /// Two replays compared on `levelId`.
    ReplayScene(GameServices& g, std::string levelId, ReplayChoice a, ReplayChoice b)
        : g_(g), levelId_(std::move(levelId)), a_(std::move(a)), b_(std::move(b)) {
        local_ = true;
        playerName_ = displayName(a_.meta);
        timeMs_ = a_.replay ? a_.replay->durationMs : 0;
    }

    void onEnter() override {
        if (local_) {
            if (!a_.valid()) return fail("Aucun replay à montrer.");
            loadLevel();
            return;
        }
        // 2.45: a record downloaded on this computer plays from its copy (no network needed).
        if (openDownloaded()) return;
        status_ = "Chargement du record…";
        g_.online.get("/api/v1/records/" + recordId_, OnlineService::guard(life_, [this](const ApiResult& r) {
            if (!r.ok) return fail("Record introuvable : " + r.message);
            const Json& rec = r.data.value("record", Json::object());
            const Json& pl = r.data.value("player", Json::object());
            playerName_ = jstr(pl, "username");
            timeMs_ = jget<int64_t>(rec, "time_ms", 0);
            RunCategory c;
            categoryText_ = categoryFromId(jstr(rec, "category"), c) ? categoryLabel(c) : "";
            rank_ = r.data.contains("rank") && r.data["rank"].is_number() ? r.data["rank"].get<int>() : 0;
            a_.meta.source = "record";
            a_.meta.label = playerName_;
            a_.meta.playerId = jstr(pl, "id");
            a_.meta.recordId = recordId_;
            a_.meta.category = jstr(rec, "category");
            a_.meta.timeMs = timeMs_;
            a_.meta.validated = true;
            // 2.75.3: an administrator reads a record hidden from the others (a profile masked, an account
            // not active, a record or replay still pending or refused): marked all along, and a record that
            // is not verified never counts as one.
            if (const Json& av = r.data.value("admin_view", Json()); av.is_object()) {
                adminNote_ = adminview::recordNote(av);
                recordStatus_ = jstr(av, "status", jstr(rec, "verification_status"));
                a_.meta.validated = adminview::countsAsVerified(recordStatus_);
            }
            downloadReplay(jstr(rec, "level_id"));
        }), false);
    }

    void onExit() override {
        if (mounted_) g_.app.resources().popMount();
        mounted_ = false;
        g_.app.input().clearPowerSlotKeys();
    }

    void update(float dt) override {
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
        if (routeFuture_.valid() && routeFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            route_ = routeFuture_.get();
            routeReady_ = true;
            trackA_.mapProgress(route_);
            hud_.setProgressTrack(route_);
            if (comparing_) cmp_.setRoute(route_);
            // The level's checkpoints in the route's order (the Sum of Best's segments, by id).
            std::vector<std::pair<float, std::pair<std::string, Vec2>>> cps;
            for (const LevelObject& o : level_.objects)
                if (o.type == "checkpoint") cps.push_back({route_.valid() ? route_.fractionAt(o.rect.bottomCenter()) : o.rect.x, {o.id, o.rect.bottomCenter()}});
            std::stable_sort(cps.begin(), cps.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            routeCheckpoints_.clear();
            for (auto& c : cps) routeCheckpoints_.push_back(std::move(c.second));
        }
        pollGap();
        updateMeasurePasses();
        if (!loaded()) return;
        if (playback_) updateExact(dt);
        else updateTrace(dt);
        if (playback_) {
            // The HUD shows each power with its key in this level for this player (2.31).
            if (!powerKeysReady_) powerKeys_ = playerPowerKeys(g_, level_);
            powerKeysReady_ = true;
            applyPowerSlotKeys(g_.app.input(), world().player(), powerKeys_);
        }
        updateGhosts(dt);
        updateCamera(dt);
    }

    void render(float) override {
        UIContext& ui = g_.ui;
        Renderer& r = g_.app.renderer();
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        if (!loaded()) {
            r.fillRect({0, 0, ui.screenW(), ui.screenH()}, Color{6, 9, 20, 255});
            float w = std::min(ui.screenW() - ui.s(80), ui.s(1100));
            Vec2 sz = ui.text().measure(status_, FontFace::Body, ui.s(fz(ui, 24)));
            float fs = sz.x > w ? fz(ui, 20) : fz(ui, 24);
            if (sz.x > w) ui.labelWrapped(status_, {(ui.screenW() - w) * 0.5f, ui.screenH() * 0.40f}, w, fs, failed_ ? ui.theme().danger : ui.theme().text);
            else
                ui.label(status_, {ui.screenW() * 0.5f, ui.screenH() * 0.45f}, fs, failed_ ? ui.theme().danger : ui.theme().text, Align::Center,
                         FontFace::Body, VAlign::Middle);
            UIContext::ButtonStyle st;
            if (ui.button("replay/back", {ui.screenW() * 0.5f - ui.s(120), ui.screenH() * 0.55f, ui.s(240), ui.s(48)}, "Retour", st) || ui.backPressed())
                g_.app.popScene();
            ui.end();
            return;
        }
        computeLayout();
        RenderOptions opt;
        opt.lighting = g_.settings.graphics.dynamicLighting;
        WorldView view = makeView(r);
        float alpha = playback_ && playing_ ? std::clamp(acc_ / kSimDt, 0.0f, 1.0f) : 1.0f;
        if (playback_) opt.threats = hud_.threatProbe(world(), g_.settings.gameplay);  // 2.60: its HUD fades over the boss's attacks
        renderer_.render(r, g_.app.resources(), world(), view, alpha, opt);
        r.setView(view);
        if ((comparing_ && showPaths_) || !playback_) drawPaths(r, view);
        if (!playback_) drawGhost(r, view, animA_, poseA(), kColorA, 0.95f);
        if (comparing_ && b_.meta.source == "tas") drawTasGhost(r, view, animB_, poseB(), ghostAlpha());  // the TAS keeps its look as B
        else if (comparing_) drawGhost(r, view, animB_, poseB(), kColorB, ghostAlpha());
        GhostTrack::Pose refPose;
        const bool refShown = !comparing_ && refGhostPose(refPose);
        const bool refTas = gapKind_ == replaygap::RefKind::Tas;
        if (refShown && refTas) drawTasGhost(r, view, animRef_, refPose, 0.85f);
        else if (refShown) drawGhost(r, view, animRef_, refPose, kColorB, 0.7f);
        refOffscreen_ = false;
        if (refShown && refTas) {
            const Vec2 sp = view.toScreen({refPose.feet.x, refPose.feet.y - 14.0f});
            refOffscreen_ = !layout_.worldArea.contains(sp);
        }
        drawGates(r);
        r.setView(WorldView{});
        hudRects_.clear();
        layoutLive();
        if (playback_) drawGameHud(view);
        if (comparing_) drawCompareProgress();
        drawLive();
        drawGateLabels(view);
        if (comparing_ || refShown) drawTags(view, refShown ? &refPose : nullptr);
        drawSeekOverlay();
        drawAdminNote();
        if (showInfo_) {
            if (comparing_) drawComparePanel(layout_.panel);
            else drawSplitsPanel(layout_.panel);
        }
        drawStrip(layout_.strip);
        handleShortcuts();
        ui.end();
    }

private:
    struct Layout {
        Rect panel, strip;
        Rect worldArea;  // what the panels leave of the screen
    };
    struct Checkpoint {
        Vec2 at;
        std::string label;
    };

    // ------------------------------------------------------------- loading
    bool loaded() const { return playback_ != nullptr || traceWorld_ != nullptr; }
    GameWorld& world() { return playback_ ? playback_->world() : *traceWorld_; }

    void fail(const std::string& m) {
        status_ = m;
        failed_ = true;
    }

    void say(const std::string& m) {
        msg_ = m;
        msgTimer_ = 4.0f;
    }

    /// A counter of the achievements (comparisons, alignments), saved and checked at once.
    void noteAchievement(const std::string& key) {
        g_.achievements.stats().add(key);
        g_.checkAchievements();
        g_.achievements.save();
    }

    /// The copy of this record kept in « Replays téléchargés », when there is a readable one.
    bool openDownloaded() {
        const std::string id = ReplayLibrary::makeId("record", recordId_);
        const DownloadedReplay* d = g_.replayLibrary.find(id);
        if (!d) return false;
        auto rep = g_.replayLibrary.load(id);
        if (!rep) return false;  // damaged: downloaded again below
        playerName_ = d->player;
        timeMs_ = d->timeMs;
        categoryText_ = d->category;
        a_.meta.source = "record";
        a_.meta.label = playerName_;
        a_.meta.playerId = d->playerId;
        a_.meta.recordId = recordId_;
        a_.meta.timeMs = timeMs_;
        a_.meta.validated = true;
        auto shared = std::make_shared<const ReplayData>(std::move(*rep));
        a_.replay = shared;
        a_.meta.levelId = d->levelId;
        a_.meta.levelHash = shared->levelHash;
        levelId_ = d->levelId;
        loadLevel();
        return true;
    }

    void downloadReplay(const std::string& levelId) {
        status_ = "Téléchargement du replay…";
        g_.online.getBinary("/api/v1/records/" + recordId_ + "/replay", [this, levelId, alive = std::weak_ptr<int>(life_)](const HttpResponse& resp) {
            if (!alive.lock()) return;
            if (resp.networkFailure()) return fail(std::string("Réseau : ") + netErrorLabel(resp.error));
            if (resp.status != 200) return fail(parseApiResponse(resp).message);
            // 2.75: an administrator watching through a privacy lock (the server decided, from the session's role);
            // 2.75.3: or a replay not verified yet, or refused (x-admin-replay-status), never counted as verified.
            const std::string lock = resp.header("x-admin-view"), status = resp.header("x-admin-replay-status");
            if (const std::string note = adminview::replayNote(lock, status); !note.empty()) adminNote_ = note;
            if (!status.empty()) {
                recordStatus_ = status;
                a_.meta.validated = adminview::countsAsVerified(status);
            }
            std::vector<uint8_t> bytes(resp.body.begin(), resp.body.end());
            std::string err;
            auto rep = ReplayData::decode(bytes, &err);
            if (!rep) return fail("Replay illisible : " + err);
            auto shared = std::make_shared<const ReplayData>(std::move(*rep));
            a_.replay = shared;
            a_.meta.levelId = levelId.empty() ? shared->levelId : levelId;
            a_.meta.levelHash = shared->levelHash;
            levelId_ = a_.meta.levelId;
            loadLevel();
        });
    }

    /// The installed level's exact versions (ranked fingerprint and gameplay one).
    bool installedVersion(const std::string& hash) const { return !hash.empty() && (hash == rankedHash_ || hash == gameplayHash_); }

    void loadLevel() {
        const LevelEntry* entry = g_.content.level(levelId_);
        if (!entry) return fail("Ce niveau n'est pas installé dans votre jeu.");
        if (auto text = FileUtil::readText(entry->path)) {
            Json j = Json::parse(*text, nullptr, false);
            if (!j.is_discarded())
                if (auto fp = fingerprintRankedLevel(g_.content, j)) rankedHash_ = fp->hash;
        }
        std::string err;
        auto level = g_.content.loadLevel(levelId_, &err);
        if (!level) return fail("Chargement du niveau impossible : " + err);
        level_ = *level;
        gameplayHash_ = fingerprintGameplay(g_.content, level_);
        // Alone, a replay is shown on its own version only (the run would not be the same);
        // in a comparison, another version is shown as its recorded trajectory.
        if (!b_.valid() && !installedVersion(a_.replay->levelHash))
            return fail("Ce replay a été enregistré sur une autre version du niveau ou du jeu.");
        if (!entry->assetRoot.empty()) {
            g_.app.resources().pushMount(entry->assetRoot);
            mounted_ = true;
        }
        for (const LevelObject& o : level_.objects)
            if (o.type == "checkpoint") checkpoints_.push_back({o.rect.bottomCenter(), o.propStr("label")});
        routeFuture_ = std::async(std::launch::async, [l = level_, &content = g_.content] { return buildProgressTrack(l, content); });
        startA();
        if (b_.valid()) setupComparison();
        refreshMeasures();
        playing_ = true;
        g_.ui.setFocus("replay/play");
        applyAutomation();
        if (startAt_ >= 0.0) {
            seekTo(std::min(startAt_, endTime()));
            startAt_ = -1.0;
        }
    }

    /// Automated screenshots (--replay-view): "at=<s>,offset=<ms>,align=<row>,cam=<n>,pause",
    /// 2.91: "ref=pb|sob" (the gap's reference), "unit=dist" (the curve in metres).
    void applyAutomation() {
        if (g_.onlineOpen.rfind("rview:", 0) != 0) return;
        std::string opts = g_.onlineOpen.substr(6);
        g_.onlineOpen.clear();
        size_t i = 0;
        while (i <= opts.size()) {
            size_t j = opts.find(',', i);
            if (j == std::string::npos) j = opts.size();
            std::string kv = opts.substr(i, j - i);
            i = j + 1;
            size_t eq = kv.find('=');
            std::string k = kv.substr(0, eq), v = eq == std::string::npos ? std::string{} : kv.substr(eq + 1);
            if (k == "pause") playing_ = false;
            else if (k == "at") seekTo(std::atof(v.c_str()));
            else if (k == "offset" && comparing_) cmp_.setOffsetMs(std::atoll(v.c_str()));
            else if (k == "align" && comparing_) alignOn(std::atoi(v.c_str()));
            else if (k == "cam" && comparing_) camMode_ = std::clamp(std::atoi(v.c_str()), 0, 2);
            else if (k == "speed") speedIdx_ = std::clamp(std::atoi(v.c_str()), 0, static_cast<int>(kSpeeds.size()) - 1);
            else if (k == "ref" && (v == "pb" || v == "sob")) selectGapRef(v == "pb" ? replaygap::RefKind::Record : replaygap::RefKind::SumOfBest);
            else if (k == "unit") gapDistance_ = v == "dist";
        }
    }

    /// (Re)starts A: re-simulated when it is an exact replay of this version, else its
    /// trajectory over a level living on its own.
    void startA() {
        ResourceManager& res = g_.app.resources();
        WorldServices ws{res, g_.app.audio(), g_.content, nullptr, g_.settings.graphics.screenShake, 0.7f};
        playback_.reset();
        traceWorld_.reset();
        bool canSimulate = a_.replay->exact() && installedVersion(a_.replay->levelHash);
        if (canSimulate && !forceTrace_) {
            playback_ = std::make_unique<ReplayPlayback>(ws, level_, *a_.replay);
            gen_ = playback_->generation();
        } else {
            traceWorld_ = std::make_unique<GameWorld>(ws, WorldMode::Play);
            traceWorld_->load(level_);
            traceWorld_->seedGameplay(levelSeed(level_.id));
            if (traceWorld_->cinematic().active() && traceWorld_->cinematic().skippable()) traceWorld_->cinematic().skip(*traceWorld_);
            traceWorld_->player().setHidden(true);
            traceWorld_->silent = true;
        }
        if (!rendererBuilt_) {
            renderer_.build(g_.app.renderer(), res, world());
            rendererBuilt_ = true;
        }
        hud_ = Hud{};
        trackA_.load(*a_.replay);
        if (routeReady_) trackA_.mapProgress(route_);
        splitsA_ = replaySplitsMs(*a_.replay, &splitsExactA_);
        animA_.setSet(world().player().anim.set());
        animB_.setSet(world().player().anim.set());
        animRef_.setSet(world().player().anim.set());
        curveDirty_ = true;
        exactTimers_.clear();  // A's measures on the simulation start again with it
        exactPasses_.clear();
        fedValid_ = false;
        t_ = 0.0f;
        extra_ = 0.0;
        acc_ = 0.0f;
        desyncNoticed_ = false;
        snapCam_ = true;
    }

    void setupComparison() {
        cmp_.set(CompareRun::fromReplay(*a_.replay, a_.meta), CompareRun::fromReplay(*b_.replay, b_.meta));
        if (routeReady_) cmp_.setRoute(route_);
        comparing_ = true;
        curveDirty_ = true;
        refreshMeasures();
        noteAchievement("replay_compares");  // 2.24: « Analyste »
        alignRow_ = 0;
        offsetText_.clear();
        if (!playback_ && camMode_ == 0) camMode_ = 0;
    }

    /// The choice screen chose A and B (from this viewer): a new A is loaded, B replaced.
    void applyChoice(ReplayChoice a, ReplayChoice b) {
        bool newA = a.replay != a_.replay;
        a_ = std::move(a);
        b_ = std::move(b);
        playerName_ = displayName(a_.meta);
        timeMs_ = a_.replay->durationMs;
        if (newA) {
            forceTrace_ = false;
            startA();
        }
        setupComparison();
        seekTo(0.0);
        playing_ = true;
    }

    /// A becomes B and B becomes A, at the same moment (the offset changes sign).
    void swapAB() {
        if (!comparing_) return;
        double at = cmp_.bTime(clock());
        int64_t off = -cmp_.offsetMs();
        std::swap(a_, b_);
        playerName_ = displayName(a_.meta);
        timeMs_ = a_.replay->durationMs;
        forceTrace_ = false;
        startA();
        setupComparison();
        cmp_.setOffsetMs(off);
        seekTo(std::max(0.0, at));
    }

    // --------------------------------------------------------------- gap (2.91)
    template <class T>
    static bool jobDone(std::future<T>& f) {
        return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    /// Every frame: the runs along the route and the references read off the main thread, the
    /// curve made again only when what it depends on changed (reference, B's offset).
    void pollGap() {
        if (routeReady_ && a_.replay && runAOf_ != a_.replay) {
            runAOf_ = a_.replay;
            runAReady_ = false;
            runAJob_ = std::async(std::launch::async, [rep = a_.replay, route = route_] { return RouteRun::fromReplay(*rep, route); });
            // The viewer's references are matched with that run.
            for (GapRefSlot& s : gapRefs_) {
                if (s.job.valid()) s.job.wait();
                s = GapRefSlot{};
            }
            curveDirty_ = true;
        }
        if (routeReady_ && comparing_ && b_.replay && runBOf_ != b_.replay) {
            runBOf_ = b_.replay;
            runBReady_ = false;
            runBJob_ = std::async(std::launch::async, [rep = b_.replay, route = route_] { return RouteRun::fromReplay(*rep, route); });
            curveDirty_ = true;
        }
        if (jobDone(runAJob_)) {
            runA_ = runAJob_.get();
            runAReady_ = true;
            curveDirty_ = true;
        }
        if (jobDone(runBJob_)) {
            runB_ = runBJob_.get();
            runBReady_ = true;
            curveDirty_ = true;
        }
        for (size_t i = 0; i < gapRefs_.size(); ++i) {
            GapRefSlot& s = gapRefs_[i];
            if (!jobDone(s.job)) continue;
            s.res = s.job.get();
            s.ready = true;
            curveDirty_ = true;
            // The record is the very run watched: the Sum of Best tells more when there is one
            // (unless a reference was chosen).
            if (i == static_cast<size_t>(replaygap::RefKind::Record) && s.res.sameRun && !gapKindChosen_ && gapKind_ == replaygap::RefKind::Record &&
                routeReady_) {
                startGapRef(replaygap::RefKind::SumOfBest);
                const GapRefSlot& sob = gapRefs_[static_cast<size_t>(replaygap::RefKind::SumOfBest)];
                if (sob.ready && sob.res.ok) gapKind_ = replaygap::RefKind::SumOfBest;
            }
            if (i == static_cast<size_t>(replaygap::RefKind::SumOfBest) && s.res.ok && !gapKindChosen_ && gapKind_ == replaygap::RefKind::Record) {
                const GapRefSlot& rec = gapRefs_[static_cast<size_t>(replaygap::RefKind::Record)];
                if (rec.ready && rec.res.sameRun) gapKind_ = replaygap::RefKind::SumOfBest;
            }
        }
        ensureGapRef();
        if (comparing_ && cmp_.ready() && cmp_.offsetMs() != curveOffset_) curveDirty_ = true;
        if (curveDirty_) rebuildCurve();
    }

    /// The viewer's reference chosen, read once (then kept); the record too while measures are
    /// shown (« Le PB sur A → B »).
    void ensureGapRef() {
        if (comparing_ || !routeReady_ || !a_.replay || failed_) return;
        startGapRef(gapKind_);
        if (measuring()) startGapRef(replaygap::RefKind::Record);
    }

    void startGapRef(replaygap::RefKind kind) {
        GapRefSlot& slot = gapRefs_[static_cast<size_t>(kind)];
        if (slot.started) return;
        replaygap::RefRequest req;
        switch (kind) {
            case replaygap::RefKind::Record: req = replaygap::prepareRecord(g_, levelId_, a_.replay->levelHash); break;
            case replaygap::RefKind::SumOfBest: req = replaygap::prepareSumOfBest(g_, levelId_, a_.replay->levelHash); break;
            case replaygap::RefKind::Tas: req = replaygap::prepareTas(g_, levelId_, a_.replay->levelHash, tasCategory()); break;
            case replaygap::RefKind::Other:
                if (!otherRef_) return;
                req.kind = replaygap::RefKind::Other;
                req.memory = otherRef_;
                req.meta = otherMeta_;
                break;
        }
        req.route = route_;
        req.run = a_.replay;
        req.checkpoints = routeCheckpoints_;
        req.bounds = level_.bounds;
        slot.started = true;
        slot.job = std::async(std::launch::async, [r = std::move(req)]() mutable { return replaygap::loadReference(std::move(r)); });
    }

    /// The category of the replay watched (« ANY_PERCENT » for a run of the campaign without one).
    std::string tasCategory() const {
        RunCategory c = RunCategory::AnyPercent;
        return categoryFromId(a_.meta.category, c) ? a_.meta.category : std::string(categoryId(RunCategory::AnyPercent));
    }
    /// The machine's run of this version in the replay's category: the viewer's « TAS » reference
    /// (nullptr: the level has none for this version, or the replay watched is the TAS itself).
    const tas::TasRun* viewerTas() const {
        if (comparing_ || watchingTas_ || !a_.replay || levelId_.empty()) return nullptr;
        RunCategory c = RunCategory::AnyPercent;
        categoryFromId(tasCategory(), c);
        return tas::catalog().tasFor(levelId_, c, a_.replay->levelHash);
    }
    /// R: Record (PB) → Somme des meilleurs → TAS (when there is one) → Record (PB).
    void cycleGapRef() {
        using K = replaygap::RefKind;
        K next = gapKind_ == K::Record ? K::SumOfBest : gapKind_ == K::SumOfBest ? (viewerTas() ? K::Tas : K::Record) : K::Record;
        selectGapRef(next);
        g_.uiSound("ui_move", 0.6f);
    }
    /// « Comparer avec le TAS »: the existing comparison, A this replay, B the TAS of its category.
    void compareWithTas() {
        const tas::TasRun* t = viewerTas();
        if (!t) return;
        auto rep = loadTasReplay(*t, tas::catalog().root());
        if (!rep) return;
        ReplayChoice b;
        b.replay = std::make_shared<const ReplayData>(std::move(*rep));
        b.meta = t->ghost().meta;
        ReplayChoice a = a_;
        applyChoice(std::move(a), std::move(b));
    }

    const replaygap::LoadedRef* viewerRef() const {
        const GapRefSlot& s = gapRefs_[static_cast<size_t>(gapKind_)];
        return s.ready ? &s.res : nullptr;
    }

    void rebuildCurve() {
        curveDirty_ = false;
        curveReady_ = false;
        curve_ = {};
        if (!runAReady_ || !a_.replay) return;
        if (comparing_) {
            if (!runBReady_ || !cmp_.ready()) return;
            curveRef_ = gapReferenceOf(cmp_, runB_);
            curveRun_ = gapRunOf(cmp_.a(), &runA_);
            curveRun_.finished = a_.replay->countEvents(ReplayEventType::Finish) > 0;
            curveOffset_ = cmp_.offsetMs();
            curve_ = gapCurve(curveRun_, curveRef_, endTime());
        } else {
            const replaygap::LoadedRef* ref = viewerRef();
            if (!ref || !ref->ok) return;
            curveRef_ = ref->ref;
            curveRun_.run = &runA_;
            curveRun_.splitsMs = splitsA_;
            curveRun_.durationMs = a_.replay->durationMs;
            curveRun_.finished = a_.replay->countEvents(ReplayEventType::Finish) > 0;
            curve_ = gapCurve(curveRun_, curveRef_, aEnd());
        }
        curveReady_ = true;
    }

    /// The gap at `t` (the live counter, the cursor of the curve).
    GapSample gapNow(double t) const { return curveReady_ ? gapAt(curveRun_, curveRef_, t) : GapSample{t}; }

    void selectGapRef(replaygap::RefKind k) {
        gapKind_ = k;
        gapKindChosen_ = true;
        curveDirty_ = true;
    }

    /// « Autre replay… »: the choice of the comparison, the run watched being A; B becomes the
    /// reference (another A chosen opens the full comparison).
    void openOtherReference() {
        ReplayCompareRequest req;
        req.levelId = level_.id;
        req.current = a_.replay;
        req.currentMeta = a_.meta;
        if (req.currentMeta.label.empty()) req.currentMeta.label = playerName_;
        auto alive = std::weak_ptr<int>(life_);
        req.onChosen = [this, alive](ReplayChoice a, ReplayChoice b) {
            if (!alive.lock()) return;
            if (a.replay == a_.replay && b.valid()) setOtherReference(std::move(b));
            else if (b.replay == a_.replay && a.valid()) setOtherReference(std::move(a));
            else applyChoice(std::move(a), std::move(b));
        };
        playing_ = false;
        g_.app.pushScene(makeReplayCompareSetup(g_, std::move(req)));
    }

    void setOtherReference(ReplayChoice c) {
        otherRef_ = c.replay;
        otherMeta_ = c.meta;
        otherMeta_.label = displayName(c.meta);
        GapRefSlot& slot = gapRefs_[static_cast<size_t>(replaygap::RefKind::Other)];
        if (slot.job.valid()) slot.job.wait();
        slot = GapRefSlot{};
        selectGapRef(replaygap::RefKind::Other);
    }

    // ------------------------------------------------- measure points (2.91)
    // The same gates A / B as in training (MeasureStore, per level version), timed in every
    // replay: on the recorded trajectories (about 1/30 s), and on the simulation itself (1/120 s)
    // while an exact replay plays through them. Read only: a time measured here never goes to the
    // store nor to the statistics; F8 (Vue + ↑) only places a gate, as in training.
    bool measuring() const { return g_.measures.inReplays && !measurePairs_.empty(); }
    const MeasurePair* measurePair() const {
        return measureSel_ >= 0 && measureSel_ < static_cast<int>(measurePairs_.size()) ? &measurePairs_[static_cast<size_t>(measureSel_)] : nullptr;
    }

    void refreshMeasures() {
        measureKey_ = a_.replay && !levelId_.empty() ? GhostStore::levelKey(levelId_, a_.replay->levelHash) : std::string{};
        measurePairs_.clear();
        if (g_.measures.inReplays && !measureKey_.empty()) measurePairs_ = g_.measures.list(measureKey_);
        measureSel_ = std::clamp(measureSel_, 0, std::max(0, static_cast<int>(measurePairs_.size()) - 1));
        measuredValid_ = false;
        exactTimers_.clear();
    }

    /// The passes of each pair in A, B (comparison) and the record (viewer), on their recorded
    /// trajectories: made again only when one of them changes.
    void updateMeasurePasses() {
        if (!measuring()) return;
        std::shared_ptr<const ReplayData> pb;
        if (!comparing_) {
            const GapRefSlot& s = gapRefs_[static_cast<size_t>(replaygap::RefKind::Record)];
            if (s.ready && s.res.ok) pb = s.res.replay;
        }
        std::shared_ptr<const ReplayData> b = comparing_ ? b_.replay : nullptr;
        if (measuredValid_ && measuredA_ == a_.replay && measuredB_ == b && measuredPb_ == pb) return;
        measuredA_ = a_.replay;
        measuredB_ = b;
        measuredPb_ = pb;
        measuredValid_ = true;
        measurePasses_.clear();
        for (const MeasurePair& p : measurePairs_) {
            if (!p.b) continue;
            PairPasses pp;
            if (a_.replay) pp.a = measurePasses(*a_.replay, p.a, *p.b);
            if (b) pp.b = measurePasses(*b, p.a, *p.b);
            if (pb) pp.pb = measurePasses(*pb, p.a, *p.b);
            measurePasses_[p.id] = std::move(pp);
        }
    }

    /// After each step of an exact replay: A's passes timed on the simulation (1/120 s). A jump
    /// (a seek, a step back) starts the chronometers again.
    void feedMeasures() {
        if (!playback_ || !measuring()) return;
        const uint32_t tick = playback_->tick();
        const bool continuous = fedValid_ && tick == fedTick_ + 1 && playback_->generation() == fedGen_;
        fedValid_ = true;
        fedTick_ = tick;
        fedGen_ = playback_->generation();
        const double t = playback_->runTime();
        const Vec2 feet = world().player().feet();
        for (const MeasurePair& p : measurePairs_) {
            if (!p.b) continue;
            auto it = exactTimers_.find(p.id);
            if (it == exactTimers_.end()) it = exactTimers_.emplace(p.id, MeasurePassTimer(p.a, *p.b)).first;
            if (!continuous) it->second.reset();
            MeasurePass pass;
            if (!it->second.feed(t, feet, pass)) continue;
            std::vector<MeasurePass>& v = exactPasses_[p.id];
            v.erase(std::remove_if(v.begin(), v.end(), [&](const MeasurePass& q) { return std::abs(q.a - pass.a) < 0.1; }), v.end());
            v.push_back(pass);
        }
    }

    /// A's pass timed on the simulation that matches `p` (its trajectory's), nullptr if none.
    const MeasurePass* exactOf(int id, const MeasurePass& p) const {
        auto it = exactPasses_.find(id);
        if (it == exactPasses_.end()) return nullptr;
        for (const MeasurePass& q : it->second)
            if (std::abs(q.a - p.a) < 0.1) return &q;
        return nullptr;
    }

    struct PassView {
        bool any = false, running = false, exact = false;
        double time = 0.0;
    };
    /// A's pass of the pair at A's time `t`: the last one begun by then, finished or going on.
    PassView aPassAt(int id, double t) const {
        PassView v;
        auto it = measurePasses_.find(id);
        if (it == measurePasses_.end()) return v;
        const MeasurePass* cur = nullptr;
        for (const MeasurePass& p : it->second.a)
            if (p.a <= t + 1e-6) cur = &p;
        if (!cur) return v;
        v.any = true;
        const MeasurePass* ex = exactOf(id, *cur);
        if (cur->done() && cur->b <= t + 1e-6) {
            v.time = ex ? ex->time() : cur->time();
            v.exact = ex != nullptr;
            return v;
        }
        v.running = true;
        double since = cur->a;
        if (auto e = exactTimers_.find(id); playback_ && e != exactTimers_.end() && e->second.openSince() >= 0.0 && std::abs(e->second.openSince() - cur->a) < 0.1) {
            since = e->second.openSince();
            v.exact = true;
        }
        v.time = std::max(0.0, t - since);
        return v;
    }
    /// The first finished pass of a list (A's exact when it was timed so).
    PassView firstPass(int id, const std::vector<MeasurePass>& list, bool isA) const {
        PassView v;
        for (const MeasurePass& p : list) {
            if (!p.done()) continue;
            v.any = true;
            const MeasurePass* ex = isA ? exactOf(id, p) : nullptr;
            v.time = ex ? ex->time() : p.time();
            v.exact = ex != nullptr;
            return v;
        }
        return v;
    }

    /// A's windows between A and B of the pair shown (shaded on the curve).
    std::vector<std::pair<double, double>> measureWindows() const {
        std::vector<std::pair<double, double>> out;
        const MeasurePair* mp = measurePair();
        if (!measuring() || !mp) return out;
        auto it = measurePasses_.find(mp->id);
        if (it == measurePasses_.end()) return out;
        for (const MeasurePass& p : it->second.a) {
            const MeasurePass* ex = exactOf(mp->id, p);
            const MeasurePass& q = ex ? *ex : p;
            out.push_back({q.a, q.done() ? q.b : endTime()});
        }
        return out;
    }

    /// F8 / Vue + ↑: a gate at A's feet (A, then B of the same pair), as in training. Only the
    /// gate is kept: no time measured here is ever written.
    void placeMeasure() {
        if (!loaded() || failed_) return;
        if (!g_.measures.inReplays) return say("Mesures dans les replays désactivées : « Mesurer aussi dans les replays ».");
        if (measureKey_.empty() || g_.measures.readOnly()) return say("Point de mesure impossible ici.");
        const int id = g_.measures.place(measureKey_, feetA(), FileUtil::nowString());
        if (id <= 0) return say("Trop de points de mesure dans ce niveau : retirez-en un à l'entraînement.");
        g_.measures.save();
        refreshMeasures();
        for (size_t i = 0; i < measurePairs_.size(); ++i)
            if (measurePairs_[i].id == id) measureSel_ = static_cast<int>(i);
        const MeasurePair* p = measurePair();
        if (p && p->complete()) say("B" + std::to_string(id) + " posé : mesure A" + std::to_string(id) + " → B" + std::to_string(id) + " prête");
        else say("A" + std::to_string(id) + " posé : posez B plus loin (F8)");
    }

    // ------------------------------------------------------------- clock
    double aEnd() const { return static_cast<double>(a_.replay->durationMs) / 1000.0; }
    double endTime() const { return comparing_ ? std::max(cmp_.endTime(), aEnd()) : aEnd(); }
    /// A's run time (its chronometer), continued after its finish while B still runs.
    double clock() const { return playback_ ? static_cast<double>(playback_->runTime()) + extra_ : t_; }
    int64_t clockMs() const { return std::llround(clock() * 1000.0); }

    void seekTo(double s) {
        s = std::clamp(s, 0.0, endTime());
        if (playback_) {
            extra_ = 0.0;
            if (s > aEnd() + 0.0005) {
                seekAfterEnd_ = s;
                playback_->seekTick(playback_->totalTicks());
            } else {
                seekAfterEnd_ = -1.0;
                playback_->seekTime(s);
            }
            desyncNoticed_ = false;
            if (playback_->advance(400)) arrived();
        } else {
            t_ = s;
        }
        snapCam_ = true;
    }

    void arrived() {
        if (seekAfterEnd_ >= 0.0 && playback_->finished()) extra_ = std::max(0.0, seekAfterEnd_ - static_cast<double>(playback_->runTime()));
        seekAfterEnd_ = -1.0;
        world().update(0.0f);
        snapCam_ = true;
    }

    void seekRelative(double seconds) { seekTo(clock() + seconds); }

    /// One step (1/120 s) forwards or backwards.
    void stepFrame(int dir) {
        playing_ = false;
        if (playback_) {
            if (playback_->seeking()) return;
            if (dir > 0) {
                if (!playback_->finished() && !playback_->desynced()) {
                    playback_->step();
                    feedMeasures();
                    world().update(kSimDt);
                    hud_.update(world(), kSimDt);
                } else if (clock() + 1e-6 < endTime()) {
                    extra_ = std::min(extra_ + kSimDt, endTime() - static_cast<double>(playback_->runTime()));
                }
            } else if (extra_ > 0.0) {
                extra_ = std::max(0.0, extra_ - kSimDt);
            } else if (playback_->tick() > 0) {
                playback_->seekTick(playback_->tick() - 1);
                if (playback_->advance(kSeekBudget)) arrived();
            }
        } else {
            t_ = std::clamp(t_ + dir * static_cast<double>(kSimDt), 0.0, endTime());
        }
    }

    /// Times of A's checkpoints and of its exit (jumps « Point »).
    std::vector<double> pointTimes() const {
        std::vector<double> out{0.0};
        for (int64_t ms : splitsA_) out.push_back(static_cast<double>(ms) / 1000.0);
        out.push_back(aEnd());
        if (comparing_ && endTime() > aEnd() + 0.001) out.push_back(endTime());
        return out;
    }

    void jumpPoint(int dir) {
        std::vector<double> pts = pointTimes();
        double now = clock();
        if (dir > 0) {
            for (double p : pts)
                if (p > now + 0.001) return seekTo(p);
        } else {
            // A little before the current point goes to the one before (pressed twice).
            for (auto it = pts.rbegin(); it != pts.rend(); ++it)
                if (*it < now - 0.25) return seekTo(*it);
            seekTo(0.0);
        }
    }

    void togglePlay() {
        if (!playing_ && clock() + 1e-4 >= endTime()) seekTo(0.0);
        playing_ = !playing_;
    }

    // ------------------------------------------------------------ updates
    void updateExact(float dt) {
        float speed = kSpeeds[static_cast<size_t>(speedIdx_)];
        bool advancing = false;
        if (playback_->seeking()) {
            if (playback_->advance(kSeekBudget)) arrived();
        } else if (playing_) {
            advancing = true;
            acc_ += dt * speed;
            int guard = 0;
            while (acc_ >= kSimDt && guard++ < 64) {
                acc_ -= kSimDt;
                if (!advanceOne()) {
                    acc_ = 0.0f;
                    playing_ = false;
                    break;
                }
            }
        }
        // Points de reprise kept behind the step shown (quick steps back), a little per frame.
        if (!playback_->seeking()) playback_->maintain(playing_ ? 160 : 240);
        if (playback_->generation() != gen_) {
            gen_ = playback_->generation();
            hud_ = Hud{};  // a new world: the HUD starts again from it
        }
        float wdt = advancing ? dt * speed : 0.0f;
        world().update(wdt);
        hud_.update(world(), wdt);
        if (playback_->desynced() && !desyncNoticed_) {
            desyncNoticed_ = true;
            playing_ = false;
        }
    }

    bool advanceOne() {
        if (playback_->desynced()) return false;
        if (!playback_->finished()) {
            const bool ok = playback_->step();
            if (ok) feedMeasures();
            return ok;
        }
        // A has finished: the comparison clock goes on until B has too.
        if (clock() + 1e-6 < endTime()) {
            extra_ = std::min(extra_ + kSimDt, endTime() - static_cast<double>(playback_->runTime()));
            return true;
        }
        return false;
    }

    void updateTrace(float dt) {
        float speed = kSpeeds[static_cast<size_t>(speedIdx_)];
        if (playing_) {
            t_ += static_cast<double>(dt * speed);
            if (t_ >= endTime()) {
                t_ = endTime();
                playing_ = false;
            }
            // The level around keeps living (enemies, decor); the world's own player is hidden.
            acc_ += dt;
            while (acc_ >= kSimDt) {
                traceWorld_->fixedUpdate(PlayerInput{}, kSimDt);
                acc_ -= kSimDt;
            }
        }
        traceWorld_->update(dt);
    }

    GhostTrack::Pose poseA() const { return trackA_.poseAt(static_cast<float>(std::min(clock(), aEnd()))); }
    GhostTrack::Pose poseB() const {
        double tb = std::clamp(cmp_.bTime(clock()), 0.0, static_cast<double>(cmp_.b().duration()));
        return cmp_.b().track.poseAt(static_cast<float>(tb));
    }
    Vec2 feetA() { return playback_ ? world().player().feet() : poseA().feet; }
    /// B waits at its start before its time, and stays at the exit once finished (dimmer).
    float ghostAlpha() const {
        double tb = cmp_.bTime(clock());
        return tb < 0.0 || tb > cmp_.b().duration() ? 0.55f : 0.9f;
    }

    void updateGhosts(float dt) {
        float speed = playing_ ? kSpeeds[static_cast<size_t>(speedIdx_)] : 0.0f;
        auto animate = [&](Animator& a, const GhostTrack::Pose& p) {
            if (!a.set()) return;
            ReplayFrame f;
            f.state = p.state;
            f.flags = p.flags;
            a.play(ghostClipFor(f, p.vel));
            a.update(dt * speed);
        };
        if (!playback_) animate(animA_, poseA());
        if (comparing_) animate(animB_, poseB());
        GhostTrack::Pose rp;
        if (!comparing_ && refGhostPose(rp)) animate(animRef_, rp);
    }

    /// The viewer's reference as a ghost (the record, the other replay, each best segment of the
    /// Sum of Best from its replay), on the run's clock. False when there is none to show.
    bool refGhostPose(GhostTrack::Pose& out) const {
        const replaygap::LoadedRef* ref = viewerRef();
        if (!ref || !ref->ok || ref->sameRun || ref->ghost.empty()) return false;
        return ref->poseAt(clock(), out);
    }

    void updateCamera(float dt) {
        if (layout_.worldArea.w <= 0.0f) return;
        Renderer& r = g_.app.renderer();
        float normal = static_cast<float>(r.height()) / Camera::kViewHeight * std::max(0.5f, level_.cameraZoom) * fitScale(r);
        Vec2 a = feetA() - Vec2{0, 12};
        Vec2 target = a;
        float scale = normal;
        if (comparing_) {
            Vec2 b = poseB().feet - Vec2{0, 12};
            if (camMode_ == 1) target = b;
            if (camMode_ == 2) {
                target = (a + b) * 0.5f;
                // Both inside the free part of the screen, with room around them.
                float room = g_.ui.s(140);
                float fx = (layout_.worldArea.w - 2 * room) / std::max(1.0f, std::abs(a.x - b.x));
                float fy = (layout_.worldArea.h - 2 * room) / std::max(1.0f, std::abs(a.y - b.y));
                scale = std::clamp(std::min({normal, fx, fy}), normal * 0.3f, normal);
            }
        }
        if (snapCam_) {
            camPos_ = target;
            camScale_ = scale;
        } else {
            camPos_ = camPos_ + (target - camPos_) * std::min(1.0f, dt * 6.0f);
            camScale_ += (scale - camScale_) * std::min(1.0f, dt * 4.0f);
        }
        snapCam_ = false;
    }

    WorldView makeView(const Renderer& r) {
        // Following A in an exact replay: the game's own camera (zones, boss arenas).
        if (playback_ && (!comparing_ || camMode_ == 0)) {
            float alpha = playing_ ? std::clamp(acc_ / kSimDt, 0.0f, 1.0f) : 1.0f;
            WorldView g = world().camera().view(r.width(), r.height(), alpha);
            // 2.62, a team replay: one player followed (at the game's scale), or the team (its camera).
            if (teamFollow_ >= 0 && teamFollow_ < world().teamSize()) {
                const Player& p = world().playerAt(teamFollow_);
                const Vec2 c = lerpv(p.prevBox.center(), p.box.center(), alpha);
                g.topLeft = c - Vec2{static_cast<float>(r.width()), static_cast<float>(r.height())} * (0.5f / g.scale);
            }
            const float k = fitScale(r);
            if (k >= 0.999f) return g;
            // Recadré : the centre of what the player saw at the centre of the free part,
            // smaller so that all of it fits there.
            Vec2 centre = g.topLeft + Vec2{static_cast<float>(r.width()), static_cast<float>(r.height())} * (0.5f / g.scale);
            g.scale *= k;
            g.topLeft = centre - layout_.worldArea.center() / g.scale;
            return g;
        }
        WorldView v;
        v.screenW = r.width();
        v.screenH = r.height();
        v.scale = camScale_ > 0.0f ? camScale_ : static_cast<float>(r.height()) / Camera::kViewHeight;
        // The point followed at the centre of the part of the screen the panels leave free.
        Vec2 c = layout_.worldArea.center();
        Vec2 tl = camPos_ - c / v.scale;
        float vw = v.screenW / v.scale, vh = v.screenH / v.scale;
        const Rect& b = level_.bounds;
        if (vw < b.w) tl.x = std::clamp(tl.x, b.x, b.right() - vw);
        if (vh < b.h) tl.y = std::clamp(tl.y, b.y, b.bottom() - vh);
        v.topLeft = tl;
        return v;
    }

    // ------------------------------------------------------------ drawing
    void computeLayout() {
        UIContext& ui = g_.ui;
        float W = ui.screenW(), H = ui.screenH();
        float stripH = stripHeight();
        float m = ui.s(16);
        float pw = comparing_ ? std::min(W * 0.36f, ui.s(560)) : std::min(W * 0.3f, ui.s(440));
        layout_.strip = {0, H - stripH, W, stripH};
        layout_.panel = {W - m - pw, m, pw, H - stripH - 2 * m};
        layout_.worldArea = {0, 0, layout_.panel.x - m, H - stripH};
        if (!showInfo_) {  // « Masquer les infos »: the run on the whole width
            layout_.panel = {};
            layout_.worldArea = {0, 0, W, H - stripH};
        }
    }

    /// The run's field of view fitted into the part of the screen the panels leave free:
    /// everything the player saw stays visible, never under the panel or the controls.
    float fitScale(const Renderer& r) const {
        const Rect& f = layout_.worldArea;
        if (f.w <= 0.0f || f.h <= 0.0f || r.width() <= 0 || r.height() <= 0) return 1.0f;
        return std::clamp(std::min(f.w / static_cast<float>(r.width()), f.h / static_cast<float>(r.height())), 0.3f, 1.0f);
    }

    float px(float size) const { return g_.ui.s(fz(g_.ui, size)); }
    float lineH(float size) const { return px(size) * 1.45f; }
    float buttonH() const { return std::max(g_.ui.s(42), px(18) * 2.1f); }
    /// The gap's header row (its reference and unit buttons) and its curve (2.91).
    float gapHeaderH() const { return std::max(g_.ui.s(30), px(15) * 1.9f); }
    float chartH() const { return std::max(g_.ui.s(104), lineH(13) * 3.2f + lineH(12) + g_.ui.s(26)); }
    float stripHeight() const {
        const UIContext& ui = g_.ui;
        return ui.s(10) + lineH(22) + ui.s(2) + gapHeaderH() + ui.s(6) + chartH() + replaygap::axisHeight(ui) + ui.s(8) + buttonH() + ui.s(10) +
               std::max(ui.s(26), px(13) * 1.7f) + ui.s(12);
    }

    void drawPaths(Renderer& r, const WorldView& view) {
        Rect vis = view.visibleWorld().expanded(200.0f);
        drawPath(r, *a_.replay, std::min(clock(), aEnd()), kColorA, vis);
        if (comparing_) drawPath(r, *b_.replay, std::clamp(cmp_.bTime(clock()), 0.0, static_cast<double>(cmp_.b().duration())), kColorB, vis);
        // Checkpoints passed: A's and B's splits where they were.
        for (int64_t ms : splitsA_) {
            Vec2 p = trackA_.poseAt(static_cast<float>(ms) / 1000.0f).feet;
            r.drawWorldCircle(p + Vec2{0, -12}, 7.0f, kColorA.withAlpha(200), 2.0f);
        }
        if (comparing_)
            for (int64_t ms : cmp_.b().splitsMs) {
                Vec2 p = cmp_.b().track.poseAt(static_cast<float>(ms) / 1000.0f).feet;
                r.drawWorldCircle(p + Vec2{0, -12}, 10.0f, kColorB.withAlpha(200), 2.0f);
            }
    }

    /// The whole trajectory faint, the last two seconds bright (`now`: the run's own time).
    static void drawPath(Renderer& r, const ReplayData& rep, double now, Color c, const Rect& vis) {
        const auto& fr = rep.frames;
        if (fr.size() < 2) return;
        int hz = std::max<int>(1, rep.sampleHz);
        int n = static_cast<int>(fr.size());
        int step = std::max(1, n / 1500);
        for (int i = step; i < n; i += step) {
            const ReplayFrame& a = fr[static_cast<size_t>(i - step)];
            const ReplayFrame& b = fr[static_cast<size_t>(i)];
            if (std::hypot(b.x - a.x, b.y - a.y) > 120.0f * static_cast<float>(step)) continue;
            if (!vis.contains({a.x, a.y}) && !vis.contains({b.x, b.y})) continue;
            r.drawWorldLine({a.x, a.y - 11}, {b.x, b.y - 11}, c.withAlpha(60), 2.0f);
        }
        int cur = std::clamp(static_cast<int>(now * hz), 0, n - 1);
        for (int i = std::max(1, cur - hz * 2); i <= cur; ++i) {
            const ReplayFrame& a = fr[static_cast<size_t>(i - 1)];
            const ReplayFrame& b = fr[static_cast<size_t>(i)];
            if (std::hypot(b.x - a.x, b.y - a.y) > 120.0f) continue;
            float k = 1.0f - static_cast<float>(cur - i) / (hz * 2.0f);
            r.drawWorldLine({a.x, a.y - 11}, {b.x, b.y - 11}, c.withAlpha(static_cast<uint8_t>(190 * k)), 2.5f, BlendMode::Additive);
        }
        for (const auto& e : rep.events) {
            if (e.frame > static_cast<uint32_t>(cur) || e.frame >= fr.size()) continue;
            const ReplayFrame& f = fr[e.frame];
            Vec2 p{f.x, f.y - 12.0f};
            if (!vis.contains(p)) continue;
            if (e.type == ReplayEventType::Death) {
                r.drawWorldLine(p + Vec2{-6, -6}, p + Vec2{6, 6}, Color{255, 77, 109, 230}, 2.5f);
                r.drawWorldLine(p + Vec2{-6, 6}, p + Vec2{6, -6}, Color{255, 77, 109, 230}, 2.5f);
            }
        }
    }

    /// The machine's ghost (the TAS as the reference, or as B): its own look (tasui::drawGhost).
    void drawTasGhost(Renderer& r, const WorldView& view, Animator& anim, const GhostTrack::Pose& p, float alpha) {
        if (!p.visible || static_cast<PlayerState>(p.state) == PlayerState::Hidden || !anim.set() || !anim.set()->texture) return;
        const bool flip = (p.flags & ReplayFrame::FacingLeft) != 0;
        tasui::drawGhost(r, *anim.set()->texture, anim.sourceRect(), anim.destRect(p.feet, flip), flip, alpha, view.scale, g_.ui.time());
    }

    void drawGhost(Renderer& r, const WorldView& view, Animator& anim, const GhostTrack::Pose& p, Color c, float alpha) {
        if (!p.visible || static_cast<PlayerState>(p.state) == PlayerState::Hidden || !anim.set() || !anim.set()->texture) return;
        bool flip = (p.flags & ReplayFrame::FacingLeft) != 0;
        Rect src = anim.sourceRect();
        Rect dst = anim.destRect(p.feet, flip);
        r.drawWorldGlow({p.feet.x, p.feet.y - 14.0f}, 30.0f, c.withAlpha(static_cast<uint8_t>(80 * alpha)));
        // A luminous outline (the silhouette drawn around in additive colour), then the body.
        float o = 1.6f / std::max(0.25f, view.scale);
        const Vec2 offs[4] = {{-o, 0}, {o, 0}, {0, -o}, {0, o}};
        for (Vec2 d : offs) {
            Rect od = dst;
            od.x += d.x;
            od.y += d.y;
            r.drawWorldTexture(*anim.set()->texture, &src, od, c.withAlpha(static_cast<uint8_t>(130 * alpha)), BlendMode::Additive, flip);
        }
        Color body = Color::lerp(Color{255, 255, 255, 255}, c, 0.5f);
        r.drawWorldTexture(*anim.set()->texture, &src, dst, body.withAlpha(static_cast<uint8_t>(235 * alpha)), BlendMode::Alpha, flip);
        if (p.flags & ReplayFrame::Shield) r.drawWorldCircle({p.feet.x, p.feet.y - 14.0f}, 24.0f, c.withAlpha(static_cast<uint8_t>(200 * alpha)), 2.0f);
    }

    /// The game's HUD for A (exact replays), placed clear of the panels: no timer (the exact
    /// chronometer is in the panels), no messages, the boss gauge above the controls.
    void drawGameHud(const WorldView& view) {
        GameWorld& w = world();
        // Not during the intro / outro, nor once finished (the game's fade to black would
        // hide the level while B still runs).
        if (w.cinematic().active() || w.state() != WorldState::Playing) return;
        UIContext& ui = g_.ui;
        GameplaySettings hs = g_.settings.gameplay;
        hs.focusMode = false;
        hs.showTimer = false;
        hs.progressBar = 0;
        hs.showMessages = false;
        hs.focusMessages = false;
        hs.showInputHints = false;
        hs.hud[HudElement::Splits].visible = false;
        hs.hud[HudElement::Enemies].visible = false;  // 2.52: placed for the game, not for this layout
        hs.hud[HudElement::Ambush].visible = false;   // 2.62.2: the same
        const float W = ui.screenW(), H = ui.screenH();
        const Rect& free = layout_.worldArea;
        const float m = ui.s(10);
        // Vitals and powers at their default places (top left).
        for (HudElement e : {HudElement::Vitals, HudElement::Powers}) hs.hud[e].custom = false;
        Rect vitals = hudVisible(hs, HudElement::Vitals) ? hud_.area(ui, w, HudElement::Vitals, hud_.place(ui, w, hs, HudElement::Vitals)) : Rect{};
        Rect powers = hudVisible(hs, HudElement::Powers) ? hud_.area(ui, w, HudElement::Powers, hud_.place(ui, w, hs, HudElement::Powers)) : Rect{};
        // Collectibles: centred on the free part, right of the vitals if needed; under the live
        // gap when they would meet (2.91), clear of the powers there.
        {
            HudPlace& hp = hs.hud[HudElement::Collectibles];
            float k = std::clamp(hp.scale, HudLayout::kMinScale, HudLayout::kMaxScale);
            float half = ui.s(153) * k;
            float cx = std::max(free.center().x, vitals.w > 0.0f ? vitals.right() + half + ui.s(16) : 0.0f);
            float cy = ui.s(34);
            auto box = [&] { return Rect{cx - half, cy - ui.s(8) * k, 2.0f * half, ui.s(50) * k}; };
            if (liveRect_.w > 0.0f && box().intersects(liveRect_.expanded(ui.s(6)))) {
                cy = liveRect_.bottom() + ui.s(10) + ui.s(8) * k;
                for (const Rect& a : {vitals, powers})
                    if (a.w > 0.0f && box().intersects(a.expanded(ui.s(8)))) cx = std::max(cx, a.right() + half + ui.s(16));
            }
            hp.custom = true;
            hp.x = cx / W;
            hp.y = cy / H;
            if (cx + half > free.right() - m || box().bottom() > free.bottom() - m) setHudVisible(hs, HudElement::Collectibles, false);
            else if (Rect c = hud_.area(ui, w, HudElement::Collectibles, hud_.place(ui, w, hs, HudElement::Collectibles)); c.w > 0.0f && hudVisible(hs, HudElement::Collectibles))
                collectRect_ = c;
        }
        // Boss gauge: at the bottom of the free part, above the controls, never under the
        // powers.
        {
            HudPlace& hp = hs.hud[HudElement::Boss];
            float k = std::clamp(hp.scale, HudLayout::kMinScale, HudLayout::kMaxScale);
            k = std::min(k, (free.w - 2 * m) / ui.s(912));
            hp.scale = std::max(HudLayout::kMinScale, k);
            hp.custom = true;
            hp.x = free.center().x / W;
            hp.y = (free.bottom() - m - ui.s(34) * hp.scale) / H;
            Rect boss = hud_.area(ui, w, HudElement::Boss, hud_.place(ui, w, hs, HudElement::Boss));
            if (boss.x < m || boss.right() > free.right() - m || (powers.w > 0.0f && boss.intersects(powers)) || (vitals.w > 0.0f && boss.intersects(vitals)))
                setHudVisible(hs, HudElement::Boss, false);
            else if (boss.w > 0.0f && w.activeBoss())
                hudRects_.push_back(boss.expanded(ui.s(6)));
        }
        for (const Rect& a : {vitals, powers, collectRect_})
            if (a.w > 0.0f) hudRects_.push_back(a.expanded(ui.s(6)));
        hud_.render(ui, w, view, nullptr, hs);
    }

    /// The level's progress bar with both runners on it (A on the rail, B under it), at the
    /// top of the free part, right of the vitals and powers.
    void drawCompareProgress() {
        if (!routeReady_ || !route_.valid() || !hud_.hasProgressTrack()) return;
        UIContext& ui = g_.ui;
        GameWorld& w = world();
        const Rect& free = layout_.worldArea;
        float left = free.x + ui.s(40);
        // The collectibles counter (top centre) and the live gap above them (2.91).
        float bottomOfHud = std::max({ui.s(76), liveRect_.w > 0.0f ? liveRect_.bottom() + ui.s(8) : 0.0f,
                                      collectRect_.w > 0.0f ? collectRect_.bottom() + ui.s(4) : 0.0f});
        if (playback_ && !w.cinematic().active() && w.state() == WorldState::Playing) {
            GameplaySettings hs = g_.settings.gameplay;
            for (HudElement e : {HudElement::Vitals, HudElement::Powers}) {
                hs.hud[e].custom = false;
                if (!hudVisible(hs, e)) continue;
                Rect a = hud_.area(ui, w, e, hud_.place(ui, w, hs, e));
                if (a.w <= 0.0f) continue;
                left = std::max(left, a.right() + ui.s(40));
            }
        }
        float right = free.right() - ui.s(80);  // the exit portal and its glow
        float y = std::max(ui.s(176), bottomOfHud + Hud::progressAbove(ui.s(1), true));
        if (right - left < ui.s(260)) {  // narrow screen: under the vitals and powers
            left = free.x + ui.s(40);
            y = std::max(ui.s(260), bottomOfHud) + Hud::progressAbove(ui.s(1), true);
        }
        if (right - left < ui.s(160)) return;
        if (y + Hud::progressBelow(ui.s(1), true) > free.bottom()) return;  // no room left under the counters
        auto racer = [](float at, Color c, const char* letter, const Animator& anim, const Texture* skin, bool flip) {
            Hud::ProgressRacer rc;
            rc.at = std::clamp(at, 0.0f, 1.0f);
            rc.color = c;
            rc.letter = letter;
            rc.texture = skin ? skin : (anim.set() ? anim.set()->texture.get() : nullptr);
            if (rc.texture) rc.src = anim.sourceRect();
            rc.flip = flip;
            return rc;
        };
        float tA = static_cast<float>(std::min(clock(), aEnd()));
        double tb = std::clamp(cmp_.bTime(clock()), 0.0, static_cast<double>(cmp_.b().duration()));
        std::vector<Hud::ProgressRacer> racers;
        if (playback_) {
            const Player& p = w.player();
            racers.push_back(racer(trackA_.progressAtTime(tA), kColorA, "A", p.anim, p.skin, p.facing < 0.0f));
        } else {
            racers.push_back(racer(trackA_.progressAtTime(tA), kColorA, "A", animA_, nullptr, (poseA().flags & ReplayFrame::FacingLeft) != 0));
        }
        racers.push_back(racer(cmp_.b().track.progressAtTime(static_cast<float>(tb)), kColorB, "B", animB_, nullptr,
                               (poseB().flags & ReplayFrame::FacingLeft) != 0));
        hud_.setProgressRacers(std::move(racers));
        hud_.drawProgressAt(ui, w, {(left + right) * 0.5f, y}, 1.0f, right - left);
        const float above = Hud::progressAbove(ui.s(1), true), below = Hud::progressBelow(ui.s(1), true);
        hudRects_.push_back({left - ui.s(40), y - above, right - left + ui.s(80), above + below});
        hud_.setProgressRacers({});
    }

    /// "A · Astrid" / "B · Record mondial" above each of them, in the free part, never on
    /// each other (moved under the feet, else left out). The viewer: its reference's ghost
    /// (« Record (PB) · 22/11 », 2.91).
    void drawTags(const WorldView& view, const GhostTrack::Pose* refPose = nullptr) {
        UIContext& ui = g_.ui;
        Rect placed[2];
        int n = 0;
        const float fs = fz(ui, 15);
        const Rect area = layout_.worldArea.expanded(-ui.s(8));
        auto tag = [&](const std::string& text, Vec2 feet, Color c) {
            Vec2 size = ui.text().measure(text, FontFace::Bold, ui.s(fs));
            Vec2 head = view.toScreen({feet.x, feet.y - 34.0f});
            Vec2 foot = view.toScreen({feet.x, feet.y + 6.0f});
            Rect box{head.x - size.x * 0.5f - ui.s(6), head.y - size.y - ui.s(4), size.x + ui.s(12), size.y + ui.s(4)};
            auto ok = [&](const Rect& b) {
                if (!area.containsRect(b)) return false;
                for (int j = 0; j < n; ++j)
                    if (b.intersects(placed[j])) return false;
                for (const Rect& hr : hudRects_)  // never on the boss gauge, the vitals, the progress bar
                    if (b.intersects(hr)) return false;
                return true;
            };
            if (!ok(box)) box.y -= box.h + ui.s(2);
            if (!ok(box)) box.y = foot.y + ui.s(2);
            if (!ok(box)) return;
            placed[n++] = box;
            ui.r().fillRoundedRect(box, ui.s(5), Color{8, 12, 26, 170});
            ui.label(text, box.center(), fs, c, Align::Center, FontFace::Bold, VAlign::Middle);
        };
        if (liveRect_.w > 0.0f) hudRects_.push_back(liveRect_.expanded(ui.s(4)));
        if (!comparing_) {
            const replaygap::LoadedRef* ref = viewerRef();
            if (refPose && ref) {
                if (gapKind_ == replaygap::RefKind::Tas) tag("TAS", refPose->feet, tasui::kTas);
                else tag(ref->name, refPose->feet, kColorB);
            }
            return;
        }
        tag("A · " + displayName(a_.meta), feetA(), kColorA);
        tag("B · " + displayName(b_.meta), poseB().feet, b_.meta.source == "tas" ? tasui::kTas : kColorB);
    }

    /// 2.75: « Vu en tant qu'admin (profil masqué) » at the top of the view, all along the playback (measured).
    void drawAdminNote() {
        if (adminNote_.empty()) return;
        UIContext& ui = g_.ui;
        float fs = 0.0f;
        std::string t;
        const Rect box = adminNoteBox(&fs, &t);
        ui.r().fillRoundedRect(box, ui.s(8), Color{60, 40, 10, 225});
        ui.r().drawRoundedRect(box, ui.s(8), ui.theme().warm, ui.s(1.5f));
        ui.label(t, box.center(), fs, ui.theme().warm, Align::Center, FontFace::Bold, VAlign::Middle);
    }

    /// Where the admin's note goes (top of the free part), measured; empty without one.
    Rect adminNoteBox(float* fontOut = nullptr, std::string* textOut = nullptr) {
        if (adminNote_.empty()) return {};
        UIContext& ui = g_.ui;
        const float room = std::max(ui.s(120), layout_.worldArea.w - ui.s(40));
        const float fs = std::max(ui.fitFont(adminNote_, FontFace::Bold, fz(ui, 16), room - ui.s(32), 0.75f), 11.5f / ui.s(1.0f));
        const std::string t = ui.elide(adminNote_, FontFace::Bold, fs, room - ui.s(32));
        const Vec2 sz = ui.text().measure(t, FontFace::Bold, ui.s(fs));
        if (fontOut) *fontOut = fs;
        if (textOut) *textOut = t;
        return {layout_.worldArea.center().x - sz.x * 0.5f - ui.s(16), layout_.worldArea.y + ui.s(12), sz.x + ui.s(32), sz.y + ui.s(14)};
    }

    // ------------------------------------------------------- live gap (2.91)
    struct LiveText {
        std::string kicker, value, words, line;
        Color valueColor, wordsColor, lineColor;
        float kf = 13, vf = 30, wf = 19, lf = 14;  // fitted font sizes
        float padY = 0.0f, rowH = 0.0f;
        int sign = 0;  // +1 behind, -1 ahead (the box's edge; the words say it too)
    };

    /// « en retard sur le record à cet instant »: what the sign of the gap means, in words
    /// (never the colour alone). `sign`: +1 behind, -1 ahead, 0 level.
    std::string liveSentence(int sign) const {
        if (comparing_) {
            if (sign > 0) return "A en retard sur B à cet instant";
            if (sign < 0) return "A en avance sur B à cet instant";
            return "A et B à égalité à cet instant";
        }
        switch (gapKind_) {
            case replaygap::RefKind::Record:
                if (sign > 0) return "en retard sur le record à cet instant";
                if (sign < 0) return "en avance sur le record à cet instant";
                return "à égalité avec le record à cet instant";
            case replaygap::RefKind::SumOfBest:
                if (sign > 0) return "en retard sur la somme des meilleurs à cet instant";
                if (sign < 0) return "en avance sur la somme des meilleurs à cet instant";
                return "à égalité avec la somme des meilleurs à cet instant";
            case replaygap::RefKind::Tas:
                if (sign > 0) return refOffscreen_ ? "en retard sur le TAS à cet instant · son fantôme est hors de l'image" : "en retard sur le TAS à cet instant";
                if (sign < 0) return refOffscreen_ ? "en avance sur le TAS à cet instant · son fantôme est hors de l'image" : "en avance sur le TAS à cet instant";
                return "à égalité avec le TAS à cet instant";
            case replaygap::RefKind::Other: break;
        }
        if (sign > 0) return "en retard sur l'autre replay à cet instant";
        if (sign < 0) return "en avance sur l'autre replay à cet instant";
        return "à égalité avec l'autre replay à cet instant";
    }

    /// The Sum of Best compared at the checkpoints only: where the gap shown was taken.
    std::string checkpointSentence(int sign, int split) const {
        if (split >= static_cast<int>(splitsA_.size())) {
            if (sign > 0) return "en retard sur la somme des meilleurs à l'arrivée";
            if (sign < 0) return "en avance sur la somme des meilleurs à l'arrivée";
            return "à égalité avec la somme des meilleurs à l'arrivée";
        }
        const std::string where = checkpointName(trackA_.poseAt(static_cast<float>(splitsA_[static_cast<size_t>(split)]) / 1000.0f).feet, split);
        if (sign > 0) return "en retard sur la somme des meilleurs à " + where;
        if (sign < 0) return "en avance sur la somme des meilleurs à " + where;
        return "à égalité avec la somme des meilleurs à " + where;
    }

    std::string liveKicker() const {
        if (comparing_) {
            if (cmp_.offsetMs() == 0) return "A − B EN DIRECT · SANS DÉCALAGE";
            return "A − B EN DIRECT · DÉCALAGE " + formatDeltaMs(cmp_.offsetMs()) + " COMPRIS";
        }
        switch (gapKind_) {
            case replaygap::RefKind::Record: return "ÉCART AU RECORD (PB), EN DIRECT";
            case replaygap::RefKind::SumOfBest:
                return curveReady_ && !curve_.continuous ? "ÉCART À LA SOMME DES MEILLEURS, AUX POINTS DE PASSAGE" : "ÉCART À LA SOMME DES MEILLEURS, EN DIRECT";
            case replaygap::RefKind::Tas: return "ÉCART AU TAS, EN DIRECT";
            case replaygap::RefKind::Other: break;
        }
        return "ÉCART À L'AUTRE REPLAY, EN DIRECT";
    }

    /// What the live counter says now (texts and colours).
    LiveText liveContent() const {
        const UITheme& th = g_.ui.theme();
        LiveText L;
        L.kicker = liveKicker();
        L.valueColor = L.wordsColor = th.text;
        L.lineColor = th.textDim;
        if (!curveReady_) {
            const replaygap::LoadedRef* ref = comparing_ ? nullptr : viewerRef();
            if (ref && !ref->ok) {
                L.line = ref->why;
                L.lineColor = th.warm;
            } else {
                L.value = "…";
                L.line = "Calcul de l'écart…";
            }
            return L;
        }
        const GapSample s = gapNow(clock());
        if (!s.hasTime && !s.hasDistance) {
            L.value = "—";
            L.line = curve_.continuous ? "au départ : pas encore d'écart" : "avant le premier point de passage";
            return L;
        }
        auto tone = [&](int sign) { return sign > 0 ? replaygap::kBehindText : sign < 0 ? replaygap::kAheadText : th.text; };
        int sign = 0;
        if (s.hasTime) {
            sign = s.timeMs > 0 ? 1 : s.timeMs < 0 ? -1 : 0;
            L.value = formatDeltaMs(s.timeMs);
            L.valueColor = tone(sign);
        } else {
            L.value = "—";
        }
        if (s.hasDistance) {
            const int dsign = s.metres < -0.05f ? 1 : s.metres > 0.05f ? -1 : 0;
            if (!s.hasTime) sign = dsign;
            L.words = replaygap::distanceWords(s.metres, comparing_);
            L.wordsColor = tone(dsign);
        }
        if (!curve_.continuous) L.line = checkpointSentence(sign, s.split);
        else L.line = s.exact ? liveSentence(sign) + " · exact" : liveSentence(sign);
        L.sign = sign;
        return L;
    }

    /// The live counter's box: at the top of the free part (under the admin's note), clear of
    /// the vitals and powers, every line fitted to its width.
    void layoutLive() {
        liveRect_ = {};
        collectRect_ = {};
        if (!loaded() || failed_) return;
        UIContext& ui = g_.ui;
        const Rect& free = layout_.worldArea;
        LiveText L = liveContent();
        const float pad = ui.s(18);
        const float maxW = std::min(free.w - ui.s(40), ui.s(660));
        if (maxW < ui.s(180)) return;
        const float inner = maxW - 2 * pad;
        auto w = [&](const std::string& t, FontFace f, float size) { return t.empty() ? 0.0f : ui.text().measure(t, f, ui.s(size)).x; };
        L.kf = ui.fitFont(L.kicker, FontFace::Bold, fz(ui, 13), inner, 0.75f);
        L.lf = ui.fitFont(L.line, FontFace::Body, fz(ui, 14), inner, 0.75f);
        L.vf = fz(ui, 30);
        L.wf = fz(ui, 19);
        const float gapW = L.words.empty() ? 0.0f : ui.s(14);
        const float rowW = w(L.value, FontFace::Title, L.vf) + gapW + w(L.words, FontFace::Bold, L.wf);
        if (rowW > inner) {
            const float k = std::max(0.6f, inner / rowW);
            L.vf = std::max(L.vf * k, 11.5f / ui.s(1.0f));
            L.wf = std::max(L.wf * k, 11.5f / ui.s(1.0f));
        }
        const float contentW = std::max({w(L.kicker, FontFace::Bold, L.kf), w(L.value, FontFace::Title, L.vf) + gapW + w(L.words, FontFace::Bold, L.wf),
                                         w(L.line, FontFace::Body, L.lf)});
        const float bw = std::min(maxW, std::max(ui.s(240), contentW + 2 * pad));
        L.padY = ui.s(10);
        L.rowH = L.value.empty() ? 0.0f : std::max(ui.s(L.vf), ui.s(L.wf)) * 1.25f;
        const float bh = L.padY * 2 + ui.s(L.kf) * 1.45f + L.rowH + (L.line.empty() ? 0.0f : ui.s(L.lf) * 1.45f);
        float top = free.y + ui.s(12);
        if (Rect a = adminNoteBox(); a.w > 0.0f) top = a.bottom() + ui.s(8);
        Rect box{free.center().x - bw * 0.5f, top, bw, bh};
        // Clear of the game's vitals and powers (top left): to their right, else under them.
        Rect vitals, powers;
        topHudAreas(vitals, powers);
        for (const Rect& a : {vitals, powers})
            if (a.w > 0.0f && box.intersects(a.expanded(ui.s(8)))) box.x = a.right() + ui.s(16);
        if (box.right() > free.right() - ui.s(12)) {
            box.x = free.center().x - bw * 0.5f;
            for (const Rect& a : {vitals, powers})
                if (a.w > 0.0f && box.intersects(a.expanded(ui.s(8)))) box.y = a.bottom() + ui.s(12);
        }
        if (box.bottom() > free.bottom() - ui.s(12) || box.x < free.x + ui.s(4)) return;
        liveRect_ = box;
        liveText_ = std::move(L);
    }

    void drawLive() {
        if (liveRect_.w <= 0.0f) return;
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const LiveText& L = liveText_;
        const Rect& b = liveRect_;
        const float pad = ui.s(18), inner = b.w - 2 * pad;
        ui.r().fillRoundedRect(b, ui.s(10), Color{8, 12, 26, 228});
        const Color edge = L.sign > 0 ? replaygap::kBehind.withAlpha(170) : L.sign < 0 ? replaygap::kAhead.withAlpha(170) : th.border;
        ui.r().drawRoundedRect(b, ui.s(10), edge, std::max(1.0f, ui.s(1.5f)));
        float y = b.y + L.padY;
        const float cx = b.center().x;
        ui.label(ui.elide(L.kicker, FontFace::Bold, L.kf, inner), {cx, y}, L.kf, th.textDim, Align::Center, FontFace::Bold);
        y += ui.s(L.kf) * 1.45f;
        if (!L.value.empty()) {
            const float gapW = L.words.empty() ? 0.0f : ui.s(14);
            const float vw = ui.text().measure(L.value, FontFace::Title, ui.s(L.vf)).x;
            const float room = std::max(0.0f, inner - vw - gapW);
            const std::string words = L.words.empty() ? std::string{} : ui.elide(L.words, FontFace::Bold, L.wf, room);
            const float ww = words.empty() ? 0.0f : ui.text().measure(words, FontFace::Bold, ui.s(L.wf)).x;
            const float x0 = cx - (vw + gapW + ww) * 0.5f;
            const float cy = y + L.rowH * 0.5f;
            ui.label(L.value, {x0, cy}, L.vf, L.valueColor, Align::Left, FontFace::Title, VAlign::Middle);
            if (!words.empty()) ui.label(words, {x0 + vw + gapW, cy}, L.wf, L.wordsColor, Align::Left, FontFace::Bold, VAlign::Middle);
            y += L.rowH;
        }
        if (!L.line.empty()) ui.label(ui.elide(L.line, FontFace::Body, L.lf, inner), {cx, y}, L.lf, L.lineColor, Align::Center, FontFace::Body);
    }

    /// The game's vitals and powers as drawGameHud places them (exact replays, while playing).
    void topHudAreas(Rect& vitals, Rect& powers) {
        vitals = powers = {};
        if (!playback_) return;
        GameWorld& w = world();
        if (w.cinematic().active() || w.state() != WorldState::Playing) return;
        UIContext& ui = g_.ui;
        GameplaySettings hs = g_.settings.gameplay;
        hs.focusMode = false;
        for (HudElement e : {HudElement::Vitals, HudElement::Powers}) hs.hud[e].custom = false;
        if (hudVisible(hs, HudElement::Vitals)) vitals = hud_.area(ui, w, HudElement::Vitals, hud_.place(ui, w, hs, HudElement::Vitals));
        if (hudVisible(hs, HudElement::Powers)) powers = hud_.area(ui, w, HudElement::Powers, hud_.place(ui, w, hs, HudElement::Powers));
    }

    void drawSeekOverlay() {
        if (!playback_ || !playback_->seeking()) return;
        UIContext& ui = g_.ui;
        std::string t = "Recherche de l'instant… " + std::to_string(static_cast<int>(playback_->seekProgress() * 100.0f)) + " %";
        float fs = fz(ui, 18);
        Vec2 sz = ui.text().measure(t, FontFace::Bold, ui.s(fs));
        Rect box{layout_.worldArea.center().x - sz.x * 0.5f - ui.s(18), layout_.worldArea.center().y - sz.y * 0.5f - ui.s(12), sz.x + ui.s(36),
                 sz.y + ui.s(24)};
        ui.r().fillRoundedRect(box, ui.s(10), Color{8, 12, 26, 220});
        ui.r().drawRoundedRect(box, ui.s(10), ui.theme().accent.withAlpha(120), ui.s(1.5f));
        ui.label(t, box.center(), fs, ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
    }

    std::string checkpointName(Vec2 feet, int index) const {
        const Checkpoint* best = nullptr;
        float bestD = 260.0f * 260.0f;
        for (const Checkpoint& c : checkpoints_) {
            float d = (c.at - feet).lengthSq();
            if (d < bestD) {
                bestD = d;
                best = &c;
            }
        }
        if (best && !best->label.empty()) return best->label;
        return "Point " + std::to_string(index + 1);
    }
    std::string rowName(int row) const {
        if (row < 0 || row >= static_cast<int>(cmp_.rows().size())) return "";
        const auto& r = cmp_.rows()[static_cast<size_t>(row)];
        if (r.exit) return "Arrivée";
        return checkpointName(trackA_.poseAt(static_cast<float>(r.aCumMs) / 1000.0f).feet, r.aSplit);
    }

    void panelBack(const Rect& area) {
        UIContext& ui = g_.ui;
        ui.r().fillRoundedRect(area, ui.s(10), Color{8, 12, 26, 228});
        ui.r().drawRoundedRect(area, ui.s(10), ui.theme().border, std::max(1.0f, ui.s(1.5f)));
    }

    // ---------------------------------------------------- splits (alone)
    void drawSplitsPanel(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        panelBack(area);
        ui.pushId("splits");
        const float pad = ui.s(14);
        float x = area.x + pad, inner = area.w - 2 * pad, y = area.y + pad;
        ui.label("CHRONOMÈTRE", {x, y}, fz(ui, 15), th.textDim, Align::Left, FontFace::Bold);
        y += lineH(15);
        std::string now = formatRunMs(clockMs());
        float big = ui.fitFont(now, FontFace::Title, fz(ui, 32), inner, 0.6f);
        ui.label(now, {x, y}, big, kColorA, Align::Left, FontFace::Title);
        y += ui.s(big) * 1.35f;
        std::string of = "sur " + formatRunMs(a_.replay->durationMs) + (splitsExactA_ || splitsA_.empty() ? "" : " · passages à 1/30 s près");
        ui.label(ui.elide(of, FontFace::Body, fz(ui, 14), inner), {x, y}, fz(ui, 14), th.textDim);
        y += lineH(14) + ui.s(6);
        if (playback_ && a_.replay->teamSize() > 1) {
            // 2.62: a team replay: follow the team (its camera) or one of its players.
            const int n = a_.replay->teamSize();
            const float bh = buttonH(), gap = ui.s(6);
            const float bw = (inner - gap * static_cast<float>(n)) / static_cast<float>(n + 1);
            for (int i = -1; i < n; ++i) {
                UIContext::ButtonStyle st;
                st.small = true;
                st.selected = teamFollow_ == i;
                const std::string label = i < 0 ? "Équipe" : "Joueur " + std::to_string(i + 1);
                st.fontSize = ui.fitFont(label, FontFace::Bold, fz(ui, 15), bw - ui.s(12), 0.6f);
                if (ui.button("follow" + std::to_string(i + 1), {x + static_cast<float>(i + 1) * (bw + gap), y, bw, bh}, label, st)) teamFollow_ = i;
            }
            y += bh + ui.s(8);
        }
        ui.r().fillRect({x, y, inner, std::max(1.0f, ui.s(1))}, th.accent.withAlpha(60));
        y += ui.s(8);
        // 2.91: the gap to the reference at each checkpoint passed (exact), in its own column.
        std::string head = "TEMPS DE PASSAGE";
        if (gapKind_ == replaygap::RefKind::Record) head += " · ÉCART AU PB";
        else if (gapKind_ == replaygap::RefKind::SumOfBest) head += " · ÉCART À LA SOMME DES MEILLEURS";
        else if (gapKind_ == replaygap::RefKind::Tas) head += " · ÉCART AU TAS";
        else head += " · ÉCART À L'AUTRE REPLAY";
        const float hf = ui.fitFont(head, FontFace::Bold, fz(ui, 15), inner, 0.8f);
        ui.label(ui.elide(head, FontFace::Bold, hf, inner), {x, y}, hf, th.textDim, Align::Left, FontFace::Bold);
        y += lineH(15);
        // Columns, measured: point | time | segment | gap (left out when the panel is too narrow).
        const float fs = fz(ui, 15);
        const float numW = ui.text().measure("10:00,000", FontFace::Body, ui.s(fs)).x + ui.s(10);
        const float gapW = std::max(ui.text().measure("+1:15,500", FontFace::Bold, ui.s(fs)).x, ui.text().measure("Écart", FontFace::Body, ui.s(fz(ui, 13))).x) +
                           ui.s(10);
        const bool gapCol = inner - 2 * numW - gapW - ui.s(6) >= ui.s(70);
        float cGap = x + inner, cSeg = gapCol ? cGap - gapW : cGap, cTime = cSeg - numW;
        float nameRoom = cTime - numW - x - ui.s(6);
        ui.label("Point", {x, y}, fz(ui, 13), th.textDim);
        ui.label("Temps", {cTime, y}, fz(ui, 13), th.textDim, Align::Right);
        ui.label("Segment", {cSeg, y}, fz(ui, 13), th.textDim, Align::Right);
        if (gapCol) ui.label("Écart", {cGap, y}, fz(ui, 13), th.textDim, Align::Right);
        y += lineH(13);
        float rh = std::max(ui.s(30), lineH(15) + ui.s(6));
        size_t n = splitsA_.size() + 1;
        float mh = measureBoxH();  // 2.91: the measure A → B under the list (when the list keeps two rows)
        if (area.bottom() - pad - y - mh - ui.s(8) < 2.0f * std::max(ui.s(30), lineH(15) + ui.s(6))) mh = 0.0f;
        // The TAS as the reference: what it is, in a box under the list (when the list keeps two rows).
        const tas::TasRun* machine = gapKind_ == replaygap::RefKind::Tas ? viewerTas() : nullptr;
        float tasH = machine ? tasBoxH(inner) : 0.0f;
        if (tasH > 0.0f && area.bottom() - pad - y - mh - (mh > 0.0f ? ui.s(8) : 0.0f) - tasH - ui.s(8) < 2.0f * std::max(ui.s(30), lineH(15) + ui.s(6))) tasH = 0.0f;
        Rect list{area.x + ui.s(4), y, area.w - ui.s(8),
                  area.bottom() - pad - y - (mh > 0.0f ? mh + ui.s(8) : 0.0f) - (tasH > 0.0f ? tasH + ui.s(8) : 0.0f)};
        ui.beginScroll("rows", list, static_cast<float>(n) * rh);
        float ry = list.y - ui.scrollOffset();
        int64_t t = clockMs();
        int64_t prev = 0;
        for (size_t i = 0; i < n; ++i, ry += rh) {
            bool exitRow = i == splitsA_.size();
            int64_t at = exitRow ? static_cast<int64_t>(a_.replay->durationMs) : splitsA_[i];
            int64_t seg = at - prev;
            prev = at;
            std::string name = exitRow ? "Arrivée" : checkpointName(trackA_.poseAt(static_cast<float>(at) / 1000.0f).feet, static_cast<int>(i));
            Rect row{list.x, ry, list.w - ui.s(12), rh - ui.s(4)};
            bool passed = at <= t;
            bool current = passed && (i + 1 >= n || (exitRow ? true : (i + 1 < splitsA_.size() ? splitsA_[i + 1] > t : a_.replay->durationMs > t)));
            bool hovered = false;
            if (ui.hotspot("r" + std::to_string(i), row, &hovered)) seekTo(static_cast<double>(at) / 1000.0);
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            if (current || hovered || ui.focused("r" + std::to_string(i)))
                ui.r().fillRoundedRect(row, ui.s(5), current ? kColorA.withAlpha(40) : Color{255, 255, 255, 18});
            if (ui.focused("r" + std::to_string(i))) ui.r().drawRoundedRect(row, ui.s(5), th.accent.withAlpha(160), ui.s(1.5f));
            Color c = passed ? th.text : th.textDim;
            float cy = row.center().y;
            ui.label(ui.elide(name, FontFace::Body, fs, nameRoom), {x, cy}, fs, c, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(formatRunMs(at), {cTime, cy}, fs, c, Align::Right, FontFace::Bold, VAlign::Middle);
            ui.label(formatRunMs(seg), {cSeg, cy}, fs, passed ? th.textDim : th.textDim.withAlpha(150), Align::Right, FontFace::Body, VAlign::Middle);
            if (gapCol) {
                const GapSample* gs = passed && curveReady_ ? curve_.atSplit(static_cast<int>(i)) : nullptr;
                if (gs && gs->hasTime) {
                    const Color gc = gs->timeMs > 0 ? replaygap::kBehindText : gs->timeMs < 0 ? replaygap::kAheadText : th.text;
                    ui.label(formatDeltaMs(gs->timeMs, false), {cGap, cy}, fs, gc, Align::Right, FontFace::Bold, VAlign::Middle);
                } else {
                    ui.label("—", {cGap, cy}, fs, th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
                }
            }
        }
        ui.endScroll();
        if (tasH > 0.0f) drawTasBox(*machine, {x, list.bottom() + ui.s(8), inner, tasH});
        if (mh > 0.0f) drawMeasureBox({x, area.bottom() - pad - mh, inner, mh});
        ui.popId();
    }

    /// « RÉFÉRENCE : TAS ANY% »: the time, what it is (the machine, the versions, the date), never
    /// ranked, and its sheet (F, or a click on the box).
    std::vector<std::pair<std::string, float>> tasBoxLines(const tas::TasRun& t) const {
        std::vector<std::pair<std::string, float>> l;
        l.push_back({"RÉFÉRENCE : TAS " + std::string(categoryLabel(t.category)), 13.0f});
        l.push_back({tas::timeText(t.timeMs) + " · meilleur trouvé par la machine", 15.0f});
        std::string v = "Niveau v" + std::to_string(t.levelVersion) + " · simulation " + std::to_string(t.simVersion);
        if (const std::string d = tas::dateText(t.computedAt); !d.empty()) v += " · calculé le " + d;
        l.push_back({v, 14.0f});
        l.push_back({"Hors classement · Fiche du TAS… (F)", 14.0f});
        return l;
    }
    float tasBoxH(float w) {
        const tas::TasRun* t = viewerTas();
        if (!t) return 0.0f;
        float h = g_.ui.s(12) * 2.0f;
        for (const auto& line : tasBoxLines(*t)) h += lineH(line.second);
        (void)w;
        return h;
    }
    void drawTasBox(const tas::TasRun& t, const Rect& box) {
        UIContext& ui = g_.ui;
        bool hovered = false;
        const bool open = ui.hotspot("tasbox", box, &hovered);
        ui.r().fillRoundedRect(box, ui.s(10), tasui::kTasCard.withAlpha(150));
        ui.r().drawRoundedRect(box, ui.s(10), hovered || ui.focused("tasbox") ? tasui::kTas : tasui::kTasLine, std::max(1.0f, ui.s(1.5f)));
        const float pad = ui.s(14), inner = box.w - 2 * pad;
        float y = box.y + ui.s(12);
        const auto lines = tasBoxLines(t);
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& [text, size] = lines[i];
            const FontFace f = i == 0 || i == 3 ? FontFace::Bold : FontFace::Body;
            const float fs = ui.fitFont(text, f, fz(ui, size), inner, 0.75f);
            const Color c = i == 0 || i == 3 ? tasui::kTas : i == 1 ? Color{200, 211, 226, 255} : ui.theme().textDim;
            ui.label(ui.elide(text, f, fs, inner), {box.x + pad, y}, fs, c, Align::Left, f);
            y += lineH(size);
        }
        if (open) tasui::openSheet(g_, t.levelId, t.category);
    }

    // -------------------------------------------------------- comparison
    void drawComparePanel(const Rect& area) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        panelBack(area);
        ui.pushId("cmp");
        const float pad = ui.s(14);
        const float x = area.x + pad, inner = area.w - 2 * pad, right = area.right() - pad;
        float y = area.y + pad;
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = fz(ui, 16);
        const float bh = std::max(ui.s(36), px(16) * 2.0f), gap = ui.s(6);

        // Who is compared.
        ui.label("COMPARAISON", {x, y}, fz(ui, 15), th.textDim, Align::Left, FontFace::Bold);
        {
            std::string sw = "Échanger A et B";
            float w = ui.text().measure(sw, FontFace::Bold, px(14)).x + ui.s(22);
            UIContext::ButtonStyle s2 = st;
            s2.fontSize = fz(ui, 14);
            if (ui.button("swap", {right - w, y - ui.s(4), w, lineH(15) + ui.s(4)}, sw, s2)) swapAB();
        }
        y += lineH(15) + ui.s(6);
        if (!cmp_.sameVersion()) {
            y += ui.labelWrapped("Autre version : comparaison non officielle", {x, y}, inner, fz(ui, 14), th.warm).y + ui.s(4);
        }
        auto side = [&](const char* letter, const ReplayChoice& c, const CompareRun& run, Color col, const std::string& how) {
            float bs = lineH(16);
            Rect badge{x, y, bs, bs};
            ui.r().fillRoundedRect(badge, ui.s(5), col);
            ui.label(letter, badge.center(), fz(ui, 15), Color{6, 12, 24, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
            std::string time = formatRunMs(run.durationMs);
            float tw = ui.text().measure(time, FontFace::Bold, px(16)).x;
            float nx = x + bs + ui.s(8);
            ui.label(ui.elide(displayName(c.meta), FontFace::Bold, fz(ui, 16), right - tw - ui.s(10) - nx), {nx, y + bs * 0.5f}, fz(ui, 16), th.text,
                     Align::Left, FontFace::Bold, VAlign::Middle);
            ui.label(time, {right, y + bs * 0.5f}, fz(ui, 16), col, Align::Right, FontFace::Bold, VAlign::Middle);
            y += bs + ui.s(2);
            std::string info = how;
            if (c.meta.levelVersion > 0) info += " · v" + std::to_string(c.meta.levelVersion);
            info += c.meta.source == "tas" ? " · machine, hors classement" : c.meta.validated ? " · vérifié en ligne" : " · local";
            if (!run.splitsExact && !run.splitsMs.empty()) info += " · passages à 1/30 s près";
            ui.label(ui.elide(info, FontFace::Body, fz(ui, 13), right - nx), {nx, y}, fz(ui, 13), th.textDim);
            y += lineH(13) + ui.s(4);
        };
        std::string howA = playback_ ? "rejoué à l'identique" : "trajectoire enregistrée";
        if (!playback_ && !installedVersion(a_.replay->levelHash)) howA += " (autre version)";
        else if (!playback_ && a_.replay->exact() && forceTrace_) howA += " (désynchronisé)";
        side("A", a_, cmp_.a(), kColorA, howA);
        side("B", b_, cmp_.b(), kColorB, "fantôme, trajectoire enregistrée");

        // The live gap A − B is above the run and its curve under it (2.91); here, B's offset.
        y += ui.s(2);
        std::string offLine = msgTimer_ > 0.0f ? msg_ : formatCompareOffset(cmp_.offsetMs()) + (cmp_.offsetMs() != 0 ? " compris" : "");
        if (msgTimer_ <= 0.0f && cmp_.alignedRow() >= 0) offLine += " · recalé sur " + rowName(cmp_.alignedRow());
        ui.label(ui.elide(offLine, FontFace::Body, fz(ui, 14), inner), {x, y}, fz(ui, 14), msgTimer_ > 0.0f ? th.warm : th.textDim);
        y += lineH(14) + ui.s(4);
        ui.r().fillRect({x, y, inner, std::max(1.0f, ui.s(1))}, th.accent.withAlpha(60));
        y += ui.s(8);

        // B's offset: fine and coarse steps, typed value, zero, alignment on a checkpoint.
        ui.label("Décalage de B", {x, y}, fz(ui, 15), th.text, Align::Left, FontFace::Bold);
        ui.label(formatDeltaMs(cmp_.offsetMs()), {right, y}, fz(ui, 15), kColorB, Align::Right, FontFace::Bold);
        y += lineH(15) + ui.s(2);
        {
            static const std::array<std::pair<const char*, int>, 6> steps{
                {{"−1 s", -1000}, {"−100 ms", -100}, {"−10 ms", -10}, {"+10 ms", 10}, {"+100 ms", 100}, {"+1 s", 1000}}};
            // Earlier on the first row, later on the second (labels never shrunk).
            float bw = (inner - gap * 2) / 3.0f;
            UIContext::ButtonStyle s3 = st;
            s3.fontSize = fz(ui, 15);
            for (size_t i = 0; i < steps.size(); ++i) {
                size_t col = i < 3 ? i : 5 - i;  // +10 ms under -10 ms, +1 s under -1 s
                float by = y + (i < 3 ? 0.0f : bh + gap);
                if (ui.button("n" + std::to_string(i), {x + static_cast<float>(col) * (bw + gap), by, bw, bh}, steps[i].first, s3))
                    cmp_.nudge(steps[i].second);
            }
            y += 2 * (bh + gap);
        }
        {
            float zw = std::max(ui.s(70), ui.text().measure("Zéro", FontFace::Bold, px(16)).x + ui.s(24));
            float aw = std::max(ui.s(90), ui.text().measure("Appliquer", FontFace::Bold, px(16)).x + ui.s(24));
            float fw = inner - zw - aw - 2 * gap;
            bool wasEditing = ui.editingText();
            ui.textField("offset", {x, y, fw, bh}, offsetText_, "ex. +1,250", 16);
            bool validated = wasEditing && !ui.editingText() && g_.app.input().keyPressed(SDL_SCANCODE_RETURN);
            editedThisFrame_ = wasEditing && !ui.editingText();
            if (ui.button("apply", {x + fw + gap, y, aw, bh}, "Appliquer", st) || validated) applyTyped();
            if (ui.button("zero", {x + fw + gap + aw + gap, y, zw, bh}, "Zéro", st)) {
                cmp_.resetOffset();
                offsetText_.clear();
            }
            y += bh + gap;
        }
        {
            int rows = static_cast<int>(cmp_.rows().size());
            alignRow_ = std::clamp(alignRow_, 0, std::max(0, rows - 1));
            float aw = ui.text().measure("Recaler B", FontFace::Bold, px(16)).x + ui.s(26);
            float sq = bh;
            if (ui.button("aprev", {x, y, sq, bh}, "‹", st)) alignRow_ = (alignRow_ + rows - 1) % std::max(1, rows);
            if (ui.button("anext", {x + inner - aw - gap - sq, y, sq, bh}, "›", st)) alignRow_ = (alignRow_ + 1) % std::max(1, rows);
            float lx = x + sq + ui.s(6), lw = inner - aw - gap - 2 * sq - ui.s(12);
            std::string pt = "Point : " + rowName(alignRow_);
            float pf = ui.fitFont(pt, FontFace::Bold, fz(ui, 15), lw, 0.85f);
            ui.label(ui.elide(pt, FontFace::Bold, pf, lw), {lx + lw * 0.5f, y + bh * 0.5f}, pf, th.text, Align::Center, FontFace::Bold, VAlign::Middle);
            UIContext::ButtonStyle s4 = st;
            s4.primary = true;
            if (ui.button("align", {x + inner - aw, y, aw, bh}, "Recaler B", s4)) alignOn(alignRow_);
            y += bh + gap;
        }
        {
            static const std::array<const char*, 3> cams{"Suivre A", "Suivre B", "Les deux"};
            float bw = (inner - gap * 3) / 4.0f;
            for (int i = 0; i < 3; ++i) {
                UIContext::ButtonStyle s5 = st;
                s5.selected = camMode_ == i;
                if (ui.button("cam" + std::to_string(i), {x + static_cast<float>(i) * (bw + gap), y, bw, bh}, cams[static_cast<size_t>(i)], s5)) {
                    camMode_ = i;
                    snapCam_ = true;
                }
            }
            UIContext::ButtonStyle s6 = st;
            s6.selected = showPaths_;
            if (ui.button("paths", {x + 3 * (bw + gap), y, bw, bh}, "Tracés", s6)) showPaths_ = !showPaths_;
            y += bh + ui.s(8);
        }
        ui.r().fillRect({x, y, inner, std::max(1.0f, ui.s(1))}, th.accent.withAlpha(60));
        y += ui.s(6);
        float mh = measureBoxH();  // 2.91: the measure A → B under the table (when it keeps three rows)
        if (area.bottom() - y - ui.s(6) - mh - ui.s(8) < lineH(13) + 4.0f * std::max(ui.s(26), lineH(14) + ui.s(4))) mh = 0.0f;
        drawCompareTable({area.x, y, area.w, area.bottom() - y - ui.s(6) - (mh > 0.0f ? mh + ui.s(8) : 0.0f)}, pad);
        if (mh > 0.0f) drawMeasureBox({x, area.bottom() - ui.s(6) - mh, inner, mh});
        ui.popId();
    }

    void applyTyped() {
        auto v = parseCompareOffset(offsetText_);
        if (!v) {
            say("Décalage illisible : tapez par exemple +1,250 ou -0,5");
            return;
        }
        cmp_.setOffsetMs(*v);
        if (cmp_.offsetMs() != *v) say("Décalage limité à " + formatDeltaMs(cmp_.offsetMs()));
        offsetText_.clear();
    }

    void alignOn(int row) {
        if (cmp_.alignOn(row)) {
            alignRow_ = row;
            noteAchievement("replay_aligns");  // 2.24: « Synchronisé »
            say("B recalé sur " + rowName(row) + " : " + formatCompareOffset(cmp_.offsetMs()));
        }
        else say("B n'est pas passé par " + rowName(row) + " : recalage impossible");
    }

    /// Segment by segment: durations of A and B, their difference, the gap at the
    /// checkpoint (offset included); the faster duration in gold.
    void drawCompareTable(const Rect& area, float pad) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const float x = area.x + pad, inner = area.w - 2 * pad;
        const float fs = fz(ui, 14), hs = fz(ui, 13);
        float y = area.y;
        const float numW = std::max(ui.text().measure("10:00,000", FontFace::Body, ui.s(fs)).x, ui.text().measure("+1:15,500", FontFace::Body, ui.s(fs)).x) +
                           ui.s(8);
        float cGap = x + inner, cDiff = cGap - numW, cB = cDiff - numW, cA = cB - numW;
        float nameRoom = cA - numW - x - ui.s(4);
        ui.label("Segment", {x, y}, hs, th.textDim);
        ui.label("A", {cA, y}, hs, kColorA, Align::Right, FontFace::Bold);
        ui.label("B", {cB, y}, hs, kColorB, Align::Right, FontFace::Bold);
        ui.label("A − B", {cDiff, y}, hs, th.textDim, Align::Right);
        ui.label("Écart", {cGap, y}, hs, th.textDim, Align::Right);
        y += lineH(hs);
        const auto& rows = cmp_.rows();
        float rh = std::max(ui.s(26), lineH(fs) + ui.s(4));
        float footH = rh + ui.s(4);
        Rect list{area.x + ui.s(4), y, area.w - ui.s(8), std::max(rh, area.bottom() - y - footH)};
        ui.beginScroll("table", list, static_cast<float>(rows.size()) * rh);
        float ry = list.y - ui.scrollOffset();
        int cur = cmp_.rowAt(clock());
        auto col = [&](int64_t v) { return v > 0 ? th.danger : v < 0 ? th.success : th.text; };
        for (size_t i = 0; i < rows.size(); ++i, ry += rh) {
            const auto& r = rows[i];
            Rect row{list.x, ry, list.w - ui.s(12), rh - ui.s(2)};
            bool hovered = false;
            std::string id = "t" + std::to_string(i);
            if (ui.hotspot(id, row, &hovered)) seekTo(static_cast<double>(r.aCumMs) / 1000.0);
            if (row.bottom() < list.y || row.y > list.bottom()) continue;
            bool focus = ui.focused(id);
            if (static_cast<int>(i) == cur || hovered || focus)
                ui.r().fillRoundedRect(row, ui.s(4), static_cast<int>(i) == cur ? kColorA.withAlpha(36) : Color{255, 255, 255, 16});
            if (focus) ui.r().drawRoundedRect(row, ui.s(4), th.accent.withAlpha(160), ui.s(1.5f));
            if (static_cast<int>(i) == cmp_.alignedRow()) ui.r().fillRect({row.x, row.y + ui.s(3), ui.s(3), row.h - ui.s(6)}, kColorB);
            float cy = row.center().y;
            ui.label(ui.elide(rowName(static_cast<int>(i)), FontFace::Body, fs, nameRoom), {x, cy}, fs, th.text, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(formatRunMs(r.aSegMs), {cA, cy}, fs, r.faster == 1 ? kGold : th.text, Align::Right, r.faster == 1 ? FontFace::Bold : FontFace::Body,
                     VAlign::Middle);
            ui.label(r.bSegMs >= 0 ? formatRunMs(r.bSegMs) : "—", {cB, cy}, fs, r.faster == 2 ? kGold : th.text, Align::Right,
                     r.faster == 2 ? FontFace::Bold : FontFace::Body, VAlign::Middle);
            if (r.hasSegDiff) ui.label(formatDeltaMs(r.segDiffMs, false), {cDiff, cy}, fs, col(r.segDiffMs), Align::Right, FontFace::Body, VAlign::Middle);
            ui.label(r.hasGap ? formatDeltaMs(r.gapMs, false) : "—", {cGap, cy}, fs, r.hasGap ? col(r.gapMs) : th.textDim, Align::Right, FontFace::Bold,
                     VAlign::Middle);
        }
        ui.endScroll();
        // Whole runs: their times and the gap at the exit (offset included).
        const auto& t = cmp_.totals();
        float fy = list.bottom() + ui.s(4);
        ui.r().fillRect({x, fy, inner, std::max(1.0f, ui.s(1))}, th.accent.withAlpha(50));
        float cy = fy + footH * 0.5f;
        ui.label("Total", {x, cy}, fs, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
        ui.label(formatRunMs(t.aMs), {cA, cy}, fs, t.faster == 1 ? kGold : th.text, Align::Right, FontFace::Bold, VAlign::Middle);
        ui.label(formatRunMs(t.bMs), {cB, cy}, fs, t.faster == 2 ? kGold : th.text, Align::Right, FontFace::Bold, VAlign::Middle);
        ui.label(formatDeltaMs(t.diffMs, false), {cDiff, cy}, fs, col(t.diffMs), Align::Right, FontFace::Body, VAlign::Middle);
        ui.label(formatDeltaMs(t.gapMs, false), {cGap, cy}, fs, col(t.gapMs), Align::Right, FontFace::Bold, VAlign::Middle);
    }

    // ------------------------------------------- measure A → B (2.91), panels
    static std::string secondsText(double s) {
        if (s >= 60.0) return formatRunMs(std::llround(s * 1000.0));
        const long long ms = std::llround(std::max(0.0, s) * 1000.0);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%lld,%03lld s", ms / 1000, ms % 1000);
        return buf;
    }

    int measureRows() const {
        const MeasurePair* p = measurePair();
        return !p || !p->complete() ? 1 : comparing_ ? 4 : 3;
    }
    float measureBoxH() const {
        if (!measuring()) return 0.0f;
        return lineH(15) + g_.ui.s(4) + g_.ui.s(14) + static_cast<float>(measureRows()) * lineH(14);
    }

    /// « MESURE A1 → B1 · Les piliers »: this replay's time against the player's best and the
    /// record's (the viewer), A's and B's and their difference (the comparison). Each value says
    /// how it was timed: on the simulation (exact) or on a trajectory (about 1/30 s).
    void drawMeasureBox(const Rect& area) {
        const MeasurePair* p = measurePair();
        if (!p) return;
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        const std::string n = std::to_string(p->id);
        std::string head = p->complete() ? "MESURE A" + n + " → B" + n : "MESURE A" + n + " → …";
        // Several pairs: ‹ › to go from one to the other (focusable).
        const float hh = lineH(15);
        float headRoom = area.w;
        if (measurePairs_.size() > 1) {
            UIContext::ButtonStyle st;
            st.small = true;
            st.fontSize = fz(ui, 14);
            const float bw = std::max(hh * 1.3f, ui.text().measure("›", FontFace::Bold, px(14)).x + ui.s(16));
            const int count = static_cast<int>(measurePairs_.size());
            if (ui.button("mnext", {area.right() - bw, area.y, bw, hh}, "›", st)) measureSel_ = (measureSel_ + 1) % count;
            if (ui.button("mprev", {area.right() - 2 * bw - ui.s(4), area.y, bw, hh}, "‹", st)) measureSel_ = (measureSel_ + count - 1) % count;
            headRoom -= 2 * bw + ui.s(14);
            p = measurePair();
        }
        const float hf = fz(ui, 14);
        const std::string shownHead = ui.elide(head, FontFace::Bold, hf, headRoom);
        const float headW = ui.text().measure(shownHead, FontFace::Bold, ui.s(hf)).x;
        ui.label(shownHead, {area.x, area.y + hh * 0.5f}, hf, th.accent, Align::Left, FontFace::Bold, VAlign::Middle);
        if (!p->name.empty() && headRoom - headW > ui.s(60)) {
            i18n::Verbatim playerWords;  // given by the player: never translated
            const float nx = area.x + headW + ui.s(8);
            ui.label(ui.elide("· " + p->name, FontFace::Bold, hf, area.x + headRoom - nx), {nx, area.y + hh * 0.5f}, hf, th.accent, Align::Left,
                     FontFace::Bold, VAlign::Middle);
        }
        const Rect box{area.x, area.y + hh + ui.s(4), area.w, area.bottom() - (area.y + hh + ui.s(4))};
        ui.r().fillRoundedRect(box, ui.s(8), Color{40, 32, 10, 150});
        ui.r().drawRoundedRect(box, ui.s(8), kGold.withAlpha(120), std::max(1.0f, ui.s(1.2f)));
        const float fs = fz(ui, 14), rx = box.right() - ui.s(10), lx = box.x + ui.s(10);
        float y = box.y + ui.s(7);
        auto row = [&](const std::string& label, const std::string& value, Color vc, bool strong) {
            const FontFace f = strong ? FontFace::Bold : FontFace::Body;
            const float vw = ui.text().measure(value, FontFace::Bold, ui.s(fs)).x;
            const float room = rx - vw - ui.s(12) - lx;
            const float cy = y + lineH(14) * 0.5f;
            if (room > ui.s(30)) ui.label(ui.elide(label, f, fs, room), {lx, cy}, fs, strong ? th.text : th.textDim, Align::Left, f, VAlign::Middle);
            ui.label(value, {rx, cy}, fs, vc, Align::Right, FontFace::Bold, VAlign::Middle);
            y += lineH(14);
        };
        if (!p->complete()) {
            const std::string t = "B" + n + " pas encore posé : F8 (ou Vue + ↑) plus loin";
            ui.label(ui.elide(t, FontFace::Body, fs, rx - lx), {lx, y + lineH(14) * 0.5f}, fs, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return;
        }
        auto tone = [&](double mine, double other) {
            const long long d = std::llround((mine - other) * 1000.0);
            return d > 0 ? replaygap::kBehindText : d < 0 ? replaygap::kAheadText : th.text;
        };
        const std::string best = p->best > 0.0f ? secondsText(p->best) : "—";
        const auto it = measurePasses_.find(p->id);
        if (!comparing_) {
            const PassView v = aPassAt(p->id, clock());
            const std::string label = !v.any ? "Ce replay" : v.exact ? "Ce replay (rejoué, exact)" : "Ce replay (trajectoire, à 0,03 s près)";
            std::string value = !v.any ? "pas encore passé" : v.running ? "en cours · " + secondsText(v.time) : secondsText(v.time);
            const Color vc = !v.any ? th.textDim : v.running ? kGold : p->best > 0.0f ? tone(v.time, p->best) : th.text;
            row(label, value, vc, true);
            row("Votre meilleur (entraînement)", best, th.text, false);
            const GapRefSlot& rec = gapRefs_[static_cast<size_t>(replaygap::RefKind::Record)];
            std::string pb = "…";
            if (rec.ready) {
                const PassView pv = it != measurePasses_.end() && rec.res.ok ? firstPass(p->id, it->second.pb, false) : PassView{};
                pb = pv.any ? secondsText(pv.time) : "—";
            }
            row("Le PB sur A → B (trajectoire)", pb, th.text, false);
            return;
        }
        const PassView a = it != measurePasses_.end() ? firstPass(p->id, it->second.a, true) : PassView{};
        const PassView b = it != measurePasses_.end() ? firstPass(p->id, it->second.b, false) : PassView{};
        const std::string diff = a.any && b.any ? formatDeltaMs(std::llround((a.time - b.time) * 1000.0)) : "—";
        row("A − B sur A" + n + " → B" + n, diff, a.any && b.any ? tone(a.time, b.time) : th.textDim, true);
        row(a.exact ? "A (rejoué, exact)" : "A (trajectoire, à 0,03 s près)", a.any ? secondsText(a.time) : "—", th.text, false);
        row("B (trajectoire, à 0,03 s près)", b.any ? secondsText(b.time) : "—", th.text, false);
        row("Votre meilleur A" + n + " → B" + n, best, th.text, false);
    }

    /// The gates in the level: dashed lines where they were placed (the pair shown brighter).
    void drawGates(Renderer& r) {
        if (!measuring() || !g_.measures.showGates) return;
        const MeasurePair* sel = measurePair();
        for (const MeasurePair& p : measurePairs_) {
            const bool on = sel && sel->id == p.id;
            auto gate = [&](const MeasureGate& g) {
                const Color c = kGold.withAlpha(on ? 220 : 110);
                for (float y = g.top(); y < g.bottom(); y += 16.0f) r.drawWorldLine({g.at.x, y}, {g.at.x, std::min(g.bottom(), y + 9.0f)}, c, on ? 3.0f : 2.0f);
            };
            gate(p.a);
            if (p.b) gate(*p.b);
        }
    }

    /// « A1 » / « B1 » at the top of each gate, in the free part, never on the HUD, the live gap,
    /// the tags or each other (moved to the gate's foot, else left out).
    void drawGateLabels(const WorldView& view) {
        if (!measuring() || !g_.measures.showGates) return;
        UIContext& ui = g_.ui;
        const float fs = fz(ui, 15);
        const Rect area = layout_.worldArea.expanded(-ui.s(6));
        std::vector<Rect> placed;
        auto free = [&](const Rect& b) {
            if (!area.containsRect(b)) return false;
            for (const Rect& o : placed)
                if (b.intersects(o)) return false;
            for (const Rect& o : hudRects_)
                if (b.intersects(o)) return false;
            return !(liveRect_.w > 0.0f && b.intersects(liveRect_));
        };
        const MeasurePair* sel = measurePair();
        for (const MeasurePair& p : measurePairs_) {
            const bool on = sel && sel->id == p.id;
            for (int k = 0; k < (p.b ? 2 : 1); ++k) {
                const MeasureGate& g = k == 0 ? p.a : *p.b;
                const std::string t = (k == 0 ? "A" : "B") + std::to_string(p.id);
                const Vec2 sz = ui.text().measure(t, FontFace::Bold, ui.s(fs));
                const Vec2 top = view.toScreen({g.at.x, g.top()}), foot = view.toScreen({g.at.x, g.bottom()});
                Rect box{top.x - sz.x * 0.5f - ui.s(8), top.y - sz.y - ui.s(10), sz.x + ui.s(16), sz.y + ui.s(8)};
                if (!free(box)) box.y = foot.y + ui.s(4);
                if (!free(box)) continue;
                placed.push_back(box);
                ui.r().fillRoundedRect(box, ui.s(6), on ? kGold : kGold.withAlpha(150));
                ui.label(t, box.center(), fs, Color{30, 22, 4, 255}, Align::Center, FontFace::Bold, VAlign::Middle);
            }
        }
        for (const Rect& b : placed) hudRects_.push_back(b.expanded(ui.s(2)));
    }

    // ------------------------------------------------------------ controls
    void drawStrip(const Rect& s) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        Renderer& r = ui.r();
        InputManager& in = g_.app.input();
        r.fillGradientV({s.x, s.y - ui.s(24), s.w, ui.s(24)}, Color{4, 8, 18, 0}, Color{4, 8, 18, 235});
        r.fillRect(s, Color{4, 8, 18, 235});
        const float x0 = s.x + ui.s(24), x1 = s.right() - ui.s(24), w = x1 - x0;
        float y = s.y + ui.s(10);

        // Title, what is shown and its integrity, on one row (measured: the mode first, the title,
        // then what is shown in the room left).
        std::string title = comparing_ ? "COMPARAISON DE REPLAYS" : "REPLAY - " + playerName_;  // the title font has no « · »
        std::string mode;
        Color mc = th.textDim;
        if (playback_) {
            if (playback_->desynced()) {
                mode = "Désynchronisé à " + formatRunTime(static_cast<int64_t>(playback_->desyncTick()) * 1000 / kSimHz) +
                       " : ce replay vient d'une autre version du moteur";
                mc = th.danger;
            } else {
                mode = std::string(comparing_ ? "A rejoué à l'identique" : "Partie rejouée à l'identique") + " · " +
                       std::to_string(playback_->checksPassed()) + " contrôles vérifiés";
                mc = th.success;
            }
        } else {
            mode = a_.replay->exact() && installedVersion(a_.replay->levelHash) ? "Trajectoire enregistrée (simulation désynchronisée)"
                   : installedVersion(a_.replay->levelHash)                   ? "Trajectoire enregistrée (ancien format de replay)"
                                                                              : "Trajectoire enregistrée (autre version du niveau)";
            mc = th.warm;
        }
        std::string sub;
        if (comparing_) {
            sub = level_.name + "  ·  A : " + displayName(a_.meta) + "  ·  B : " + displayName(b_.meta) + "  ·  " + formatCompareOffset(cmp_.offsetMs());
        } else {
            sub = level_.name;
            if (!categoryText_.empty()) sub += " · " + categoryText_;
            if (timeMs_ > 0) sub += " · " + formatRunMs(timeMs_);
            if (rank_ > 0) sub += " · n°" + std::to_string(rank_);
            // 2.75.3: a record an administrator watches before (or after refusing) its verification.
            if (!recordStatus_.empty() && !adminview::countsAsVerified(recordStatus_)) sub += " · non vérifié : " + adminview::statusLabel(recordStatus_);
        }
        {
            const float tf = fz(ui, 22), sf = fz(ui, 15);
            const float modeRoom = w * 0.45f;
            const float mf = ui.fitFont(mode, FontFace::Bold, fz(ui, 15), modeRoom, 0.85f);
            const std::string modeShown = ui.elide(mode, FontFace::Bold, mf, modeRoom);
            const float modeW = ui.text().measure(modeShown, FontFace::Bold, ui.s(mf)).x;
            const float cy = y + lineH(22) * 0.5f;
            const float left = w - modeW - ui.s(24);
            const std::string titleShown = ui.elide(title, FontFace::Title, tf, std::min(left, std::max(left * 0.55f, left - ui.s(260))));
            const float titleW = ui.text().measure(titleShown, FontFace::Title, ui.s(tf)).x;
            ui.label(titleShown, {x0, cy}, tf, kColorA, Align::Left, FontFace::Title, VAlign::Middle);
            const float subRoom = left - titleW - ui.s(18);
            if (subRoom >= ui.s(80)) ui.label(ui.elide(sub, FontFace::Body, sf, subRoom), {x0 + titleW + ui.s(18), cy}, sf, th.text, Align::Left, FontFace::Body, VAlign::Middle);
            ui.label(modeShown, {x1, cy}, mf, mc, Align::Right, FontFace::Bold, VAlign::Middle);
            y += lineH(22);
        }

        // The gap (2.91): its reference and unit, then the curve, which is the timeline (A's
        // checkpoints dashed and named, B's on A's clock under the curve; a click or a drag goes
        // anywhere).
        y += ui.s(2);
        drawGapHeader({x0, y, w, gapHeaderH()});
        y += gapHeaderH() + ui.s(6);
        const double end = std::max(0.001, endTime());
        const double shownT = dragging_ ? dragAt_ : clock();
        replaygap::ChartSpec spec;
        const GapSample cursorSample = gapNow(shownT);
        spec.curve = curveReady_ ? &curve_ : nullptr;
        spec.distance = gapDistance_;
        spec.compare = comparing_;
        spec.end = end;
        spec.cursor = shownT;
        spec.cursorSample = curveReady_ ? &cursorSample : nullptr;
        spec.message = gapNote(true);
        spec.chrono = formatRunMs(std::llround(shownT * 1000.0)) + " / " + formatRunMs(std::llround(end * 1000.0));
        spec.chronoColor = kColorA;
        for (size_t i = 0; i < splitsA_.size(); ++i)
            spec.checkpoints.push_back({static_cast<double>(splitsA_[i]) / 1000.0,
                                        checkpointName(trackA_.poseAt(static_cast<float>(splitsA_[i]) / 1000.0f).feet, static_cast<int>(i))});
        spec.topColor = kColorA;
        spec.bottomColor = kColorB;
        spec.windows = measureWindows();  // 2.91: A between the gates of the measure shown
        if (comparing_) {
            for (const auto& row : cmp_.rows())
                if (!row.exit && row.bCumMs >= 0 && row.bCumMs <= std::llround(end * 1000.0)) spec.bottomTicks.push_back(static_cast<double>(row.bCumMs) / 1000.0);
            spec.bottomTicks.push_back(static_cast<double>(cmp_.b().durationMs + cmp_.offsetMs()) / 1000.0);
            spec.topTicks.push_back(aEnd());
        }
        const Rect chartArea{x0, y, w, chartH() + replaygap::axisHeight(ui)};
        const replaygap::ChartOut co = replaygap::drawChart(ui, chartArea, spec);
        const Rect grab = co.plot.expanded(ui.s(4));
        if (ui.acceptsInput() && in.mousePressed(SDL_BUTTON_LEFT) && grab.contains(in.mousePos())) dragging_ = true;
        if (dragging_) {
            dragAt_ = std::clamp(static_cast<double>((in.mousePos().x - co.plot.x) / co.plot.w), 0.0, 1.0) * end;
            // The trajectory follows at once; an exact replay jumps when the button is let go
            // (going back means simulating the run again from its start).
            if (!playback_) t_ = dragAt_;
            if (!in.mouseDown(SDL_BUTTON_LEFT)) {
                dragging_ = false;
                seekTo(dragAt_);
            }
        }
        y = chartArea.bottom() + ui.s(8);

        // Buttons.
        drawButtons(x0, y, w);
        y += buttonH() + ui.s(10);

        // Inputs of the step and the shortcuts.
        float rowH = std::max(ui.s(26), px(13) * 1.7f);
        float used = drawInputs({x0, y, w, rowH});
        drawShortcuts({x0 + used + ui.s(24), y, w - used - ui.s(24), rowH});
    }

    /// What the gap compares, in words: the header's note (`forChart` false) or, when there is
    /// no curve to draw in the unit chosen, why (in the chart).
    std::string gapNote(bool forChart) const {
        if (comparing_) {
            if (!curveReady_) return "Calcul de l'écart…";
            if (forChart) return gapDistance_ && !curve_.continuous ? "Trajet du niveau inconnu : la distance ne se mesure pas" : "";
            std::string n = cmp_.offsetMs() == 0 ? "sans décalage de B" : "décalage de B : " + formatDeltaMs(cmp_.offsetMs()) + ", compris dans la courbe";
            if (gapDistance_ && curve_.continuous) n += " · 1 m = 1 tuile (16 px)";
            return n;
        }
        if (gapKind_ == replaygap::RefKind::Other && !otherRef_) return "Choisissez l'autre replay : « Autre replay… »";
        const GapRefSlot& s = gapRefs_[static_cast<size_t>(gapKind_)];
        if (!routeReady_ || !s.ready || !runAReady_) return "Calcul de l'écart…";
        if (!s.res.ok) return s.res.why;
        if (forChart) return gapDistance_ && curveReady_ && !curve_.continuous ? "Aux points de passage seulement : pas de distance entre eux" : "";
        std::string n;
        if (gapKind_ == replaygap::RefKind::SumOfBest) {
            if (curve_.continuous) n = "chaque meilleur segment lu sur son replay";
            else if (s.res.withReplay <= 1)
                n = std::to_string(s.res.withReplay) + " segment sur " + std::to_string(s.res.segments) + " a son replay : comparé aux points de passage seulement";
            else
                n = std::to_string(s.res.withReplay) + " segments sur " + std::to_string(s.res.segments) + " ont leur replay : comparé aux points de passage seulement";
        } else if (gapKind_ == replaygap::RefKind::Record && s.res.sameRun) {
            n = "ce replay est votre record : écart nul";
        } else if (gapKind_ == replaygap::RefKind::Tas) {
            if (const tas::TasRun* t = viewerTas()) n = s.res.name + " · v" + std::to_string(t->levelVersion) + " · lu sur son replay, en continu";
            else n = s.res.name;
        } else if (gapKind_ == replaygap::RefKind::Other) {
            n = "référence : " + s.res.name;
        } else if (curveReady_ && !curve_.continuous) {
            n = "trajet du niveau inconnu : points de passage seulement";
        }
        if (gapDistance_ && curveReady_ && curve_.continuous) n += n.empty() ? "1 m = 1 tuile (16 px)" : " · 1 m = 1 tuile (16 px)";
        return n;
    }

    /// « ÉCART À LA RÉFÉRENCE » [Record (PB) | Somme des meilleurs | Autre replay…] [En temps |
    /// En distance], what is compared, and the colours' legend: measured, the buttons' font
    /// shrunk first, then the legend and the note left out when there is no room.
    void drawGapHeader(const Rect& row) {
        UIContext& ui = g_.ui;
        const UITheme& th = ui.theme();
        struct Seg {
            const char* id;
            std::string label;
            bool on;
        };
        std::vector<Seg> refs, units;
        if (!comparing_)
        {
            refs = {{"pb", "Record (PB)", gapKind_ == replaygap::RefKind::Record},
                    {"sob", "Somme des meilleurs", gapKind_ == replaygap::RefKind::SumOfBest}};
            if (viewerTas()) refs.push_back({"tas", "TAS", gapKind_ == replaygap::RefKind::Tas});
            refs.push_back({"other", "Autre replay…", gapKind_ == replaygap::RefKind::Other});
        }
        units = {{"time", "En temps", !gapDistance_}, {"dist", "En distance", gapDistance_}};
        const std::string kicker = comparing_ ? "ÉCART A − B" : "ÉCART À LA RÉFÉRENCE";
        const float kf = fz(ui, 14), pad = ui.s(22), segGap = ui.s(3), groupGap = ui.s(14);
        float fs = fz(ui, 15);
        auto segW = [&](const Seg& s) { return ui.text().measure(s.label, FontFace::Bold, ui.s(fs)).x + pad; };
        auto groupW = [&](const std::vector<Seg>& g) {
            float t = 0.0f;
            for (const Seg& s : g) t += segW(s) + segGap;
            return g.empty() ? 0.0f : t - segGap;
        };
        float kw = ui.text().measure(kicker, FontFace::Bold, ui.s(kf)).x;
        auto need = [&] { return (kw > 0.0f ? kw + groupGap : 0.0f) + groupW(refs) + (refs.empty() ? 0.0f : groupGap) + groupW(units); };
        if (need() > row.w) {
            const float fixed = (kw > 0.0f ? kw + groupGap : 0.0f) + (refs.empty() ? 0.0f : groupGap) + pad * static_cast<float>(refs.size() + units.size());
            const float textNow = need() - fixed;
            fs = std::max(11.5f / ui.s(1.0f), fs * std::max(0.1f, (row.w - fixed) / std::max(1.0f, textNow)));
        }
        if (need() > row.w) kw = 0.0f;  // the kicker goes last
        float x = row.x;
        const float cy = row.center().y;
        if (kw > 0.0f) {
            ui.label(kicker, {x, cy}, kf, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
            x += kw + groupGap;
        }
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = fs;
        std::string pressed;
        ui.pushId("gap");
        for (const std::vector<Seg>* g : {&refs, &units}) {
            if (g->empty()) continue;
            for (const Seg& s : *g) {
                const float bw = segW(s);
                st.selected = s.on;
                if (ui.button(s.id, {x, row.y, bw, row.h}, s.label, st)) pressed = s.id;
                if (std::string(s.id) == "tas")
                    ui.r().drawRoundedRect({x, row.y, bw, row.h}, ui.s(6), s.on ? tasui::kTas : tasui::kTasLine, std::max(1.0f, ui.s(s.on ? 2.0f : 1.2f)));
                x += bw + segGap;
            }
            x += groupGap - segGap;
        }
        ui.popId();
        // The legend at the right, then what is compared between the buttons and it.
        const float lf = fz(ui, 13), sw = ui.s(14);
        struct Leg {
            Color c;
            std::string text;
        };
        std::vector<Leg> legend = {{replaygap::kBehind, comparing_ ? "A en retard" : "en retard"}, {replaygap::kAhead, comparing_ ? "A en avance" : "en avance"}};
        if (const MeasurePair* mp = measurePair(); mp && mp->complete() && !measureWindows().empty())
            legend.push_back({replaygap::kWindow, "entre A" + std::to_string(mp->id) + " et B" + std::to_string(mp->id)});
        float legendW = 0.0f;
        for (const Leg& l : legend) legendW += sw + ui.s(6) + ui.text().measure(l.text, FontFace::Body, ui.s(lf)).x + ui.s(14);
        float right = row.right();
        if (right - x >= legendW + ui.s(140)) {
            float lx = right - legendW + ui.s(14);
            for (const Leg& l : legend) {
                ui.r().fillRoundedRect({lx, cy - sw * 0.35f, sw, sw * 0.7f}, ui.s(2), l.c.withAlpha(170));
                lx += sw + ui.s(6);
                lx += ui.label(l.text, {lx, cy}, lf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle).x + ui.s(14);
            }
            right -= legendW + ui.s(8);
        }
        const std::string note = gapNote(false);
        const GapRefSlot& slot = gapRefs_[static_cast<size_t>(gapKind_)];
        const bool warn = !comparing_ && slot.ready && !slot.res.ok;
        if (!note.empty() && right - x >= ui.s(60)) {
            const float nf = ui.fitFont(note, FontFace::Body, fz(ui, 14), right - x, 0.85f);
            ui.label(ui.elide(note, FontFace::Body, nf, right - x), {x, cy}, nf, warn ? th.warm : th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        }
        if (pressed == "pb") selectGapRef(replaygap::RefKind::Record);
        else if (pressed == "sob") selectGapRef(replaygap::RefKind::SumOfBest);
        else if (pressed == "tas") selectGapRef(replaygap::RefKind::Tas);
        else if (pressed == "other") {
            if (gapKind_ != replaygap::RefKind::Other && otherRef_) selectGapRef(replaygap::RefKind::Other);
            else openOtherReference();
        } else if (pressed == "time") gapDistance_ = false;
        else if (pressed == "dist") gapDistance_ = true;
    }

    void drawButtons(float x0, float y, float w) {
        UIContext& ui = g_.ui;
        struct Btn {
            const char* id;
            std::string label;
            int priority;  // what goes first when there is no room
            bool primary = false;
        };
        std::vector<Btn> left = {{"start", "Début", 1},           {"prevcp", "« Point", 3},  {"prevfr", "‹ Image", 4},
                                 {"play", playing_ ? "Pause" : "Lecture", 9, true}, {"nextfr", "Image ›", 4},
                                 {"nextcp", "Point »", 3},         {"back5", "−5 s", 0},      {"fwd5", "+5 s", 0},
                                 {"speed", std::string("Vitesse ") + kSpeedLabels[static_cast<size_t>(speedIdx_)], 2}};
        std::vector<Btn> right;
        if (playback_ && playback_->desynced()) right.push_back({"trace", "Voir la trajectoire", 8});
        if (viewerTas()) right.push_back({"comparetas", "Comparer avec le TAS", 7});
        right.push_back({"info", showInfo_ ? "Masquer les infos" : "Afficher les infos", 6});
        right.push_back({"compare", comparing_ ? "Changer A / B…" : "Comparer…", 5});
        right.push_back({"close", "Retour", 9});
        const float fs = fz(ui, 18), gap = ui.s(8), h = buttonH();
        auto natural = [&](const Btn& b) { return std::max(ui.s(64), ui.text().measure(b.label, FontFace::Bold, ui.s(fs)).x + ui.s(28)); };
        auto total = [&]() {
            float t = 0.0f;
            for (const auto& b : left) t += natural(b) + gap;
            for (const auto& b : right) t += natural(b) + gap;
            return t + ui.s(24);
        };
        // Too narrow: the least useful ones go (their shortcuts stay).
        for (int p = 0; p < 9 && total() > w; ++p) {
            left.erase(std::remove_if(left.begin(), left.end(), [&](const Btn& b) { return b.priority == p; }), left.end());
            right.erase(std::remove_if(right.begin(), right.end(), [&](const Btn& b) { return b.priority == p; }), right.end());
        }
        ui.pushId("replay");
        UIContext::ButtonStyle st;
        st.small = true;
        st.fontSize = fs;
        float x = x0;
        std::string pressed;
        for (const auto& b : left) {
            float bw = natural(b);
            UIContext::ButtonStyle s2 = st;
            s2.primary = b.primary;
            if (ui.button(b.id, {x, y, bw, h}, b.label, s2)) pressed = b.id;
            x += bw + gap;
        }
        float rx = x0 + w;
        for (auto it = right.rbegin(); it != right.rend(); ++it) {
            float bw = natural(*it);
            rx -= bw;
            if (ui.button(it->id, {rx, y, bw, h}, it->label, st)) pressed = it->id;
            if (std::string(it->id) == "comparetas") ui.r().drawRoundedRect({rx, y, bw, h}, ui.s(8), tasui::kTas, std::max(1.0f, ui.s(1.5f)));
            if (std::string(it->id) == "info") ui.newBadge(it->id, {rx, y, bw, h}, "replay.info_toggle");  // « NOUVEAU » (2.30.1)
            rx -= gap;
        }
        ui.popId();
        if (pressed == "start") seekTo(0.0);
        else if (pressed == "prevcp") jumpPoint(-1);
        else if (pressed == "prevfr") stepFrame(-1);
        else if (pressed == "play") togglePlay();
        else if (pressed == "nextfr") stepFrame(1);
        else if (pressed == "nextcp") jumpPoint(1);
        else if (pressed == "back5") seekRelative(-5.0);
        else if (pressed == "fwd5") seekRelative(5.0);
        else if (pressed == "speed") speedIdx_ = (speedIdx_ + 1) % static_cast<int>(kSpeeds.size());
        else if (pressed == "trace") switchToTrace();
        else if (pressed == "comparetas") compareWithTas();
        else if (pressed == "compare") openCompare();
        else if (pressed == "info") toggleInfo();
        else if (pressed == "close") closeRequested_ = true;
    }

    /// Inputs of the current step, like a speedrun input display. Returns the width used.
    float drawInputs(const Rect& area) {
        UIContext& ui = g_.ui;
        const InputFrame* in = playback_ ? playback_->currentInput() : nullptr;
        float fs = fz(ui, 13);
        if (!playback_) {
            std::string t = "Entrées : non enregistrées dans ce mode";
            ui.label(t, {area.x, area.center().y}, fs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            return ui.text().measure(t, FontFace::Body, ui.s(fs)).x;
        }
        struct Key {
            Action a;
            std::string label;
        };
        std::vector<Key> keys = {{Action::MoveLeft, "←"}, {Action::MoveRight, "→"}, {Action::MoveUp, "↑"}, {Action::MoveDown, "↓"},
                                 {Action::Jump, "Saut"},  {Action::Attack, "Tir"}};
        if (a_.replay && a_.replay->keysPerSlot) {
            // Since 2.24: the keys of the power slots, P1 to P4 (up to P8 when the level
            // lets the player carry more powers).
            keys.push_back({Action::Interact, "Inter."});
            int slots = level_.powerSlots() <= 0 ? kPowerSlotKeys : std::clamp(level_.powerSlots(), 4, kPowerSlotKeys);
            for (int s = 0; s < slots; ++s) keys.push_back({powerSlotAction(s), "P" + std::to_string(s + 1)});
        } else {
            // A replay recorded before: one button per power.
            for (const Key& k : {Key{Action::Dash, "Dash"}, Key{Action::Wave, "Onde"}, Key{Action::Shield, "Boucl."}, Key{Action::Impulse, "Imp."},
                                 Key{Action::Teleport, "Télép."}, Key{Action::Interact, "Inter."}, Key{Action::Special, "Spéc."}})
                keys.push_back(k);
        }
        float x = area.x, h = area.h;
        std::string lead = "Entrées";
        ui.label(lead, {x, area.center().y}, fs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        x += ui.text().measure(lead, FontFace::Body, ui.s(fs)).x + ui.s(8);
        for (const auto& k : keys) {
            bool on = in && ((in->held >> static_cast<int>(k.a)) & 1u);
            float kw = std::max(h, ui.text().measure(k.label, FontFace::Bold, ui.s(fs)).x + ui.s(12));
            if (x + kw > area.right()) break;
            Rect b{x, area.y, kw, h};
            ui.r().fillRoundedRect(b, ui.s(5), on ? ui.theme().accent.withAlpha(220) : Color{20, 30, 56, 200});
            ui.label(k.label, b.center(), fs, on ? Color{6, 12, 24, 255} : ui.theme().textDim, Align::Center, FontFace::Bold, VAlign::Middle);
            x += kw + ui.s(4);
        }
        return x - area.x;
    }

    /// The shortcuts of the device in use, with its glyphs (keycaps, gamepad icons): as
    /// many as fit, the most useful first.
    void drawShortcuts(const Rect& area) {
        UIContext& ui = g_.ui;
        if (area.w <= ui.s(40)) return;
        const InputManager& in = g_.app.input();
        bool pad = in.lastDevice() == InputDevice::Gamepad && in.hasGamepad();
        struct Glyph {
            std::string key;  // keycap label (keyboard)
            PadBinding pb;    // gamepad
            Action action = Action::Count;  // or a menu action (its binding, rebindable)
        };
        struct Hint {
            std::vector<Glyph> glyphs;
            std::string text;
        };
        using B = PadBinding;
        std::vector<Hint> hints;
        // 2.93: every glyph is the binding the player chose (Paramètres › Contrôles › « Replays »).
        auto act = [](Action a) { return Glyph{"", {}, a}; };
        if (pad) {
            // The two ways of one stick (±5 s by default): its glyph once.
            const PadBinding sb = in.bindings().padOf(Action::ReplayBack), sf = in.bindings().padOf(Action::ReplayForward);
            const bool oneStick = sb.kind != B::Kind::Button && sf.kind != B::Kind::Button && sb.kind != B::Kind::None && sb.code == sf.code;
            hints = {{{act(Action::ReplayPlay)}, "lecture"},
                     {{act(Action::ReplayFramePrev), act(Action::ReplayFrameNext)}, "image"},
                     {{act(Action::MenuTabPrev), act(Action::MenuTabNext)}, "point"},
                     {oneStick ? std::vector<Glyph>{act(Action::ReplayForward)} : std::vector<Glyph>{act(Action::ReplayBack), act(Action::ReplayForward)}, "±5 s"}};
            if (comparing_) hints.push_back({{act(Action::ReplayCamera)}, "caméra"});
            hints.push_back({{act(Action::ReplayInfo)}, "infos"});
            if (viewerTas()) hints.push_back({{act(Action::ReplayCamera)}, "comparer avec le TAS"});
            if (g_.measures.inReplays) hints.push_back({{act(Action::ReplayInfo), {"", B::button(SDL_GAMEPAD_BUTTON_DPAD_UP)}}, "point de mesure"});
        } else {
            hints = {{{act(Action::ReplayPlay)}, "lecture"},
                     {{act(Action::ReplayFramePrev), act(Action::ReplayFrameNext)}, "image"},
                     {{act(Action::ReplayBack), act(Action::ReplayForward)}, "±5 s"},
                     {{act(Action::MenuTabPrev), act(Action::MenuTabNext)}, "point"},
                     {{act(Action::ReplaySlower), act(Action::ReplayFaster)}, "vitesse"}};
            if (comparing_) {
                hints.push_back({{act(Action::ReplayCamera)}, "caméra"});
                hints.push_back({{act(Action::ReplayAlign)}, "recaler"});
            }
            if (!comparing_) hints.push_back({{act(Action::ReplayAlign)}, viewerTas() ? "référence (PB, SOB, TAS)" : "référence (PB, SOB)"});
            hints.push_back({{act(Action::ReplayInfo)}, "infos"});
            if (viewerTas()) {
                hints.push_back({{act(Action::ReplayPaths)}, "comparer avec le TAS"});
                hints.push_back({{act(Action::ReplayTasSheet)}, "fiche du TAS"});
            }
            if (g_.measures.inReplays) hints.push_back({{act(Action::MeasurePlace)}, "point de mesure"});
        }
        const float h = std::max(ui.s(22), px(13) * 1.5f);
        const float fs = fz(ui, 13), gap = ui.s(4), sep = ui.s(14);
        GamepadStyle style = in.gamepadStyle();
        const InputBindings& bind = in.bindings();
        auto bound = [&](Action a) {
            // What drawAction shows: the first binding of the device in use.
            Glyph g;
            int i = static_cast<int>(a);
            if (pad) g.pb = bind.pad[i][0].kind != PadBinding::Kind::None ? bind.pad[i][0] : bind.pad[i][1];
            else g.key = keyName(bind.keys[i][0] != SDL_SCANCODE_UNKNOWN ? bind.keys[i][0] : bind.keys[i][1]);
            return g;
        };
        auto glyphW = [&](const Glyph& g0) {
            Glyph gl = g0.action != Action::Count ? bound(g0.action) : g0;
            if (gl.key.empty() && glyphIndex(gl.pb) >= 0) return h;
            std::string label = gl.key.empty() ? padBindingName(gl.pb, style) : gl.key;
            return std::max(h, ui.text().measure(label, FontFace::Bold, h * 0.55f).x + h * 0.6f);
        };
        auto hintW = [&](const Hint& hi) {
            float w = 0.0f;
            for (const auto& gl : hi.glyphs) w += glyphW(gl) + gap;
            return w + ui.text().measure(hi.text, FontFace::Body, ui.s(fs)).x;
        };
        size_t n = hints.size();
        auto total = [&](size_t k) {
            float w = 0.0f;
            for (size_t i = 0; i < k; ++i) w += hintW(hints[i]) + (i > 0 ? sep : 0.0f);
            return w;
        };
        while (n > 0 && total(n) > area.w) --n;
        float x = area.right() - total(n), cy = area.center().y;
        for (size_t i = 0; i < n; ++i) {
            for (const auto& g0 : hints[i].glyphs) {
                Glyph gl = g0.action != Action::Count ? bound(g0.action) : g0;
                float w = gl.key.empty() ? Glyphs::drawPadButton(ui, gl.pb, {x, cy}, h) : Glyphs::drawKeycap(ui, gl.key, {x, cy}, h);
                x += w + gap;
            }
            Vec2 sz = ui.label(hints[i].text, {x, cy}, fs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
            x += sz.x + sep;
        }
    }

    // ------------------------------------------------------------ input
    /// Shortcuts of the player (keyboard and gamepad), besides the buttons (mouse, focus).
    /// « Masquer / Afficher les infos »: the side panel out of the way, the run framed again
    /// on the whole width (the camera follows at once).
    void toggleInfo() {
        showInfo_ = !showInfo_;
        snapCam_ = true;
        g_.uiSound("ui_confirm");
    }

    void handleShortcuts() {
        UIContext& ui = g_.ui;
        InputManager& in = g_.app.input();
        float dt = g_.app.frameTime() / 1000.0f;
        if (closeRequested_ || (ui.backPressed() && !editedThisFrame_)) {
            g_.app.popScene();
            return;
        }
        editedThisFrame_ = false;
        if (!ui.acceptsInput() || ui.editingText() || !loaded()) return;
        // 2.93: the player's keys and buttons (Paramètres › Contrôles › « Replays », docs/CONTROLES.md);
        // a key the system repeats while it is held repeats its shortcut, as before.
        auto key = [&](Action a) { return in.keyPressedFor(a, true); };
        if (key(Action::ReplayPlay) || in.padPressed(Action::ReplayPlay)) togglePlay();
        if (key(Action::ReplayBack)) seekRelative(-5.0);
        if (key(Action::ReplayForward)) seekRelative(5.0);
        if (key(Action::ReplayFramePrev)) stepFrame(-1);
        if (key(Action::ReplayFrameNext)) stepFrame(1);
        if (in.commandPressed(Action::ReplayStart)) seekTo(0.0);
        if (in.commandPressed(Action::ReplayEnd)) seekTo(endTime());
        if (in.menuPressed(Action::MenuTabPrev)) jumpPoint(-1);
        if (in.menuPressed(Action::MenuTabNext)) jumpPoint(1);
        if (in.commandPressed(Action::ReplaySlower)) speedIdx_ = std::max(0, speedIdx_ - 1);
        if (in.commandPressed(Action::ReplayFaster)) speedIdx_ = std::min(static_cast<int>(kSpeeds.size()) - 1, speedIdx_ + 1);
        if (key(Action::ReplayInfo)) toggleInfo();
        if (in.commandPressed(Action::MeasurePlace)) placeMeasure();  // 2.91: a measure point, as in training
        if (comparing_) {
            if (in.commandPressed(Action::ReplayCamera)) cycleCamera();
            if (in.commandPressed(Action::ReplayPaths)) showPaths_ = !showPaths_;
            if (in.commandPressed(Action::ReplayAlign)) alignOn(alignRow_);
        } else {
            // 2.94: outside a comparison the same keys serve the TAS (docs/CONTROLES.md): « Recaler »
            // changes the reference of the gap (PB, SOB, TAS), « Caméra » / « Trajectoires » compare
            // with the TAS, and « Fiche du TAS » opens its sheet.
            if (in.commandPressed(Action::ReplayAlign)) cycleGapRef();
            if (const tas::TasRun* t = viewerTas()) {
                if (in.commandPressed(Action::ReplayCamera) || in.commandPressed(Action::ReplayPaths)) {
                    compareWithTas();
                    return;
                }
                if (in.commandPressed(Action::ReplayTasSheet)) {
                    tasui::openSheet(g_, t->levelId, t->category);
                    return;
                }
            }
        }
        // Gamepad: X play / pause, triggers step by step (held: repeated), right stick ±5 s,
        // Y the camera (2.93: the buttons the player chose).
        if (!in.hasGamepad()) return;
        auto edge = [&](bool now, bool& prev) {
            bool e = now && !prev;
            prev = now;
            return e;
        };
        // Vue alone shows / hides the infos (when let go); held with ↑, a measure point (2.91, as
        // in training; ↑ then moves nothing between the buttons).
        {
            const bool back = in.padHeld(Action::ReplayInfo);
            if (back && !padBack_) backChord_ = false;
            if (back) {
                ui.consumeNavigation(false, true);
                if (edge(in.rawPadButton(SDL_GAMEPAD_BUTTON_DPAD_UP), padUp_)) {
                    backChord_ = true;
                    placeMeasure();
                }
            } else {
                padUp_ = false;
                if (padBack_ && !backChord_) toggleInfo();
            }
            padBack_ = back;
        }
        auto repeat = [&](bool held, float& timer, float first, float next) {
            if (!held) {
                timer = -1.0f;
                return false;
            }
            if (timer < 0.0f) {
                timer = first;
                return true;
            }
            timer -= dt;
            if (timer <= 0.0f) {
                timer = next;
                return true;
            }
            return false;
        };
        if (repeat(in.padHeld(Action::ReplayFramePrev), ltTimer_, 0.35f, 0.05f)) stepFrame(-1);
        if (repeat(in.padHeld(Action::ReplayFrameNext), rtTimer_, 0.35f, 0.05f)) stepFrame(1);
        if (repeat(in.padHeld(Action::ReplayBack), rsLeft_, 0.45f, 0.35f)) seekRelative(-5.0);
        if (repeat(in.padHeld(Action::ReplayForward), rsRight_, 0.45f, 0.35f)) seekRelative(5.0);
    }

    void cycleCamera() {
        camMode_ = (camMode_ + 1) % 3;
        snapCam_ = true;
    }

    /// Switches to the recorded trajectory (after a desynchronisation).
    void switchToTrace() {
        double at = clock();
        forceTrace_ = true;
        startA();
        t_ = std::clamp(at, 0.0, endTime());
        playing_ = true;
    }

    void openCompare() {
        ReplayCompareRequest req;
        req.levelId = level_.id;
        req.current = a_.replay;
        req.currentMeta = a_.meta;
        if (req.currentMeta.label.empty()) req.currentMeta.label = playerName_;
        if (comparing_) {
            // The same B offered first again (when it is one of the sources).
            if (!b_.meta.recordId.empty()) req.preselect = {"current", b_.meta.recordId};
            else if (isMine(b_.meta)) req.preselect = {"current", b_.meta.source};
        }
        auto alive = std::weak_ptr<int>(life_);
        req.onChosen = [this, alive](ReplayChoice a, ReplayChoice b) {
            if (!alive.lock()) return;
            applyChoice(std::move(a), std::move(b));
        };
        playing_ = false;
        g_.app.pushScene(makeReplayCompareSetup(g_, std::move(req)));
    }

    GameServices& g_;
    std::string recordId_;
    bool local_ = false;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    std::string status_;
    std::string adminNote_;     // 2.75: « Vu en tant qu'admin (profil masqué) » (the server's x-admin-view)
    std::string recordStatus_;  // 2.75.3: an administrator's record not verified (PENDING, REJECTED); "" otherwise
    bool failed_ = false;
    std::string levelId_;
    ReplayChoice a_, b_;
    LevelData level_;
    PowerKeyMap powerKeys_;  // the keys of the level's powers, for the HUD (2.31)
    bool powerKeysReady_ = false;
    std::string rankedHash_, gameplayHash_;
    bool mounted_ = false;
    std::unique_ptr<ReplayPlayback> playback_;
    std::unique_ptr<GameWorld> traceWorld_;
    uint32_t gen_ = 0;
    bool forceTrace_ = false;
    bool desyncNoticed_ = false;
    GameRenderer renderer_;
    bool rendererBuilt_ = false;
    Hud hud_;
    GhostTrack trackA_;
    std::vector<int64_t> splitsA_;
    bool splitsExactA_ = false;
    std::vector<Checkpoint> checkpoints_;
    std::future<LevelProgressTrack> routeFuture_;
    LevelProgressTrack route_;
    bool routeReady_ = false;
    std::vector<std::pair<std::string, Vec2>> routeCheckpoints_;  // the level's (id, feet), in the route's order
    // Gap to a reference (2.91).
    struct GapRefSlot {
        bool started = false, ready = false;
        std::future<replaygap::LoadedRef> job;
        replaygap::LoadedRef res;
    };
    replaygap::RefKind gapKind_ = replaygap::RefKind::Record;
    bool gapKindChosen_ = false;
    bool gapDistance_ = false;  // the curve in metres
    std::array<GapRefSlot, replaygap::kRefKinds> gapRefs_;
    bool watchingTas_ = false;    // the replay watched is the TAS itself (makeTasReplayViewer)
    double startAt_ = -1.0;       // where the playback starts (the sheet's route clicked)
    bool refOffscreen_ = false;   // the reference's ghost is out of the view (the live counter says so)
    std::shared_ptr<const ReplayData> otherRef_;  // « Autre replay… »
    GhostMeta otherMeta_;
    std::future<RouteRun> runAJob_, runBJob_;
    RouteRun runA_, runB_;
    std::shared_ptr<const ReplayData> runAOf_, runBOf_;  // the replays runA_ / runB_ are made from
    bool runAReady_ = false, runBReady_ = false;
    GapCurve curve_;
    GapReference curveRef_;
    GapRun curveRun_;
    bool curveReady_ = false, curveDirty_ = true;
    int64_t curveOffset_ = 0;
    Rect liveRect_, collectRect_;  // this frame's live counter and collectibles (the HUD and the tags keep clear of them)
    LiveText liveText_;
    // Measure points (2.91).
    struct PairPasses {
        std::vector<MeasurePass> a, b, pb;  // on the recorded trajectories (own clocks)
    };
    std::string measureKey_;
    std::vector<MeasurePair> measurePairs_;
    int measureSel_ = 0;
    std::map<int, PairPasses> measurePasses_;            // by pair id
    std::map<int, MeasurePassTimer> exactTimers_;        // A on the simulation, by pair id
    std::map<int, std::vector<MeasurePass>> exactPasses_;  // A's passes timed so (1/120 s)
    std::shared_ptr<const ReplayData> measuredA_, measuredB_, measuredPb_;
    bool measuredValid_ = false;
    bool fedValid_ = false;
    uint32_t fedTick_ = 0, fedGen_ = 0;
    bool backChord_ = false;  // Vue held: ↑ placed a gate (Vue alone shows / hides the infos on release)
    Animator animRef_;             // the viewer's reference ghost
    // Comparison.
    bool comparing_ = false;
    ReplayComparison cmp_;
    int alignRow_ = 0;
    std::string offsetText_;
    int camMode_ = 0;  // 0 A, 1 B, 2 both
    int teamFollow_ = -1;  // 2.62, a team replay: -1 the team, else that player
    bool showPaths_ = true;  // comparison: both trajectories (a replay alone shows its player)
    bool showInfo_ = true;   // the side panel (splits / comparison); hidden: the run on the whole width
    std::vector<Rect> hudRects_;  // HUD drawn this frame (vitals, powers, boss gauge, progress): the tags avoid them
    // Clock and playback.
    double t_ = 0.0;      // trace mode: A's run time
    double extra_ = 0.0;  // exact mode: time after A's finish (comparison)
    double seekAfterEnd_ = -1.0;
    float acc_ = 0.0f;
    bool playing_ = false;
    int speedIdx_ = 2;
    bool dragging_ = false;
    double dragAt_ = 0.0;
    // Display.
    Animator animA_, animB_;
    Vec2 camPos_;
    float camScale_ = 0.0f;
    bool snapCam_ = true;
    Layout layout_;
    std::string playerName_, categoryText_;
    int64_t timeMs_ = 0;
    int rank_ = 0;
    std::string msg_;
    float msgTimer_ = 0.0f;
    bool closeRequested_ = false;
    bool editedThisFrame_ = false;
    bool padBack_ = false, padUp_ = false;
    float ltTimer_ = -1.0f, rtTimer_ = -1.0f, rsLeft_ = -1.0f, rsRight_ = -1.0f;
};

}  // namespace

std::unique_ptr<Scene> makeReplayViewer(GameServices& g, const std::string& recordId) { return std::make_unique<ReplayScene>(g, recordId); }

std::unique_ptr<Scene> makeLocalReplayViewer(GameServices& g, ReplayData replay, const std::string& title, const std::string& adminNote) {
    return std::make_unique<ReplayScene>(g, std::move(replay), title, adminNote);
}

std::unique_ptr<Scene> makeTasReplayViewer(GameServices& g, ReplayData replay, const tas::TasRun& t, double at) {
    auto s = std::make_unique<ReplayScene>(g, std::move(replay), "TAS · " + std::string(categoryLabel(t.category)) + " · meilleur temps trouvé par la machine");
    s->watchTas(t, at);
    return s;
}

std::unique_ptr<Scene> makeReplayCompare(GameServices& g, const std::string& levelId, ReplayChoice a, ReplayChoice b) {
    return std::make_unique<ReplayScene>(g, levelId, std::move(a), std::move(b));
}

}  // namespace cosmic
