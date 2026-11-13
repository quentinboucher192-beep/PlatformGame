#include "game/levels/DifficultyRating.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/content/ContentDatabase.h"
#include "game/enemies/EnemyDefinition.h"

namespace cosmic {

namespace {

// ------------------------------------------------------------------ weights
// Calibrated on the campaign (see docs/LEVEL_FORMAT.md, "Note de difficulté"). Changing a
// weight changes every rating: bump kRatingVersion.
constexpr float kJumpWeight = 0.55f;     // per 100 tiles
constexpr float kHazardWeight = 0.30f;   // per 100 tiles
constexpr float kEnemyWeight = 0.22f;    // per 100 tiles
constexpr float kBossWeight = 1.0f;
constexpr float kOffset = 6.0f;          // what any complete level has (length, a few jumps...)
constexpr float kCurve = 8.0f;           // score = 5 (1 - exp(-(raw - kOffset) / kCurve))

struct Physics {
    float jumpHeight = 58.0f;  // with the air jumps (double jump)
    float jumpSpan = 100.0f;   // full-speed jump (with the air jumps) back to the same height
    float runSpeed = 150.0f;
    int maxHealth = 10;
};

Physics physicsFrom(const Json& cfg) {
    Physics p;
    p.jumpHeight = jget<float>(cfg, "jumpHeight", 58.0f);
    float apex = std::max(0.05f, jget<float>(cfg, "timeToApex", 0.38f));
    float g = 2.0f * p.jumpHeight / (apex * apex);
    float gFall = g * std::max(1.0f, jget<float>(cfg, "fallGravityMultiplier", 1.65f));
    // Rise of the first jump, then of each air jump started at its apex, then the fall.
    float h1 = p.jumpHeight;
    int airJumps = std::max(0, jget<int>(cfg, "airJumps", 1));
    float h2 = jget<float>(cfg, "doubleJumpHeight", 44.0f);
    float total = apex;
    float height = h1;
    for (int k = 0; k < airJumps; ++k) {
        total += std::sqrt(2.0f * h2 / g);
        height += h2;
    }
    total += std::sqrt(2.0f * height / gFall);
    p.jumpHeight = height;
    p.runSpeed = jget<float>(cfg, "maxRunSpeed", 150.0f);
    p.jumpSpan = p.runSpeed * total;
    p.maxHealth = std::max(1, jget<int>(cfg, "maxHealth", 10));
    return p;
}

bool isSupport(const LevelObject& o) {
    if (o.type == "solid") return o.prop<bool>("collision", true);
    return o.type == "oneway" || o.type == "moving_platform" || o.type == "crumble_platform" || o.type == "bounce_pad" ||
           o.type == "power_gate" || (o.type == "door" && !o.prop<bool>("open", false));
}

bool isDeadly(const LevelObject& o) {
    if (o.type == "liquid") {
        std::string k = o.propStr("kind", "acid");
        return k == "acid" || k == "lava";
    }
    return o.type == "spikes" || o.type == "damage_zone";
}

/// Not a jump: the player swims, climbs or floats there.
bool isCarrier(const LevelObject& o) {
    if (o.type == "ladder" || o.type == "updraft") return true;
    if (o.type == "liquid") {
        std::string k = o.propStr("kind", "acid");
        return k == "water" || k == "icewater";
    }
    return false;
}

float rectPointDistance(const Rect& r, Vec2 p) {
    float dx = std::max({r.x - p.x, 0.0f, p.x - r.right()});
    float dy = std::max({r.y - p.y, 0.0f, p.y - r.bottom()});
    return std::sqrt(dx * dx + dy * dy);
}

struct Route {
    std::vector<Vec2> pts;
    std::vector<float> along;  // px from the start
    float distance(const Rect& r, size_t* nearest = nullptr) const {
        float best = 1e9f;
        size_t bi = 0;
        for (size_t i = 0; i < pts.size(); ++i) {
            float d = rectPointDistance(r, pts[i]);
            if (d < best) {
                best = d;
                bi = i;
            }
        }
        if (nearest) *nearest = bi;
        return best;
    }
};

/// Effective damage per second of an enemy's attack set, against a player who reacts to
/// telegraphs: a long wind-up is easy to dodge, a short one much less.
float effectiveDps(const EnemyDefinition& d, float cooldownMul) {
    float wsum = 0.0f, dps = 0.0f;
    for (const auto& a : d.attacks) {
        if (a.damage <= 0.0f) continue;
        // ~0.3 s of reaction: a 0.3 s wind-up hits almost always, 0.9 s or more almost never.
        float hit = std::clamp((0.9f - a.telegraph) / 0.6f, 0.05f, 1.0f);
        if (a.isProjectileType() && a.count > 1) hit = std::min(1.0f, hit * (1.0f + 0.15f * static_cast<float>(a.count - 1)));
        float cycle = a.telegraph + a.active + a.recover + a.cooldown * cooldownMul;
        dps += a.weight * a.damage * hit / std::max(0.4f, cycle);
        wsum += a.weight;
    }
    return wsum > 0.0f ? dps / wsum : 0.0f;
}

float enemyThreat(const EnemyDefinition& d) {
    float t = 0.4f + d.hp * 0.12f + d.contactDamage * 0.3f;
    float best = 0.0f;
    int extra = 0;
    bool ranged = false;
    for (const auto& a : d.attacks) {
        if (a.damage <= 0.0f && a.type != AttackType::Summon) continue;
        best = std::max(best, a.damage * 0.35f / std::max(0.35f, a.telegraph));
        ++extra;
        ranged = ranged || a.isProjectileType() || a.type == AttackType::Laser;
    }
    t += best + 0.15f * static_cast<float>(std::max(0, extra - 1));
    if (ranged) t += 0.6f;
    if (d.flying) t += 0.4f;
    if (d.canTeleport) t += 0.3f;
    if (d.canDefend) t += 0.3f;
    if (d.callsReinforcements) t += 0.3f;
    if (d.deathSpawnCount > 0) t += 0.2f * static_cast<float>(d.deathSpawnCount);
    return t;
}

}  // namespace

// ------------------------------------------------------------------- rating

DifficultyRating rateDifficulty(const LevelData& level, const ContentDatabase& content) {
    LevelAnalyzerOptions opt;
    return rateDifficulty(level, content, analyzeLevel(level, content, opt));
}

DifficultyRating rateDifficulty(const LevelData& level, const ContentDatabase& content, const LevelAnalysis& a) {
    DifficultyRating r;
    const Physics ph = physicsFrom(content.playerConfig());
    Route route;
    route.pts = a.intendedPath.empty() ? a.route : a.intendedPath;
    r.reachable = a.reachable && !route.pts.empty();
    float acc = 0.0f;
    for (size_t i = 0; i < route.pts.size(); ++i) {
        if (i > 0) acc += (route.pts[i] - route.pts[i - 1]).length();
        route.along.push_back(acc);
    }
    r.routeTiles = std::max(1.0f, acc / 16.0f);
    const float per100 = 100.0f / std::max(40.0f, r.routeTiles);

    std::vector<const LevelObject*> supports, deadly, carriers;
    for (const auto& o : level.objects) {
        if (isSupport(o)) supports.push_back(&o);
        if (isDeadly(o)) deadly.push_back(&o);
        if (isCarrier(o)) carriers.push_back(&o);
    }
    auto supportAt = [&](Vec2 p) -> const LevelObject* {
        for (const auto* o : supports) {
            const Rect& rc = o->rect;
            if (p.x >= rc.x - 4.0f && p.x <= rc.right() + 4.0f && p.y >= rc.y - 14.0f && p.y <= rc.y + 6.0f) return o;
        }
        return nullptr;
    };
    auto carried = [&](Vec2 p) {
        for (const auto* o : carriers)
            if (o->rect.expanded(4.0f).contains(p)) return true;
        return false;
    };
    // What stops a fall at x from height y: a support (safe), a deadly hazard or the void.
    const float bottom = level.effectiveKillY();
    auto fallIsDeadly = [&](float x, float y) {
        float hitY = bottom;
        bool deadlyHit = true;
        for (const auto* o : supports)
            if (x >= o->rect.x && x <= o->rect.right() && o->rect.y >= y - 6.0f && o->rect.y < hitY) {
                hitY = o->rect.y;
                deadlyHit = false;
            }
        for (const auto* o : deadly)
            if (x >= o->rect.x && x <= o->rect.right() && o->rect.y > y && o->rect.y <= hitY + 2.0f) {
                hitY = o->rect.y;
                deadlyHit = true;
            }
        for (const auto* o : carriers)  // water below: a swim, not a death
            if (x >= o->rect.x && x <= o->rect.right() && o->rect.y > y && o->rect.y < hitY && o->type == "liquid") deadlyHit = false;
        return deadlyHit;
    };

    // ------------------------------------------------------------- jumps
    float jumpSum = 0.0f;
    float iceTiles = 0.0f;
    size_t i = 0;
    const size_t n = route.pts.size();
    while (i < n) {
        const LevelObject* sup = supportAt(route.pts[i]);
        if (sup || carried(route.pts[i])) {
            if (sup && sup->type == "solid" && sup->propStr("material") == "glacier_ice" && i > 0)
                iceTiles += (route.pts[i] - route.pts[i - 1]).length() / 16.0f;
            ++i;
            continue;
        }
        // Airborne stretch [i, j)
        size_t j = i;
        while (j < n && !supportAt(route.pts[j]) && !carried(route.pts[j])) ++j;
        Vec2 from = route.pts[i > 0 ? i - 1 : i];
        Vec2 to = route.pts[j < n ? j : n - 1];
        float top = from.y;
        for (size_t k = i; k < j; ++k) top = std::min(top, route.pts[k].y);
        float span = std::abs(to.x - from.x);
        float rise = std::max(0.0f, from.y - top);
        float p = std::max(span / ph.jumpSpan, rise / ph.jumpHeight);
        if (p > 0.55f) {
            bool fatal = false;
            float x0 = std::min(from.x, to.x), x1 = std::max(from.x, to.x);
            for (float x = x0 + 8.0f; x < x1 - 4.0f && !fatal; x += 16.0f) fatal = fallIsDeadly(x, std::max(from.y, to.y));
            float w = std::min(3.0f, (p - 0.55f) * 3.0f);
            if (fatal) w *= 2.0f;
            jumpSum += w;
            if (p >= 0.75f) {
                ++r.preciseJumps;
                if (fatal) ++r.deadlyJumps;
            }
        }
        i = j;
    }

    // ------------------------------------------------------------ hazards
    float hazardSum = 0.0f;
    for (const auto& o : level.objects) {
        float w = 0.0f;
        float reach = 72.0f;
        if (o.type == "spikes") {
            float len = std::max(o.rect.w, o.rect.h) / 16.0f;
            w = 0.4f + 0.15f * std::min(len, 8.0f);
            if (o.prop<int>("damage", 1) >= 2) w *= 1.6f;
            if (o.prop<bool>("instantKill", false)) w *= 2.5f;
        } else if (o.type == "laser") {
            float on = o.prop<float>("onTime", 1.5f), off = o.prop<float>("offTime", 1.5f);
            float duty = off <= 0.0f ? 1.0f : on / std::max(0.1f, on + off);
            w = 1.0f + 1.5f * duty;
            if (!o.prop<bool>("active", true)) w *= 0.7f;
            reach = 48.0f;
        } else if (o.type == "liquid") {
            std::string k = o.propStr("kind", "acid");
            if (k == "acid" || k == "lava") w = 0.25f * std::min(o.rect.w / 16.0f, 20.0f);
            else w = 0.15f;
            reach = 96.0f;
        } else if (o.type == "damage_zone") {
            w = o.prop<bool>("visible", true) ? 1.2f : 1.6f;
        } else if (o.type == "slow_zone") {
            w = 0.4f;
        } else if (o.type == "crumble_platform") {
            w = 1.0f;
            if (o.prop<float>("delay", 0.45f) < 0.35f) w += 0.6f;
            if (o.prop<float>("respawn", 3.0f) <= 0.0f) w += 0.3f;
            reach = 24.0f;
        } else if (o.type == "moving_platform") {
            w = 0.6f + (std::abs(o.prop<float>("dy", 0.0f)) > 32.0f ? 0.3f : 0.0f);
            reach = 24.0f;
        } else if (o.type == "power_gate") {
            w = 0.3f;
        } else if (o.type == "grapple_point") {
            w = 0.5f;
            reach = 160.0f;
        } else if (o.type == "updraft") {
            w = 0.3f;
            reach = 24.0f;
        } else if (o.type == "bounce_pad") {
            w = 0.2f;
            reach = 24.0f;
        }
        if (w <= 0.0f) continue;
        if (route.distance(o.rect) > reach) continue;
        hazardSum += w;
        ++r.hazardCount;
    }
    hazardSum += 0.25f * iceTiles / 4.0f;

    // ------------------------------------------------------------ enemies
    float enemySum = 0.0f;
    std::vector<const EnemyDefinition*> bosses;
    for (const auto& o : level.objects) {
        if (o.type != "enemy") continue;
        const EnemyDefinition* d = content.enemy(o.propStr("enemy"));
        if (!d) continue;
        if (d->isBoss || o.prop<bool>("boss", false)) {
            bosses.push_back(d);
            continue;
        }
        if (route.distance(o.rect) > 192.0f) continue;
        enemySum += enemyThreat(*d);
        ++r.enemyCount;
    }

    // --------------------------------------------------------------- boss
    float bossSum = 0.0f;
    for (const EnemyDefinition* d : bosses) {
        // Time the player needs to win against the time the boss needs to win.
        float cdMul = 1.0f;
        if (!d->phases.empty()) {
            float s = 0.0f;
            for (const auto& p : d->phases) s += p.cooldownMultiplier;
            cdMul = s / static_cast<float>(d->phases.size());
        }
        float dps = effectiveDps(*d, cdMul) + d->contactDamage * 0.03f;
        float playerDps = 0.6f;  // realistic hits per second on a boss that fights back
        float bossTime = d->hp / playerDps;
        float playerTime = static_cast<float>(ph.maxHealth) / std::max(0.01f, dps);
        bossSum += 1.6f * bossTime / playerTime + 0.3f * static_cast<float>(d->phases.size());
    }

    // -------------------------------------------------------- checkpoints
    std::vector<float> marks{0.0f};
    for (const auto& c : a.checkpoints) {
        size_t k = 0;
        route.distance(Rect{c.pos.x - 1, c.pos.y - 1, 2, 2}, &k);
        if (!route.along.empty()) marks.push_back(route.along[k]);
    }
    marks.push_back(acc);
    std::sort(marks.begin(), marks.end());
    float gapSum = 0.0f, gapMax = 0.0f;
    int gaps = 0;
    for (size_t k = 1; k < marks.size(); ++k) {
        float g = (marks[k] - marks[k - 1]) / 16.0f;
        if (g < 8.0f) continue;
        gapSum += g;
        gapMax = std::max(gapMax, g);
        ++gaps;
    }
    float meanGap = gaps > 0 ? gapSum / static_cast<float>(gaps) : r.routeTiles;
    r.longestCheckpointGap = gapMax;
    r.checkpoints = 0.6f * std::pow(meanGap / 100.0f, 1.2f) + 0.4f * gapMax / 200.0f;

    // ------------------------------------------------------------ healing
    float heal = 0.0f;
    for (const auto& o : level.objects) {
        if (o.type == "health_orb") heal += static_cast<float>(o.prop<int>("amount", 2));
        if (o.type == "heart_container") heal += 2.0f * static_cast<float>(o.prop<int>("hearts", 1));
    }
    r.healPer100 = heal * per100;
    r.healing = -std::min(2.5f, 0.12f * r.healPer100);

    // -------------------------------------------------------------- timer
    if (level.timerMode == TimerMode::Limited && level.timeLimit > 0.0f) {
        float needed = acc / (ph.runSpeed * 0.55f);
        float tight = needed / level.timeLimit;
        if (tight > 0.5f) r.timer = std::min(2.0f, (tight - 0.5f) * 4.0f);
    }
    r.length = std::min(1.0f, 0.15f * r.routeTiles / 100.0f);

    r.jumps = kJumpWeight * jumpSum * per100;
    r.hazards = kHazardWeight * hazardSum * per100;
    r.enemies = kEnemyWeight * enemySum * per100;
    r.boss = kBossWeight * bossSum;
    r.raw = std::max(0.0f, r.jumps + r.hazards + r.enemies + r.boss + r.checkpoints + r.healing + r.timer + r.length);
    float s = 5.0f * (1.0f - std::exp(-std::max(0.0f, r.raw - kOffset) / kCurve));
    r.score = std::clamp(std::round(s * 2.0f) / 2.0f, 0.5f, 5.0f);
    return r;
}

int ratingStars(float score) { return std::clamp(static_cast<int>(std::lround(score)), 1, 5); }

// ------------------------------------------------------------------- output

Json DifficultyRating::toJson() const {
    auto r2 = [](float v) { return std::round(static_cast<double>(v) * 100.0) / 100.0; };
    return {{"version", kRatingVersion},
            {"score", static_cast<double>(score)},
            {"raw", r2(raw)},
            {"route_tiles", r2(routeTiles)},
            {"components",
             {{"jumps", r2(jumps)},
              {"hazards", r2(hazards)},
              {"enemies", r2(enemies)},
              {"boss", r2(boss)},
              {"checkpoints", r2(checkpoints)},
              {"healing", r2(healing)},
              {"timer", r2(timer)},
              {"length", r2(length)}}},
            {"facts",
             {{"precise_jumps", preciseJumps},
              {"deadly_jumps", deadlyJumps},
              {"hazards", hazardCount},
              {"enemies", enemyCount},
              {"longest_checkpoint_gap", r2(longestCheckpointGap)},
              {"heal_per_100_tiles", r2(healPer100)}}}};
}

DifficultyRating DifficultyRating::fromJson(const Json& j) {
    DifficultyRating r;
    if (!j.is_object()) return r;
    r.score = jget<float>(j, "score", 0.0f);
    r.raw = jget<float>(j, "raw", 0.0f);
    r.routeTiles = jget<float>(j, "route_tiles", 0.0f);
    if (auto c = j.find("components"); c != j.end() && c->is_object()) {
        r.jumps = jget<float>(*c, "jumps", 0.0f);
        r.hazards = jget<float>(*c, "hazards", 0.0f);
        r.enemies = jget<float>(*c, "enemies", 0.0f);
        r.boss = jget<float>(*c, "boss", 0.0f);
        r.checkpoints = jget<float>(*c, "checkpoints", 0.0f);
        r.healing = jget<float>(*c, "healing", 0.0f);
        r.timer = jget<float>(*c, "timer", 0.0f);
        r.length = jget<float>(*c, "length", 0.0f);
    }
    if (auto f = j.find("facts"); f != j.end() && f->is_object()) {
        r.preciseJumps = jget<int>(*f, "precise_jumps", 0);
        r.deadlyJumps = jget<int>(*f, "deadly_jumps", 0);
        r.hazardCount = jget<int>(*f, "hazards", 0);
        r.enemyCount = jget<int>(*f, "enemies", 0);
        r.longestCheckpointGap = jget<float>(*f, "longest_checkpoint_gap", 0.0f);
        r.healPer100 = jget<float>(*f, "heal_per_100_tiles", 0.0f);
    }
    return r;
}

std::string DifficultyRating::label() const {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.1f/5", static_cast<double>(score));
    std::string s = buf;
    if (auto p = s.find('.'); p != std::string::npos) s[p] = ',';
    return s;
}

std::string DifficultyRating::summary() const {
    struct Part {
        float v;
        std::string text;
    };
    std::vector<Part> parts = {
        {jumps, std::to_string(preciseJumps) + " sauts précis" + (deadlyJumps > 0 ? " (" + std::to_string(deadlyJumps) + " au-dessus du vide)" : "")},
        {hazards, std::to_string(hazardCount) + " pièges sur le parcours"},
        {enemies, std::to_string(enemyCount) + " ennemis sur le parcours"},
        {boss, "un gardien redoutable"},
        {checkpoints, "points de passage espacés"},
        {timer, "temps limité serré"},
    };
    std::stable_sort(parts.begin(), parts.end(), [](const Part& x, const Part& y) { return x.v > y.v; });
    std::string out;
    int k = 0;
    for (const auto& p : parts) {
        if (p.v < 0.3f || k == 3) break;
        out += (k++ ? ", " : "") + p.text;
    }
    if (healing < -0.5f) out += (out.empty() ? "" : " ; ") + std::string("nombreux soins");
    return out.empty() ? "parcours sans difficulté particulière" : out;
}

}  // namespace cosmic
