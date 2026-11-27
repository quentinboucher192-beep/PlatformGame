#pragma once

#include <array>

#include "engine/core/Json.h"
#include "saves/HudLayout.h"

namespace cosmic {

/// 2.72: the HUD of the clan raids, placed by the player apart from the solo / co-op one
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
    Heart,          // 2.74: the Cœur d'Asterion's health shared by every group (its arena only)
    HeartEffects,   // 2.74: what the branches did to the Cœur (at its arena's door)
    Remaining,      // 2.75: « Ce qu'il reste à faire »: the zone's requirements ticked, the branch's zones to finish
    Notices,        // 2.75.5: the raid menu's button and the short notifications under it (ui/online/RaidMenu)
    LairPassage,    // 2.78: the distance to the lair's passage in the zone before it, its arrow and its state
    Chat,           // 2.95: the raid's chat (its recent lines, the history and the field when open: ui/online/RaidChatUi)
    Count
};
constexpr int kRaidHudElements = static_cast<int>(RaidHudElement::Count);

/// 2.95 (docs/RAIDS.md §39): the chat's own settings (Paramètres › HUD › « Raids » › Chat), besides its place and
/// size: its width, the lines it shows, how long a new line stays, and the opacity of the lines' background.
struct RaidChatSettings {
    static constexpr float kMinWidth = 0.6f, kMaxWidth = 1.6f;      // × its default width
    static constexpr int kMinLines = 3, kMaxLines = 12;
    static constexpr float kMinFade = 3.0f, kMaxFade = 30.0f;       // seconds
    static constexpr float kMinOpacity = 0.1f, kMaxOpacity = 1.0f;
    float width = 1.0f;
    int lines = 7;
    float fade = 10.0f;
    float opacity = 0.55f;
    bool operator==(const RaidChatSettings&) const = default;
};

struct RaidHudLayout {
    std::array<HudPlace, kRaidHudElements> items{};
    RaidChatSettings chat;  // 2.95
    /// The arrows on the screen's border toward what is out of view (enemies already met, the
    /// ambush's, the gigaboss, the allies), as in the solo levels.
    bool arrows = true;
    /// 2.79: the thoughts of a raid test's simulated players over them (their role, what they do, how they
    /// move); also shown or hidden with its key (Action::BotThoughts) and the order wheel.
    bool thoughts = true;

    HudPlace& operator[](RaidHudElement e) { return items[static_cast<size_t>(e)]; }
    const HudPlace& operator[](RaidHudElement e) const { return items[static_cast<size_t>(e)]; }

    static const char* id(RaidHudElement e);
    static const char* label(RaidHudElement e);
    /// The game's HUD element an element is (Vitals, Powers, Boss), HudElement::Count for the raid's own.
    static HudElement hudElement(RaidHudElement e);
    /// The danger band, the network line and the notifications (with the menu's button) cannot be
    /// hidden (what the group must see); they are moved and resized like the others.
    static bool canHide(RaidHudElement e) {
        return e != RaidHudElement::Danger && e != RaidHudElement::Status && e != RaidHudElement::Notices;
    }

    Json toJson() const;
    static RaidHudLayout fromJson(const Json& j);
};

}  // namespace cosmic
