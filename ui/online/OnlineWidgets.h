#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>

#include "engine/core/Math.h"
#include "engine/rendering/Text.h"
#include "engine/rendering/Texture.h"
#include "game/online/OnlineGlobe.h"
#include "game/online/OnlineTypes.h"

namespace cosmic {

class GameServices;
class UIContext;

/// Shared drawing helpers and data of the Cosmic Online screens: flags, avatars (preset
/// atlas or downloaded custom PNG), status chips, connection indicator.
class OnlineUi {
public:
    explicit OnlineUi(GameServices& g);

    const CountryTable& countries() const { return countries_; }
    const CountryIndexMap& countryMap() const { return countryMap_; }
    std::string countryName(const std::string& code) const;

    void drawFlag(const std::string& code, const Rect& dst, float alpha = 1.0f) const;
    /// `avatar` = "avatar_01".."avatar_16" or "custom" (downloaded once per player). A click on
    /// it opens the player's profile (2.33: everywhere), unless avatarsOpenProfile is off.
    void drawAvatar(const std::string& avatar, const std::string& playerId, const Rect& dst);
    bool avatarsOpenProfile = true;
    int avatarCount() const { return 16; }
    std::string avatarName(int i) const;
    void drawStatusChip(RecordStatus s, Vec2 pos, float height) const;
    void drawConnection(Vec2 pos) const;
    /// Pseudonym as shown to this player (hidden in streaming mode).
    std::string displayName(const std::string& name, int rank, bool me) const;
    /// The same for a player object of the server ({username, clan_tag?}): « [TAG] name » (2.66,
    /// the tag of their clan before the name), both hidden in streaming mode.
    std::string displayName(const Json& player, int rank, bool me) const;
    /// 2.62.5: the « [TAG] » at the start of `shown` (a name drawn left aligned at `left.x`,
    /// centred on `left.y`, at the unscaled `size` and `face`, within `room`) opens the sheet of
    /// the player's clan when clicked, underlined under the mouse. Mouse only, like the avatars:
    /// the keyboard and the gamepad reach it through the player's profile (tab « Clan »).
    /// Nothing when `shown` does not start with the tag (streaming mode, « Vous »).
    void clanTagLink(const std::string& key, const Json& player, const std::string& shown, Vec2 left, float size, FontFace face, float room);

    /// Searchable country list; returns true when the player picked one (`code` updated).
    bool countryPicker(const std::string& id, const Rect& area, std::string& filter, std::string& code);
    /// 16 preset avatars; returns true when one was clicked (`avatar` updated).
    bool avatarPicker(const std::string& id, const Rect& area, std::string& avatar);

    /// A counter (unread messages, requests…) in a pill whose right edge is at `right`,
    /// centred on `cy`; « 99+ » beyond 99. Sizes are unscaled. Returns its left edge, so
    /// that the caller fits its label (or another pill) in the room left of it.
    static float countPill(UIContext& ui, float right, float cy, int count, Color fill, Color text, float height = 24.0f,
                           float fontSize = 14.0f);
    static float countPillWidth(UIContext& ui, int count, float height = 24.0f, float fontSize = 14.0f);

    static std::string ageText(double seconds);
    /// ageText() of an ISO-8601 UTC date from the server ("" if unreadable).
    static std::string ageOf(const std::string& iso);
    static Color statusColor(RecordStatus s);

private:
    void drawAvatarImage(const std::string& avatar, const std::string& playerId, const Rect& dst);
    GameServices& g_;
    CountryTable countries_;
    CountryIndexMap countryMap_;
    Json avatarNames_;
    std::map<std::string, TexturePtr> customAvatars_;
    std::set<std::string> requested_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

/// A player's presence as the others see it (2.33.1; 2.47 « hidden »: « Montrer ma présence »
/// unticked). Colour of the pill and its word (« En ligne », « Absent », « Hors ligne »,
/// « Présence masquée »), capitalised or not.
Color presenceDotColor(const std::string& presence);
std::string presenceLabel(const std::string& presence, bool capital);
/// The pill: a dark ring of `ring` pixels around a disc of `radius`; a hidden presence is a grey
/// disc crossed out (never a guess at whether the player is there).
void drawPresenceDot(UIContext& ui, Vec2 centre, float radius, const std::string& presence, float ring);

}  // namespace cosmic
