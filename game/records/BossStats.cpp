#include "game/records/BossStats.h"

#include <algorithm>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "saves/SaveFile.h"

namespace cosmic {

namespace {
SaveFile bossFile() { return SaveFile("boss_stats", BossStats::kVersion); }
fs::path bossPath() { return Paths::savesDir() / "boss_stats.json"; }

Json ints(const std::vector<int>& v) {
    Json a = Json::array();
    for (int x : v) a.push_back(x);
    return a;
}
std::vector<int> readInts(const Json& j, const char* key) {
    std::vector<int> v;
    if (j.contains(key) && j[key].is_array())
        for (const auto& x : j[key]) v.push_back(x.is_number_integer() ? std::max(0, x.get<int>()) : 0);
    return v;
}
void grow(std::vector<int>& v, size_t n) {
    if (v.size() < n) v.resize(n, 0);
}
}  // namespace

int BossRecord::noHitPhases() const {
    int n = 0;
    for (int x : phaseNoHit) n += x;
    return n;
}

Json BossRecord::toJson() const {
    Json names = Json::array();
    for (const auto& n : phaseNames) names.push_back(n);
    return {{"name", name},     {"fights", fights},          {"kills", kills},          {"defeats", defeats},
            {"noHitKills", noHitKills}, {"phaseNames", names}, {"phaseClears", ints(phaseClears)}, {"phaseNoHit", ints(phaseNoHit)},
            {"bestKill", bestKill},   {"lastKill", lastKill}};
}

BossRecord BossRecord::fromJson(const Json& j) {
    BossRecord r;
    r.name = jstr(j, "name");
    r.fights = std::max(0, jget<int>(j, "fights", 0));
    r.kills = std::max(0, jget<int>(j, "kills", 0));
    r.defeats = std::max(0, jget<int>(j, "defeats", 0));
    r.noHitKills = std::max(0, jget<int>(j, "noHitKills", 0));
    if (j.contains("phaseNames") && j["phaseNames"].is_array())
        for (const auto& n : j["phaseNames"]) r.phaseNames.push_back(n.is_string() ? n.get<std::string>() : std::string{});
    r.phaseClears = readInts(j, "phaseClears");
    r.phaseNoHit = readInts(j, "phaseNoHit");
    r.bestKill = std::max(0.0f, jget<float>(j, "bestKill", 0.0f));
    r.lastKill = jstr(j, "lastKill");
    return r;
}

const BossRecord* BossStats::find(const std::string& bossId) const {
    auto it = bosses_.find(bossId);
    return it == bosses_.end() ? nullptr : &it->second;
}

Json BossStats::toJson() const {
    Json b = Json::object();
    for (const auto& [id, r] : bosses_) b[id] = r.toJson();
    return {{"bosses", b}, {"storiesSeen", std::vector<std::string>(storiesSeen_.begin(), storiesSeen_.end())}};
}

void BossStats::fromJson(const Json& j) {
    bosses_.clear();
    storiesSeen_.clear();
    if (j.contains("bosses") && j["bosses"].is_object())
        for (const auto& [id, r] : j["bosses"].items()) bosses_[id] = BossRecord::fromJson(r);
    if (j.contains("storiesSeen") && j["storiesSeen"].is_array())
        for (const auto& id : j["storiesSeen"])
            if (id.is_string() && !id.get<std::string>().empty()) storiesSeen_.insert(id.get<std::string>());
}

void BossStats::load() {
    if (auto data = bossFile().load(bossPath())) fromJson(*data);
    dirty_ = false;
}

bool BossStats::save() {
    if (!dirty_) return true;
    bool ok = bossFile().save(bossPath(), toJson());
    if (ok) dirty_ = false;
    return ok;
}

// ===================================================================== tracker

void BossFightTracker::onWorldEvent(const std::string& source, const std::string& trigger) {
    if (trigger == "bossDefeated" && fight_ && !fight_->objectId.empty() && source == fight_->objectId) defeatedEvent_ = true;
}

void BossFightTracker::begin(GameWorld& w, Enemy& boss, BossStats& stats, int hits) {
    Fight f;
    f.enemy = &boss;
    f.objectId = boss.objectId();
    f.bossId = boss.def().id;
    f.name = boss.def().name;
    f.phase = boss.phaseIndex();
    f.hitsAtPhase = hits;
    f.hitsAtStart = hits;
    BossRecord& r = stats.at(f.bossId);
    r.name = boss.def().name;
    size_t phases = std::max<size_t>(1, boss.def().phases.size());
    r.phaseNames.assign(phases, std::string{});
    for (size_t i = 0; i < boss.def().phases.size(); ++i) r.phaseNames[i] = boss.def().phases[i].name;
    grow(r.phaseClears, phases);
    grow(r.phaseNoHit, phases);
    r.fights++;
    stats.touch();
    std::string key = f.objectId.empty() ? f.bossId : f.objectId;
    if (!firstStart_.count(key)) firstStart_[key] = w.time();
    else if (hitBefore_[key]) f.taintedPhase = boss.phaseIndex();  // resumed: this phase began before
    engaged_.insert(key);
    interrupted_.erase(key);
    fight_ = f;
    defeatedEvent_ = false;
}

void BossFightTracker::phaseDone(BossStats& stats, int hits) {
    BossRecord& r = stats.at(fight_->bossId);
    size_t p = static_cast<size_t>(std::max(0, fight_->phase));
    grow(r.phaseClears, p + 1);
    grow(r.phaseNoHit, p + 1);
    r.phaseClears[p]++;
    if (hits == fight_->hitsAtPhase && fight_->phase != fight_->taintedPhase) {
        r.phaseNoHit[p]++;
        Event e;
        e.kind = Event::Kind::PhaseNoHit;
        e.bossId = fight_->bossId;
        e.bossName = r.name;
        e.phase = static_cast<int>(p);
        e.phaseName = p < r.phaseNames.size() ? r.phaseNames[p] : std::string{};
        events_.push_back(e);
    }
    stats.touch();
}

void BossFightTracker::step(GameWorld& w, BossStats& stats) {
    // A training run (boss training) never counts: no fight, victory, defeat nor phase.
    if (w.training()) return;
    const int deaths = w.stats().deaths;
    const int hits = w.stats().damageTaken + deaths;
    Enemy* b = w.activeBoss();
    if (deaths > lastDeaths_) {
        // Any death taints every boss of this run not beaten yet — even when it is counted
        // after the fight was left (instant death, boss already back to sleep): finishing that
        // boss later is never « sans dégât ».
        for (const auto& k : engaged_) {
            hitBefore_[k] = true;
            diedBefore_[k] = true;
        }
        // A fight left just before the death (boss gone back to sleep first): a defeat too.
        if (!fight_)
            for (const auto& [k, bossId] : interrupted_) {
                stats.at(bossId).defeats++;
                stats.touch();
            }
        interrupted_.clear();
    }
    if (fight_) {
        std::string key = fight_->objectId.empty() ? fight_->bossId : fight_->objectId;
        if (deaths > lastDeaths_) {
            // The player died during the fight: lost (the boss keeps its wounds).
            BossRecord& r = stats.at(fight_->bossId);
            r.defeats++;
            stats.touch();
            hitBefore_[key] = true;
            diedBefore_[key] = true;
            fight_.reset();
        } else if (b == fight_->enemy && b && b->alive()) {
            // Phases beaten while the boss is still standing.
            while (fight_->phase < b->phaseIndex()) {
                phaseDone(stats, hits);
                fight_->phase++;
                fight_->hitsAtPhase = hits;
            }
        } else {
            // The boss is no longer the active one: beaten, or gone.
            bool beaten = defeatedEvent_;
            if (!beaten)
                for (const auto& e : w.enemies())
                    if (e.get() == fight_->enemy) {
                        beaten = !e->alive();
                        break;
                    }
            if (!beaten) interrupted_[key] = fight_->bossId;
            if (beaten) {
                engaged_.erase(key);
                phaseDone(stats, hits);
                BossRecord& r = stats.at(fight_->bossId);
                r.kills++;
                bool noHit = hits == fight_->hitsAtStart && !hitBefore_[key];
                if (noHit) r.noHitKills++;
                if (!diedBefore_[key]) {
                    float t = w.time() - firstStart_[key];
                    if (t > 0.0f && (r.bestKill <= 0.0f || t < r.bestKill)) r.bestKill = t;
                }
                r.lastKill = FileUtil::nowString();
                stats.touch();
                Event e;
                e.kind = Event::Kind::Kill;
                e.bossId = fight_->bossId;
                e.bossName = r.name;
                e.noHit = noHit;
                e.kills = r.kills;
                events_.push_back(e);
                kills_.push_back(e);
            }
            if (hits != fight_->hitsAtStart) hitBefore_[key] = true;
            fight_.reset();
        }
        if (fight_ && hits != fight_->hitsAtStart) hitBefore_[key] = true;
    }
    lastDeaths_ = deaths;
    // A fight starts (or starts again after a death, once the player is back).
    if (!fight_ && b && b->alive() && w.player().alive() && w.player().state() != PlayerState::Respawn) begin(w, *b, stats, hits);
}

std::vector<BossFightTracker::Event> BossFightTracker::takeEvents() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

}  // namespace cosmic
