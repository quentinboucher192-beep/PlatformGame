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

#include <algorithm>
#include <array>
#include <cmath>
#include <future>

#include "engine/core/FileUtil.h"
#include "game/GameServices.h"
#include "game/gameplay/GameRenderer.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/gameplay/LevelProgress.h"
#include "game/ghost/Ghost.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/ReplayCompare.h"
#include "game/ghost/ReplayPlayback.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/ReplayCompareNav.h"
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
    /// A replay already in memory (file, local run).
    ReplayScene(GameServices& g, ReplayData replay, std::string title) : g_(g) {
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
            downloadReplay(jstr(rec, "level_id"));
        }), false);
    }

    void onExit() override {
        if (mounted_) g_.app.resources().popMount();
        mounted_ = false;
    }

    void update(float dt) override {
        if (msgTimer_ > 0.0f) msgTimer_ -= dt;
        if (routeFuture_.valid() && routeFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            route_ = routeFuture_.get();
            routeReady_ = true;
            trackA_.mapProgress(route_);
            hud_.setProgressTrack(route_);
            if (comparing_) cmp_.setRoute(route_);
        }
        if (!loaded()) return;
        if (playback_) updateExact(dt);
        else updateTrace(dt);
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
        renderer_.render(r, g_.app.resources(), world(), view, alpha, opt);
        r.setView(view);
        if ((comparing_ && showPaths_) || !playback_) drawPaths(r, view);
        if (!playback_) drawGhost(r, view, animA_, poseA(), kColorA, 0.95f);
        if (comparing_) drawGhost(r, view, animB_, poseB(), kColorB, ghostAlpha());
        r.setView(WorldView{});
        if (playback_) drawGameHud(view);
        if (comparing_) drawCompareProgress();
        if (comparing_) drawTags(view);
        drawSeekOverlay();
        if (comparing_) drawComparePanel(layout_.panel);
        else drawSplitsPanel(layout_.panel);
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

    void downloadReplay(const std::string& levelId) {
        status_ = "Téléchargement du replay…";
        g_.online.getBinary("/api/v1/records/" + recordId_ + "/replay", [this, levelId, alive = std::weak_ptr<int>(life_)](const HttpResponse& resp) {
            if (!alive.lock()) return;
            if (resp.networkFailure()) return fail(std::string("Réseau : ") + netErrorLabel(resp.error));
            if (resp.status != 200) return fail(parseApiResponse(resp).message);
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
        playing_ = true;
        g_.ui.setFocus("replay/play");
        applyAutomation();
    }

    /// Automated screenshots (--replay-view): "at=<s>,offset=<ms>,align=<row>,cam=<n>,pause".
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
        if (!playback_->finished()) return playback_->step();
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
    }

    void updateCamera(float dt) {
        if (layout_.worldArea.w <= 0.0f) return;
        Renderer& r = g_.app.renderer();
        float normal = static_cast<float>(r.height()) / Camera::kViewHeight * std::max(0.5f, level_.cameraZoom);
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
            return world().camera().view(r.width(), r.height(), alpha);
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
    }

    float px(float size) const { return g_.ui.s(fz(g_.ui, size)); }
    float lineH(float size) const { return px(size) * 1.45f; }
    float buttonH() const { return std::max(g_.ui.s(42), px(18) * 2.1f); }
    float stripHeight() const {
        const UIContext& ui = g_.ui;
        return ui.s(10) + lineH(22) + lineH(15) + ui.s(26) + buttonH() + ui.s(10) + std::max(ui.s(26), px(13) * 1.7f) + ui.s(12);
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
        const float W = ui.screenW(), H = ui.screenH();
        const Rect& free = layout_.worldArea;
        const float m = ui.s(10);
        // Vitals and powers at their default places (top left).
        for (HudElement e : {HudElement::Vitals, HudElement::Powers}) hs.hud[e].custom = false;
        Rect vitals = hudVisible(hs, HudElement::Vitals) ? hud_.area(ui, w, HudElement::Vitals, hud_.place(ui, w, hs, HudElement::Vitals)) : Rect{};
        Rect powers = hudVisible(hs, HudElement::Powers) ? hud_.area(ui, w, HudElement::Powers, hud_.place(ui, w, hs, HudElement::Powers)) : Rect{};
        // Collectibles: centred on the free part, right of the vitals if needed.
        {
            HudPlace& hp = hs.hud[HudElement::Collectibles];
            float k = std::clamp(hp.scale, HudLayout::kMinScale, HudLayout::kMaxScale);
            float half = ui.s(153) * k;
            float cx = std::max(free.center().x, vitals.w > 0.0f ? vitals.right() + half + ui.s(16) : 0.0f);
            hp.custom = true;
            hp.x = cx / W;
            hp.y = ui.s(34) / H;
            if (cx + half > free.right() - m) setHudVisible(hs, HudElement::Collectibles, false);
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
        }
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
        float bottomOfHud = ui.s(76);  // the collectibles counter (top centre)
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
            y = ui.s(260) + Hud::progressAbove(ui.s(1), true);
        }
        if (right - left < ui.s(160)) return;
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
        hud_.setProgressRacers({});
    }

    /// "A · Astrid" / "B · Record mondial" above each of them, in the free part, never on
    /// each other (moved under the feet, else left out).
    void drawTags(const WorldView& view) {
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
                return true;
            };
            if (!ok(box)) box.y -= box.h + ui.s(2);
            if (!ok(box)) box.y = foot.y + ui.s(2);
            if (!ok(box)) return;
            placed[n++] = box;
            ui.r().fillRoundedRect(box, ui.s(5), Color{8, 12, 26, 170});
            ui.label(text, box.center(), fs, c, Align::Center, FontFace::Bold, VAlign::Middle);
        };
        tag("A · " + displayName(a_.meta), feetA(), kColorA);
        tag("B · " + displayName(b_.meta), poseB().feet, kColorB);
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
        ui.r().fillRect({x, y, inner, std::max(1.0f, ui.s(1))}, th.accent.withAlpha(60));
        y += ui.s(8);
        ui.label("TEMPS DE PASSAGE", {x, y}, fz(ui, 15), th.textDim, Align::Left, FontFace::Bold);
        y += lineH(15);
        // Columns, measured: point | time | segment.
        const float fs = fz(ui, 15);
        const float numW = ui.text().measure("10:00,000", FontFace::Body, ui.s(fs)).x + ui.s(10);
        float cSeg = x + inner, cTime = cSeg - numW;
        float nameRoom = cTime - numW - x - ui.s(6);
        ui.label("Point", {x, y}, fz(ui, 13), th.textDim);
        ui.label("Temps", {cTime, y}, fz(ui, 13), th.textDim, Align::Right);
        ui.label("Segment", {cSeg, y}, fz(ui, 13), th.textDim, Align::Right);
        y += lineH(13);
        float rh = std::max(ui.s(30), lineH(15) + ui.s(6));
        size_t n = splitsA_.size() + 1;
        Rect list{area.x + ui.s(4), y, area.w - ui.s(8), area.bottom() - pad - y};
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
        }
        ui.endScroll();
        ui.popId();
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
            info += c.meta.validated ? " · vérifié en ligne" : " · local";
            if (!run.splitsExact && !run.splitsMs.empty()) info += " · passages à 1/30 s près";
            ui.label(ui.elide(info, FontFace::Body, fz(ui, 13), right - nx), {nx, y}, fz(ui, 13), th.textDim);
            y += lineH(13) + ui.s(4);
        };
        std::string howA = playback_ ? "rejoué à l'identique" : "trajectoire enregistrée";
        if (!playback_ && !installedVersion(a_.replay->levelHash)) howA += " (autre version)";
        else if (!playback_ && a_.replay->exact() && forceTrace_) howA += " (désynchronisé)";
        side("A", a_, cmp_.a(), kColorA, howA);
        side("B", b_, cmp_.b(), kColorB, "fantôme, trajectoire enregistrée");

        // Live gap.
        y += ui.s(2);
        auto live = cmp_.liveGapMs(clock());
        ui.label("Écart A − B", {x, y}, fz(ui, 14), th.textDim);
        std::string gapText = live ? formatDeltaMs(*live) : "…";
        Color gc = !live ? th.textDim : *live > 0 ? th.danger : *live < 0 ? th.success : th.text;
        float gf = ui.fitFont(gapText, FontFace::Title, fz(ui, 28), inner * 0.55f, 0.6f);
        ui.label(gapText, {right, y}, gf, gc, Align::Right, FontFace::Title);
        const char* word = !live ? "" : *live > 0 ? "A en retard" : *live < 0 ? "A en avance" : "égalité";
        ui.label(word, {x, y + lineH(14)}, fz(ui, 15), gc, Align::Left, FontFace::Bold);
        y += std::max(ui.s(gf) * 1.35f, lineH(14) + lineH(15));
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
        drawCompareTable({area.x, y, area.w, area.bottom() - y - ui.s(6)}, pad);
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

        // Title and integrity.
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
        float tf = fz(ui, 22);
        float modeRoom = w * 0.55f;
        float mf = ui.fitFont(mode, FontFace::Bold, fz(ui, 15), modeRoom, 0.85f);
        std::string modeShown = ui.elide(mode, FontFace::Bold, mf, modeRoom);
        float modeW = ui.text().measure(modeShown, FontFace::Bold, ui.s(mf)).x;
        float cy = y + lineH(22) * 0.5f;
        ui.label(ui.elide(title, FontFace::Title, tf, w - modeW - ui.s(24)), {x0, cy}, tf, kColorA, Align::Left, FontFace::Title, VAlign::Middle);
        ui.label(modeShown, {x1, cy}, mf, mc, Align::Right, FontFace::Bold, VAlign::Middle);
        y += lineH(22);

        // What is shown.
        std::string sub;
        if (comparing_) {
            sub = level_.name + "  ·  A : " + displayName(a_.meta) + "  ·  B : " + displayName(b_.meta) + "  ·  " + formatCompareOffset(cmp_.offsetMs());
        } else {
            sub = level_.name;
            if (!categoryText_.empty()) sub += " · " + categoryText_;
            if (timeMs_ > 0) sub += " · " + formatRunMs(timeMs_);
            if (rank_ > 0) sub += " · n°" + std::to_string(rank_);
        }
        ui.label(ui.elide(sub, FontFace::Body, fz(ui, 15), w), {x0, y + lineH(15) * 0.5f}, fz(ui, 15), th.text, Align::Left, FontFace::Body, VAlign::Middle);
        y += lineH(15);

        // Timeline: A's checkpoints above, B's (offset included) below; drag to go anywhere.
        const double end = std::max(0.001, endTime());
        std::string chrono = formatRunMs(dragging_ ? std::llround(dragAt_ * 1000.0) : clockMs()) + " / " + formatRunMs(std::llround(end * 1000.0));
        float cf = fz(ui, 17);
        float chronoW = ui.text().measure("00:00,000 / 00:00,000", FontFace::Bold, ui.s(cf)).x;
        Rect bar{x0, y + ui.s(8), w - chronoW - ui.s(18), ui.s(10)};
        r.fillRoundedRect(bar, ui.s(5), Color{20, 30, 56, 230});
        auto xAt = [&](double t) { return bar.x + bar.w * static_cast<float>(std::clamp(t / end, 0.0, 1.0)); };
        double shownT = dragging_ ? dragAt_ : clock();
        r.fillRoundedRect({bar.x, bar.y, xAt(shownT) - bar.x, bar.h}, ui.s(5), kColorA.withAlphaF(0.75f));
        for (int64_t ms : splitsA_) {
            float mx = xAt(static_cast<double>(ms) / 1000.0);
            r.fillRect({mx - ui.s(1), bar.y - ui.s(6), std::max(1.0f, ui.s(2)), ui.s(6)}, kColorA);
        }
        if (comparing_) {
            for (const auto& row : cmp_.rows())
                if (!row.exit && row.bCumMs >= 0 && row.bCumMs <= std::llround(end * 1000.0)) {
                    float mx = xAt(static_cast<double>(row.bCumMs) / 1000.0);
                    r.fillRect({mx - ui.s(1), bar.bottom(), std::max(1.0f, ui.s(2)), ui.s(6)}, kColorB);
                }
            float be = xAt(static_cast<double>(cmp_.b().durationMs + cmp_.offsetMs()) / 1000.0);
            r.fillRect({be - ui.s(1.5f), bar.bottom(), std::max(1.0f, ui.s(3)), ui.s(7)}, kColorB);
            float ae = xAt(aEnd());
            r.fillRect({ae - ui.s(1.5f), bar.y - ui.s(7), std::max(1.0f, ui.s(3)), ui.s(7)}, kColorA);
        }
        r.fillCircle({xAt(shownT), bar.center().y}, ui.s(8), Color::white());
        r.fillCircle({xAt(shownT), bar.center().y}, ui.s(5), kColorA);
        ui.label(chrono, {x1, bar.center().y}, cf, kColorA, Align::Right, FontFace::Bold, VAlign::Middle);
        Rect grab = bar.expanded(ui.s(10));
        if (ui.acceptsInput() && in.mousePressed(SDL_BUTTON_LEFT) && grab.contains(in.mousePos())) dragging_ = true;
        if (dragging_) {
            dragAt_ = std::clamp(static_cast<double>((in.mousePos().x - bar.x) / bar.w), 0.0, 1.0) * end;
            // The trajectory follows at once; an exact replay jumps when the button is let go
            // (going back means simulating the run again from its start).
            if (!playback_) t_ = dragAt_;
            if (!in.mouseDown(SDL_BUTTON_LEFT)) {
                dragging_ = false;
                seekTo(dragAt_);
            }
        }
        y = bar.bottom() + ui.s(16);

        // Buttons.
        drawButtons(x0, y, w);
        y += buttonH() + ui.s(10);

        // Inputs of the step and the shortcuts.
        float rowH = std::max(ui.s(26), px(13) * 1.7f);
        float used = drawInputs({x0, y, w, rowH});
        drawShortcuts({x0 + used + ui.s(24), y, w - used - ui.s(24), rowH});
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
        else if (pressed == "compare") openCompare();
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
            const char* label;
        };
        static const Key keys[] = {{Action::MoveLeft, "←"},    {Action::MoveRight, "→"},   {Action::MoveUp, "↑"},       {Action::MoveDown, "↓"},
                                   {Action::Jump, "Saut"},     {Action::Attack, "Tir"},    {Action::Dash, "Dash"},      {Action::Wave, "Onde"},
                                   {Action::Shield, "Boucl."}, {Action::Impulse, "Imp."},  {Action::Teleport, "Télép."}, {Action::Interact, "Inter."},
                                   {Action::Special, "Spéc."}};
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
        if (pad) {
            hints = {{{{"", B::button(SDL_GAMEPAD_BUTTON_WEST)}}, "lecture"},
                     {{{"", B::axisPos(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)}, {"", B::axisPos(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)}}, "image"},
                     {{{"", {}, Action::MenuTabPrev}, {"", {}, Action::MenuTabNext}}, "point"},
                     {{{"", B::axisPos(SDL_GAMEPAD_AXIS_RIGHTX)}}, "±5 s"}};
            if (comparing_) hints.push_back({{{"", B::button(SDL_GAMEPAD_BUTTON_NORTH)}}, "caméra"});
        } else {
            hints = {{{{"K", {}}}, "lecture"}, {{{",", {}}, {".", {}}}, "image"}, {{{"J", {}}, {"L", {}}}, "±5 s"},
                     {{{"", {}, Action::MenuTabPrev}, {"", {}, Action::MenuTabNext}}, "point"}, {{{"−", {}}, {"+", {}}}, "vitesse"}};
            if (comparing_) {
                hints.push_back({{{"C", {}}}, "caméra"});
                hints.push_back({{{"R", {}}}, "recaler"});
            }
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
        auto key = [&](SDL_Keycode k) { return in.shortcutPressed(k); };
        if (key(SDLK_K)) togglePlay();
        if (key(SDLK_J)) seekRelative(-5.0);
        if (key(SDLK_L)) seekRelative(5.0);
        if (key(SDLK_COMMA)) stepFrame(-1);
        if (key(SDLK_PERIOD) || key(SDLK_SEMICOLON)) stepFrame(1);
        if (in.keyPressed(SDL_SCANCODE_HOME)) seekTo(0.0);
        if (in.keyPressed(SDL_SCANCODE_END)) seekTo(endTime());
        if (in.menuPressed(Action::MenuTabPrev)) jumpPoint(-1);
        if (in.menuPressed(Action::MenuTabNext)) jumpPoint(1);
        if (key(SDLK_MINUS) || in.keyPressed(SDL_SCANCODE_KP_MINUS)) speedIdx_ = std::max(0, speedIdx_ - 1);
        if (key(SDLK_PLUS) || key(SDLK_EQUALS) || in.keyPressed(SDL_SCANCODE_KP_PLUS))
            speedIdx_ = std::min(static_cast<int>(kSpeeds.size()) - 1, speedIdx_ + 1);
        if (comparing_) {
            if (key(SDLK_C)) cycleCamera();
            if (key(SDLK_T)) showPaths_ = !showPaths_;
            if (key(SDLK_R)) alignOn(alignRow_);
        }
        // Gamepad: X play / pause, triggers step by step (held: repeated), right stick ±5 s,
        // Y the camera.
        if (!in.hasGamepad()) return;
        auto edge = [&](bool now, bool& prev) {
            bool e = now && !prev;
            prev = now;
            return e;
        };
        if (edge(in.rawPadButton(SDL_GAMEPAD_BUTTON_WEST), padX_)) togglePlay();
        if (edge(in.rawPadButton(SDL_GAMEPAD_BUTTON_NORTH), padY_) && comparing_) cycleCamera();
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
        float thr = std::max(0.3f, in.bindings().gamepad.triggerThreshold);
        if (repeat(in.leftTrigger() > thr, ltTimer_, 0.35f, 0.05f)) stepFrame(-1);
        if (repeat(in.rightTrigger() > thr, rtTimer_, 0.35f, 0.05f)) stepFrame(1);
        float sx = in.rightStick().x;
        if (repeat(sx < -0.6f, rsLeft_, 0.45f, 0.35f)) seekRelative(-5.0);
        if (repeat(sx > 0.6f, rsRight_, 0.45f, 0.35f)) seekRelative(5.0);
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
    bool failed_ = false;
    std::string levelId_;
    ReplayChoice a_, b_;
    LevelData level_;
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
    // Comparison.
    bool comparing_ = false;
    ReplayComparison cmp_;
    int alignRow_ = 0;
    std::string offsetText_;
    int camMode_ = 0;  // 0 A, 1 B, 2 both
    bool showPaths_ = true;  // comparison: both trajectories (a replay alone shows its player)
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
    bool padX_ = false, padY_ = false;
    float ltTimer_ = -1.0f, rtTimer_ = -1.0f, rsLeft_ = -1.0f, rsRight_ = -1.0f;
};

}  // namespace

std::unique_ptr<Scene> makeReplayViewer(GameServices& g, const std::string& recordId) { return std::make_unique<ReplayScene>(g, recordId); }

std::unique_ptr<Scene> makeLocalReplayViewer(GameServices& g, ReplayData replay, const std::string& title) {
    return std::make_unique<ReplayScene>(g, std::move(replay), title);
}

std::unique_ptr<Scene> makeReplayCompare(GameServices& g, const std::string& levelId, ReplayChoice a, ReplayChoice b) {
    return std::make_unique<ReplayScene>(g, levelId, std::move(a), std::move(b));
}

}  // namespace cosmic
