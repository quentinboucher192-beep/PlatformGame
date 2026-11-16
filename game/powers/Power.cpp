#include "game/powers/Power.h"

#include "engine/input/InputManager.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "game/powers/Powers.h"

namespace cosmic {

bool Power::keyPressed(const PlayerInput& in) const { return key_ != Action::Count && in.isPressed(key_); }

bool Power::wantsActivation(const Player& /*p*/, const GameWorld& /*w*/, const PlayerInput& in) const { return keyPressed(in); }

PowerTriggerInfo powerTriggerInfo(const Power& p) {
    PowerTriggerInfo t;
    t.kind = p.trigger();
    if (t.kind == PowerTrigger::StandardKeys) {
        t.keys = p.standardKeys();
        t.how = p.standardKeysHow();
    }
    return t;
}

PowerTriggerInfo powerTriggerInfo(const PowerDefinition& def) {
    auto probe = PowerRegistry::instance().create(def);
    return probe ? powerTriggerInfo(*probe) : PowerTriggerInfo{};
}

bool Power::ready(const Player& p) const { return cooldown_ <= 0.0f && p.energy + 0.001f >= activationCost(p); }

void Power::activate(Player& p, GameWorld& w) {
    p.spendEnergy(activationCost(p));
    cooldown_ = def_.cooldown;
    if (!def_.sound.empty()) w.playSound(def_.sound, p.center(), 0.9f);
    attackTag_ = w.newAttackTag(id());  // 2.54: statistics only (what this use reaches)
    GameWorld::AttackScope tag(w, attackTag_);
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
    registerBehavior("glide", [](const PowerDefinition& d) { return std::make_unique<GlidePower>(d); });
    registerBehavior("ground_pound", [](const PowerDefinition& d) { return std::make_unique<GroundPoundPower>(d); });
    registerBehavior("charge_shot", [](const PowerDefinition& d) { return std::make_unique<ChargeShotPower>(d); });
    registerBehavior("grapple", [](const PowerDefinition& d) { return std::make_unique<GrapplePower>(d); });
    registerBehavior("air_jump", [](const PowerDefinition& d) { return std::make_unique<AirJumpPower>(d); });
    registerBehavior("time_slow", [](const PowerDefinition& d) { return std::make_unique<TimeSlowPower>(d); });
    registerBehavior("magnet", [](const PowerDefinition& d) { return std::make_unique<MagnetPower>(d); });
    registerBehavior("air_dash", [](const PowerDefinition& d) { return std::make_unique<AirDashPower>(d); });
    registerBehavior("plasma_bomb", [](const PowerDefinition& d) { return std::make_unique<PlasmaBombPower>(d); });
    // 2.19: the powers of the four sanctuaries.
    registerBehavior("boomerang", [](const PowerDefinition& d) { return std::make_unique<BoomerangPower>(d); });
    registerBehavior("frost_nova", [](const PowerDefinition& d) { return std::make_unique<FrostNovaPower>(d); });
    registerBehavior("decoy", [](const PowerDefinition& d) { return std::make_unique<DecoyPower>(d); });
    registerBehavior("recall", [](const PowerDefinition& d) { return std::make_unique<RecallPower>(d); });
    // 2.50: hold to charge, release to throw; the blade spins on its target.
    registerBehavior("boomerang_charge", [](const PowerDefinition& d) { return std::make_unique<BoomerangChargePower>(d); });
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
