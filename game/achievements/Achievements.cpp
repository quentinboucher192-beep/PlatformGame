#include "game/achievements/Achievements.h"

#include <algorithm>
#include <cmath>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/content/ContentDatabase.h"
#include "game/progression/Progression.h"
#include "game/records/BossStats.h"
#include "game/records/Records.h"

namespace cosmic {

// ---------------------------------------------------------------- catalogue

bool AchievementCatalog::load(const fs::path& file, std::string* error) {
    categories_.clear();
    achievements_.clear();
    index_.clear();
    auto j = FileUtil::loadJson(file);
    if (!j || jstr(*j, "format") != "cosmic.achievements") {
        if (error) *error = "Catalogue des succès introuvable ou illisible.";
        return false;
    }
    if (j->contains("categories") && (*j)["categories"].is_array())
        for (const Json& c : (*j)["categories"]) {
            AchievementCategory cat;
            cat.id = jstr(c, "id");
            cat.name = jstr(c, "name");
            cat.description = jstr(c, "description");
            cat.color = Color::fromHex(jstr(c, "color"), cat.color);
            if (!cat.id.empty()) categories_.push_back(cat);
        }
    if (j->contains("achievements") && (*j)["achievements"].is_array())
        for (const Json& a : (*j)["achievements"]) {
            AchievementDef d;
            d.id = jstr(a, "id");
            d.category = jstr(a, "category");
            d.name = jstr(a, "name");
            d.description = jstr(a, "description");
            d.stars = std::clamp(jget<int>(a, "stars", 1), 1, 5);
            if (a.contains("condition") && a["condition"].is_object()) {
                const Json& c = a["condition"];
                AchievementCondition& k = d.condition;
                k.type = jstr(c, "type");
                k.key = jstr(c, "key");
                k.level = jstr(c, "level");
                k.planet = jstr(c, "planet");
                k.boss = jstr(c, "boss");
                k.power = jstr(c, "power");
                k.variant = jstr(c, "variant");
                k.planets = jget<std::vector<std::string>>(c, "planets", {});
                k.count = std::max<int64_t>(1, jget<int64_t>(c, "count", 1));
                k.medal = std::clamp(jget<int>(c, "medal", 0), 0, 4);
                k.seconds = jget<double>(c, "seconds", 0.0);
                k.noHit = jget<bool>(c, "nohit", false);
            }
            d.after = jget<std::vector<std::string>>(a, "requires", {});
            if (d.id.empty() || index_.count(d.id) || !category(d.category)) continue;
            index_[d.id] = achievements_.size();
            achievements_.push_back(std::move(d));
        }
    // Profile badges (2.41): steps in increasing order (stars and slots), else none at all.
    badgeSlots_.clear();
    if (auto it = j->find("badgeSlots"); it != j->end() && it->is_array())
        for (const Json& b : *it) {
            BadgeSlotStep st{jget<int>(b, "stars", 0), jget<int>(b, "slots", 0)};
            bool ok = st.stars > 0 && st.slots > 0 && st.slots <= 10 &&
                      (badgeSlots_.empty() || (st.stars > badgeSlots_.back().stars && st.slots > badgeSlots_.back().slots));
            if (!ok) {
                Log::warn("Succès : paliers des badges invalides (badgeSlots), badges désactivés");
                badgeSlots_.clear();
                break;
            }
            badgeSlots_.push_back(st);
        }
    if (achievements_.empty()) {
        if (error) *error = "Aucun succès dans le catalogue.";
        return false;
    }
    return true;
}

int AchievementCatalog::badgeSlots(int stars) const {
    int n = 0;
    for (const auto& st : badgeSlots_)
        if (stars >= st.stars) n = st.slots;
    return n;
}

const BadgeSlotStep* AchievementCatalog::nextBadgeStep(int stars) const {
    for (const auto& st : badgeSlots_)
        if (stars < st.stars) return &st;
    return nullptr;
}

const AchievementDef* AchievementCatalog::find(const std::string& id) const {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &achievements_[it->second];
}

const AchievementCategory* AchievementCatalog::category(const std::string& id) const {
    for (const auto& c : categories_)
        if (c.id == id) return &c;
    return nullptr;
}

int AchievementCatalog::totalStars() const {
    int n = 0;
    for (const auto& a : achievements_) n += a.stars;
    return n;
}

// ---------------------------------------------------------------- counters

void AchievementStats::add(const std::string& key, int64_t n) {
    if (n > 0) counters[key] += n;
}
void AchievementStats::raise(const std::string& key, int64_t value) {
    int64_t& v = maxima[key];
    v = std::max(v, value);
}
void AchievementStats::insert(const std::string& set, const std::string& value) {
    if (!value.empty()) sets[set].insert(value);
}
int64_t AchievementStats::counter(const std::string& key) const {
    auto it = counters.find(key);
    return it == counters.end() ? 0 : it->second;
}
int64_t AchievementStats::maximum(const std::string& key) const {
    auto it = maxima.find(key);
    return it == maxima.end() ? 0 : it->second;
}
size_t AchievementStats::setSize(const std::string& key) const {
    auto it = sets.find(key);
    return it == sets.end() ? 0 : it->second.size();
}
bool AchievementStats::setHas(const std::string& key, const std::string& value) const {
    auto it = sets.find(key);
    return it != sets.end() && it->second.count(value) > 0;
}

// ---------------------------------------------------------------- conditions

namespace {

bool isEcho(const AchievementInputs& in, const std::string& bossId) {
    if (in.content)
        if (const EnemyDefinition* d = in.content->enemy(bossId)) return !d->variantOf.empty();
    return bossId.rfind("echo_", 0) == 0;
}

bool variantMatches(const AchievementInputs& in, const std::string& bossId, const std::string& variant) {
    if (variant.empty()) return true;
    bool echo = isEcho(in, bossId);
    if (variant == "echo") return echo;
    // "original": a planet's guardian (neither an echo nor the Astrarque of the star).
    return !echo && bossId != "astrarque";
}

int completedWith(const AchievementInputs& in, bool (*pred)(const LevelRecord&)) {
    if (!in.records) return 0;
    int n = 0;
    for (const auto& id : in.campaignLevels)
        if (const LevelRecord* r = in.records->get(id); r && r->completions > 0 && pred(*r)) ++n;
    return n;
}

std::vector<std::string> planetLevels(const AchievementInputs& in, const std::string& planet) {
    std::vector<std::string> out;
    if (!in.content) return out;
    for (const LevelEntry* e : in.content->levelsForPlanet(planet)) out.push_back(e->id);
    return out;
}

bool completed(const AchievementInputs& in, const std::string& level) {
    if (in.progression && in.progression->levelCompleted(level)) return true;
    const LevelRecord* r = in.records ? in.records->get(level) : nullptr;
    return r && r->completions > 0;
}

int medalOf(const AchievementInputs& in, const std::string& level) {
    const LevelRecord* r = in.records ? in.records->get(level) : nullptr;
    return r ? static_cast<int>(r->bestMedal) : 0;
}

bool allSecrets(const LevelRecord& r) {
    for (const RunResult& run : r.leaderboard)
        if (run.completed && run.totalSecrets > 0 && run.secrets >= run.totalSecrets) return true;
    return false;
}

bool noDeath(const LevelRecord& r) { return r.fewestDeaths == 0; }
bool fullCollect(const LevelRecord& r) { return r.totalCollectibles > 0 && r.mostCollectibles >= r.totalCollectibles; }

/// Levels done in a way (`pred` on their record, or the run counters' `set`): the campaign's,
/// counted as before 2.24; with a planet, that planet's levels only, each once.
int64_t levelsDone(const AchievementInputs& in, const AchievementStats& st, const std::string& planet, bool (*pred)(const LevelRecord&),
                   const char* set) {
    if (planet.empty()) return std::max<int64_t>(completedWith(in, pred), static_cast<int64_t>(st.setSize(set)));
    int64_t n = 0;
    for (const auto& id : planetLevels(in, planet)) {
        const LevelRecord* r = in.records ? in.records->get(id) : nullptr;
        n += (r && r->completions > 0 && pred(*r)) || st.setHas(set, id) ? 1 : 0;
    }
    return n;
}

}  // namespace

AchievementProgress AchievementBook::progress(const AchievementDef& def, const AchievementInputs& in) {
    const AchievementCondition& c = def.condition;
    AchievementProgress p;
    p.target = c.count;
    const AchievementStats empty;
    const AchievementStats& st = in.stats ? *in.stats : empty;
    const std::string& t = c.type;
    if (t == "levels_completed") {
        int n = 0;
        for (const auto& id : in.campaignLevels) n += completed(in, id) ? 1 : 0;
        p.value = n;
    } else if (t == "level") {
        p.target = 1;
        p.value = completed(in, c.level) ? 1 : 0;
    } else if (t == "planet" || t == "planets" || t == "all_levels") {
        std::vector<std::string> levels;
        if (t == "planet") levels = planetLevels(in, c.planet);
        else if (t == "planets")
            for (const auto& pl : c.planets) {
                auto l = planetLevels(in, pl);
                levels.insert(levels.end(), l.begin(), l.end());
            }
        else levels = in.campaignLevels;
        p.target = std::max<int64_t>(1, static_cast<int64_t>(levels.size()));
        for (const auto& id : levels) p.value += completed(in, id) ? 1 : 0;
    } else if (t == "medal_count") {
        for (const auto& id : in.campaignLevels) p.value += medalOf(in, id) >= c.medal ? 1 : 0;
    } else if (t == "medal_level") {
        p.target = 1;
        p.value = medalOf(in, c.level) >= c.medal ? 1 : 0;
    } else if (t == "medal_planet_any") {
        // The planet closest to it counts (its levels with the medal / its levels).
        p.value = 0;
        p.target = 4;
        double best = -1.0;
        if (in.content)
            for (const PlanetData* pl : in.content->planets()) {
                auto levels = planetLevels(in, pl->id);
                if (levels.empty()) continue;
                int n = 0;
                for (const auto& id : levels) n += medalOf(in, id) >= c.medal ? 1 : 0;
                double f = static_cast<double>(n) / static_cast<double>(levels.size());
                if (f > best) {
                    best = f;
                    p.value = n;
                    p.target = static_cast<int64_t>(levels.size());
                }
            }
    } else if (t == "stat") {
        p.value = st.counter(c.key);
    } else if (t == "max") {
        p.value = st.maximum(c.key);
        if (c.key == "run_kills" && in.records)
            for (const auto& id : in.campaignLevels)
                if (const LevelRecord* r = in.records->get(id)) p.value = std::max<int64_t>(p.value, r->mostKills);
        if (c.key == "level_attempts" && in.records)
            for (const auto& id : in.campaignLevels)
                if (const LevelRecord* r = in.records->get(id)) p.value = std::max<int64_t>(p.value, r->attempts);
    } else if (t == "set") {
        p.value = static_cast<int64_t>(st.setSize(c.key));
        if (c.key == "community_levels") p.value = std::max<int64_t>(p.value, in.communityLevelsFinished);
    } else if (t == "medal_planet") {
        // Every level of one planet with the medal (2.24).
        auto levels = planetLevels(in, c.planet);
        p.target = std::max<int64_t>(1, static_cast<int64_t>(levels.size()));
        for (const auto& id : levels) p.value += medalOf(in, id) >= c.medal ? 1 : 0;
    } else if (t == "levels_no_death") {
        p.value = levelsDone(in, st, c.planet, noDeath, "no_death_levels");
    } else if (t == "level_no_death") {
        p.target = 1;
        const LevelRecord* r = in.records ? in.records->get(c.level) : nullptr;
        p.value = (r && r->completions > 0 && r->fewestDeaths == 0) || st.setHas("no_death_levels", c.level) ? 1 : 0;
    } else if (t == "levels_full_collect") {
        p.value = levelsDone(in, st, c.planet, fullCollect, "full_collect_levels");
    } else if (t == "levels_all_secrets") {
        p.value = levelsDone(in, st, c.planet, allSecrets, "all_secrets_levels");
    } else if (t == "boss") {
        p.target = 1;
        const BossRecord* b = in.bosses ? in.bosses->find(c.boss) : nullptr;
        // "nohit": this boss beaten without being hit at least once (2.19).
        p.value = b && (c.noHit ? b->noHitKills > 0 : b->kills > 0) ? 1 : 0;
    } else if (t == "boss_distinct" || t == "boss_nohit_distinct" || t == "boss_total" || t == "boss_phase_nohit") {
        if (in.bosses)
            for (const auto& [id, b] : in.bosses->all()) {
                if (!variantMatches(in, id, c.variant)) continue;
                if (t == "boss_distinct") p.value += b.kills > 0 ? 1 : 0;
                else if (t == "boss_nohit_distinct") p.value += b.noHitKills > 0 ? 1 : 0;
                else if (t == "boss_total") p.value += b.kills;
                else p.value += b.noHitPhases();
            }
    } else if (t == "boss_phases_nohit") {
        // Each phase of this boss beaten at least once without being hit, not necessarily in
        // the same fight (2.24); its phases are those of its definition.
        const BossRecord* b = in.bosses ? in.bosses->find(c.boss) : nullptr;
        int64_t phases = 0;
        if (in.content)
            if (const EnemyDefinition* d = in.content->enemy(c.boss)) phases = static_cast<int64_t>(d->phases.size());
        if (phases <= 0 && b) phases = b->phaseCount();
        p.target = std::max<int64_t>(1, phases);
        if (b)
            for (size_t i = 0; i < b->phaseNoHit.size() && static_cast<int64_t>(i) < p.target; ++i) p.value += b->phaseNoHit[i] > 0 ? 1 : 0;
    } else if (t == "tracks_heard") {
        p.value = in.tracksHeard;
    } else if (t == "creatures_met") {
        p.value = in.creaturesMet;
    } else if (t == "power_uses") {
        auto it = st.powerUses.find(c.power);
        p.value = it == st.powerUses.end() ? 0 : it->second;
    } else if (t == "powers_distinct") {
        p.value = static_cast<int64_t>(st.setSize("powers_used"));
    } else if (t == "playtime") {
        p.target = static_cast<int64_t>(c.seconds);
        p.value = static_cast<int64_t>(in.playTimeSeconds);
    } else if (t == "user_levels") {
        p.value = in.userLevels;
    } else if (t == "user_enemies") {
        p.value = in.userEnemies;
    } else if (t == "completion_proofs") {
        p.value = std::max<int64_t>(in.completionProofs, st.counter("completion_proofs"));
    } else if (t == "online_account") {
        p.target = 1;
        p.value = in.onlineAccount || st.counter("online_login") > 0 ? 1 : 0;
    } else if (t == "friends") {
        p.value = std::max<int64_t>(in.friends, st.maximum("friends"));
    } else if (t == "raid_reward") {
        // 2.68: a badge of the clan raids, claimed from Cosmic Online (its reward's id in `key`).
        p.target = 1;
        p.value = in.raidRewards.count(c.key) ? 1 : 0;
    } else {
        p.value = 0;  // unknown condition (newer catalogue): never unlocked by mistake
        p.target = std::max<int64_t>(p.target, 1);
    }
    p.value = std::max<int64_t>(0, p.value);
    return p;
}

// ---------------------------------------------------------------- book

std::string AchievementBook::unlockDate(const std::string& id) const {
    auto it = unlocked_.find(id);
    return it == unlocked_.end() ? std::string{} : it->second;
}

int AchievementBook::stars(const AchievementCatalog& catalog) const {
    int n = 0;
    for (const auto& a : catalog.achievements()) n += unlocked(a.id) ? a.stars : 0;
    return n;
}

int AchievementBook::stars(const AchievementCatalog& catalog, const std::string& category) const {
    int n = 0;
    for (const auto& a : catalog.achievements())
        if (a.category == category && unlocked(a.id)) n += a.stars;
    return n;
}

bool AchievementBook::available(const AchievementDef& def) const {
    return std::all_of(def.after.begin(), def.after.end(), [&](const std::string& r) { return unlocked(r); });
}

std::vector<const AchievementDef*> AchievementBook::evaluate(const AchievementCatalog& catalog, const AchievementInputs& in,
                                                              const std::string& date) {
    std::vector<const AchievementDef*> out;
    // Until nothing changes: an unlock can open the next ones of its thread.
    for (bool changed = true; changed;) {
        changed = false;
        for (const AchievementDef& a : catalog.achievements()) {
            if (unlocked(a.id) || !available(a)) continue;
            if (!progress(a, in).done()) continue;
            unlocked_[a.id] = date;
            untrack(a.id);  // done: no longer followed (its slot is free again)
            announce_.push_back(a.id);
            out.push_back(&a);
            changed = true;
        }
    }
    return out;
}

bool AchievementBook::isTracked(const std::string& id) const { return std::find(tracked_.begin(), tracked_.end(), id) != tracked_.end(); }

bool AchievementBook::track(const std::string& id) {
    if (id.empty() || id.size() > 64 || unlocked(id)) return false;
    if (isTracked(id)) return true;
    if (tracked_.size() >= kMaxTracked) return false;
    tracked_.push_back(id);
    return true;
}

void AchievementBook::untrack(const std::string& id) { tracked_.erase(std::remove(tracked_.begin(), tracked_.end(), id), tracked_.end()); }

std::vector<std::string> AchievementBook::takeAnnouncements() {
    std::vector<std::string> v;
    v.swap(announce_);
    return v;
}

Json AchievementBook::toJson() const {
    Json counters = Json::object(), maxima = Json::object(), sets = Json::object(), powers = Json::object(), unlocked = Json::object();
    for (const auto& [k, v] : stats_.counters) counters[k] = v;
    for (const auto& [k, v] : stats_.maxima) maxima[k] = v;
    for (const auto& [k, v] : stats_.sets) sets[k] = std::vector<std::string>(v.begin(), v.end());
    for (const auto& [k, v] : stats_.powerUses) powers[k] = v;
    for (const auto& [k, v] : unlocked_) unlocked[k] = v;
    return Json{{"format", "cosmic.achievement_book"}, {"version", kVersion}, {"unlocked", unlocked},
                {"counters", counters},             {"maxima", maxima},       {"sets", sets}, {"power_uses", powers},
                {"tracked", tracked_}};
}

void AchievementBook::fromJson(const Json& j) {
    unlocked_.clear();
    tracked_.clear();
    stats_ = AchievementStats{};
    if (!j.is_object()) return;
    // A save is user data: keys and values are bounded and checked.
    auto okKey = [](const std::string& k) { return !k.empty() && k.size() <= 64; };
    auto num = [](const Json& v) -> int64_t {
        if (!v.is_number()) return 0;
        double d = v.get<double>();
        return std::isfinite(d) ? static_cast<int64_t>(std::clamp(d, 0.0, 1e15)) : 0;
    };
    if (j.contains("unlocked") && j["unlocked"].is_object())
        for (const auto& [k, v] : j["unlocked"].items())
            if (okKey(k) && unlocked_.size() < 2000) unlocked_[k] = v.is_string() ? v.get<std::string>().substr(0, 32) : std::string{};
    auto readMap = [&](const char* key, std::map<std::string, int64_t>& out) {
        if (!j.contains(key) || !j[key].is_object()) return;
        for (const auto& [k, v] : j[key].items())
            if (okKey(k) && out.size() < 500) out[k] = num(v);
    };
    // Followed achievements (2.52): each once, at most kMaxTracked, none already unlocked.
    if (j.contains("tracked") && j["tracked"].is_array())
        for (const Json& x : j["tracked"])
            if (x.is_string()) track(x.get<std::string>());
    readMap("counters", stats_.counters);
    readMap("maxima", stats_.maxima);
    readMap("power_uses", stats_.powerUses);
    if (j.contains("sets") && j["sets"].is_object())
        for (const auto& [k, v] : j["sets"].items()) {
            if (!okKey(k) || !v.is_array() || stats_.sets.size() >= 100) continue;
            auto& s = stats_.sets[k];
            for (const Json& x : v)
                if (x.is_string() && s.size() < 5000) {
                    std::string val = x.get<std::string>();
                    if (val.size() <= 128) s.insert(val);
                }
        }
}

void AchievementBook::load() {
    if (file_.empty()) file_ = Paths::savesDir() / "achievements.json";
    std::error_code ec;
    if (!fs::exists(file_, ec)) return;
    if (auto j = FileUtil::loadJson(file_)) fromJson(*j);
}

bool AchievementBook::save() const {
    if (file_.empty()) return false;
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    return FileUtil::writeTextAtomic(file_, toJson().dump(1));
}

// ---------------------------------------------------------------- profile badges

std::vector<std::string> shownBadges(const std::vector<std::string>& chosen, const AchievementCatalog& catalog, const AchievementBook& book) {
    const size_t slots = static_cast<size_t>(std::max(0, catalog.badgeSlots(book.stars(catalog))));
    std::vector<std::string> out;
    for (const std::string& id : chosen) {
        if (out.size() >= slots) break;
        if (!catalog.find(id) || !book.unlocked(id) || std::find(out.begin(), out.end(), id) != out.end()) continue;
        out.push_back(id);
    }
    return out;
}

}  // namespace cosmic
