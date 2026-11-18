#pragma once

#include <array>

#include "engine/core/Json.h"
#include "saves/HudLayout.h"

namespace cosmic {

/// 2.71: the HUD of the clan raids, placed by the player apart from the solo / co-op one
/// (Paramètres › HUD › « Raids », docs/RAIDS.md §15). Vitals, Powers and Boss are the game's HUD
/// elements (their places are given to Hud as a HudLayout would: an anchor); the others are the
/// raid's own panels (ui/online/RaidHud): their custom place is their top-left corner, fractions of
/// the screen. Display only.
enum class RaidHudElement : int {
    Vitals,         // this player's hearts and energy
    Powers,         // this player's powers
    Minimap,        // the minimap of the zone (its legend)
    Group,          // the group panel (8 players)
    Objectives,     // the zone's objectives and the branch's progress
    Alerts,         // the alerts of the group, the branch and the server
    Boss,           // the gigaboss's bar
    Danger,         // the danger band (« Montée de magma dans 3 s »)
    Contributions,  // this player's contributions
    Status,         // the network line, or the « TEST ADMIN » badge of a test
    Timer,          // the raid's chrono: since the instance started, the zone's time under it
    Count
};
constexpr int kRaidHudElements = static_cast<int>(RaidHudElement::Count);

struct RaidHudLayout {
    std::array<HudPlace, kRaidHudElements> items{};
    /// The arrows on the screen's border toward what is out of view (enemies already met, the
    /// ambush's, the gigaboss, the allies), as in the solo levels.
    bool arrows = true;

    HudPlace& operator[](RaidHudElement e) { return items[static_cast<size_t>(e)]; }
    const HudPlace& operator[](RaidHudElement e) const { return items[static_cast<size_t>(e)]; }

    static const char* id(RaidHudElement e);
    static const char* label(RaidHudElement e);
    /// The game's HUD element an element is (Vitals, Powers, Boss), HudElement::Count for the raid's own.
    static HudElement hudElement(RaidHudElement e);
    /// The danger band and the network line cannot be hidden (what the group must see).
    static bool canHide(RaidHudElement e) { return e != RaidHudElement::Danger && e != RaidHudElement::Status; }

    Json toJson() const;
    static RaidHudLayout fromJson(const Json& j);
};

}  // namespace cosmic
