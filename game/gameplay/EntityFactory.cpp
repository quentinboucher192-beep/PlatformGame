#include "game/gameplay/EntityFactory.h"

#include "game/gameplay/entities/Entities.h"

namespace cosmic {

namespace {
template <typename T>
EntityFactory::Creator make() {
    return [](const LevelObject& o) { return std::make_unique<T>(o); };
}
}  // namespace

std::map<std::string, EntityFactory::Creator>& EntityFactory::registry() {
    static std::map<std::string, Creator> r = {
        {"moving_platform", make<MovingPlatformEntity>()},
        {"crumble_platform", make<CrumblePlatformEntity>()},
        {"power_gate", make<PowerGateEntity>()},
        {"bounce_pad", make<BouncePadEntity>()},
        {"ladder", make<LadderEntity>()},
        {"spikes", make<SpikesEntity>()},
        {"liquid", make<LiquidEntity>()},
        {"damage_zone", make<DamageZoneEntity>()},
        {"slow_zone", make<SlowZoneEntity>()},
        {"laser", make<LaserEntity>()},
        {"checkpoint", make<CheckpointEntity>()},
        {"exit", make<ExitEntity>()},
        {"shard", make<CollectibleEntity>()},
        {"relic", make<CollectibleEntity>()},
        {"health_orb", make<CollectibleEntity>()},
        {"energy_orb", make<CollectibleEntity>()},
        {"power_unlock", make<PowerUnlockEntity>()},
        {"door", make<DoorEntity>()},
        {"lever", make<LeverEntity>()},
        {"receptor", make<ReceptorEntity>()},
        {"sign", make<SignEntity>()},
        {"trigger", make<TriggerEntity>()},
        {"combat_zone", make<CombatZoneEntity>()},
        {"camera_zone", make<CameraZoneEntity>()},
        {"secret_area", make<SecretAreaEntity>()},
        {"enemy", make<EnemySpawnEntity>()},
        {"decor", make<DecorEntity>()},
        {"light", make<LightEntity>()},
        {"emitter", make<EmitterEntity>()},
    };
    return r;
}

std::unique_ptr<LevelEntity> EntityFactory::create(const LevelObject& obj) {
    auto it = registry().find(obj.type);
    if (it == registry().end()) return nullptr;
    return it->second(obj);
}

void EntityFactory::registerType(const std::string& type, Creator c) { registry()[type] = std::move(c); }

bool EntityFactory::knows(const std::string& type) { return registry().count(type) > 0; }

}  // namespace cosmic
