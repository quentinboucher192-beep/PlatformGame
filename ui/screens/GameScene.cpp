#include "ui/screens/GameScene.h"

#include <cmath>
#include <format>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/GameServices.h"
#include "game/enemies/Enemy.h"
#include "ui/screens/PauseScene.h"
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
    if (opt_.spawn) world_->startAt(*opt_.spawn);
    if (const LevelRecord* rec = g_.records.get(levelId_)) world_->setReferenceSplits(rec->bestSplits);
    renderer_.build(g_.app.renderer(), res, *world_);
    if (!opt_.editorTest) {
        g_.records.registerAttempt(levelId_);
        g_.progression.lastLevel = levelId_;
        g_.progression.lastPlanet = level_.planet;
    }
    g_.app.input().consumePlayerInput();  // drop the menu's pending presses
    if (!g_.inputScript.empty()) script_.parse(g_.inputScript);
    // Cosmic Online: only official levels played from their start can be ranked.
    if (!opt_.editorTest && !opt_.level && !opt_.spawn) {
        if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, levelId_)) {
            fingerprint_ = *fp;
            attemptId_ = makeUuidV4();
            recorder_.begin(levelId_, fp->hash, attemptId_);
            world_->eventObserver = [this](const std::string& src, const std::string& trig) { recorder_.onWorldEvent(src, trig); };
        }
    }
}

void GameScene::onExit() {
    if (mounted_) g_.app.resources().popMount();
    mounted_ = false;
    g_.app.audio().stopAmbience(600);
    g_.app.audio().setMusicDuck(1.0f);
    if (!opt_.editorTest) {
        g_.progression.playTimeSeconds += playTime_;
        g_.progression.save();
        g_.records.save();
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
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST && world_ && world_->state() == WorldState::Playing && !ended_) {
        g_.app.pushScene(std::make_unique<PauseScene>(g_, *this));
    }
}

void GameScene::restart() {
    PlayOptions o = opt_;
    if (!o.level && !opt_.editorTest) o.level.reset();
    g_.app.replaceTop(makeGameScene(g_, levelId_, o), 0.3f);
}

void GameScene::quitToMenu() {
    if (opt_.editorTest) {
        g_.app.popScene();
        return;
    }
    std::string planet = level_.planet.empty() ? "sylvanox" : level_.planet;
    g_.app.switchScene(makeLevelSelect(g_, planet), 0.4f);
}

std::string GameScene::nextLevelId() const {
    if (opt_.editorTest) return {};
    auto list = g_.content.levelsForPlanet(level_.planet);
    for (size_t i = 0; i + 1 < list.size(); ++i)
        if (list[i]->id == levelId_ && g_.progression.levelUnlocked(*list[i + 1], g_.content)) return list[i + 1]->id;
    return {};
}

void GameScene::fixedUpdate(float dt) {
    if (!world_) return;
    PlayerInput in = g_.app.input().consumePlayerInput();
    if (script_.active()) in = script_.next();
    // Hold confirm to skip a cinematic.
    if (world_->cinematic().active() && world_->cinematic().skippable()) {
        bool held = g_.app.input().held(Action::MenuConfirm) || g_.app.input().held(Action::Jump);
        skipHold_ = held ? skipHold_ + dt / 0.8f : std::max(0.0f, skipHold_ - dt * 2.0f);
        if (skipHold_ >= 1.0f) {
            world_->cinematic().skip(*world_);
            skipHold_ = 0.0f;
            g_.app.input().consumePlayerInput();
            in = PlayerInput{};
        }
    } else {
        skipHold_ = 0.0f;
    }
    hud_.setSkipHold(skipHold_);
    world_->fixedUpdate(in, dt);
    if (!ended_) recorder_.tick(*world_);
    playTime_ += dt;
    if (!ended_ && world_->state() == WorldState::Completed) finishRun(true);
    if (!ended_ && world_->state() == WorldState::Failed) finishRun(false);
}

void GameScene::finishRun(bool completed) {
    ended_ = true;
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
    if (completed && fingerprint_ && recorder_.active()) {
        recorder_.finish(*world_, r.time);
        ReplayData replay = recorder_.take();
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
    } else {
        recorder_.abort();
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
    data.online = online;
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
    hud_.update(*world_, dt);
    InputManager& in = g_.app.input();
    if (!ended_ && in.menuPressed(Action::Pause)) {
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
    hud_.render(g_.ui, *world_, view, g_.records.get(levelId_), g_.settings.gameplay);
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
