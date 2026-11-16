#include "ui/screens/GameScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/GameServices.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/RunCounters.h"
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
    // 2.45: a challenge keeps the revision of the campaign level it was drawn on: played from
    // the file of that revision kept on this computer (bundled or downloaded) when another
    // revision is current.
    std::optional<LevelData> challengeRevision;
    if (!opt_.level && opt_.challenge && !opt_.challenge->challenge.community()) {
        const std::string& want = opt_.challenge->challenge.level.hash;
        const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, levelId_);
        if (!fp || fp->hash != want)
            if (auto file = g_.campaignRevisions.fileFor(g_.content, levelId_, want))
                if (auto j = FileUtil::loadJson(*file))
                    if ((challengeRevision = LevelData::fromJson(*j))) challengeRevisionHash_ = want;
    }
    if (opt_.level) {
        level_ = *opt_.level;
        levelId_ = level_.id;
    } else if (challengeRevision) {
        level_ = std::move(*challengeRevision);
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
    // 2.24: each power slot has its key (Paramètres › Clavier / souris, Manette).
    world_->setKeysPerSlot(true);
    world_->load(level_);
    applyCosmetics();
    // Same randomness for every player of this level (and in its replays).
    world_->seedGameplay(levelSeed(levelId_));
    // Boss training: set up with the world (start, hearts, powers, phase), marked as training.
    if (opt_.bossTraining) {
        if (!applyBossTraining(*world_, *opt_.bossTraining))
            Log::warn("Entraînement : boss {} introuvable dans {}", opt_.bossTraining->bossId, levelId_);
    } else if (opt_.spawn) {
        world_->startAt(*opt_.spawn);
    }
    // Daily challenge: its rules right after the load (simulation 11), its own recording.
    if (opt_.challenge) startChallenge();
    // 2.48: a hardcore level played as a practice run (respawn where Nova fell, not ranked).
    if (opt_.practiceRespawn && level_.extreme && !opt_.challenge && !opt_.bossTraining) world_->setPracticeRespawn(true);
    // Keys of the powers (2.31): the level's key powers, and those a training or a challenge gives.
    if (opt_.bossTraining) powerKeyExtra_ = opt_.bossTraining->powers;
    if (opt_.challenge) powerKeyExtra_ = opt_.challenge->challenge.rules.powers;
    refreshPowerKeys();
    const bool counted = !world_->training();  // a training (or challenge) run records nothing
    if (opt_.training) startTraining();
    if (const LevelRecord* rec = g_.records.get(levelId_); rec && !opt_.training && counted) world_->setReferenceSplits(rec->bestSplits);
    renderer_.build(g_.app.renderer(), res, *world_);
    if (g_.settings.gameplay.progressBar > 0)
        trackFuture_ = std::async(std::launch::async, [level = level_, &content = g_.content] { return buildProgressTrack(level, content); });
    if (!opt_.editorTest && !opt_.training && counted) {
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
    if (!opt_.spawn && counted) {
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
        if (ended_) return;
        if (trig == "jump" && src == "player") ++runJumps_;
        else if (trig == "powerUsed") ++runPowers_[src];
        else if (trig == "checkpoint") runCheckpoints_.push_back(src);
    };
    // Ghost Racing: the hash of this exact version of the level (the one its replays carry).
    if (!opt_.editorTest && !opt_.level && counted) {
        if (recordMode_ == RecordMode::Official && fingerprint_) ghostHash_ = fingerprint_->hash;
        else if (!gameplayHash_.empty()) ghostHash_ = gameplayHash_;
        else if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, levelId_)) ghostHash_ = fp->hash;
        else ghostHash_ = fingerprintGameplay(g_.content, level_);
        startGhosts();
    }
    startSpectate();
}

void GameScene::startSpectate() {
    // A run of a campaign level, of an installed community level or of a challenge, from its
    // start: never an editor test, a training, a boss training or a level given as data.
    if (opt_.editorTest || opt_.training || opt_.bossTraining || opt_.spawn || opt_.level || !recorder_.active() || !world_) return;
    SpectateHeader h;
    if (recordMode_ == RecordMode::Challenge && opt_.challenge) {
        const DailyChallenge& c = opt_.challenge->challenge;
        if (!challengeSendable_) return;  // another version of the level: a friend could not follow
        h.levelSource = c.level.source;
        h.levelId = c.level.levelId;
        h.levelVersion = c.level.version;
        h.levelHash = challengeHash_;
        h.challengeId = c.id;
    } else if (recordMode_ == RecordMode::Official && fingerprint_) {
        h.levelSource = "campaign";
        h.levelId = levelId_;
        h.levelVersion = fingerprint_->version;
        h.levelHash = fingerprint_->hash;
    } else if (const Json* c = g_.online.communityInstall(levelId_); recordMode_ == RecordMode::Community && c) {
        h.levelSource = "community";
        h.levelId = jstr(*c, "id");
        h.levelVersion = jget<int>(*c, "version", 0);
        h.levelHash = gameplayHash_;
    } else {
        return;
    }
    h.runId = makeUuidV4();  // its own id (never the attempt's, sent with a record)
    h.seed = world_->gameplaySeed();
    h.keysPerSlot = world_->keysPerSlot();
    h.simVersion = kSimulationVersion;
    h.gameVersion = COSMIC_VERSION_STRING;
    spectateToken_ = g_.spectate.begin(std::move(h));
    spectateChecks_ = 0;
}

void GameScene::endSpectate(const char* reason) {
    if (!spectateToken_) return;
    g_.spectate.end(spectateToken_, reason);
    spectateToken_ = 0;
}

void GameScene::startChallenge() {
    const DailyChallenge& c = opt_.challenge->challenge;
    world_->setChallenge(c.rules);
    hud_.setChallengeTiers(static_cast<float>(c.silverMs) / 1000.0f, static_cast<float>(c.goldMs) / 1000.0f, c.goldNoDamage, c.finishObjective);
    // The level as installed: its fingerprint goes with the run (the server compares it with
    // the challenge's); the replay is written in version 4 (the rules after the digests).
    if (!challengeRevisionHash_.empty()) {
        challengeHash_ = challengeRevisionHash_;  // the challenge's revision, checked by fileFor
    } else if (!c.community()) {
        if (const LevelFingerprint* fp = g_.online.fingerprints().get(g_.content, levelId_)) challengeHash_ = fp->hash;
    } else {
        challengeHash_ = fingerprintGameplay(g_.content, level_);
    }
    if (challengeHash_.empty()) challengeHash_ = fingerprintGameplay(g_.content, level_);
    challengeSendable_ = challengeHash_ == c.level.hash;
    Log::info("Défi {}{} : niveau {} (empreinte {}{})", c.id, opt_.challenge->practice ? " (hors classement)" : "", levelId_, challengeHash_,
              challengeSendable_ ? ", celle du défi" : ", attendue " + c.level.hash);
    attemptId_ = makeUuidV4();
    recorder_.begin(levelId_, challengeHash_, attemptId_, world_->gameplaySeed());
    recordMode_ = RecordMode::Challenge;
    // « Plus loin » (2.34): the route of the level (the one declared online, same
    // computation), off the main thread; practice runs rank nothing.
    if (c.progressAvailable && !opt_.challenge->practice) {
        challengeTrack_.begin(buildRaceLayout(level_, g_.content, false));
        challengeRouteFuture_ = std::async(std::launch::async, [level = level_, &content = g_.content] { return raceRoute(intendedRoute(level, content)); });
    }
    // The ghosts chosen before the start (mine, the first of the day): display only.
    if (opt_.ghostsChosen && !opt_.ghosts.empty()) {
        ghostHash_ = challengeHash_;
        ghostRun_.setSuitSheet([this](const GhostMeta& m) -> TexturePtr {
            return g_.suitSheet(m.mine() ? g_.cosmeticLook("suit") : g_.cosmetics.lookOr("suit", m.suit));
        });
        ghostRun_.begin(opt_.ghosts, level_, g_.content, ghostHash_, g_.settings.ghosts, false);
        ghostRun_.setVisible(true);
    }
}

void GameScene::finishChallenge(bool completed) {
    ended_ = true;
    const DailyChallenge& c = opt_.challenge->challenge;
    ChallengeEndData d;
    d.challenge = c;
    d.practice = opt_.challenge->practice;
    d.completed = completed;
    d.failReason = world_->failReason();
    const RunStats& s = world_->stats();
    float time = world_->timer().elapsed();
    d.timeMs = static_cast<int64_t>(std::llround(static_cast<double>(time) * 1000.0));
    d.deaths = s.deaths;
    d.damageTaken = s.damageTaken;
    // « Plus loin »: a fall ends the attempt (already decided at the first death without
    // checkpoints); a finished run counts 100 by itself.
    if (!completed && !d.practice) takeChallengeAttempt(false);
    if (!completed && attemptReport_) {
        d.attempt = attemptReport_;
        d.attemptId = attemptId_;  // its sending state, followed live
    }
    if (completed && recorder_.active()) {
        recorder_.finish(*world_, time);
        lastReplay_ = std::make_shared<ReplayData>(recorder_.take());
        if (!g_.saveReplayPath.empty()) {
            auto bytes = lastReplay_->encode();
            if (FileUtil::writeBinary(Paths::fromUtf8(g_.saveReplayPath), bytes))
                Log::info("Replay enregistré : {} ({} octets)", g_.saveReplayPath, bytes.size());
        }
    } else {
        recorder_.abort();
    }
    if (completed) {
        d.tier = c.tierFor(d.timeMs, d.damageTaken);
        ghostRun_.finish(time);
        // The challenge: kept here (best time, its replay) and sent when it can be (« hors
        // délai » once it is over). Practice keeps nothing.
        if (!d.practice) {
            d.attemptId = attemptId_;
            std::string whyNot = challengeSendable_ ? std::string() : "Votre version du niveau diffère de celle du défi : temps gardé sur ce PC.";
            d.report = g_.challenges.recordRun(c, d.timeMs, d.deaths, d.damageTaken, challengeHash_, attemptId_, lastReplay_.get(), challengeSendable_, whyNot);
        }
        std::string victory = g_.app.resources().soundPath("music/victory.ogg");
        if (!victory.empty()) g_.app.audio().playSound(victory, 0.9f, 1.0f, 0.0f, AudioBus::Music);
    }
    d.streak = g_.challenges.streak(c.kind);
    d.replay = lastReplay_;
    g_.app.pushScene(makeChallengeEnd(g_, *this, std::move(d)));
}

void GameScene::takeChallengeAttempt(bool firstLife) {
    if (attemptDone_ || !challengeTrack_.active() || !recorder_.active() || !world_ || !opt_.challenge || opt_.challenge->practice) return;
    attemptDone_ = true;
    const DailyChallenge& c = opt_.challenge->challenge;
    const RaceProgress& p = challengeTrack_.progress();
    const float time = world_->timer().elapsed();
    // Its replay up to now: the first life is copied (the run goes on), else it ends here.
    ReplayData replay;
    if (firstLife) {
        ReplayRecorder copy = recorder_;
        copy.finishAttempt(*world_, time);
        replay = copy.take();
    } else {
        recorder_.finishAttempt(*world_, time);
        replay = recorder_.take();
    }
    std::vector<int> bosses;
    for (float b : p.bossRemoved) bosses.push_back(static_cast<int>(std::clamp(std::lround(b * 1000.0f), 0L, 1000L)));
    std::string whyNot = challengeSendable_ ? std::string() : "Votre version du niveau diffère de celle du défi : essai gardé sur ce PC.";
    attemptReport_ = g_.challenges.recordAttempt(c, raceScore(challengeTrack_.layout(), p), static_cast<int64_t>(std::llround(static_cast<double>(time) * 1000.0)),
                                                 std::move(bosses), p.wavesDone, challengeHash_, attemptId_, &replay, challengeSendable_, whyNot);
    Log::info("Défi {} : essai à {:.1f} %{}", c.id, attemptReport_->progress, attemptReport_->newBest ? " (nouveau record de progression)" : "");
    // Without checkpoints the run goes on after the first life: said on screen.
    if (firstLife && attemptReport_->newBest) {
        char pct[32];
        std::snprintf(pct, sizeof pct, "%.1f", attemptReport_->progress);
        std::string s = pct;
        std::replace(s.begin(), s.end(), '.', ',');
        world_->hint("Nouveau record de progression : " + s + " %", 3.0f);
    }
}

void GameScene::drawChallengeBanner() {
    // Always: what this run is, at the bottom left.
    UIContext& ui = g_.ui;
    const DailyChallenge& c = opt_.challenge->challenge;
    const ChallengeRules& r = c.rules;
    std::string tag = opt_.challenge->practice      ? "DÉFI HORS CLASSEMENT"
                      : g_.challenges.ended(c)      ? "DÉFI HORS DÉLAI"
                      : c.kind == ChallengeKind::Weekly  ? "DÉFI DE LA SEMAINE"
                      : c.kind == ChallengeKind::Monthly ? "DÉFI DU MOIS"
                                                         : "DÉFI DU JOUR";
    if (c.finishObjective) tag += " · finir le niveau";
    tag += " · " + std::to_string(r.hearts) + (r.hearts > 1 ? " cœurs" : " cœur");
    if (!r.checkpoints) tag += " · sans points de passage";
    tag += " · " + keyName(SDL_SCANCODE_R) + " / Vue : recommencer";
    ui.label(ui.elide(tag, FontFace::Bold, 15, ui.screenW() * 0.5f), {ui.s(24), ui.screenH() - ui.s(28)}, 15,
             opt_.challenge->practice ? ui.theme().warm : ui.theme().accent, Align::Left, FontFace::Bold, VAlign::Middle);
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
    // Each ghost wears its player's suit (2.23): mine the one equipped now, the others the one
    // their leaderboard entry gave (a look this game knows — a suit created on Cosmic Online
    // drawn from its colours — else the normal sheet).
    ghostRun_.setSuitSheet([this](const GhostMeta& m) -> TexturePtr {
        return g_.suitSheet(m.mine() ? g_.cosmeticLook("suit") : g_.cosmetics.lookOr("suit", m.suit));
    });
    ghostRun_.begin(std::move(choices), level_, g_.content, ghostHash_, gs, opt_.training);
    if (opt_.training && opt_.spawn) ghostRun_.setTrainingFrom(*opt_.spawn);
    if (pb) {
        std::string key = GhostStore::levelKey(levelId_, ghostHash_);
        ghostRun_.setPersonal(g_.segments.bestSegments(key, pb->meta.splits.size()), pb->meta.splits,
                              static_cast<float>(pb->meta.timeMs) / 1000.0f);
    }
    ghostRun_.setVisible(gs.enabled || opt_.ghostsChosen);
}

void GameScene::noteAchievementStats(bool completed, const RecordUpdate* upd) {
    if (statsNoted_ || !world_ || opt_.editorTest || loadFailed_) return;
    statsNoted_ = true;
    // Section training (its powers may be chosen): never counted for the achievements.
    if (opt_.training) return;
    AchievementStats& a = g_.achievements.stats();
    const RunStats& s = world_->stats();
    // The world's counters (kills, by family too, deaths, collectibles, powers carried): none for a training.
    if (!noteRunCounters(a, *world_, completed)) return;
    a.add("jumps", runJumps_);
    int64_t uses = 0;
    for (const auto& [id, n] : runPowers_) {
        if (!g_.content.power(id)) continue;  // only the player's powers
        a.powerUses[id] += n;
        a.insert("powers_used", id);
        uses += n;
    }
    a.add("power_uses", uses);
    if (level_.extreme && !opt_.spawn) a.add("extreme_attempts");
    if (completed) {
        // Level achievements: a campaign level played from its start.
        if (!workshop_ && !opt_.level && !opt_.spawn && !opt_.training) {
            if (s.deaths == 0) a.insert("no_death_levels", levelId_);
            if (s.damageTaken == 0) a.insert("no_damage_levels", levelId_);
            if (s.powersUsed.empty() && uses == 0) a.insert("no_power_levels", levelId_);
            if (s.totalEnemies > 0 && s.kills >= s.totalEnemies) a.insert("cleared_levels", levelId_);
            if (s.totalCollectibles() > 0 && s.collectibles() >= s.totalCollectibles()) a.insert("full_collect_levels", levelId_);
            if (s.totalSecrets > 0 && s.secrets >= s.totalSecrets) a.insert("all_secrets_levels", levelId_);
        }
        if (const Json* c = g_.online.communityInstall(levelId_)) a.insert("community_levels", jstr(*c, "id"));
        if (level_.extreme && !opt_.spawn) a.add("extreme_finished");
        if (upd && upd->newBestTime && upd->previousBestTime > 0.0f) a.add("pb_improved");
        // Ghost Racing.
        const GhostRace& gr = ghostRun_.race();
        if (!gr.empty() && gr.finished()) {
            a.add("ghost_races");
            if (gr.size() >= 3) a.add("ghost_pack_races");
            for (const auto& sr : gr.splits()) a.add("gold_segments", sr.goldSegment ? 1 : 0);
            bool photo = false, world = false;
            for (int i = 0; i < gr.size(); ++i) {
                const GhostRace::Racer& g = gr.racer(i);
                const std::string& src = g.meta.source;
                world = world || src == "world";
                if (!g.hasLiveDelta || !g.sameVersion) continue;
                photo = photo || std::abs(g.liveDelta) < 0.1f;
                if (g.liveDelta >= 0.0f) continue;
                if (src == "pb") a.add("pb_ghost_wins");
                else if (src == "world") a.add("world_ghost_wins");
                else if (src == "friend") a.add("friend_ghost_wins");
            }
            if (world) a.add("world_ghost_races");
            if (photo) a.add("photo_finish");
        }
    }
    g_.checkAchievements();
    g_.achievements.save();
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
    g_.segments.add(GhostStore::levelKey(levelId_, ghostHash_), r.splits, r.time, r.date, runCheckpoints_);
    g_.segments.save();
}

void GameScene::onExit() {
    endSpectate("quit");  // left another way (a finished run has already ended it)
    g_.app.input().clearPowerSlotKeys();  // the keys of this level's powers stay here
    noteAchievementStats(false, nullptr);  // a run left or restarted (a finished one is already counted)
    if (mounted_) g_.app.resources().popMount();
    mounted_ = false;
    g_.app.audio().stopAmbience(600);
    g_.app.audio().setMusicDuck(1.0f);
    if (!opt_.editorTest && !(world_ && world_->training())) {
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
    if (collectionDirty_) g_.collection.save();  // creatures met (the gallery)
}

void GameScene::applyGhostSettings() { ghostRun_.setSettings(g_.settings.ghosts); }

void GameScene::onResume() {
    g_.app.audio().setMusicDuck(1.0f);
    refreshPowerKeys();  // « Touches de ce niveau », the settings: maybe other keys now
    g_.app.input().consumePlayerInput();
}

void GameScene::refreshPowerKeys() {
    if (!world_) return;
    powerKeys_ = levelPowerKeys(level_, g_.content, g_.settings.input, &g_.powerKeyOverrides, powerKeyExtra_);
    syncPowerKeys();
}

void GameScene::syncPowerKeys() {
    if (!world_) return;
    // Slot N takes the key of the power carried there (outside the simulation: the inputs
    // recorded stay the slot bits). A power given by something the level did not list gets
    // its key too.
    std::vector<std::string> missing = applyPowerSlotKeys(g_.app.input(), world_->player(), powerKeys_);
    if (missing.empty()) return;
    powerKeyExtra_.insert(powerKeyExtra_.end(), missing.begin(), missing.end());
    powerKeys_ = levelPowerKeys(level_, g_.content, g_.settings.input, &g_.powerKeyOverrides, powerKeyExtra_);
    applyPowerSlotKeys(g_.app.input(), world_->player(), powerKeys_);
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
    endSpectate("quit");
    abandonRaceAttempt();
    if (!ended_) takeChallengeAttempt(false);  // « Plus loin »: restarting ends the attempt
    PlayOptions o = opt_;
    if (!o.level && !opt_.editorTest) o.level.reset();
    g_.app.replaceTop(makeGameScene(g_, levelId_, o), 0.3f);
}

void GameScene::restartPractice() {
    endSpectate("quit");
    abandonRaceAttempt();
    PlayOptions o = opt_;
    if (!o.level && !opt_.editorTest) o.level.reset();
    o.practiceRespawn = true;
    g_.app.replaceTop(makeGameScene(g_, levelId_, o), 0.3f);
}

void GameScene::quitToMenu() {
    endSpectate("quit");
    abandonRaceAttempt();
    if (!ended_) takeChallengeAttempt(false);  // « Plus loin »: leaving ends the attempt
    if (opt_.bossTraining) {
        g_.app.switchScene(makeBossStats(g_, opt_.bossTraining->bossId), 0.4f);
        return;
    }
    if (opt_.challenge) {
        // Back to the challenges: its kind's tab while it runs, else the calendar on its card.
        g_.app.switchScene(makeChallengesOn(g_, opt_.challenge->challenge.id), 0.4f);
        return;
    }
    if (opt_.editorTest) {
        g_.app.popScene();
        return;
    }
    if (opt_.training) {
        // A section training: back to its setup, on the same segment (2.31).
        GhostSetupRequest req;
        req.fromOnline = opt_.fromOnline;
        req.training = true;
        req.trainingCheckpoint = opt_.trainingCheckpoint;
        g_.app.switchScene(makeGhostSetup(g_, levelId_, std::move(req)), 0.4f);
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
    if (!ended_ && spectateToken_) g_.spectate.input(spectateToken_, frame);  // the same step, for the friends watching
    stepWorld(*world_, frame, kSimDt);
    syncPowerKeys();  // a power picked up, replaced: its own key on its slot from the next input
    if (!ended_) recorder_.tick(*world_);
    if (!ended_ && spectateToken_ && recorder_.active())
        for (const auto& checks = recorder_.data().checks; spectateChecks_ < checks.size(); ++spectateChecks_)
            g_.spectate.check(spectateToken_, checks[spectateChecks_]);
    if (!ended_) {
        bool gliding = world_->player().gliding();
        if (gliding && !wasGliding_) ++runPowers_["glide"];
        wasGliding_ = gliding;
    }
    if (!ended_ && !opt_.editorTest) {
        bossFights_.step(*world_, g_.bossStats);
        bossFeedback();
    }
    if (!ended_ && race_.active()) {
        race_.update(*world_);
        recorder_.raceProgress(race_.progress().bossRemoved, race_.progress().wavesDone);
    }
    // « Plus loin »: how far the challenge attempt goes (its bosses and waves in the replay);
    // without checkpoints, the attempt is the first life only.
    if (!ended_ && challengeTrack_.active() && !attemptDone_) {
        challengeTrack_.update(*world_);
        recorder_.raceProgress(challengeTrack_.progress().bossRemoved, challengeTrack_.progress().wavesDone);
        if (!opt_.challenge->challenge.rules.checkpoints && world_->stats().deaths > 0 && world_->state() == WorldState::Playing) takeChallengeAttempt(true);
    }
    if (!ended_ && opt_.bossTraining) updateBossTraining();
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
    endSpectate(completed ? "finished" : "failed");
    if (opt_.challenge) {
        finishChallenge(completed);
        return;
    }
    if (opt_.bossTraining) {
        // The level ended otherwise (its exit, its rules): the training ends with it.
        if (!ended_) endBossTraining(bossTrainingWon(*world_, *opt_.bossTraining));
        return;
    }
    if (opt_.training) {
        // Training never makes a record nor progress: the exit ends the segment, a fall ends
        // the attempt (both start again).
        if (completed) trainingSegmentDone();
        else {
            ended_ = true;
            segmentBanner_ = 0.8f;
            segmentTime_ = -1.0f;
        }
        return;
    }
    ended_ = true;
    if (!opt_.editorTest) {
        bossFights_.step(*world_, g_.bossStats);
        g_.bossStats.save();
    }
    RunResult r = runResultOf(*world_, completed);
    r.levelId = levelId_;
    const RunStats& s = world_->stats();
    r.score = RecordsManager::computeScore(level_, s, r.time, completed);
    r.medal = RecordsManager::evaluateMedal(level_, r);
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
            g_.progression.noteRun(r);
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
            if (FileUtil::writeBinary(rp, bytes) && FileUtil::writeTextAtomic(sp, side.dump(1)) && (g_.achievements.stats().add("completion_proofs"), true))
                g_.online.notify(OnlineService::Notification::Kind::Success, "Fin de niveau enregistrée",
                                 "Cette version peut être publiée : ATELIER → Mes niveaux → Publier en ligne.");
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
    noteAchievementStats(completed, &upd);
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
    data.hardcore = level_.extreme && level_.oneLife() && !opt_.challenge && !opt_.bossTraining && !opt_.training && !opt_.editorTest;
    data.practice = world_->practiceRespawn();
    data.nextLevel = nextLevelId();
    data.editorTest = opt_.editorTest;
    data.replay = lastReplay_;
    data.fromOnline = opt_.fromOnline;
    data.fromWorkshop = workshop_ && !opt_.fromOnline;
    data.online = online;
    data.raceScore = raceEstimate;
    data.bossKills = bossFights_.kills();
    if (completed && !opt_.editorTest) {
        auto seg = std::make_shared<SegmentSummary>();
        seg->levelName = level_.name;
        seg->levelKey = GhostStore::levelKey(levelId_, ghostHash_);
        seg->levelId = levelId_;
        for (const std::string& id : runCheckpoints_) {
            std::string label;
            for (const LevelObject& o : level_.objects)
                if (o.id == id) label = o.propStr("label");
            seg->names.push_back(label.empty() ? "Point " + std::to_string(seg->names.size() + 1) : label);
        }
        seg->splits = r.splits;
        seg->time = r.time;
        const GhostRace& gr = ghostRun_.race();
        seg->bestSegments = gr.personalBestSegments();
        seg->pbSplits = gr.personalSplits();
        seg->pbTime = gr.personalTime();
        if (seg->bestSegments.empty()) seg->bestSegments = g_.segments.bestSegments(seg->levelKey, r.splits.size());
        for (int i = 0; i < gr.size(); ++i) {
            const GhostRace::Racer& g = gr.racer(i);
            std::string name = g.meta.sourceLabel();
            if (!g.meta.label.empty() && g.meta.label != "Moi") name += " · " + g.meta.label;
            seg->ghosts.push_back({name, g.color, g.track.splits(), g.track.duration(), g.liveDelta, g.sameVersion});
        }
        data.segments = seg;
    }
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
    syncPowerKeys();
    world_->update(dt);
    // The pause map: what the camera has shown (display only, outside the simulation).
    if (explored_.cols == 0) explored_.reset(world_->level().bounds);
    explored_.mark(world_->camera().visibleArea(1920, 1080));
    if (trackFuture_.valid() && trackFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) hud_.setProgressTrack(trackFuture_.get());
    if (raceRouteFuture_.valid() && raceRouteFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) race_.setRoute(raceRouteFuture_.get());
    if (challengeRouteFuture_.valid() && challengeRouteFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        challengeTrack_.setRoute(challengeRouteFuture_.get());
    hud_.update(*world_, dt);
    // Collection (display only): trail, victory effect, creatures met.
    {
        const Player& pl = world_->player();
        bool fast = std::abs(pl.vel.x) > 150.0f || std::abs(pl.vel.y) > 420.0f;
        trail_.update(pl.center(), dt, fast && pl.alive() && world_->state() == WorldState::Playing);
        if (victoryT_ < 0.0f && world_->state() == WorldState::Completed) {
            victoryT_ = 0.0f;
            victoryAt_ = pl.box.bottomCenter();
        } else if (victoryT_ >= 0.0f) {
            victoryT_ += dt;
        }
        if ((metTimer_ -= dt) <= 0.0f) {
            metTimer_ = 0.25f;
            noteCreaturesMet();
        }
    }
    InputManager& in = g_.app.input();
    // « Mode concentré »: the reduced HUD chosen in the HUD editor (display only).
    if (in.menuPressed(Action::FocusToggle) && !ended_) {
        g_.settings.gameplay.focusMode = !g_.settings.gameplay.focusMode;
        g_.saveSettings();
        world_->hint(g_.settings.gameplay.focusMode ? "Mode concentré" : "HUD complet", 1.4f);
    }
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
    if (opt_.forceBossPhase > 0)
        if (Enemy* b = world_->activeBoss(); b && b->alive() && !b->dormant()) {
            // A capture never counts (boss statistics, achievements, records), as a training.
            world_->setTraining(true);
            const int n = static_cast<int>(b->def().phases.size());
            if (b->phaseIndex() < std::min(opt_.forceBossPhase, n) - 1) jumpBossToPhase(*world_, *b, std::min(opt_.forceBossPhase, n) - 1);
            if (opt_.forceBossPhase <= n || b->lastStand()) {
                opt_.forceBossPhase = 0;
            } else {
                // Its last stand: the last blow of the fight, as soon as it can be hurt.
                b->setWounds(1.0f, n - 1);
                DamageInfo d;
                d.amount = 1000.0f;
                d.type = DamageType::Projectile;
                d.source = b->center();
                b->takeDamage(*world_, d);
            }
        }
    bool autoPause = opt_.openPause && world_->state() == WorldState::Playing &&
                     (opt_.pauseAfter <= 0.0f || world_->timer().elapsed() >= opt_.pauseAfter);
    if (autoPause) opt_.openPause = false;
    if (!ended_ && (in.menuPressed(Action::Pause) || autoPause)) {
        if (world_->state() == WorldState::Playing || world_->state() == WorldState::Intro) {
            g_.app.pushScene(std::make_unique<PauseScene>(g_, *this));
            return;
        }
    }
    // Quick restart (contre-la-montre, training): R key or the gamepad View/Back button.
    if (!ended_ && (in.keyPressed(SDL_SCANCODE_R) || in.rawPadButton(SDL_GAMEPAD_BUTTON_BACK)) &&
        world_->state() == WorldState::Playing && (level_.timerMode == TimerMode::TimeAttack || opt_.training || opt_.bossTraining || opt_.challenge)) {
        restart();
        return;
    }
    // Training: the segment ends at its checkpoint; the next attempt starts by itself.
    if (opt_.training && !ended_ && !opt_.trainingEnd.empty() && world_->checkpointId() == opt_.trainingEnd) trainingSegmentDone();
    if (segmentBanner_ > 0.0f) {
        segmentBanner_ -= dt;
        if (segmentBanner_ <= 0.0f || in.menuPressed(Action::MenuConfirm)) restart();
    }
    // Boss training: its end screen, once the boss's fall (or the player's) has been seen and
    // the level's messages (the boss's name...) are gone, so that nothing shows under it.
    if (bossTrainingEnd_ && (bossTrainingDelay_ -= dt) <= 0.0f && (world_->messages().empty() || bossTrainingDelay_ < -4.0f)) {
        g_.app.pushScene(makeBossTrainingEnd(g_, *this, *bossTrainingEnd_));
        bossTrainingEnd_.reset();
    }
}

void GameScene::startTraining() {
    // The checkpoint the segment starts from is reached (silently): the timer's first split.
    if (!opt_.trainingCheckpoint.empty())
        if (LevelEntity* e = world_->entity(opt_.trainingCheckpoint)) e->handleAction(*world_, "activate", Json::object());
    if (opt_.trainingPowersChosen) {
        // The powers chosen in the training setup, exactly these (the level's start powers
        // only if chosen too), as a boss training does.
        Player& p = world_->player();
        p.clearPowers();
        int slots = p.powerSlots() > 0 ? p.powerSlots() : LevelData::kMaxPowerSlots;
        for (const std::string& id : fitTrainingPowers(g_.content, opt_.trainingPowers, slots))
            if (const PowerDefinition* d = g_.content.power(id)) p.grantPower(*d);
        return;
    }
    // The powers of the tablets placed before it on the route (the last ones if the slots are
    // fewer: those a player would keep).
    std::vector<std::string> powers = opt_.trainingPowers;
    int slots = world_->player().powerSlots();
    if (slots > 0 && static_cast<int>(powers.size()) > slots) powers.erase(powers.begin(), powers.end() - slots);
    for (const std::string& id : powers)
        if (const PowerDefinition* d = g_.content.power(id); d && !world_->player().hasPower(id)) world_->player().grantPower(*d);
}

void GameScene::trainingSegmentDone() {
    ended_ = true;
    float t = world_->timer().elapsed();
    // The segment's comparison is its last split (the run itself is not over: no finish).
    if (opt_.trainingEnd.empty()) ghostRun_.finish(t);  // a segment ending at the exit
    std::string key = levelId_ + ":" + opt_.trainingCheckpoint;
    std::vector<float>& tries = g_.trainingAttempts[key];
    segmentBest_ = tries.empty() ? -1.0f : *std::min_element(tries.begin(), tries.end());
    tries.push_back(t);
    if (tries.size() > 50) tries.erase(tries.begin());
    segmentTime_ = t;
    // The best training time of this segment, kept (saves/training.json): never a record,
    // never a gold segment of the full runs.
    trainingBestBefore_ = -1.0f;
    trainingNewBest_ = false;
    if (!ghostHash_.empty()) {
        std::string lk = GhostStore::levelKey(levelId_, ghostHash_);
        if (const TrainingRecords::Entry* e = g_.training.get(lk, opt_.trainingCheckpoint, opt_.trainingEnd)) trainingBestBefore_ = e->best;
        trainingNewBest_ = g_.training.add(lk, opt_.trainingCheckpoint, opt_.trainingEnd, t, FileUtil::nowString());
        g_.training.save();
    }
    segmentBanner_ = 3.0f;
    g_.uiSound(trainingNewBest_ || segmentBest_ < 0.0f || t < segmentBest_ ? "medal" : "ui_confirm", 0.6f);
}

void GameScene::applyCosmetics() {
    // The equipped suit: the same sheet recoloured (assets/sprites/player/suits, or at run time
    // for a suit created on Cosmic Online). Display only: the simulation never sees it.
    suitTexture_ = g_.suitSheet(g_.cosmeticLook("suit"));
    world_->player().skin = suitTexture_ ? suitTexture_.get() : nullptr;
    trailLook_ = g_.cosmeticLook("trail");
    victoryLook_ = g_.cosmeticLook("victory");
    trail_.reset();
    victoryT_ = -1.0f;
}

void GameScene::noteCreaturesMet() {
    // A creature enters the gallery once it has been on screen, awake, in a level (never in a
    // training run).
    if (world_->training()) return;
    Rect seen = world_->camera().visibleArea(g_.app.renderer().width(), g_.app.renderer().height());
    for (const auto& e : world_->enemies()) {
        if (!e->alive() || e->dormant() || !seen.intersects(e->box)) continue;
        if (g_.collection.met.insert(e->def().id).second) collectionDirty_ = true;
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
    trail_.draw(r, trailLook_, true);
    if (victoryT_ >= 0.0f) drawVictoryFx(r, victoryLook_, victoryAt_, victoryT_, true);
    // A challenge has its own rules: the level's record is no reference for it.
    hud_.render(g_.ui, *world_, view, opt_.challenge ? nullptr : g_.records.get(levelId_), g_.settings.gameplay);
    // The ghosts' HUD: not under the results screen (nor under its notifications).
    if (ghostRun_.active() && !world_->cinematic().active() && world_->state() != WorldState::Intro && (!ended_ || opt_.training)) {
        const GameplaySettings& gs = g_.settings.gameplay;
        Rect keepOut[kHudElements + 3];
        int n = 0;
        for (int e = 0; e < kHudElements; ++e) {
            auto el = static_cast<HudElement>(e);
            if (el != HudElement::Splits && hudVisible(gs, el)) keepOut[n++] = hud_.area(g_.ui, *world_, el, hud_.place(g_.ui, *world_, gs, el));
        }
        // Section training: the ghosts' names never go under its references, its tag or the
        // banner of an attempt (their places of the previous frame).
        if (opt_.training) {
            keepOut[n++] = trainingRefsRect_;
            keepOut[n++] = trainingTagRect_;
            if (segmentBanner_ > 0.0f) keepOut[n++] = trainingBannerRect_;
        }
        Hud::Place sp = hud_.place(g_.ui, *world_, gs, HudElement::Splits);
        ghostRun_.drawHud(g_.ui, view, hud_.area(g_.ui, *world_, HudElement::Splits, sp), sp.k, hudVisible(gs, HudElement::Splits), keepOut, n);
    }
    drawBossArrow(view);
    if (opt_.training || opt_.bossTraining) drawTrainingBanner();
    else if (opt_.challenge && !ended_) drawChallengeBanner();
    if (g_.settings.graphics.showFps) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f FPS", g_.app.fps());
        g_.ui.label(buf, {g_.ui.s(12), g_.ui.screenH() - g_.ui.s(30)}, 18, g_.ui.theme().textDim);
    }
}

void GameScene::drawTrainingBanner() {
    UIContext& ui = g_.ui;
    const float W = ui.screenW();
    if (opt_.bossTraining) {
        // Boss training: what this run is, always in view (bottom left), in a dark pill.
        const std::string tag = "Entraînement — ne compte pas pour les succès ni les statistiques";
        const std::string keys = keyName(SDL_SCANCODE_R) + " / Vue : recommencer";  // R as the keyboard layout names it
        float fs = std::max(15.0f, 11.5f / ui.s(1.0f));
        const float room = W * 0.62f;
        float tw = ui.text().measure(tag, FontFace::Bold, ui.s(fs)).x;
        float kw = ui.text().measure(keys, FontFace::Body, ui.s(fs)).x;
        if (tw + kw + ui.s(52) > room) {
            fs = ui.fitFont(tag + "      " + keys, FontFace::Bold, fs, room - ui.s(52), 0.7f);
            tw = ui.text().measure(tag, FontFace::Bold, ui.s(fs)).x;
            kw = ui.text().measure(keys, FontFace::Body, ui.s(fs)).x;
        }
        Rect pill{ui.s(16), ui.screenH() - ui.s(16) - ui.s(fs + 18), std::min(room, tw + kw + ui.s(52)), ui.s(fs + 18)};
        ui.r().fillRoundedRect(pill, pill.h * 0.5f, Color{8, 12, 26, 215});
        ui.r().drawRoundedRect(pill, pill.h * 0.5f, ui.theme().warm.withAlpha(170), std::max(1.0f, ui.s(1.5f)));
        Vec2 ts = ui.label(ui.elide(tag, FontFace::Bold, fs, pill.w - ui.s(28)), {pill.x + ui.s(14), pill.center().y}, fs, ui.theme().warm,
                           Align::Left, FontFace::Bold, VAlign::Middle);
        float kx = pill.x + ui.s(14) + ts.x + ui.s(24);
        if (kx + kw <= pill.right() - ui.s(10))
            ui.label(keys, {kx, pill.center().y}, fs, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
        return;
    }
    // Always: what this run is (no record), at the bottom left.
    const float H = ui.screenH();
    const float fs = std::max(15.0f, 11.5f / ui.s(1.0f));
    std::string tag = "ENTRAÎNEMENT · aucun record · " + keyName(SDL_SCANCODE_R) + " / Vue : recommencer";
    std::string shownTag = ui.elide(tag, FontFace::Bold, fs, W * 0.5f);
    Vec2 ts = ui.label(shownTag, {ui.s(24), H - ui.s(28)}, fs, ui.theme().warm, Align::Left, FontFace::Bold, VAlign::Middle);
    const Rect tagRect{ui.s(24), H - ui.s(28) - ts.y * 0.5f, ts.x, ts.y};
    // The end of an attempt: centred, above the middle (its place is kept clear even before).
    const float bw = std::min(ui.s(680), W - ui.s(40));
    struct Line {
        std::string text;
        Color color;
        bool bold = false;
    };
    std::vector<Line> lines;  // under the time, each on its own line
    if (segmentTime_ >= 0.0f) {
        const UITheme& th = ui.theme();
        if (trainingNewBest_)
            lines.push_back({trainingBestBefore_ > 0.0f ? "Nouveau meilleur à l'entraînement ! (" + formatGhostDelta(segmentTime_ - trainingBestBefore_) + ")"
                                                        : "Nouveau meilleur à l'entraînement !",
                             th.success, true});
        else if (trainingBestBefore_ > 0.0f)
            lines.push_back({"Meilleur à l'entraînement " + formatGhostTime(trainingBestBefore_) + " (" + formatGhostDelta(segmentTime_ - trainingBestBefore_) + ")",
                             th.textDim, false});
        std::string refs;
        if (opt_.trainingBest > 0.0f) refs = "Meilleur segment " + formatGhostDelta(segmentTime_ - opt_.trainingBest);
        if (opt_.trainingPbSegment > 0.0f)
            refs += (refs.empty() ? "" : "   ·   ") + std::string("Dans mon record ") + formatGhostDelta(segmentTime_ - opt_.trainingPbSegment);
        if (refs.empty()) refs = "Pas encore de course complète : aucun segment de référence";
        lines.push_back({refs, th.text, false});
        std::string more;
        const GhostRace& gr = ghostRun_.race();
        if (!gr.empty() && !gr.splits().empty() && gr.splits().back().comparable) {
            float d = gr.splits().back().delta;
            if (gr.splits().back().ghostSegment >= 0.0f) d = gr.splits().back().segment - gr.splits().back().ghostSegment;
            more = formatGhostDelta(d) + " sur le fantôme   ·   ";
        }
        std::vector<float>& tries = g_.trainingAttempts[levelId_ + ":" + opt_.trainingCheckpoint];
        more += std::to_string(tries.size()) + (tries.size() > 1 ? " essais cette séance" : " essai cette séance");
        lines.push_back({more, th.textDim, false});
    }
    const float lineH = ui.s(fs + 13.0f);
    const float bh = segmentTime_ < 0.0f ? ui.s(110) : ui.s(70) + lineH * static_cast<float>(lines.size()) + ui.s(46);
    const Rect b = Rect::fromCenter({W * 0.5f, H * 0.36f}, {bw, bh});
    trainingTagRect_ = tagRect;
    trainingBannerRect_ = b;
    drawTrainingRefs(tagRect, b);
    if (segmentBanner_ <= 0.0f) return;
    ui.r().fillRoundedRect(b, ui.s(14), Color{8, 12, 26, 225});
    ui.r().drawRoundedRect(b, ui.s(14), ui.theme().accent.withAlpha(140), ui.s(2));
    const float room = b.w - ui.s(40);
    if (segmentTime_ < 0.0f) {
        std::string miss = "Essai manqué : on recommence";
        ui.label(miss, b.center(), ui.fitFont(miss, FontFace::Bold, 26, room), ui.theme().text, Align::Center, FontFace::Bold, VAlign::Middle);
        return;
    }
    std::string head = "SEGMENT  " + formatGhostTime(segmentTime_);
    ui.label(head, {b.center().x, b.y + ui.s(38)}, ui.fitFont(head, FontFace::Title, 34, room), ui.theme().text, Align::Center,
             FontFace::Title, VAlign::Middle);
    float y = b.y + ui.s(70) + lineH * 0.5f;
    for (const Line& l : lines) {
        FontFace face = l.bold ? FontFace::Bold : FontFace::Body;
        float f = ui.fitFont(l.text, face, fs + 3.0f, room, 0.75f);
        ui.label(ui.elide(l.text, face, f, room), {b.center().x, y}, f, l.color, Align::Center, face, VAlign::Middle);
        y += lineH;
    }
    ui.label("Nouvel essai…", {b.center().x, b.bottom() - ui.s(26)}, fs, ui.theme().accent, Align::Center, FontFace::Body, VAlign::Middle);
}

void GameScene::drawTrainingRefs(const Rect& tagRect, const Rect& banner) {
    // What this segment is worth, always in view during a section training: its best duration
    // in full runs, its duration in my record, my best training time. Placed where no HUD
    // element is (the HUD layout can be moved), never under the attempt's banner.
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    const float W = ui.screenW(), H = ui.screenH();
    const float fs = std::max(15.0f, 11.5f / ui.s(1.0f));
    float train = -1.0f;
    if (!ghostHash_.empty())
        if (const TrainingRecords::Entry* e = g_.training.get(GhostStore::levelKey(levelId_, ghostHash_), opt_.trainingCheckpoint, opt_.trainingEnd))
            train = e->best;
    const std::pair<std::string, float> rows[3] = {{"Meilleur segment", opt_.trainingBest},
                                                   {"Dans mon record", opt_.trainingPbSegment},
                                                   {"Meilleur à l'entraînement", train}};
    float lw = 0.0f, vw = 0.0f;
    for (const auto& [label, v] : rows) {
        lw = std::max(lw, ui.text().measure(label, FontFace::Body, ui.s(fs)).x);
        vw = std::max(vw, ui.text().measure(v > 0.0f ? formatGhostTime(v) : "—", FontFace::Bold, ui.s(fs)).x);
    }
    const float pad = ui.s(12), gap = ui.s(18), lineH = ui.s(fs + 9.0f);
    const float bw = std::min(pad * 2 + lw + gap + vw, W * 0.4f), bh = pad * 2 + lineH * 3;
    // Every HUD element shown, the ghosts' box, the tag, the hints' line and the banner.
    std::vector<Rect> keep;
    const GameplaySettings& gs = g_.settings.gameplay;
    for (int e = 0; e < kHudElements; ++e) {
        auto el = static_cast<HudElement>(e);
        if (!hudVisible(gs, el)) continue;
        Rect a = hud_.area(ui, *world_, el, hud_.place(ui, *world_, gs, el));
        if (a.w > 0.0f && a.h > 0.0f) keep.push_back(a);
    }
    if (ghostRun_.active() && ghostRun_.visible() && ghostRun_.hudRect().w > 0.0f) keep.push_back(ghostRun_.hudRect());
    keep.push_back(tagRect);
    keep.push_back(banner);
    keep.push_back(Rect::fromCenter({W * 0.5f, H - ui.s(world_->activeBoss() ? 210 : 150)}, {W * 0.6f, ui.s(64)}));
    const float m = ui.s(16);
    const Rect candidates[] = {
        {m, tagRect.y - ui.s(10) - bh, bw, bh},               // bottom left, above the tag
        {W - m - bw, H - m - bh, bw, bh},                     // bottom right
        {m, H * 0.5f - bh * 0.5f, bw, bh},                    // left, half way down
        {W - m - bw, H * 0.5f - bh * 0.5f, bw, bh},           // right, half way down
        {W * 0.5f - bw * 0.5f, H - m - bh, bw, bh},           // bottom centre
        {W * 0.5f - bw * 0.5f, ui.s(110), bw, bh},            // top centre
    };
    Rect at = candidates[0];
    float leastOverlap = 1e30f;
    for (const Rect& c : candidates) {
        float overlap = 0.0f;
        for (const Rect& k : keep)
            if (c.intersects(k.expanded(ui.s(6)))) {
                Rect i = c.intersection(k.expanded(ui.s(6)));
                overlap += std::max(0.0f, i.w) * std::max(0.0f, i.h);
            }
        if (overlap < leastOverlap) {
            leastOverlap = overlap;
            at = c;
        }
        if (overlap <= 0.0f) break;
    }
    trainingRefsRect_ = at;
    ui.r().fillRoundedRect(at, ui.s(10), Color{8, 12, 26, 200});
    ui.r().drawRoundedRect(at, ui.s(10), th.warm.withAlpha(110), std::max(1.0f, ui.s(1.5f)));
    float y = at.y + pad + lineH * 0.5f;
    const float labelRoom = at.w - pad * 2 - gap - vw;
    for (const auto& [label, v] : rows) {
        ui.label(ui.elide(label, FontFace::Body, fs, labelRoom), {at.x + pad, y}, fs, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        ui.label(v > 0.0f ? formatGhostTime(v) : "—", {at.right() - pad, y}, fs, v > 0.0f ? th.text : th.textDim, Align::Right, FontFace::Bold,
                 VAlign::Middle);
        y += lineH;
    }
}

void GameScene::updateBossTraining() {
    const BossTrainingSetup& s = *opt_.bossTraining;
    const Enemy* boss = bossTrainingEnemy(*world_, s);
    // The fight's time runs from its start (the boss awake and fighting).
    if (boss && boss->alive() && world_->activeBoss() == boss && fightStart_ < 0.0f) fightStart_ = world_->time();
    if (boss && boss->alive()) trainingPhase_ = boss->phaseIndex();
    if (bossTrainingWon(*world_, s)) endBossTraining(true);
    else if (world_->stats().deaths > 0 || world_->state() == WorldState::Failed) endBossTraining(false);
}

void GameScene::endBossTraining(bool won) {
    ended_ = true;
    const BossTrainingSetup& s = *opt_.bossTraining;
    BossTrainingResult r;
    r.won = won;
    r.setup = s;
    r.time = fightStart_ >= 0.0f ? world_->time() - fightStart_ : 0.0f;
    r.damage = world_->stats().damageTaken;
    r.phase = trainingPhase_;
    if (const EnemyDefinition* d = g_.content.enemy(s.bossId)) {
        r.bossName = d->name;
        r.phases = static_cast<int>(d->phases.size());
        if (won && s.phase < 0) r.phase = std::max(0, r.phases - 1);
        if (r.phase >= 0 && r.phase < r.phases) r.phaseName = d->phases[static_cast<size_t>(r.phase)].name;
    }
    bossTrainingEnd_ = r;
    bossTrainingDelay_ = won ? 1.4f : 1.0f;  // the boss's fall (or the player's) is seen first
    g_.uiSound(won ? "medal" : "ui_back", 0.7f);
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
