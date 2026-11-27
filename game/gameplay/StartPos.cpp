#include "game/gameplay/StartPos.h"

#include <algorithm>
#include <cmath>

#include "game/content/ContentDatabase.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/player/Player.h"

namespace cosmic {

namespace {

bool validId(const std::string& s) {
    if (s.empty() || s.size() > 96) return false;
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

bool readIds(const Json& j, std::vector<std::string>& out) {
    if (!j.is_array() || j.size() > StartPosState::kMaxIds) return false;
    for (const Json& v : j) {
        if (!v.is_string() || !validId(v.get<std::string>())) return false;
        out.push_back(v.get<std::string>());
    }
    return true;
}

}  // namespace

Json StartPosState::toJson() const {
    return Json{{"feet", {feet.x, feet.y}}, {"left", faceLeft},       {"powers", powers},  {"health", health},
                {"bonus", bonusHealth},    {"energy", energy},        {"reached", reached}};
}

std::optional<StartPosState> StartPosState::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    StartPosState s;
    const Json& f = jfield(j, "feet");
    if (!f.is_array() || f.size() != 2 || !f[0].is_number() || !f[1].is_number()) return std::nullopt;
    s.feet = {f[0].get<float>(), f[1].get<float>()};
    if (!std::isfinite(s.feet.x) || !std::isfinite(s.feet.y) || std::abs(s.feet.x) > 1e7f || std::abs(s.feet.y) > 1e7f) return std::nullopt;
    s.faceLeft = jget<bool>(j, "left", false);
    s.health = jget<int>(j, "health", 0);
    s.bonusHealth = jget<int>(j, "bonus", 0);
    s.energy = jget<float>(j, "energy", 0.0f);
    if (s.health < 1 || s.health > 40 || s.bonusHealth < 0 || s.bonusHealth > 40 || !std::isfinite(s.energy) || s.energy < 0.0f ||
        s.energy > 10000.0f)
        return std::nullopt;
    if (j.contains("powers") && !readIds(j["powers"], s.powers)) return std::nullopt;
    if (s.powers.size() > 16) return std::nullopt;
    if (j.contains("reached") && !readIds(j["reached"], s.reached)) return std::nullopt;
    return s;
}

std::optional<StartPosState> captureStartPos(const GameWorld& w) {
    const Player& p = w.player();
    if (w.teamSize() > 1 || w.state() != WorldState::Playing || w.cinematic().active() || !p.alive() || p.state() == PlayerState::Respawn)
        return std::nullopt;
    StartPosState s;
    s.feet = p.feet();
    if (!std::isfinite(s.feet.x) || !std::isfinite(s.feet.y)) return std::nullopt;
    s.faceLeft = p.facing < 0.0f;
    for (const auto& pw : p.powers())
        if (pw) s.powers.push_back(pw->id());
    s.health = std::max(1, p.health);
    s.bonusHealth = std::max(0, p.bonusHealth);
    s.energy = std::max(0.0f, p.energy);
    for (const auto& e : w.entities())
        if (const auto* cp = dynamic_cast<const CheckpointEntity*>(e.get()); cp && cp->activated() && s.reached.size() < StartPosState::kMaxIds)
            s.reached.push_back(cp->id());
    return s;
}

void applyStartPos(GameWorld& w, const StartPosState& s) {
    w.startAt(s.feet, s.faceLeft);
    // The checkpoints lit before it: lit again without their event (no split, no respawn).
    for (const std::string& id : s.reached)
        if (auto* cp = dynamic_cast<CheckpointEntity*>(w.entity(id))) cp->restoreKept(w, Json{{"on", true}});
    Player& p = w.player();
    p.clearPowers();
    const int slots = p.powerSlots() > 0 ? p.powerSlots() : LevelData::kMaxPowerSlots;
    for (const std::string& id : fitTrainingPowers(w.content(), s.powers, slots))
        if (const PowerDefinition* d = w.content().power(id)) p.grantPower(*d);
    p.bonusHealth = std::clamp(s.bonusHealth, 0, 40);
    p.health = std::clamp(s.health, 1, p.maxHealth());
    p.energy = std::clamp(s.energy, 0.0f, p.config().maxEnergy);
}

// ------------------------------------------------------------------ placed from the menu (2.92)

namespace {

/// A solid as the world builds it (GameWorld::buildStatic): a wall reaching the top of the level
/// goes on up to the ceiling.
std::optional<Rect> solidOf(const LevelData& level, const LevelObject& o) {
    if (o.type != "solid" || !o.prop<bool>("collision", true)) return std::nullopt;
    Rect r = o.rect;
    if (r.y <= level.bounds.y + 0.5f) {
        r.h += r.y - (level.bounds.y - kCeilingMargin);
        r.y = level.bounds.y - kCeilingMargin;
    }
    return r;
}

}  // namespace

StartPosProblem startPosProblem(const LevelData& level, Vec2 body, Vec2 feet) {
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y)) return StartPosProblem::OutOfLevel;
    const Rect b = Rect::fromBottomCenter(feet, body);
    const Rect& lb = level.bounds;
    // The level's invisible borders (its sides, its ceiling) and what lies under it (a fall).
    if (b.x < lb.x || b.right() > lb.right() || b.y < lb.y - kCeilingMargin || feet.y > std::min(lb.bottom(), level.effectiveKillY()))
        return StartPosProblem::OutOfLevel;
    for (const LevelObject& o : level.objects)
        if (auto r = solidOf(level, o); r && r->intersects(b)) return StartPosProblem::InSolid;
    return StartPosProblem::None;
}

std::optional<Vec2> standingSpot(const LevelData& level, Vec2 body, Vec2 near) {
    // The tops of the terrain under the body's width: solids and one-way platforms.
    const float x0 = near.x - body.x * 0.5f, x1 = near.x + body.x * 0.5f;
    std::optional<Vec2> best;
    float bestD = 1e30f;
    for (const LevelObject& o : level.objects) {
        float top = 0.0f;
        if (auto r = solidOf(level, o)) top = r->y;
        else if (o.type == "oneway") top = o.rect.y;
        else continue;
        if (o.rect.right() <= x0 || o.rect.x >= x1) continue;
        const Vec2 f{near.x, top};
        if (startPosProblem(level, body, f) != StartPosProblem::None) continue;
        // Under the point first: a place above it counts twice its distance.
        const float d = top >= near.y ? top - near.y : (near.y - top) * 2.0f;
        if (d < bestD) {
            bestD = d;
            best = f;
        }
    }
    return best;
}

StartPosState segmentStartPos(Vec2 feet, std::vector<std::string> reached, const std::vector<std::string>& startPowers,
                              const std::vector<std::string>& tabletsBefore, int slots, const std::vector<std::string>* chosen, int maxHealth,
                              float maxEnergy) {
    StartPosState s;
    s.feet = feet;
    if (chosen) {
        s.powers = *chosen;  // exactly those chosen for the segment
    } else {
        // The level's start powers, then the tablets placed before the segment (the last ones
        // when the slots are fewer: those a player would keep), each once.
        s.powers = startPowers;
        std::vector<std::string> tablets = tabletsBefore;
        if (slots > 0 && static_cast<int>(tablets.size()) > slots) tablets.erase(tablets.begin(), tablets.end() - slots);
        for (const std::string& t : tablets)
            if (std::find(s.powers.begin(), s.powers.end(), t) == s.powers.end()) s.powers.push_back(t);
    }
    if (s.powers.size() > 16) s.powers.resize(16);
    if (reached.size() > StartPosState::kMaxIds) reached.resize(StartPosState::kMaxIds);
    s.reached = std::move(reached);
    s.health = std::clamp(maxHealth, 1, 40);
    s.bonusHealth = 0;
    s.energy = std::clamp(maxEnergy, 0.0f, 10000.0f);
    return s;
}

void GameWorld::noteMarks(uint8_t flags) {
    static constexpr uint8_t kOrder[] = {InputFrame::PlaceStartPos, InputFrame::PrevStartPos, InputFrame::NextStartPos, InputFrame::PlaceMeasure};
    std::optional<StartPosState> now;
    bool read = false;
    for (uint8_t f : kOrder) {
        if (!(flags & f)) continue;
        if (!read) {
            now = captureStartPos(*this);
            read = true;
        }
        TrainingMark m;
        m.flag = f;
        m.chrono = timer_.elapsed();
        m.valid = now.has_value();
        if (now) m.state = *now;
        if (marks_.size() < 256) marks_.push_back(std::move(m));  // a scene takes them every step
    }
}

}  // namespace cosmic
