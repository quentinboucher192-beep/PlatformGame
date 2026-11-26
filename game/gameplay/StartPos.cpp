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
