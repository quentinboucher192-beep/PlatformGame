#include "game/race/Race.h"

#include <algorithm>
#include <cmath>

#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/levels/LevelAnalyzer.h"
#include "game/levels/LevelData.h"

namespace cosmic {

Json RaceLayout::toJson() const {
    Json r = Json::array();
    for (const auto& p : route) r.push_back({std::round(p.x), std::round(p.y)});
    Json b = Json::array(), a = Json::array();
    for (const auto& x : bosses) b.push_back({{"id", x.id}, {"name", x.name}});
    for (const auto& x : ambushes) a.push_back({{"id", x.id}, {"waves", x.waves}});
    return {{"route", r}, {"bosses", b}, {"ambushes", a}};
}

RaceLayout buildRaceLayout(const LevelData& level, const ContentDatabase& content) {
    RaceLayout l;
    l.route = intendedRoute(level, content);
    // Fewer points: the route stays faithful (the server projects the replay on it).
    if (l.route.size() > 400) {
        std::vector<Vec2> thin;
        size_t step = (l.route.size() + 399) / 400;
        for (size_t i = 0; i < l.route.size(); i += step) thin.push_back(l.route[i]);
        if (!(thin.back() == l.route.back())) thin.push_back(l.route.back());
        l.route = std::move(thin);
    }
    for (const auto& o : level.objects) {
        if (o.type == "enemy") {
            const EnemyDefinition* d = content.enemy(o.propStr("enemy"));
            if ((d && d->isBoss) || o.prop<bool>("boss", false)) l.bosses.push_back({o.id, d ? d->name : o.propStr("enemy")});
        } else if (o.type == "combat_zone") {
            int waves = static_cast<int>(CombatZoneEntity::countWaves(o, level));
            if (waves > 0) l.ambushes.push_back({o.id, waves});
        }
    }
    return l;
}

int RaceProgress::bossesBeaten() const {
    return static_cast<int>(std::count_if(bossRemoved.begin(), bossRemoved.end(), [](float b) { return b >= 0.999f; }));
}

float raceScore(const RaceLayout& layout, const RaceProgress& p) {
    if (p.finished) return 100.0f;
    const float shares = 2.0f * static_cast<float>(layout.bosses.size()) + static_cast<float>(layout.ambushes.size());
    float score;
    if (shares <= 0.0f) {
        score = 100.0f * std::clamp(p.route, 0.0f, 1.0f);
    } else {
        float got = 0.0f;
        for (size_t i = 0; i < layout.bosses.size(); ++i) got += 2.0f * std::clamp(i < p.bossRemoved.size() ? p.bossRemoved[i] : 0.0f, 0.0f, 1.0f);
        for (size_t i = 0; i < layout.ambushes.size(); ++i) {
            int waves = std::max(1, layout.ambushes[i].waves);
            got += std::clamp(static_cast<float>(i < p.wavesDone.size() ? p.wavesDone[i] : 0) / static_cast<float>(waves), 0.0f, 1.0f);
        }
        score = 40.0f * std::clamp(p.route, 0.0f, 1.0f) + 60.0f * got / shares;
    }
    // Not finished: never 100, one decimal.
    return std::min(99.9f, std::floor(score * 10.0f) / 10.0f);
}

void RaceTracker::begin(const RaceLayout& layout) {
    layout_ = layout;
    progress_ = {};
    progress_.bossRemoved.assign(layout_.bosses.size(), 0.0f);
    progress_.wavesDone.assign(layout_.ambushes.size(), 0);
    cumulative_.assign(layout_.route.size(), 0.0f);
    for (size_t i = 1; i < layout_.route.size(); ++i) cumulative_[i] = cumulative_[i - 1] + (layout_.route[i] - layout_.route[i - 1]).length();
    length_ = cumulative_.empty() ? 0.0f : cumulative_.back();
    hint_ = 0;
}

void RaceTracker::update(GameWorld& world) {
    if (!active() || length_ <= 1.0f) return;
    // The furthest point of the route reached (local search, as the progress bar does).
    Vec2 feet = world.player().feet();
    size_t lo = hint_ > 60 ? hint_ - 60 : 0, hi = std::min(layout_.route.size(), hint_ + 60);
    auto nearest = [&](size_t a, size_t b) {
        size_t best = a;
        float bd = 1e30f;
        for (size_t i = a; i < b; ++i) {
            float d = (layout_.route[i] - feet).lengthSq();
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        return best;
    };
    size_t i = nearest(lo, hi);
    if ((layout_.route[i] - feet).lengthSq() > 160.0f * 160.0f) {
        size_t g = nearest(0, layout_.route.size());
        if ((layout_.route[g] - feet).lengthSq() < (layout_.route[i] - feet).lengthSq()) i = g;
    }
    hint_ = i;
    progress_.route = std::max(progress_.route, cumulative_[i] / length_);
    // Bosses: the health removed (a defeated boss counts whole).
    for (size_t b = 0; b < layout_.bosses.size(); ++b) {
        auto* es = dynamic_cast<EnemySpawnEntity*>(world.entity(layout_.bosses[b].id));
        if (!es) continue;
        float removed = 0.0f;
        if (es->defeated()) removed = 1.0f;
        else if (Enemy* e = es->enemy(world); e && e->maxHp() > 0.0f) removed = e->alive() ? 1.0f - e->hpRatio() : 1.0f;
        progress_.bossRemoved[b] = std::max(progress_.bossRemoved[b], std::clamp(removed, 0.0f, 1.0f));
    }
    for (size_t a = 0; a < layout_.ambushes.size(); ++a)
        if (const auto* z = dynamic_cast<const CombatZoneEntity*>(world.entity(layout_.ambushes[a].id)))
            progress_.wavesDone[a] = std::max(progress_.wavesDone[a], static_cast<int>(z->wavesDone()));
    if (world.state() == WorldState::Completed || world.state() == WorldState::Outro) progress_.finished = true;
}

}  // namespace cosmic
