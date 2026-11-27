// 2.54 — the player's statistics: the recorder of a run (a WorldListener that only reads) and
// the store (saves/stats.json, written on a worker thread). See PlayStats.h.

#include <algorithm>
#include <cmath>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/levels/LevelData.h"
#include "game/player/Player.h"
#include "game/powers/PowerDefinition.h"
#include "game/records/PlayStats.h"
#include "saves/SaveFile.h"

namespace cosmic {

using namespace playstats;

namespace {

/// Distance from a point to a rectangle (0 inside).
float distToRect(Vec2 p, const Rect& r) {
    float dx = std::max({r.x - p.x, 0.0f, p.x - r.right()});
    float dy = std::max({r.y - p.y, 0.0f, p.y - r.bottom()});
    return std::sqrt(dx * dx + dy * dy);
}

/// Distance between two rectangles (0 when they touch).
float rectGap(const Rect& a, const Rect& b) {
    float dx = std::max({a.x - b.right(), 0.0f, b.x - a.right()});
    float dy = std::max({a.y - b.bottom(), 0.0f, b.y - a.bottom()});
    return std::sqrt(dx * dx + dy * dy);
}

/// An attack carried by the enemy's own body (a blow, a charge, a leap, a dive…): its zone is
/// the enemy. Lasers, rains, floods and the other attacks at a distance are not followed (their
/// shots are, one by one).
bool bodyAttack(const Enemy& e) {
    if (e.state() != EnemyState::Attack) return false;
    const std::string& name = e.currentAttackName();
    if (name.empty()) return false;
    for (const auto& a : e.def().attacks) {
        if (a.name != name) continue;
        switch (a.type) {
            case AttackType::Contact:
            case AttackType::Melee:
            case AttackType::Lunge:
            case AttackType::Slam:
            case AttackType::Charge:
            case AttackType::Leap:
            case AttackType::Dive:
            case AttackType::Blink:
            case AttackType::Perch: return true;
            default: return false;
        }
    }
    return false;
}

}  // namespace

// ================================================================== recorder

PlayStatsRecorder::PlayStatsRecorder(PlayStatsStore& store, GameWorld& w, std::string levelId, std::string levelName, Kind kind)
    : store_(store), world_(&w), levelId_(std::move(levelId)), levelName_(std::move(levelName)), kind_(kind) {
    lastDeaths_ = w.stats().deaths;
}

PlayStatsRecorder::~PlayStatsRecorder() {
    if (!finished_) finish();
}

void PlayStatsRecorder::setRoute(LevelProgressTrack t) {
    track_ = std::move(t);
    trackReady_ = track_.valid();
}

void PlayStatsRecorder::step(const GameWorld& w) {
    if (!active_ || finished_) return;
    if (w.invincible()) {
        // « Invincible » (administrators) is never counted: what this run gathered is dropped.
        active_ = false;
        run_ = {};
        attempts_.clear();
        phaseMarks_.clear();
        segTimes_.clear();
        if (world_ && world_->listener == this) world_->listener = nullptr;
        return;
    }
    ++stepNo_;
    if (ended_) return;
    if (kind_ == Kind::Training) {
        run_.trainSec += kSimDt;
        if (w.state() == WorldState::Completed || w.state() == WorldState::Failed) {
            ended_ = true;
            commit();
        }
        return;
    }
    run_.playSec += kSimDt;
    if (!trackReady_ && trackFuture_.valid() && trackFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        setRoute(trackFuture_.get());
    if (w.state() == WorldState::Playing && !attemptNoted_) {
        attemptNoted_ = true;
        attemptPending_ = classic_;
    }
    const int deaths = w.stats().deaths;
    const bool died = deaths > lastDeaths_;
    trackPosition(w);
    trackThreats(w);
    trackBoss(w, died);
    if (classic_) trackSplits(w);
    if (died) {
        run_.deaths += deaths - lastDeaths_;
        lastDeaths_ = deaths;
        endLife(true);
        commit();
    }
    if (w.state() == WorldState::Completed || w.state() == WorldState::Failed) {
        ended_ = true;
        if (classic_ && w.state() == WorldState::Completed) finishTime_ = w.timer().elapsed();
        if (fight_) {
            const Enemy* fe = w.findEnemy(fight_->uid);
            endFight(fe && !fe->alive(), false);
        }
        endLife(false);
        for (const auto& [uid, s] : shots_) resolveShot(s);
        for (const auto& [uid, b] : blows_) resolveBlow(b);
        shots_.clear();
        blows_.clear();
        commit();
    }
}

void PlayStatsRecorder::trackPosition(const GameWorld& w) {
    if (!trackReady_ || w.state() != WorldState::Playing) return;
    const Player& p = w.player();
    if (!p.alive() || p.state() == PlayerState::Respawn) return;
    float f = track_.project(p.feet(), hint_);
    int s = std::clamp(static_cast<int>(f * kSlices), 0, kSlices - 1);
    if (lastSlice_ >= 0 && std::abs(s - lastSlice_) <= 3) {
        for (int i = std::min(s, lastSlice_); i <= std::max(s, lastSlice_); ++i) life_.set(static_cast<size_t>(i));
    } else {
        life_.set(static_cast<size_t>(s));
    }
    lastSlice_ = s;
}

void PlayStatsRecorder::endLife(bool died) {
    for (int i = 0; i < kSlices; ++i)
        if (life_.test(static_cast<size_t>(i))) slicePasses_[static_cast<size_t>(i)]++;
    if (died && lastSlice_ >= 0) {
        size_t k = static_cast<size_t>(lastSlice_);
        sliceDeaths_[k]++;
        if (!life_.test(k)) slicePasses_[k]++;
    }
    life_.reset();
    lastSlice_ = -1;
}

void PlayStatsRecorder::resolveShot(const ShotWatch& s) {
    if (!s.threat && !s.hit) return;
    run_.threats++;
    if (!s.hit) run_.dodged++;
}

void PlayStatsRecorder::resolveBlow(const BlowWatch& b) {
    if (!b.active || (!b.threat && !b.hit)) return;
    run_.threats++;
    if (!b.hit) run_.dodged++;
}

void PlayStatsRecorder::trackThreats(const GameWorld& w) {
    const Player& p = w.player();
    const bool exposed = p.alive() && p.state() != PlayerState::Respawn && w.state() == WorldState::Playing;
    for (const Projectile& pr : w.projectiles()) {
        if (!pr.alive || pr.fromPlayer || pr.uid == 0) continue;
        ShotWatch& s = shots_[pr.uid];
        s.seen = std::max(s.seen, stepNo_);
        if (exposed && distToRect(pr.pos, p.box) - pr.radius < kThreatDistance) s.threat = true;
    }
    for (auto it = shots_.begin(); it != shots_.end();) {
        if (it->second.seen < stepNo_) {
            resolveShot(it->second);
            it = shots_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& e : w.enemies()) {
        if (!e->alive() || !bodyAttack(*e)) continue;
        BlowWatch& b = blows_[e->uid()];
        if (!b.active) b = BlowWatch{true, false, false, 0};
        b.seen = std::max(b.seen, stepNo_);
        if (exposed && rectGap(e->box, p.box) < kThreatDistance) b.threat = true;
    }
    for (auto it = blows_.begin(); it != blows_.end();) {
        if (it->second.seen < stepNo_) {
            resolveBlow(it->second);
            it = blows_.erase(it);
        } else {
            ++it;
        }
    }
}

void PlayStatsRecorder::blow(const HurtSource& src, bool fall) {
    if (!active_ || finished_ || ended_ || kind_ != Kind::Counted) return;
    run_.hurt[static_cast<size_t>(std::min(src.origin, HurtOrigin::Other))]++;
    const uint32_t enemy = src.enemy ? src.enemy->uid() : 0;
    if (src.shot != 0) {
        ShotWatch& s = shots_[src.shot];
        s.hit = s.threat = true;
        s.seen = std::max(s.seen, stepNo_ + 1);  // resolved once the shot is gone
    } else if (enemy != 0 && !fall &&
               (src.origin == HurtOrigin::EnemyAttack || src.origin == HurtOrigin::BossAttack || src.origin == HurtOrigin::Contact)) {
        auto it = blows_.find(enemy);
        if (it != blows_.end() && it->second.active) {
            it->second.hit = true;
        } else if (src.origin != HurtOrigin::Contact && bodyAttack(*src.enemy)) {
            blows_[enemy] = BlowWatch{true, true, true, stepNo_ + 1};  // hit before it was seen
        }
    }
    lastBlow_ = {src.origin, enemy, src.attack};
}

void PlayStatsRecorder::playerHurt(const GameWorld&, const DamageInfo&, const HurtSource& src) { blow(src, false); }

void PlayStatsRecorder::playerKilled(const GameWorld&, const HurtSource& src) { blow(src, true); }

void PlayStatsRecorder::attackStarted(const GameWorld& w, uint32_t tag, const std::string& power) {
    if (!active_ || finished_ || ended_ || kind_ != Kind::Counted) return;
    std::string behavior = power;
    if (power != "attack") {
        const PowerDefinition* def = w.content().power(power);
        behavior = def ? def->behavior : std::string();
    }
    if (!countsAsAttack(behavior)) return;
    run_.attacks++;
    run_.powers[power][0]++;
    tags_.push_back({tag, power, false});
    if (tags_.size() > 256) tags_.pop_front();
}

void PlayStatsRecorder::enemyHit(const GameWorld&, const Enemy&, uint32_t tag) {
    if (!active_ || finished_ || ended_ || kind_ != Kind::Counted) return;
    for (auto it = tags_.rbegin(); it != tags_.rend(); ++it) {
        if (it->tag != tag) continue;
        if (!it->hit) {
            it->hit = true;  // once, however many enemies it went through
            run_.hits++;
            run_.powers[it->power][1]++;
        }
        return;
    }
}

void PlayStatsRecorder::trackBoss(const GameWorld& w, bool died) {
    const Enemy* b = w.activeBoss();
    if (fight_) {
        const Enemy* fe = w.findEnemy(fight_->uid);
        if (fe) {
            fight_->removed = std::clamp(1.0f - fe->hpRatio(), 0.0f, 1.0f);
            while (fe->alive() && fight_->phase < fe->phaseIndex() && fight_->phase + 1 < fight_->phases) {
                fight_->phase++;
                phaseMarks_.push_back({fight_->bossId, {fight_->phase, false}});
            }
        }
        if (died) endFight(false, true);
        else if (fe && !fe->alive()) endFight(true, false);
        else if (!fe || b != fe) endFight(false, false);  // gone back to sleep, left: abandoned
    }
    const Player& p = w.player();
    if (!fight_ && !died && b && b->alive() && p.alive() && p.state() != PlayerState::Respawn) {
        Fight f;
        f.uid = b->uid();
        f.bossId = b->def().id;
        f.name = b->def().name;
        f.phases = std::max<int>(1, static_cast<int>(b->def().phases.size()));
        f.phase = std::clamp(b->phaseIndex(), 0, f.phases - 1);
        f.removed = std::clamp(1.0f - b->hpRatio(), 0.0f, 1.0f);
        bossNames_[f.bossId] = f.name;
        bossPhases_[f.bossId] = f.phases;
        phaseMarks_.push_back({f.bossId, {f.phase, false}});
        fight_ = f;
    }
}

std::string PlayStatsRecorder::fatalName(const Fight& f) const {
    switch (lastBlow_.origin) {
        case HurtOrigin::Fall: return "Chute dans le vide";
        case HurtOrigin::Hazard: return "Pièges du niveau";
        case HurtOrigin::Contact: return lastBlow_.enemy == f.uid ? "Contact avec le boss" : "Contact avec un ennemi";
        case HurtOrigin::EnemyShot:
        case HurtOrigin::EnemyAttack: return "Ennemis de l'arène";
        case HurtOrigin::BossAttack: return lastBlow_.attack.empty() ? std::string("Attaque du boss") : lastBlow_.attack;
        default: return "Autre";
    }
}

void PlayStatsRecorder::endFight(bool won, bool defeat) {
    if (!fight_) return;
    BossAttemptRec r;
    r.day = store_.today();
    r.won = won;
    r.removed = won ? 1.0f : fight_->removed;
    r.phase = fight_->phase;
    if (defeat) {
        r.attack = fatalName(*fight_);
        phaseMarks_.push_back({fight_->bossId, {fight_->phase, true}});
    }
    attempts_.push_back({fight_->bossId, r});
    fight_.reset();
}

void PlayStatsRecorder::trackSplits(const GameWorld& w) {
    // The route of the run (its checkpoints in the order lit). 2.95: its segments' times come
    // from noteSegment (the rules of the personal Sum of Best: a split taken after a respawn,
    // of a checkpoint passed before, never starts one).
    const auto& sp = w.timer().splits();
    while (splitCount_ < sp.size()) {
        cps_.push_back(w.checkpointId());
        ++splitCount_;
    }
}

void PlayStatsRecorder::noteSegment(const std::string& from, const std::string& to, float seconds) {
    if (!active_ || ended_ || !classic_ || kind_ != Kind::Counted || !(seconds > 0.0f) || !std::isfinite(seconds)) return;
    segTimes_.push_back({{from, to}, seconds});
}

void PlayStatsRecorder::commit() {
    PlayStatsStore::Delta d;
    d.counters = run_;
    d.levelId = levelId_;
    d.levelName = levelName_;
    d.passes = slicePasses_;
    d.deaths = sliceDeaths_;
    d.attempts = std::move(attempts_);
    d.bossNames = bossNames_;
    d.bossPhases = bossPhases_;
    d.phaseMarks = std::move(phaseMarks_);
    d.classic = classic_;
    d.version = version_;
    d.attempt = attemptPending_;
    d.segments = std::move(segTimes_);
    d.finish = finishTime_;
    if (finishTime_ > 0.0f) d.route = cps_;
    d.trainingLevel = trainingLevel_;
    d.training = std::move(trainTimes_);
    store_.merge(d);
    store_.saveAsync();
    run_ = {};
    slicePasses_.fill(0);
    sliceDeaths_.fill(0);
    attempts_.clear();
    phaseMarks_.clear();
    segTimes_.clear();
    trainTimes_.clear();
    attemptPending_ = false;
    finishTime_ = -1.0f;
}

void PlayStatsRecorder::noteTrainingSegment(const std::string& from, const std::string& to, float seconds) {
    if (!active_ || !trainingLevel_ || !(seconds > 0.0f) || !std::isfinite(seconds)) return;
    trainTimes_.push_back({{from, to}, seconds});
}

void PlayStatsRecorder::finish() {
    if (finished_) return;
    if (active_ && !ended_ && kind_ == Kind::Counted) {
        if (fight_) endFight(false, false);  // left during the fight: an attempt abandoned
        endLife(false);
        for (const auto& [uid, s] : shots_) resolveShot(s);
        for (const auto& [uid, b] : blows_) resolveBlow(b);
        shots_.clear();
        blows_.clear();
    }
    if (active_) commit();
    finished_ = true;
    if (world_ && world_->listener == this) world_->listener = nullptr;
}

// ================================================================== store

namespace {
SaveFile statsFile() { return SaveFile("stats", PlayStatsStore::kVersion); }

/// 2.95: a level's segment bests timed by the rules before (a respawn could start a segment)
/// go when its first segments of now come; its times stay (regularity).
void dropOldRules(LevelRuns& r) {
    if (r.sobRules >= kSegmentRules) return;
    for (SegmentRec& s : r.segments) {
        s.best = -1.0f;
        s.trainBest = -1.0f;
    }
    r.sobRules = kSegmentRules;
}
}  // namespace

PlayStatsStore::~PlayStatsStore() {
    flush();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

int PlayStatsStore::today() const { return dayOverride > 0 ? dayOverride : localDay(); }

void PlayStatsStore::open(const fs::path& file) {
    file_ = file;
    data_ = PlayStatsData{};
    damaged_ = readOnly_ = false;
    if (!FileUtil::exists(file)) return;
    std::optional<Json> data;
    int from = 0;
    if (auto text = FileUtil::readText(file)) {
        try {
            data = statsFile().migrate(Json::parse(*text), &from);
        } catch (const Json::exception& e) {
            Log::error("Statistiques illisibles ({}) : {}", Paths::toUtf8(file), e.what());
        }
    }
    if (!data || !data->is_object()) {
        // Damaged: kept aside (stats.json.bak), the statistics start again.
        std::error_code ec;
        fs::path bak = file;
        bak += ".bak";
        fs::remove(bak, ec);
        fs::rename(file, bak, ec);
        if (ec) fs::copy_file(file, bak, fs::copy_options::overwrite_existing, ec);
        damaged_ = true;
        Log::warn("Statistiques abîmées : gardées dans {}, nouvelles statistiques", Paths::toUtf8(bak));
        return;
    }
    readOnly_ = from > kVersion;
    data_ = PlayStatsData::fromJson(*data);
    data_.compact(today());
    // 2.55: the boss attempts of a 2.54 file get their identifier, written at once so that they
    // stay the same (a copy of the progression sent before the next run carries the same ones).
    if (data_.assignIds()) saveAsync();
}

std::unique_ptr<PlayStatsRecorder> PlayStatsStore::beginRun(GameWorld& w, const PlayStatsRun& run, const ContentDatabase& content) {
    if (!enabled || run.editorTest || w.invincible() || run.levelId.empty()) return nullptr;
    const bool training = run.training || (w.training() && !w.challenge());
    std::string name = run.level ? run.level->name : std::string();
    if (const LevelEntry* e = content.level(run.levelId); e && !e->name.empty()) name = e->name;
    auto r = std::make_unique<PlayStatsRecorder>(*this, w, run.levelId, name,
                                                 training ? PlayStatsRecorder::Kind::Training : PlayStatsRecorder::Kind::Counted);
    if (!training && run.level)
        r->setRouteFuture(std::async(std::launch::async, [l = *run.level, &content] { return buildProgressTrack(l, content); }));
    if (!training && run.classic) r->setClassic(run.levelVersion);
    if (training && run.trainingSegments) r->setTrainingSegments(run.levelVersion);
    w.listener = r.get();
    return r;
}

void PlayStatsStore::merge(const Delta& d) {
    const int day = today();
    StatsCounters& c = data_.at(day);
    c.add(d.counters);
    if (currentStars) c.stars = std::max(c.stars, currentStars());
    // The slices of the level.
    bool anySlice = false;
    for (int i = 0; i < kSlices; ++i) anySlice = anySlice || d.passes[static_cast<size_t>(i)] > 0 || d.deaths[static_cast<size_t>(i)] > 0;
    if (anySlice && !d.levelId.empty()) {
        LevelSlices& l = data_.levels[d.levelId];
        if (!d.levelName.empty()) l.name = d.levelName;
        l.decayTo(day);
        for (int i = 0; i < kSlices; ++i) {
            size_t k = static_cast<size_t>(i);
            l.passes[k] += d.passes[k];
            l.deaths[k] += d.deaths[k];
            l.wPasses[k] += static_cast<float>(d.passes[k]);
            l.wDeaths[k] += static_cast<float>(d.deaths[k]);
        }
        l.lastDay = day;
    }
    // The bosses.
    auto bossOf = [&](const std::string& id) -> BossHistory& {
        BossHistory& b = data_.bosses[id];
        if (auto it = d.bossNames.find(id); it != d.bossNames.end() && !it->second.empty()) b.name = it->second;
        if (auto it = d.bossPhases.find(id); it != d.bossPhases.end()) b.phases = std::max(1, it->second);
        if (b.reached.size() < static_cast<size_t>(b.phases)) b.reached.resize(static_cast<size_t>(b.phases), 0);
        if (b.defeats.size() < static_cast<size_t>(b.phases)) b.defeats.resize(static_cast<size_t>(b.phases), 0);
        return b;
    };
    for (const auto& [id, mark] : d.phaseMarks) {
        BossHistory& b = bossOf(id);
        size_t p = static_cast<size_t>(std::clamp(mark.first, 0, b.phases - 1));
        if (mark.second) b.defeats[p]++;
        else b.reached[p]++;
    }
    for (const auto& [id, rec] : d.attempts) {
        BossHistory& b = bossOf(id);
        b.attempts++;
        if (rec.won) {
            b.wins++;
            if (b.firstWin == 0) b.firstWin = b.attempts;
        }
        b.recent.push_back(rec);
        // Its day (never before the previous attempt's: the list stays in order if the clock goes back).
        if (b.recent.back().day <= 0) b.recent.back().day = day;
        if (b.recent.size() > 1) b.recent.back().day = std::max(b.recent.back().day, b.recent[b.recent.size() - 2].day);
        // 2.55: its identifier (the same attempt is counted once when two copies are merged).
        for (bool taken = true; taken;) {
            b.recent.back().id = newAttemptId();
            taken = std::any_of(b.recent.begin(), b.recent.end() - 1, [&](const BossAttemptRec& o) { return o.key() == b.recent.back().key(); });
        }
        if (!rec.attack.empty()) b.fatal[rec.attack]++;
        b.lastSeq = ++data_.seq;
    }
    // A classic level's segments.
    if (d.classic && !d.levelId.empty() && (d.attempt || !d.segments.empty() || d.finish > 0.0f)) {
        LevelRuns& r = data_.runs[d.levelId];
        if (r.version != d.version) {
            // Another version of the level: its segments are not the same any more.
            r = LevelRuns{};
            r.version = d.version;
        }
        if (!d.levelName.empty()) r.name = d.levelName;
        r.lastDay = day;
        dropOldRules(r);
        const float pbBefore = r.pb, sobBefore = r.sob();
        if (d.attempt) r.attempts++;
        for (const auto& [ends, t] : d.segments) {
            if (t <= 0.0f || !std::isfinite(t)) continue;
            SegmentRec& s = r.segmentAt(ends.first, ends.second);
            s.times.push_back(t);
            if (s.best <= 0.0f || t < s.best) s.best = t;
        }
        if (d.finish > 0.0f) {
            r.finished = std::min(r.attempts, r.finished + 1);
            r.finishes.push_back({day, d.finish});
            if (r.pb <= 0.0f || d.finish < r.pb) {
                r.pb = d.finish;
                r.pbRoute = d.route;
            }
        }
        const float sob = r.sob();
        auto changed = [](float a, float b) { return (a > 0.0f) != (b > 0.0f) || std::abs(a - b) > 0.005f; };
        if (r.pb > 0.0f && (changed(r.pb, pbBefore) || changed(sob, sobBefore))) {
            if (!r.history.empty() && r.history.back().day == day) r.history.back() = {day, r.pb, sob};
            else r.history.push_back({day, r.pb, sob});
        }
    }
    // 2.60.3: a section training of a classic level: the best time of its segment, counted in
    // the Sum of Best only (never the times, the best of the full runs, the PB nor the attempts).
    if (d.trainingLevel && !d.levelId.empty() && !d.training.empty()) {
        LevelRuns& r = data_.runs[d.levelId];
        if (r.version != d.version) {
            r = LevelRuns{};
            r.version = d.version;
        }
        if (!d.levelName.empty()) r.name = d.levelName;
        r.lastDay = day;
        dropOldRules(r);
        const float sobBefore = r.sob();
        for (const auto& [ends, t] : d.training) {
            if (t <= 0.0f || !std::isfinite(t)) continue;
            SegmentRec& s = r.segmentAt(ends.first, ends.second);
            if (s.trainBest <= 0.0f || t < s.trainBest) s.trainBest = t;
        }
        const float sob = r.sob();
        auto changed = [](float a, float b) { return (a > 0.0f) != (b > 0.0f) || std::abs(a - b) > 0.005f; };
        if (r.pb > 0.0f && changed(sob, sobBefore)) {
            if (!r.history.empty() && r.history.back().day == day) r.history.back() = {day, r.pb, sob};
            else r.history.push_back({day, r.pb, sob});
        }
    }
    data_.compact(day);
}

std::string PlayStatsStore::serialize(const PlayStatsData& d) {
    Json env;
    env["format"] = "cosmic.stats";
    env["version"] = kVersion;
    env["savedAt"] = FileUtil::nowString();
    env["engine"] = COSMIC_VERSION_STRING;
    env["data"] = d.toJson();
    return env.dump(-1, ' ', false, Json::error_handler_t::replace) + "\n";  // never throws on a bad character
}

void PlayStatsStore::saveAsync() {
    if (readOnly_ || file_.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = data_;  // a copy: the worker serialises and writes it
        if (!thread_.joinable()) thread_ = std::thread([this] { worker(); });
    }
    cv_.notify_all();
}

void PlayStatsStore::worker() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        cv_.wait(lock, [this] { return pending_.has_value() || stop_; });
        if (!pending_) return;  // stopping, nothing left to write
        PlayStatsData snap = std::move(*pending_);
        pending_.reset();
        writing_ = true;
        lock.unlock();
        const std::string text = serialize(snap);
        if (!FileUtil::writeTextAtomic(file_, text)) Log::error("Statistiques : écriture impossible ({})", Paths::toUtf8(file_));
        lock.lock();
        writing_ = false;
        cv_.notify_all();
    }
}

void PlayStatsStore::flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !pending_.has_value() && !writing_; });
}

void PlayStatsStore::reset() {
    data_ = PlayStatsData{};
    damaged_ = false;
    saveAsync();
}

void PlayStatsStore::replace(PlayStatsData d) {
    data_ = std::move(d);
    data_.assignIds();
    data_.compact(today());
    damaged_ = false;
    saveAsync();
}

}  // namespace cosmic
