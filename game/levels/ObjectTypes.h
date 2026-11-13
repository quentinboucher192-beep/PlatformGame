#pragma once

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

enum class PropType {
    Bool,
    Int,
    Float,
    String,
    Text,      // multi-line text
    Enum,      // one of `options`
    Color,
    EnemyRef,  // enemy definition id
    PowerRef,  // power id
    ObjectRef, // level object id(s), comma separated
    DecorRef,  // decor atlas sprite
    EffectRef, // particle preset
    SoundRef,  // sound file
};

struct PropDef {
    std::string key;
    std::string label;
    PropType type = PropType::Float;
    Json def;
    float min = 0.0f;
    float max = 0.0f;
    float step = 1.0f;
    std::vector<std::string> options;  // Enum values
    std::vector<std::string> optionLabels;
    std::string help;
};

enum class ObjCategory { Terrain, Platforms, Hazards, Gameplay, Interactive, Enemies, Zones, Decor, Lights };
const char* categoryLabel(ObjCategory c);

/// Static description of an object type: shared by the runtime (defaults), the level
/// editor (palette, property panel) and the packager (asset dependencies).
struct ObjectTypeInfo {
    std::string id;
    std::string name;
    ObjCategory category = ObjCategory::Gameplay;
    Vec2 defaultSize{16, 16};
    bool resizable = true;
    bool freeRotation = false;  // any angle (decor) vs quarter turns (solids, spikes)
    bool rotatable = false;
    float snap = 16.0f;
    Color editorColor{80, 200, 255, 255};
    std::string defaultLayer = "gameplay";
    bool unique = false;  // only one per level (player start)
    std::vector<PropDef> props;
    std::vector<std::string> assets;  // asset files this type always uses
    std::string help;

    Json defaultProps() const;
    const PropDef* prop(const std::string& key) const;
};

/// A palette entry of the level editor: a type with preset properties.
struct PaletteEntry {
    std::string label;
    std::string type;
    Json props = Json::object();
    Vec2 size;  // 0 = type default
};

namespace ObjectTypes {
const std::vector<ObjectTypeInfo>& all();
const ObjectTypeInfo* find(const std::string& id);
const std::vector<PaletteEntry>& palette();
}  // namespace ObjectTypes

}  // namespace cosmic
