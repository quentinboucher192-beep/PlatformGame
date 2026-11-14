#pragma once

#include <functional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/levels/ObjectTypes.h"

namespace cosmic {

class UIContext;
class GameServices;

/// Draws editable widgets for a list of property definitions (the object type schema)
/// on a JSON object. Shared by the level editor (objects) and the event editors.
class PropertyEditor {
public:
    /// Called when the user wants to pick an object id in the viewport (ObjectRef).
    std::function<void(const std::string& key)> onPickObject;

    /// Returns true if a value changed. `y` advances.
    bool edit(GameServices& g, const std::vector<PropDef>& defs, Json& props, float x, float& y, float width);
    bool editOne(GameServices& g, const PropDef& def, Json& props, float x, float& y, float width);

    /// Colour field: "#rrggbb" text (any colour), its swatch, and ready-made colours
    /// (the first, "A", clears it: automatic colour). `y` advances.
    static bool colorPicker(UIContext& ui, const std::string& id, float x, float& y, float width, float labelW,
                            const std::string& label, std::string& value);
    static std::vector<std::string> enemyIds(GameServices& g);
    static std::vector<std::string> powerIds(GameServices& g);
    static std::vector<std::string> decorNames(GameServices& g);
    static std::vector<std::string> effectNames(GameServices& g);
    static std::vector<std::string> assetFiles(const std::string& folder, const std::string& ext);
};

/// Small helper: a labelled row (label on the left, widget area on the right).
struct FieldRow {
    UIContext& ui;
    float x, width, labelW;
    float& y;
    Rect field(const std::string& label, float h = 0.0f);
};

}  // namespace cosmic
