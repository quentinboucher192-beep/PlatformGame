#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "game/gameplay/Entity.h"

namespace cosmic {

/// Creates runtime entities from level objects. New object types register a creator
/// here (and a description in ObjectTypes) without touching the world.
class EntityFactory {
public:
    using Creator = std::function<std::unique_ptr<LevelEntity>(const LevelObject&)>;
    static std::unique_ptr<LevelEntity> create(const LevelObject& obj);
    static void registerType(const std::string& type, Creator c);
    static bool knows(const std::string& type);

private:
    static std::map<std::string, Creator>& registry();
};

}  // namespace cosmic
