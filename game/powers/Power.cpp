#include "game/powers/Power.h"

#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "game/powers/Powers.h"

namespace cosmic {

bool Power::ready(const Player& p) const { return cooldown_ <= 0.0f && p.energy + 0.001f >= def_.energyCost; }

void Power::activate(Player& p, GameWorld& w) {
    p.spendEnergy(def_.energyCost);
    cooldown_ = def_.cooldown;
    if (!def_.sound.empty()) w.playSound(def_.sound, p.center(), 0.9f);
    onActivate(p, w);
}

void Power::fixedUpdate(Player& /*p*/, GameWorld& /*w*/, float dt) { cooldown_ = std::max(0.0f, cooldown_ - dt); }

PowerRegistry& PowerRegistry::instance() {
    static PowerRegistry r;
    return r;
}

PowerRegistry::PowerRegistry() {
    registerBehavior("dash", [](const PowerDefinition& d) { return std::make_unique<DashPower>(d); });
    registerBehavior("wave", [](const PowerDefinition& d) { return std::make_unique<WavePower>(d); });
    registerBehavior("shield", [](const PowerDefinition& d) { return std::make_unique<ShieldPower>(d); });
    registerBehavior("impulse", [](const PowerDefinition& d) { return std::make_unique<ImpulsePower>(d); });
    registerBehavior("teleport", [](const PowerDefinition& d) { return std::make_unique<TeleportPower>(d); });
}

void PowerRegistry::registerBehavior(const std::string& behavior, Factory f) { factories_[behavior] = std::move(f); }

std::unique_ptr<Power> PowerRegistry::create(const PowerDefinition& def) const {
    auto it = factories_.find(def.behavior);
    if (it == factories_.end()) return nullptr;
    return it->second(def);
}

std::vector<std::string> PowerRegistry::behaviors() const {
    std::vector<std::string> v;
    for (const auto& [k, f] : factories_) v.push_back(k);
    return v;
}

}  // namespace cosmic
