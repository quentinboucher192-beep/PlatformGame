#pragma once

// 2.59: trails, avatar frames and victory effects created on Cosmic Online by an administrator
// (ADMINISTRATION › Créations, besides the suits of SuitRecolor.h): two colours and a style
// among those the game already draws (ui/screens/CosmeticFx), and for a trail its width, how
// long its trace lasts and its sparkles. The server keeps the same lists and bounds
// (server/src/creations.ts): anything else read from it, or from the files kept on this
// computer, is left out, never guessed. Display only: nothing here touches a run, a replay or
// a record.

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Paths.h"
#include "game/cosmetics/SuitRecolor.h"

namespace cosmic {

/// A trail: its width in percent of the usual one, how long its trace lasts (ms).
constexpr int kTrailWidthMin = 50, kTrailWidthMax = 200;
constexpr int kTrailLifeMin = 200, kTrailLifeMax = 1000;

/// What a trail, an avatar frame or a victory effect created on the server looks like.
struct CreationLook {
    std::string kind;   // "trail", "frame", "victory"
    std::string style;  // one of creationStyles(kind)
    /// trail: start (by Nova), end (its tail); frame: border, glow; victory: main, second.
    Rgb8 a, b;
    int width = 100;        // trail only: percent of the usual size (kTrailWidthMin..Max)
    int lifeMs = 450;       // trail only: how long its trace lasts (kTrailLifeMin..Max)
    bool particles = true;  // trail only: sparkles around it
    float life() const { return static_cast<float>(lifeMs) / 1000.0f; }
    bool operator==(const CreationLook& o) const {
        return kind == o.kind && style == o.style && a == o.a && b == o.b && width == o.width && lifeMs == o.lifeMs && particles == o.particles;
    }
    bool operator!=(const CreationLook& o) const { return !(*this == o); }
};

/// "trail", "frame" or "victory".
bool isCreationLookKind(const std::string& kind);
/// The styles of a kind, in the order the editor offers them (empty for any other kind).
const std::vector<std::string>& creationStyles(const std::string& kind);
bool isCreationStyle(const std::string& kind, const std::string& style);
/// « Comète », « Anneau poli », « Feu d'artifice »...: the French name of a style ("" when unknown).
std::string creationStyleName(const std::string& kind, const std::string& style);
/// The JSON names of the two colours of a kind: trail {start, end}, frame {border, glow},
/// victory {main, second} ({"", ""} for any other kind).
std::pair<std::string, std::string> creationColorKeys(const std::string& kind);
/// A look of `kind` read from a creation's JSON (`colors`, `style`, and a trail's `width`,
/// `life_ms`, `particles`); nullopt when anything is missing, malformed, out of its bounds or
/// of the lists.
std::optional<CreationLook> creationLookFromJson(const std::string& kind, const Json& j);
/// The same fields (`colors`, `style`, a trail's settings): the body of POST /admin/creations
/// without its kind and name, and what the files of this computer keep.
Json creationLookToJson(const CreationLook& l);
/// What a new creation of `kind` starts from in the editor.
CreationLook defaultCreationLook(const std::string& kind);

/// "srvtrail:<n>", "srvframe:<n>", "srvvictory:<n>" (n from 1, at most 10 digits): a trail, a
/// frame or a victory effect created on Cosmic Online (`kind` receives which). Its look in the
/// game is this reference too.
bool isServerLookRef(const std::string& ref, std::string* kind = nullptr);
/// Any creation of Cosmic Online: a suit ("srvsuit:<n>") or one of the above.
bool isServerCreationRef(const std::string& ref);

/// A trail, a frame or a victory effect created on Cosmic Online, as GET /creations gives it.
struct ServerLook {
    std::string ref;  // "srvtrail:<n>"...
    std::string name;
    CreationLook look;
    std::string updatedAt;
};

/// The trails, frames and victory effects created on Cosmic Online that this game knows: the
/// public list (GET /creations, its suits kept by ServerSuitBook), kept on this computer (user
/// folder, online/creation_looks.json) so that another player's frame or trail is drawn offline
/// after one reading. Never trusted blindly: malformed entries are left out.
class ServerLookBook {
public:
    void setFile(fs::path file) { file_ = std::move(file); }
    void load();
    bool save() const;
    /// `creations` of GET /creations (the suits ignored) replaces what is known; true when something changed.
    bool applyList(const Json& creations);
    const ServerLook* find(const std::string& ref) const;
    const std::vector<ServerLook>& all() const { return looks_; }
    bool known() const { return known_; }

    static std::optional<ServerLook> fromJson(const Json& j);
    static Json toJson(const ServerLook& s);

private:
    fs::path file_;
    std::vector<ServerLook> looks_;
    bool known_ = false;
};

/// Where the drawing code finds the look of a reference "srvtrail:<n>"... (GameServices: the
/// list kept on this computer, then the rewards won; unknown: nullopt, the category's simple
/// look is drawn meanwhile). Unset: nothing is known.
using CreationLookResolver = std::function<std::optional<CreationLook>(const std::string& ref)>;
void setCreationLookResolver(CreationLookResolver resolver);
std::optional<CreationLook> resolveCreationLook(const std::string& ref);

}  // namespace cosmic
