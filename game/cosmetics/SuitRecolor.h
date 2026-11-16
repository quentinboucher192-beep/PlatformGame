#pragma once

// Suits created on Cosmic Online by an administrator (ADMINISTRATION › Créations): Nova's sheet
// (sprites/player/nova.png) recoloured at run time from three colours — the suit, its accent
// (visor, energy), its scarf — exactly as tools/assetgen/characters/nova_suits.py makes the
// catalogue's suits offline. recolorSuitPixels() is a line-by-line port of its recolor(): the
// same classification of each pixel (outline, accent, scarf, suit), the same HLS arithmetic of
// Python's colorsys in double precision (the build forbids fused multiply-adds), the same
// rounding (half to even), so that the colours of a catalogue suit give its PNG back exactly
// (tests/test_suit_recolor.cpp). Display only: nothing here touches a run, a replay or a record.

#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "engine/rendering/Texture.h"

namespace cosmic {

class Renderer;
class ResourceManager;

struct Rgb8 {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb8& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb8& o) const { return !(*this == o); }
};

/// The three target colours of a suit, in the order of nova_suits.py.
struct SuitColors {
    Rgb8 suit, accent, scarf;
    /// "#rrggbb#rrggbb#rrggbb": the key of the texture cache.
    std::string key() const;
    bool operator==(const SuitColors& o) const { return suit == o.suit && accent == o.accent && scarf == o.scarf; }
    bool operator!=(const SuitColors& o) const { return !(*this == o); }
};

/// "#rrggbb" (either case) → the colour; nullopt for anything else.
std::optional<Rgb8> parseHexColor(const std::string& s);
/// "#rrggbb" (lower case).
std::string hexColor(Rgb8 c);
/// `{ suit, accent, scarf }` of "#rrggbb"; nullopt when one is missing or malformed.
std::optional<SuitColors> suitColorsFromJson(const Json& j);
Json suitColorsToJson(const SuitColors& c);

/// Recolours `count` RGBA8 pixels of Nova's sheet toward `c` (`src` and `dst` may be the same
/// buffer). A transparent pixel is left as it is; the outline keeps its colour; the others are
/// recoloured keeping their lightness, as recolor() of nova_suits.py (suit_mix 0.55).
void recolorSuitPixels(const uint8_t* src, uint8_t* dst, size_t count, const SuitColors& c);

/// Reads an image file into RGBA8 pixels (rows one after the other, 4 bytes per pixel).
bool loadRgbaImage(const fs::path& file, std::vector<uint8_t>& pixels, int& w, int& h);

/// "srvsuit:<n>" (n from 1, at most 10 digits): a suit created on Cosmic Online. Its look in
/// the game is this reference too.
bool isServerSuitRef(const std::string& ref);

/// A suit created on Cosmic Online, as GET /api/v1/creations describes it.
struct ServerSuit {
    std::string ref;  // "srvsuit:<n>"
    std::string name;
    SuitColors colors;
    std::string updatedAt;
};

/// The suits created on Cosmic Online that this game knows: the public list (GET /creations),
/// kept on this computer (user folder, online/creations.json) so that another player's suit is
/// drawn offline after one reading. Never trusted blindly: malformed entries are left out.
class ServerSuitBook {
public:
    void setFile(fs::path file) { file_ = std::move(file); }
    void load();
    bool save() const;
    /// `creations` of GET /creations replaces what is known; true when something changed.
    bool applyList(const Json& creations);
    const ServerSuit* find(const std::string& ref) const;
    const std::vector<ServerSuit>& all() const { return suits_; }
    bool known() const { return known_; }

    static std::optional<ServerSuit> fromJson(const Json& j);
    static Json toJson(const ServerSuit& s);

private:
    fs::path file_;
    std::vector<ServerSuit> suits_;
    bool known_ = false;  // a list was read (from the server or the file)
};

/// The recoloured sheets, made from sprites/player/nova.png once read, cached by colours (the
/// most recent ones: the administration's live preview makes a new one at each change).
class SuitTextureCache {
public:
    static constexpr size_t kMaxTextures = 24;
    TexturePtr get(ResourceManager& resources, Renderer& renderer, const SuitColors& c);
    void clear();
    /// Reads Nova's sheet (done by get() on first use, from the assets).
    bool loadBase(const fs::path& novaSheet);
    /// The sheet recoloured toward `c` (RGBA8; empty before loadBase): the same pixels as
    /// recolorSuitPixels() on the whole sheet, computed once per distinct colour.
    std::vector<uint8_t> pixels(const SuitColors& c) const;

private:
    std::vector<uint8_t> base_;
    int w_ = 0, h_ = 0;
    bool baseTried_ = false;
    // The sheet's distinct colours (RGBA, opaque) and each pixel's one (kNone: transparent): a
    // new suit recolours a few hundred colours, not every pixel (the live preview follows a drag).
    static constexpr uint32_t kNone = 0xFFFFFFFFu;
    std::vector<uint8_t> distinct_;
    std::vector<uint32_t> indexOf_;
    std::list<std::pair<std::string, TexturePtr>> lru_;  // the most recent first
    std::unordered_map<std::string, std::list<std::pair<std::string, TexturePtr>>::iterator> byKey_;
};

}  // namespace cosmic
