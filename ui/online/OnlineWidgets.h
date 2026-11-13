#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>

#include "engine/core/Math.h"
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
    /// `avatar` = "avatar_01".."avatar_16" or "custom" (downloaded once per player).
    void drawAvatar(const std::string& avatar, const std::string& playerId, const Rect& dst);
    int avatarCount() const { return 16; }
    std::string avatarName(int i) const;
    void drawStatusChip(RecordStatus s, Vec2 pos, float height) const;
    void drawConnection(Vec2 pos) const;
    /// Pseudonym as shown to this player (hidden in streaming mode).
    std::string displayName(const std::string& name, int rank, bool me) const;

    /// Searchable country list; returns true when the player picked one (`code` updated).
    bool countryPicker(const std::string& id, const Rect& area, std::string& filter, std::string& code);
    /// 16 preset avatars; returns true when one was clicked (`avatar` updated).
    bool avatarPicker(const std::string& id, const Rect& area, std::string& avatar);

    static std::string ageText(double seconds);
    static Color statusColor(RecordStatus s);

private:
    GameServices& g_;
    CountryTable countries_;
    CountryIndexMap countryMap_;
    Json avatarNames_;
    std::map<std::string, TexturePtr> customAvatars_;
    std::set<std::string> requested_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
};

}  // namespace cosmic
