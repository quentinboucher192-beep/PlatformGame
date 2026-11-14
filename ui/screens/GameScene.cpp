#include "ui/screens/GameScene.h"

#include <cmath>
#include <format>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/GameServices.h"
#include "game/enemies/Enemy.h"
#include "game/levels/LevelAnalyzer.h"
#include "game/online/LevelFingerprint.h"
#include "ui/screens/PauseScene.h"
#include "ui/screens/PowerChoiceScene.h"
#include "ui/screens/ResultsScene.h"

namespace cosmic {

std::unique_ptr<Scene> makeGameScene(GameServices& g, const std::string& levelId, PlayOptions opt) {
    return std::make_unique<GameScene>(g, levelId, std::move(opt));
}

GameScene::GameScene(GameServices& g, std::string levelId, PlayOptions opt)
    : g_(g), levelId_(std::move(levelId)), opt_(std::move(opt)) {}

GameScene::~GameScene() = default;

void GameScene::onEnter() {
    ResourceManager& res = g_.app.resources();
    if (opt_.level) {
        level_ = *opt_.level;
        levelId_ = level_.id;
    } else if (auto lvl = g_.content.loadLevel(levelId_, &loadError_)) {
        level_ = *lvl;
    } else {
        loadFailed_ = true;
        Log::error("Cannot load level {}: {}", levelId_, loadError_);
        return;
    }
    workshop_ = !opt_.editorTest && g_.content.campaignPlanetOf(levelId_).empty();
    std::string root = opt_.assetRoot;
    if (root.empty())
        if (const LevelEntry* e = g_.content.level(levelId_)) root = Paths::toUtf8(e->assetRoot);
    if (!root.empty()) {
        res.pushMount(Paths::fromUtf8(root));
        mounted_ = true;
    }
    WorldServices ws{res, g_.app.audio(), g_.content, &g_.app.input(), g_.settings.graphics.screenShake,
                     g_.settings.graphics.particleQuality == 0 ? 0.4f : g_.settings.graphics.particleQuality == 1 ? 0.7f : 1.0f};
    world_ = std::make_unique<GameWorld>(ws, WorldMode::Play);
    world_->load(level_);
    // Same randomness for every player of this level (and in its replays).
    world_->seedGameplay(levelSeed(levelId_));
    if (opt_.spawn) world_->startAt(*opt_.spawn);
    if (const LevelRecord* rec = g_.records.get(levelId_)) world_->setReferenceSplits(rec->bestSplits);
    renderer_.build(g_.app.renderer(), res, *world_);
    if (g_.settings.gameplay.progressBar > 0)
        trackFuture_ = std::async(std::launch::async, [level = level_, &content = g_.content] { return buildProgressTrack(level, content); });
    if (!opt_.editorTest) {
        g_.records.registerAttempt(levelId_);
        if (!workshop_) {
            g_.progression.lastLevel = levelId_;
            g_.progression.lastPlanet = level_.planet;
        }
    }
    g_.app.input().consumePlayerInput();  // drop the menu's pending presses
    if (!g_.inputScript.empty()) script_.parse(g_.inputScript);
    // Cosmic Online: official levels and community levels played from their start can be
    // ranked; a player level finished in an editor test (from its start) can be published.
    if (!opt_.spawn) {
        bool community = !opt_.editorTest && !opt_.level && g_.online.communityInstall(levelId_) != nullptr;
        if (opt_.editorTest || community) {
            gameplayHash_ = fingerprintGameplay(g_.content, level_);
            attemptId_ = makeUuidV4();
            recorder_.begin(levelId_, gameplayHash_, attemptId_, world_->gameplaySeed());
            recordMode_ = community ? RecordMode::Community : RecordMode::EditorCompletion;
        } else if (!opt_.level) {
            if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, levelId_)) {
                fingerprint_ = *fp;
                attemptId_ = makeUuidV4();
                recorder_.begin(levelId_, fp->hash, attemptId_, world_->gameplaySeed());
                recordMode_ = RecordMode::Official;
            }
        }
        if (recordMode_ == RecordMode::None && !opt_.level) {
            // Any other level played from its start: recorded for the ghosts only.
            gameplayHash_ = fingerprintGameplay(g_.content, level_);
            attemptId_ = makeUuidV4();
            recorder_.begin(levelId_, gameplayHash_, attemptId_, world_->gameplaySeed());
            recordMode_ = RecordMode::Local;
        }
        if (level_.extreme && level_.oneLife() && (recordMode_ == RecordMode::Official || recordMode_ == RecordMode::Community)) {
            raceTitle_ = level_.name;
            if (recordMode_ == RecordMode::Official) {
                raceKind_ = "official";
                raceLevelId_ = levelId_;
                raceHash_ = fingerprint_->hash;
            } else if (const Json* c = g_.online.communityInstall(levelId_)) {
                raceKind_ = "community";
                raceLevelId_ = jstr(*c, "id");
                raceVersion_ = jget<int>(*c, "version", 0);
                if (!jstr(*c, "title").empty()) raceTitle_ = jstr(*c, "title");
            }
            // The route is the one declared online (same computation), off the main thread.
            race_.begin(buildRaceLayout(level_, g_.content, false));
            raceRouteFuture_ = std::async(std::launch::async, [level = level_, &content = g_.content] { return raceRoute(intendedRoute(level, content)); });
        }
    }
    world_->eventObserver = [this](const std::string& src, const std::string& trig) {
        if (recordMode_ != RecordMode::None) recorder_.onWorldEvent(src, trig);
        bossFights_.onWorldEvent(src, trig);
    };
    // Ghost Racing: the hash of this exact version of the level (the one its replays carry).
    if (!opt_.editorTest && !opt_.level) {
        if (recordMode_ == RecordMode::Official && fingerprint_) ghostHash_ = fingerprint_->hash;
        else if (!gameplayHash_.empty()) ghostHash_ = gameplayHash_;
        else if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, levelId_)) ghostHash_ = fp->hash;
        else ghostHash_ = fingerprintGameplay(g_.content, level_);
        startGhosts();
    }
}

void GameScene::startGhosts() {
    const GhostSettings& gs = g_.settings.ghosts;
    std::vector<GhostEntry> local = g_.ghosts.localGhosts(levelId_);
    const GhostEntry* pb = nullptr;
    for (const GhostEntry& e : local)
        if (e.meta.source == "pb" && e.meta.levelHash == ghostHash_) pb = &e;
    if (pb) pbGhostMs_ = pb->meta.timeMs;
    std::vector<GhostEntry> choices;
    if (opt_.ghostsChosen) choices = opt_.ghosts;
    else if (gs.enabled && gs.personalByDefault && pb) choices.push_back(*pb);
    if (choices.empty()) return;
    ghostRun_.begin(std::move(choices), level_, g_.content, ghostHash_, gs, opt_.training);
    if (pb) {
        std::string key = GhostStore::levelKey(levelId_, ghostHash_);
        ghostRun_.setPersonal(g_.segments.bestSegments(key, pb->meta.splits.size()), pb->meta.splits,
                              static_cast<float>(pb->meta.timeMs) / 1000.0f);
    }
    ghostRun_.setVisible(gs.enabled || opt_.ghostsChosen);
}

void GameScene::saveGhosts(const RunResult& r) {
    if (!lastReplay_ || ghostHash_.empty() || lastReplay_->levelHash != ghostHash_) return;
    GhostMeta m;
    m.source = "last";
    m.label = g_.online.loggedIn() && !g_.online.username().empty() ? g_.online.username() : "Moi";
    m.playerId = g_.online.loggedIn() ? g_.online.playerId() : std::string{};
    m.levelId = levelId_;
    m.levelHash = ghostHash_;
    m.timeMs = static_cast<int64_t>(std::llround(static_cast<double>(r.time) * 1000.0));
    m.date = r.date;
    m.splits = r.splits;
    m.community = recordMode_ == RecordMode::Community;
    m.gameVersion = COSMIC_VERSION_STRING;
    if (fingerprint_) m.levelVersion = fingerprint_->version;
    bool newBest = pbGhostMs_ <= 0 || m.timeMs < pbGhostMs_;
    g_.ghosts.recordRun(*lastReplay_, m, newBest);
    g_.segments.add(GhostStore::levelKey(levelId_, ghostHash_), r.splits, r.time, r.date);
    g_.segments.save();
}

void GameScene::onExit() {
    if (mounted_) g_.app.resources().popMount();
    mounted_ = false;
    g_.app.audio().stopAmbience(600);
    g_.app.audio().setMusicDuck(1.0f);
    if (!opt_.editorTest) {
        g_.progression.addPlayTime(opt_.level ? std::string{} : levelId_, playTime_);
        // Online profile: official levels by id, community levels by their online id.
        if (!opt_.level) {
            std::string key = "other";
            if (const Json* c = g_.online.communityInstall(levelId_)) key = jstr(*c, "id");
            else if (const LevelEntry* e = g_.content.level(levelId_); e && e->source == ContentSource::Builtin) key = levelId_;
            g_.online.notePlaytime(key, playTime_);
        }
        g_.progression.save();
        g_.records.save();
        g_.bossStats.save();
    }
}

void GameScene::onResume() {
    g_.app.audio().setMusicDuck(1.0f);
    g_.app.input().consumePlayerInput();
}

void GameScene::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_RENDER_TARGETS_RESET || e.type == SDL_EVENT_RENDER_DEVICE_RESET) {
        if (world_) renderer_.rebuildTerrain(g_.app.renderer(), g_.app.resources(), *world_);
    }
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST && world_ && world_->state() == WorldState::Playing && !ended_ && !g_.app.headless()) {
        g_.app.pushScene(std::make_unique<PauseScene>(g_, *this));
    }
}

void GameScene::renderMapView(const Rect& dst, const Rect& win) {
    if (!world_ || win.w <= 0.0f || win.h <= 0.0f) return;
    Renderer& r = g_.app.renderer();
    WorldView v;
    v.scale = std::min(dst.w / win.w, dst.h / win.h);
    v.screenW = static_cast<int>(dst.w);
    v.screenH = static_cast<int>(dst.h);
    v.topLeft = {win.x - dst.x / v.scale, win.y - dst.y / v.scale};
    v.origin = dst.pos();
    RenderOptions opt;
    opt.lighting = false;
    opt.mapView = true;
    r.pushClip(dst);
    renderer_.render(r, g_.app.resources(), *world_, v, 1.0f, opt);
    r.setView(WorldView{});
    r.popClip();
}

void GameScene::sendRaceAttempt(bool finished, std::vector<uint8_t> replay) {
    if (!race_.active() || raceKind_.empty() || replay.empty()) return;
    const RaceProgress& p = race_.progress();
    OnlineService::RaceAttempt a;
    a.kind = raceKind_;
    a.levelId = raceLevelId_;
    a.levelHash = raceHash_;
    a.version = raceVersion_;
    a.title = raceTitle_;
    a.attemptId = attemptId_;
    a.timeMs = static_cast<int64_t>(std::llround(static_cast<double>(world_->timer().elapsed()) * 1000.0));
    a.finished = finished;
    for (float b : p.bossRemoved) a.bosses.push_back(static_cast<int>(std::clamp(std::lround(b * 1000.0f), 0L, 1000L)));
    a.waves = p.wavesDone;
    a.estimate = raceScore(race_.layout(), p);
    // Nothing done (left at once): not an attempt worth ranking.
    if (!finished && a.estimate < 0.1f) return;
    g_.online.submitRaceAttempt(std::move(a), std::move(replay));
}

void GameScene::abandonRaceAttempt() {
    // Restarting or leaving during an EXTRÊME attempt still ranks how far it went.
    if (ended_ || !race_.active() || !recorder_.active() || !world_) return;
    ended_ = true;
    recorder_.finishAttempt(*world_, world_->timer().elapsed());
    ReplayData replay = recorder_.take();
    sendRaceAttempt(false, replay.encode());
}

void GameScene::restart() {
    abandonRaceAttempt();
    PlayOptions o = opt_;
    if (!o.level && !opt_.editorTest) o.level.reset();
    g_.app.replaceTop(makeGameScene(g_, levelId_, o), 0.3f);
}

void GameScene::quitToMenu() {
    abandonRaceAttempt();
    if (opt_.editorTest) {
        g_.app.popScene();
        return;
    }
    if (opt_.fromOnline) {
        if (const Json* c = g_.online.communityInstall(levelId_)) g_.app.switchScene(makeOnlineLevels(g_, jstr(*c, "id")), 0.4f);
        else g_.app.switchScene(makeOnlineRanking(g_, levelId_), 0.4f);
        return;
    }
    if (workshop_) {
        g_.app.switchScene(makeContentManager(g_), 0.4f);
        return;
    }
    std::string planet = level_.planet.empty() ? "sylvanox" : level_.planet;
    g_.app.switchScene(makeLevelSelect(g_, planet), 0.4f);
}

std::string GameScene::nextLevelId() const {
    if (opt_.editorTest || opt_.fromOnline || workshop_) return {};
    auto list = g_.content.levelsForPlanet(level_.planet);
    for (size_t i = 0; i + 1 < list.size(); ++i)
        if (list[i]->id == levelId_ && g_.progression.levelUnlocked(*list[i + 1], g_.content)) return list[i + 1]->id;
    return {};
}

void GameScene::fixedUpdate(float dt) {
    if (!world_) return;
    // A power offer waits for the player's answer (PowerChoiceScene): nothing is simulated
    // or recorded until it comes. Scripted runs leave the power unless told otherwise.
    uint8_t choice = 0;
    if (world_->powerOffer()) {
        choice = pendingChoice_;
        if (!choice && script_.active()) choice = script_.choice() ? script_.choice() : InputFrame::kChoiceLeave;
        if (!choice) return;
        pendingChoice_ = 0;
    }
    PlayerInput in = g_.app.input().consumePlayerInput();
    if (script_.active()) in = script_.next();
    // The simulation only ever sees the quantised frame, exactly what a replay stores.
    InputFrame frame = InputFrame::fromPlayerInput(in);
    // Hold confirm to skip a cinematic.
    if (world_->cinematic().active() && world_->cinematic().skippable()) {
        bool held = g_.app.input().held(Action::MenuConfirm) || g_.app.input().held(Action::Jump);
        skipHold_ = held ? skipHold_ + dt / 0.8f : std::max(0.0f, skipHold_ - dt * 2.0f);
        if (skipHold_ >= 1.0f) {
            skipHold_ = 0.0f;
            g_.app.input().consumePlayerInput();
            frame = InputFrame{};
            frame.flags = InputFrame::SkipCinematic;
        }
    } else {
        skipHold_ = 0.0f;
    }
    hud_.setSkipHold(skipHold_);
    frame.choice = choice;
    if (!ended_) recorder_.recordInput(frame);
    stepWorld(*world_, frame, kSimDt);
    if (!ended_) recorder_.tick(*world_);
    if (!ended_ && !opt_.editorTest) {
        bossFights_.step(*world_, g_.bossStats);
        bossFeedback();
    }
    if (!ended_ && race_.active()) {
        race_.update(*world_);
        recorder_.raceProgress(race_.progress().bossRemoved, race_.progress().wavesDone);
    }
    playTime_ += dt;
    if (!ended_ && world_->state() == WorldState::Completed) finishRun(true);
    if (!ended_ && world_->state() == WorldState::Failed) finishRun(false);
}

void GameScene::drawBossArrow(const WorldView& view) {
    Enemy* boss = world_->activeBoss();
    if (!boss || !boss->alive() || world_->cinematic().active()) return;
    UIContext& ui = g_.ui;
    const float W = ui.screenW(), H = ui.screenH();
    Rect onScreen = view.toScreen(boss->box);
    if (onScreen.intersects({0, 0, W, H})) return;
    auto tex = g_.app.resources().texture("ui/boss_arrow.png");
    if (!tex) return;
    // Along the line from the screen's centre to the boss, stopped by a frame that keeps
    // clear of the HUD (hearts and progress bar above, boss bar below).
    Rect frame{ui.s(56), ui.s(150), W - ui.s(112), H - ui.s(150) - ui.s(130)};
    Vec2 c = frame.center();
    Vec2 d = onScreen.center() - c;
    if (d.lengthSq() < 1.0f) return;
    float tx = std::abs(d.x) > 1e-3f ? (frame.w * 0.5f) / std::abs(d.x) : 1e9f;
    float ty = std::abs(d.y) > 1e-3f ? (frame.h * 0.5f) / std::abs(d.y) : 1e9f;
    Vec2 p = c + d * std::min(tx, ty);
    // Never over the ghosts' comparison box (under the timer): slid along the border below it.
    if (const Rect& gb = ghostRun_.hudRect(); gb.w > 0.0f && Rect::fromCenter(p, {ui.s(100), ui.s(100)}).intersects(gb))
        p.y = std::min(frame.bottom(), gb.bottom() + ui.s(56));
    float angle = std::atan2(d.y, d.x) * 57.29578f;
    // Blinks (bright 0.32 s, dim 0.18 s) with an additive halo while bright: seen at once.
    bool bright = std::fmod(ui.time(), 0.5f) < 0.32f;
    float pulse = 1.0f + 0.1f * std::sin(ui.time() * 12.0f);
    float size = ui.s(84) * (bright ? pulse : 0.92f);
    Rect dst = Rect::fromCenter(p, {size, size});
    ui.r().drawTexture(*tex, nullptr, dst, Color::white().withAlphaF(bright ? 1.0f : 0.45f), BlendMode::Alpha, false, false, angle);
    if (bright) {
        Rect halo = Rect::fromCenter(p, {size * 1.35f, size * 1.35f});
        ui.r().drawTexture(*tex, nullptr, halo, Color{255, 150, 150, 255}.withAlphaF(0.55f), BlendMode::Additive, false, false, angle);
        ui.r().drawTexture(*tex, nullptr, dst, Color::white().withAlphaF(0.5f), BlendMode::Additive, false, false, angle);
    }
}

void GameScene::bossFeedback() {
    for (const auto& e : bossFights_.takeEvents()) {
        if (e.kind == BossFightTracker::Event::Kind::PhaseNoHit) {
            g_.uiSound("boss_phase_clear", 0.8f);
            std::string ph = e.phaseName.empty() ? "Phase " + std::to_string(e.phase + 1) : e.phaseName;
            world_->hint(ph + " sans dégât !", 2.2f);
        } else if (e.noHit) {
            g_.uiSound("boss_phase_clear", 1.0f);
            world_->hint(e.bossName + " vaincu sans dégât !", 3.0f);
        }
    }
}

void GameScene::finishRun(bool completed) {
    ended_ = true;
    if (!opt_.editorTest) {
        bossFights_.step(*world_, g_.bossStats);
        g_.bossStats.save();
    }
    RunResult r;
    r.levelId = levelId_;
    r.completed = completed;
    r.time = world_->timer().elapsed();
    const RunStats& s = world_->stats();
    r.deaths = s.deaths;
    r.collectibles = s.collectibles();
    r.totalCollectibles = s.totalCollectibles();
    r.relics = s.relics;
    r.secrets = s.secrets;
    r.totalSecrets = s.totalSecrets;
    r.kills = s.kills;
    r.score = RecordsManager::computeScore(level_, s, r.time, completed);
    r.medal = RecordsManager::evaluateMedal(level_, r);
    r.powersUsed.assign(s.powersUsed.begin(), s.powersUsed.end());
    r.splits = world_->timer().splits();
    r.date = FileUtil::nowString();
    // A campaign record belongs to this version of the level (notifications drop it if the
    // level changes later).
    if (!workshop_ && !opt_.editorTest) r.levelHash = fingerprintGameplay(g_.content, level_);
    const LevelRecord* before = g_.records.get(levelId_);
    LevelRecord previous = before ? *before : LevelRecord{};
    RecordUpdate upd;
    if (!opt_.editorTest) {
        upd = g_.records.submit(r);
        g_.records.save();
        if (completed) {
            g_.progression.markCompleted(levelId_);
            g_.progression.save();
        }
    }
    std::optional<OnlineAttempt> online;
    std::vector<uint8_t> raceReplay;  // the same replay, for the EXTRÊME race
    if (completed && fingerprint_ && recorder_.active()) {
        recorder_.finish(*world_, r.time);
        ReplayData replay = recorder_.take();
        lastReplay_ = std::make_shared<ReplayData>(replay);
        if (!g_.saveReplayPath.empty()) {
            auto bytes = replay.encode();
            if (FileUtil::writeBinary(Paths::fromUtf8(g_.saveReplayPath), bytes))
                Log::info("Replay enregistré : {} ({} octets)", g_.saveReplayPath, bytes.size());
        }
        if (race_.active()) raceReplay = replay.encode();
        RunCandidate run;
        run.levelId = levelId_;
        run.levelVersion = fingerprint_->version;
        run.levelHash = fingerprint_->hash;
        run.timeMs = static_cast<int64_t>(std::llround(static_cast<double>(r.time) * 1000.0));
        run.deaths = s.deaths;
        run.damageTaken = s.damageTaken;
        run.collectibles = s.collectibles();
        run.totalCollectibles = s.totalCollectibles();
        run.kills = s.kills;
        run.score = r.score;
        run.fromStart = true;
        if (const OnlineAttempt* a = g_.online.recordRun(run, attemptId_, &replay)) online = *a;
    } else if (completed && recorder_.active() && recordMode_ != RecordMode::None && recordMode_ != RecordMode::Official) {
        recorder_.finish(*world_, r.time);
        ReplayData replay = recorder_.take();
        lastReplay_ = std::make_shared<ReplayData>(replay);
        std::vector<uint8_t> bytes = replay.encode();
        int64_t timeMs = static_cast<int64_t>(std::llround(static_cast<double>(r.time) * 1000.0));
        if (race_.active()) raceReplay = bytes;
        if (recordMode_ == RecordMode::Community) {
            g_.online.submitCommunityRun(levelId_, timeMs, s.deaths, attemptId_, std::move(bytes));
        } else if (const LevelEntry* e = g_.content.level(levelId_);
                   recordMode_ == RecordMode::EditorCompletion && e && e->source == ContentSource::User) {
            // Proof that this version of the level can be finished: required to publish it.
            fs::path base = Paths::userLevelsDir() / FileUtil::slugify(levelId_);
            fs::path rp = base;
            rp += ".completion.cprz";
            fs::path sp = base;
            sp += ".completion.json";
            Json side = {{"format", "cosmic.completion"}, {"gameplay_hash", gameplayHash_}, {"time_ms", timeMs},
                         {"deaths", s.deaths}, {"date", FileUtil::nowString()}};
            if (FileUtil::writeBinary(rp, bytes) && FileUtil::writeTextAtomic(sp, side.dump(1)))
                g_.online.notify(OnlineService::Notification::Kind::Success, "Fin de niveau enregistrée",
                                 "Cette version peut être publiée : WORKSHOP → Mes niveaux → Publier en ligne.");
        }
    } else if (!completed && race_.active() && recorder_.active()) {
        // EXTRÊME: a failed attempt is ranked by how far it went.
        recorder_.finishAttempt(*world_, r.time);
        ReplayData replay = recorder_.take();
        lastReplay_ = std::make_shared<ReplayData>(replay);
        raceReplay = replay.encode();
        if (!g_.saveReplayPath.empty() && FileUtil::writeBinary(Paths::fromUtf8(g_.saveReplayPath), raceReplay))
            Log::info("Replay enregistré : {} ({} octets)", g_.saveReplayPath, raceReplay.size());
    } else {
        recorder_.abort();
    }
    // Ghost Racing: the run against the ghosts ends; this run becomes my last ghost (and my
    // best one if faster), its segments join the history.
    if (completed) ghostRun_.finish(r.time);
    if (completed && !opt_.editorTest && !opt_.training) saveGhosts(r);
    std::optional<float> raceEstimate;
    if (race_.active()) {
        raceEstimate = raceScore(race_.layout(), race_.progress());
        sendRaceAttempt(completed, std::move(raceReplay));
    }
    if (completed) {
        std::string victory = g_.app.resources().soundPath("music/victory.ogg");
        if (!victory.empty()) g_.app.audio().playSound(victory, 0.9f, 1.0f, 0.0f, AudioBus::Music);
    }
    ResultsData data;
    data.run = r;
    data.update = upd;
    data.previous = previous;
    data.level = level_;
    data.failReason = world_->failReason();
    data.nextLevel = nextLevelId();
    data.editorTest = opt_.editorTest;
    data.replay = lastReplay_;
    data.fromOnline = opt_.fromOnline;
    data.fromWorkshop = workshop_ && !opt_.fromOnline;
    data.online = online;
    data.raceScore = raceEstimate;
    data.bossKills = bossFights_.kills();
    if (race_.active()) {
        data.raceBosses = race_.progress().bossesBeaten();
        data.raceBossTotal = static_cast<int>(race_.layout().bosses.size());
        data.raceKind = raceKind_;
        data.raceLevelId = raceLevelId_;
        data.raceVersion = raceVersion_;
        data.raceHash = raceHash_;
    }
    g_.app.pushScene(std::make_unique<ResultsScene>(g_, *this, std::move(data)));
}

void GameScene::update(float dt) {
    if (loadFailed_) {
        g_.ui.begin(g_.app, dt);
        if (g_.ui.backPressed() || g_.app.input().menuPressed(Action::MenuConfirm)) quitToMenu();
        return;
    }
    if (!world_) return;
    world_->update(dt);
    // The pause map: what the camera has shown (display only, outside the simulation).
    if (explored_.cols == 0) explored_.reset(world_->level().bounds);
    explored_.mark(world_->camera().visibleArea(1920, 1080));
    if (trackFuture_.valid() && trackFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) hud_.setProgressTrack(trackFuture_.get());
    if (raceRouteFuture_.valid() && raceRouteFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) race_.setRoute(raceRouteFuture_.get());
    hud_.update(*world_, dt);
    InputManager& in = g_.app.input();
    if (ghostRun_.active()) {
        ghostRun_.update(*world_, dt);
        // Quick toggle (G / left stick click): display only, never part of the run.
        if (in.menuPressed(Action::GhostToggle)) {
            ghostRun_.setVisible(!ghostRun_.visible());
            world_->hint(ghostRun_.visible() ? "Fantômes affichés" : "Fantômes masqués", 1.4f);
        }
    }
    // A power tablet with every slot taken: ask which power to keep (live play only).
    if (!ended_ && world_->powerOffer() && !pendingChoice_ && !script_.active()) {
        g_.app.pushScene(std::make_unique<PowerChoiceScene>(g_, *this));
        return;
    }
    if (!opt_.forceBossAttack.empty())
        if (Enemy* b = world_->activeBoss(); b && b->alive() && (b->state() == EnemyState::Chase || b->state() == EnemyState::Alert ||
                                                                 b->state() == EnemyState::Idle || b->state() == EnemyState::Patrol))
            b->forceAttack(*world_, opt_.forceBossAttack);
    bool autoPause = opt_.openPause && world_->state() == WorldState::Playing &&
                     (opt_.pauseAfter <= 0.0f || world_->timer().elapsed() >= opt_.pauseAfter);
    if (autoPause) opt_.openPause = false;
    if (!ended_ && (in.menuPressed(Action::Pause) || autoPause)) {
        if (world_->state() == WorldState::Playing || world_->state() == WorldState::Intro) {
            g_.app.pushScene(std::make_unique<PauseScene>(g_, *this));
            return;
        }
    }
    // Quick restart (contre-la-montre): R key or the gamepad View/Back button.
    if (!ended_ && (in.keyPressed(SDL_SCANCODE_R) || in.rawPadButton(SDL_GAMEPAD_BUTTON_BACK)) &&
        world_->state() == WorldState::Playing && level_.timerMode == TimerMode::TimeAttack) {
        restart();
    }
}

void GameScene::render(float alpha) {
    Renderer& r = g_.app.renderer();
    g_.ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
    if (loadFailed_ || !world_) {
        r.fillRect({0, 0, static_cast<float>(r.width()), static_cast<float>(r.height())}, {8, 10, 20, 255});
        g_.ui.label("Impossible de charger le niveau", {g_.ui.screenW() * 0.5f, g_.ui.screenH() * 0.42f}, 40,
                    g_.ui.theme().danger, Align::Center, FontFace::Title, VAlign::Middle);
        g_.ui.label(loadError_, {g_.ui.screenW() * 0.5f, g_.ui.screenH() * 0.5f}, 24, g_.ui.theme().text, Align::Center,
                    FontFace::Body, VAlign::Middle);
        return;
    }
    WorldView view = world_->camera().view(r.width(), r.height(), alpha);
    RenderOptions opt;
    opt.lighting = g_.settings.graphics.dynamicLighting;
    opt.debug = g_.app.debugOverlay;
    renderer_.render(r, g_.app.resources(), *world_, view, alpha, opt);
    if (ghostRun_.active()) ghostRun_.drawWorld(r, view, world_->player().anim.set());
    hud_.render(g_.ui, *world_, view, g_.records.get(levelId_), g_.settings.gameplay);
    if (ghostRun_.active() && !world_->cinematic().active() && world_->state() != WorldState::Intro) {
        const GameplaySettings& gs = g_.settings.gameplay;
        Rect keepOut[kHudElements];
        int n = 0;
        for (int e = 0; e < kHudElements; ++e) {
            auto el = static_cast<HudElement>(e);
            if (el != HudElement::Splits && hudVisible(gs, el)) keepOut[n++] = hud_.area(g_.ui, *world_, el, hud_.place(g_.ui, *world_, gs, el));
        }
        Hud::Place sp = hud_.place(g_.ui, *world_, gs, HudElement::Splits);
        ghostRun_.drawHud(g_.ui, view, hud_.area(g_.ui, *world_, HudElement::Splits, sp), sp.k, hudVisible(gs, HudElement::Splits), keepOut, n);
    }
    drawBossArrow(view);
    if (g_.settings.graphics.showFps) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f FPS", g_.app.fps());
        g_.ui.label(buf, {g_.ui.s(12), g_.ui.screenH() - g_.ui.s(30)}, 18, g_.ui.theme().textDim);
    }
}

void GameScene::renderDebug() {
    if (!world_) return;
    UIContext& ui = g_.ui;
    const Player& p = world_->player();
    int alive = 0;
    for (const auto& e : world_->enemies()) alive += e->alive() ? 1 : 0;
    std::vector<std::string> lines = {
        std::format("FPS {:.0f}  dt {:.2f} ms  draws {}", g_.app.fps(), g_.app.frameTime(), g_.app.renderer().drawCallsLastFrame()),
        std::format("Joueur ({:.1f}, {:.1f})  vit ({:.0f}, {:.0f})  {}", p.box.x, p.box.y, p.vel.x, p.vel.y,
                    playerStateName(p.state())),
        std::format("PV {}  énergie {:.0f}  sol {}", p.health, p.energy, p.grounded() ? "oui" : "non"),
        std::format("Chrono {} ({})  état {}", LevelTimer::format(world_->timer().elapsed()),
                    timerModeLabel(world_->timer().mode()), static_cast<int>(world_->state())),
        std::format("Particules {}  lumières {}  ennemis {}  projectiles {}", world_->particles().count(),
                    renderer_.lastLightCount(), alive, world_->projectiles().size()),
        std::format("Entités {}  colliders {}  chunks {}", world_->entities().size(),
                    world_->physics().colliders().size(), renderer_.terrain().chunkCount()),
    };
    for (const auto& e : world_->enemies())
        if (e->alive() && distance(e->center(), p.center()) < 300.0f) lines.push_back("  " + e->debugInfo());
    float y = ui.screenH() * 0.30f;
    ui.r().fillRect({ui.s(10), y - ui.s(8), ui.s(720), ui.s(26) * lines.size() + ui.s(16)}, {0, 0, 0, 170});
    for (const auto& l : lines) {
        ui.label(l, {ui.s(20), y}, 19, {180, 255, 220, 255});
        y += ui.s(26);
    }
}

}  // namespace cosmic
