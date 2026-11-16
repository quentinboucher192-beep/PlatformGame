#pragma once

// Profile badges (2.41): achievements the player chose in the Collection, shown on their
// online profile next to their name and title. A badge is drawn as the achievement's emblem:
// a medal in its category's colour (the colours of the web of the Succès screen) with its
// stars (1 to 5) at its heart. The emblems are focusable: under the mouse or the keyboard /
// gamepad focus, a tooltip gives the achievement's name, its category, its stars and what it
// asks for.

#include <string>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class GameServices;
class UIContext;
struct AchievementDef;

/// The emblem of `a`, centred on `c`, `radius` pixels (alpha: 1 normal, less: dimmed).
void drawBadgeEmblem(GameServices& g, const AchievementDef& a, Vec2 c, float radius, float alpha = 1.0f);
/// An empty slot of the profile (a free place for a badge): a dim ring and a « + ».
void drawBadgeSlot(UIContext& ui, Vec2 c, float radius, bool focused);

/// Badges side by side (focusable) and the tooltip of the one hovered or focused.
class BadgeRow {
public:
    /// Width of `n` emblems of diameter `size` separated by `gap`.
    static float width(size_t n, float size, float gap);
    /// Emblems of `defs` from `x` (left edge), centred on `cy`, `size` wide, `gap` apart; each
    /// a hotspot named `prefix` + its index. Returns the index activated (click, Entrée,
    /// button A), -1 when none.
    int draw(GameServices& g, const std::string& prefix, const std::vector<const AchievementDef*>& defs, float x, float cy, float size,
             float gap);
    /// The tooltip of the emblem hovered (mouse) or focused (keyboard, gamepad), over the rest:
    /// to call once everything else of the screen is drawn. `hint`: one more line at its foot
    /// (what the confirm key does there), "" for none.
    void drawTooltip(GameServices& g, const std::string& hint = {});
    bool showingTooltip() const { return tip_ != nullptr; }
    /// Index of the emblem focused by the keyboard / gamepad (-1: none).
    int focusedIndex() const { return focused_; }

private:
    const AchievementDef* tip_ = nullptr;
    Rect tipRect_;
    int focused_ = -1;
};

}  // namespace cosmic
