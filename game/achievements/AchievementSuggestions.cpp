#include "game/achievements/AchievementSuggestions.h"

#include <algorithm>
#include <cmath>

#include "game/content/ContentDatabase.h"
#include "game/levels/LevelData.h"
#include "game/progression/Progression.h"
#include "game/records/BossStats.h"
#include "game/records/Records.h"

namespace cosmic {

namespace {

/// The conditions AchievementBook::progress() knows (a newer catalogue may have others).
bool knownType(const std::string& t) {
    static const std::set<std::string> ids = {"levels_completed", "all_levels", "medal_count", "medal_level", "medal_planet_any", "medal_planet",
                                              "levels_no_death", "level_no_death", "levels_full_collect", "levels_all_secrets", "boss_distinct",
                                              "boss_nohit_distinct", "boss_total", "boss_phase_nohit", "boss_phases_nohit", "tracks_heard",
                                              "creatures_met", "power_uses", "powers_distinct", "user_levels", "user_enemies",
                                              "completion_proofs", "online_account"};
    return ids.count(t) > 0 || t == "level" || t == "planet" || t == "planets" || t == "stat" || t == "max" || t == "set" || t == "boss" ||
           t == "playtime" || t == "friends";
}

std::string thousands(int64_t v) {
    std::string s = std::to_string(std::max<int64_t>(0, v)), out;
    int n = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (n && n % 3 == 0) out.insert(out.begin(), ' ');
        out.insert(out.begin(), *it);
        ++n;
    }
    return out;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

/// Rough minutes of play per unit still to do (never shown: it only ranks the suggestions).
double minutesPerUnit(const AchievementCondition& c) {
    const std::string& t = c.type;
    const std::string& k = c.key;
    if (t == "stat") {
        if (startsWith(k, "kills")) return 0.1;
        if (k == "jumps") return 0.01;
        if (k == "deaths") return 0.3;
        if (k == "shards") return 0.1;
        if (k == "power_uses") return 0.05;
        if (k == "relics") return 3.0;
        if (k == "secrets") return 4.0;
        if (k == "extreme_attempts") return 2.0;
        if (k == "extreme_finished") return 30.0;
        if (k == "runs_completed") return 5.0;
        if (k == "menu_music" || k == "replay_compares" || k == "replay_aligns" || k == "exports") return 1.0;
        return 6.0;
    }
    if (t == "max") return k == "run_kills" ? 0.3 : 1.0;
    if (t == "set") {
        if (k == "equipped_items" || k == "photographed_planets" || k == "rated_levels" || k == "rated_campaign") return 0.5;
        if (k == "powers_owned") return 5.0;
        if (k == "community_levels") return 5.0;
        return 12.0;
    }
    if (t == "power_uses") return 0.05;
    if (t == "powers_distinct") return 5.0;
    if (t == "playtime") return 1.0 / 60.0;  // per second
    if (t == "tracks_heard") return 1.0;
    if (t == "creatures_met") return 3.0;
    if (startsWith(t, "medal")) return 4.0 + 3.0 * static_cast<double>(std::max(1, c.medal));
    if (t == "boss") return c.noHit ? 25.0 : 8.0;
    if (t == "boss_distinct" || t == "boss_total") return 8.0;
    if (t == "boss_nohit_distinct") return 25.0;
    if (t == "boss_phase_nohit" || t == "boss_phases_nohit") return 6.0;
    if (t == "levels_no_death" || t == "level_no_death" || t == "levels_full_collect" || t == "levels_all_secrets") return 10.0;
    if (t == "user_levels") return 15.0;
    if (t == "user_enemies" || t == "completion_proofs" || t == "friends") return 10.0;
    if (t == "online_account") return 2.0;
    return 6.0;  // levels to finish
}

/// What one unit is, for « Plus que 3 … » (empty: just the number).
const char* unitOf(const AchievementCondition& c, int64_t n) {
    const bool one = n <= 1;
    const std::string& t = c.type;
    const std::string& k = c.key;
    if (t == "stat") {
        if (startsWith(k, "kills")) return one ? "ennemi" : "ennemis";
        if (k == "jumps") return one ? "saut" : "sauts";
        if (k == "deaths") return one ? "chute" : "chutes";
        if (k == "shards") return one ? "éclat" : "éclats";
        if (k == "relics") return one ? "relique" : "reliques";
        if (k == "secrets") return one ? "secret" : "secrets";
        if (k == "power_uses") return one ? "pouvoir utilisé" : "pouvoirs utilisés";
        if (k == "runs_completed") return one ? "niveau à finir" : "niveaux à finir";
        if (k == "pb_improved" || k == "pb_ghost_wins") return one ? "record à battre" : "records à battre";
        if (k == "extreme_attempts") return one ? "essai" : "essais";
        if (k == "ghost_races" || k == "ghost_pack_races" || k == "world_ghost_races") return one ? "course" : "courses";
        if (k == "gold_segments") return one ? "segment d'or" : "segments d'or";
        if (k == "comments_posted") return one ? "commentaire" : "commentaires";
        if (k == "exports") return one ? "export" : "exports";
        if (k == "friend_ghost_wins" || k == "world_ghost_wins") return one ? "victoire" : "victoires";
        return "";
    }
    if (t == "max") {
        if (k == "run_kills") return one ? "ennemi en une partie" : "ennemis en une partie";
        return one ? "essai" : "essais";
    }
    if (t == "set") {
        if (k == "cleared_levels") return one ? "niveau nettoyé" : "niveaux nettoyés";
        if (k == "no_damage_levels") return one ? "niveau sans dégât" : "niveaux sans dégât";
        if (k == "no_power_levels") return one ? "niveau sans pouvoir" : "niveaux sans pouvoir";
        if (k == "community_levels") return one ? "niveau de joueur" : "niveaux de joueurs";
        if (k == "rated_levels" || k == "rated_campaign") return one ? "niveau à noter" : "niveaux à noter";
        if (k == "published_levels" || k == "published_extreme") return one ? "niveau à publier" : "niveaux à publier";
        if (k == "equipped_items") return one ? "récompense" : "récompenses";
        if (k == "photographed_planets") return one ? "planète" : "planètes";
        if (k == "powers_owned") return one ? "pouvoir" : "pouvoirs";
        return "";
    }
    if (t == "levels_completed" || t == "planet" || t == "planets" || t == "all_levels" || t == "levels_all_secrets") return one ? "niveau" : "niveaux";
    if (startsWith(t, "medal")) return one ? "médaille" : "médailles";
    if (t == "levels_no_death") return one ? "niveau sans mourir" : "niveaux sans mourir";
    if (t == "levels_full_collect") return one ? "niveau tout ramassé" : "niveaux tout ramassés";
    if (t == "boss_distinct") return one ? "boss à vaincre" : "boss à vaincre";
    if (t == "boss_nohit_distinct") return one ? "boss sans être touché" : "boss sans être touché";
    if (t == "boss_total") return one ? "victoire" : "victoires";
    if (t == "boss_phase_nohit" || t == "boss_phases_nohit") return one ? "phase sans être touché" : "phases sans être touché";
    if (t == "tracks_heard") return one ? "morceau" : "morceaux";
    if (t == "creatures_met") return one ? "créature" : "créatures";
    if (t == "power_uses") return one ? "utilisation" : "utilisations";
    if (t == "powers_distinct") return one ? "pouvoir" : "pouvoirs";
    if (t == "user_levels") return one ? "niveau à créer" : "niveaux à créer";
    if (t == "user_enemies") return one ? "ennemi à créer" : "ennemis à créer";
    if (t == "friends") return one ? "ami" : "amis";
    if (t == "completion_proofs") return one ? "preuve de fin" : "preuves de fin";
    return "";
}

bool levelOpen(const AchievementInputs& in, const std::string& levelId) {
    if (!in.content) return true;
    const LevelEntry* e = in.content->level(levelId);
    if (!e) return false;
    if (!in.progression) return true;
    if (const PlanetData* p = in.content->planet(in.content->campaignPlanetOf(levelId)); p && !in.progression->planetUnlocked(*p, *in.content))
        return false;
    return in.progression->levelUnlocked(*e, *in.content);
}

bool planetOpen(const AchievementInputs& in, const std::string& planetId) {
    if (!in.content) return true;
    const PlanetData* p = in.content->planet(planetId);
    if (!p) return false;
    return !in.progression || in.progression->planetUnlocked(*p, *in.content);
}

enum class Reach { Yes, Unsure, No };

/// Can it be done now: its level or planet open, its boss met or in an open level.
Reach reachOf(const AchievementDef& d, const AchievementInputs& in, const SuggestionContext& ctx) {
    const AchievementCondition& c = d.condition;
    const std::string& t = c.type;
    if (t == "level" || t == "medal_level" || t == "level_no_death") return levelOpen(in, c.level) ? Reach::Yes : Reach::No;
    if (!c.planet.empty() && (t == "planet" || t == "medal_planet" || t == "levels_no_death" || t == "levels_full_collect" || t == "levels_all_secrets"))
        return planetOpen(in, c.planet) ? Reach::Yes : Reach::No;
    if (t == "planets") {
        for (const auto& p : c.planets)
            if (planetOpen(in, p)) return Reach::Yes;
        return Reach::No;
    }
    if (t == "boss" || t == "boss_phases_nohit") {
        const BossRecord* b = in.bosses ? in.bosses->find(c.boss) : nullptr;
        if (b && b->fights > 0) return Reach::Yes;  // already met
        if (!ctx.bossLevelsKnown) return Reach::Unsure;
        auto it = ctx.bossLevels.find(c.boss);
        if (it == ctx.bossLevels.end()) return Reach::No;
        for (const auto& l : it->second)
            if (levelOpen(in, l)) return Reach::Yes;
        return Reach::No;
    }
    return Reach::Yes;
}

/// The planet of a level of the campaign ("" when none).
std::string planetOfLevel(const AchievementInputs& in, const std::string& level) {
    return in.content && !level.empty() ? in.content->campaignPlanetOf(level) : std::string{};
}

/// 2: about the level played last, 1: about its planet, 0: neither.
int recency(const AchievementDef& d, const AchievementInputs& in, const SuggestionContext& ctx) {
    const AchievementCondition& c = d.condition;
    if (!ctx.recentLevel.empty() && c.level == ctx.recentLevel) return 2;
    const std::string& planet = ctx.recentPlanet.empty() ? planetOfLevel(in, ctx.recentLevel) : ctx.recentPlanet;
    if (planet.empty()) return 0;
    if (c.planet == planet || std::find(c.planets.begin(), c.planets.end(), planet) != c.planets.end()) return 1;
    if (!c.level.empty() && planetOfLevel(in, c.level) == planet) return 1;
    if (!c.boss.empty())
        if (auto it = ctx.bossLevels.find(c.boss); it != ctx.bossLevels.end())
            for (const auto& l : it->second)
                if (l == ctx.recentLevel || planetOfLevel(in, l) == planet) return 1;
    return 0;
}

/// Why it is suggested, in one short line (only the player's own numbers).
std::string reasonOf(const AchievementDef& d, const AchievementProgress& p, const AchievementInputs& in, const SuggestionContext& ctx, int recent) {
    const AchievementCondition& c = d.condition;
    const std::string& t = c.type;
    if (achievementNeedsOnline(c) && !ctx.online) return "Avec Cosmic Online (compte gratuit)";
    if (t == "level" || t == "level_no_death" || t == "medal_level") {
        const LevelRecord* r = in.records ? in.records->get(c.level) : nullptr;
        if (t == "medal_level" && r && r->bestMedal != Medal::None)
            return std::string("Meilleure médaille : ") + medalName(r->bestMedal);
        if (t == "level_no_death" && r && r->completions > 0 && r->fewestDeaths > 0)
            return "Au mieux " + std::to_string(r->fewestDeaths) + (r->fewestDeaths > 1 ? " chutes" : " chute") + " dans ce niveau";
        if (t == "medal_level" && r && r->completions > 0) return "Terminé, encore sans médaille";
        if (r && r->completions > 0) return "Niveau déjà terminé";
        if (r && r->attempts > 0) return "Tenté " + std::to_string(r->attempts) + " fois, jamais terminé";
        return "Niveau débloqué, jamais terminé";
    }
    if (t == "boss" || t == "boss_phases_nohit") {
        const BossRecord* b = in.bosses ? in.bosses->find(c.boss) : nullptr;
        if (t == "boss_phases_nohit" && p.value > 0) {
            int64_t left = p.target - p.value;
            return "Plus que " + std::to_string(left) + (left > 1 ? " phases sans être touché" : " phase sans être touché");
        }
        if (b && c.noHit && b->kills > 0) return "Vaincu " + std::to_string(b->kills) + " fois, jamais sans être touché";
        if (b && b->fights > 0) return "Affronté " + std::to_string(b->fights) + " fois, pas encore vaincu";
        if (recent > 0) return "Boss de la planète jouée récemment";
        return "Boss jamais affronté";
    }
    if (t == "online_account") return "Un compte gratuit suffit";
    const int64_t left = std::max<int64_t>(0, p.target - p.value);
    const double ratio = static_cast<double>(p.value) / static_cast<double>(std::max<int64_t>(1, p.target));
    if (p.value > 0 && (ratio >= 0.5 || left <= 3)) {
        if (t == "playtime") return "Plus que " + playTimeText(static_cast<double>(left));
        std::string unit = unitOf(c, left);
        return unit.empty() ? "Plus que " + thousands(left) : "Plus que " + thousands(left) + " " + unit;
    }
    if (p.value > 0) {
        int pct = std::clamp(static_cast<int>(std::floor(ratio * 100.0)), 1, 99);
        return std::to_string(pct) + " % fait";
    }
    if (recent == 2) return "Sur le niveau joué en dernier";
    if (recent == 1) return "Sur la planète jouée récemment";
    return "Pas encore commencé";
}

}  // namespace

bool achievementNeedsOnline(const AchievementCondition& c) {
    if (c.type == "online_account" || c.type == "friends") return true;
    if (c.type == "set")
        return c.key == "community_levels" || c.key == "rated_levels" || c.key == "rated_campaign" || c.key == "published_levels" ||
               c.key == "published_extreme";
    if (c.type == "stat")
        return c.key == "comments_posted" || c.key == "race_verified" || c.key == "world_ghost_races" || c.key == "world_ghost_wins" ||
               c.key == "friend_ghost_wins";
    return false;
}

bool achievementMovesDuringRun(const AchievementCondition& c) {
    const std::string& t = c.type;
    const std::string& k = c.key;
    // The run's counters (GameScene adds them as a run left now would: RunCounters, jumps,
    // power uses, EXTRÊME attempts) and the bosses' records (written as each fight ends).
    if (t == "stat")
        return startsWith(k, "kills") || k == "deaths" || k == "shards" || k == "relics" || k == "secrets" || k == "jumps" ||
               k == "power_uses" || k == "extreme_attempts";
    if (t == "max") return k == "run_kills";
    if (t == "set") return k == "powers_owned";
    return t == "power_uses" || t == "powers_distinct" || t == "boss" || t == "boss_distinct" || t == "boss_nohit_distinct" ||
           t == "boss_total" || t == "boss_phase_nohit" || t == "boss_phases_nohit";
}

std::string achievementProgressText(const AchievementDef& def, const AchievementProgress& p) {
    const int64_t v = std::min(p.value, p.target);
    if (def.condition.type == "playtime") return playTimeText(static_cast<double>(v)) + " / " + playTimeText(static_cast<double>(p.target));
    return thousands(v) + " / " + thousands(p.target);
}

std::vector<AchievementSuggestion> suggestAchievements(const AchievementCatalog& catalog, const AchievementBook& book, const AchievementInputs& in,
                                                       const SuggestionContext& ctx, size_t count) {
    struct Candidate {
        AchievementSuggestion s;
        size_t order = 0;
    };
    std::vector<Candidate> all;
    const auto& defs = catalog.achievements();
    for (size_t i = 0; i < defs.size(); ++i) {
        const AchievementDef& d = defs[i];
        if (book.unlocked(d.id) || !book.available(d) || !knownType(d.condition.type)) continue;
        const Reach reach = reachOf(d, in, ctx);
        if (reach == Reach::No) continue;
        AchievementProgress p = AchievementBook::progress(d, in);
        if (p.target <= 0 || p.done()) continue;  // done: unlocked at the next check, nothing to aim at
        const double ratio = std::clamp(static_cast<double>(p.value) / static_cast<double>(p.target), 0.0, 1.0);
        const double left = static_cast<double>(std::max<int64_t>(0, p.target - p.value));
        const double effort = left * minutesPerUnit(d.condition);  // minutes, roughly
        const int recent = recency(d, in, ctx);
        double score = 45.0 * ratio;                    // close to done
        score += 30.0 / (1.0 + effort / 15.0);          // little left to do
        score += 2.5 * static_cast<double>(d.stars);    // worth stars
        score += recent == 2 ? 10.0 : recent == 1 ? 6.0 : 0.0;  // played recently
        if (p.value > 0) score += 5.0;                   // already started
        if (achievementNeedsOnline(d.condition) && !ctx.online) score -= 40.0;  // offline first
        if (reach == Reach::Unsure) score -= 12.0;
        Candidate c;
        c.s.def = &d;
        c.s.progress = p;
        c.s.score = score;
        c.s.reason = reasonOf(d, p, in, ctx, recent);
        c.order = i;
        all.push_back(std::move(c));
    }
    std::stable_sort(all.begin(), all.end(), [](const Candidate& a, const Candidate& b) {
        if (a.s.score != b.s.score) return a.s.score > b.s.score;
        return a.order < b.order;
    });
    std::vector<AchievementSuggestion> out;
    std::map<std::string, int> perCategory;
    for (const Candidate& c : all) {
        if (out.size() >= count) break;
        int& n = perCategory[c.s.def->category];
        if (n >= kSuggestionsPerCategory) continue;
        ++n;
        out.push_back(c.s);
    }
    return out;
}

}  // namespace cosmic
