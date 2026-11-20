#pragma once
// Gifts (« Cadeaux », server/src/gifts.ts, docs/COLLECTION.md « Cadeaux »): rewards an administrator gives to
// players, as the game shows them, without drawing anything (ui/screens/GiftsScene, GiftArt and AdminGifts draw).
//
// A gift's content stays hidden until it is opened: GET /gifts/me sends no reward for a wrapped gift, and
// OPENING it (POST /gifts/{id}/open) is what grants it, on the server, through the rewards already owned online
// (an item: the rewards won, GET /players/me/rewards, kept on this computer for offline play; a badge: the raid
// rewards claimed). The game never decides what a gift holds nor that it was opened.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic::gifts {

/// The eleven wraps, in the order of the server (GIFT_WRAPS) and of assets/ui/gifts/wraps.png
/// (tools/assetgen/environment/gift_wraps.py: one row of 80 x 40 each, the body then the lid).
constexpr int kWrapCount = 11;
constexpr int kWrapCell = 40;
constexpr int kLidDy = -14;  // the lid is drawn 14 px higher than the body (wraps.json `lid_dy`)
constexpr const char* kWrapsTexture = "ui/gifts/wraps.png";
const std::vector<std::string>& wrapIds();
int wrapIndex(const std::string& id);  // -1: unknown
/// « Or », « Améthyste »… (the tab's names).
std::string wrapName(const std::string& id);
/// The light of a wrap (the rays, the halo, the sparks of the opening; wraps.json `light`).
Color wrapLight(const std::string& id);

/// The longest message (characters, as the server counts them).
constexpr int kMaxMessage = 140;
constexpr int kMaxRecipients = 100;
constexpr int kMaxRewards = 12;
/// Characters of `s` (UTF-8 code points).
int textLength(const std::string& s);
/// As the server keeps it: control and direction characters removed, trimmed.
std::string cleanMessage(const std::string& s);
/// `s` cut to `n` characters (on a character boundary).
std::string cutText(const std::string& s, int n);

/// A reward: an item of the Collection in its category (suit, trail, frame, title, victory), or a profile
/// badge (`kind` "badge", `id` the achievement).
struct Reward {
    std::string kind;
    std::string id;
    std::string name;  // the server's (the game prefers its own catalogue's)
    std::string key() const { return kind + ":" + id; }
    bool operator==(const Reward& o) const { return kind == o.kind && id == o.id; }
};
/// The six kinds, in the tab's order.
const std::vector<std::string>& rewardKinds();
/// « Combinaison », « Traînée », « Cadre d'avatar », « Titre », « Effet de victoire », « Badge de profil ».
std::string kindName(const std::string& kind);

struct Gift {
    int64_t id = 0;
    std::string fromId, from;
    std::string wrap;
    std::string message;
    std::string createdAt, openedAt;
    bool opened = false;
    std::optional<Reward> reward;  // only once opened
};
/// One gift of GET /gifts/me (nullopt when malformed: an unknown wrap draws as « or »).
std::optional<Gift> parseGift(const Json& j);
/// `data` of GET /gifts/me (the newest first, as the server sends them).
std::vector<Gift> parseList(const Json& data);

enum class Filter { ToOpen, Opened, All };
/// The gifts a filter shows, in the list's order.
std::vector<const Gift*> filtered(const std::vector<Gift>& all, Filter f);
int unopenedCount(const std::vector<Gift>& all);
/// The gift « Cadeau suivant » opens after `current`: the next one still wrapped in the list's order (then from the
/// top), never `current` itself; nullptr when none is left.
const Gift* nextToOpen(const std::vector<Gift>& all, int64_t current);
/// The list once `opened` came back from the server (that gift replaced, the others kept).
void applyOpened(std::vector<Gift>& all, const Gift& opened);

/// « 20/11/2026 » (local date of this computer), "" when unreadable.
std::string dateText(const std::string& iso);

}  // namespace cosmic::gifts
