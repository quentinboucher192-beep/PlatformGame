#pragma once
// Clans of Cosmic Online (2.66): what the game shows of them, without drawing anything. The server
// (server/src/clans.ts) decides every rule; these helpers only phrase what it answers (roles,
// joining, results) and check a form before it is sent, with the same limits, so that a mistake
// is told at once and in place (the server checks again).

#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

namespace clans {

constexpr int kMaxMembers = 50;
constexpr int kNameMin = 3;
constexpr int kNameMax = 32;
constexpr int kMottoMax = 80;
constexpr int kMessageMax = 120;

/// "[AUBE] Nova" (the tag between brackets before the username), "Nova" without a tag.
std::string tagged(const std::string& username, const std::string& tag);
/// The same for a player object of the server ({username, clan_tag?}).
std::string taggedName(const Json& player);
/// "[AUBE]" ("" without a tag).
std::string bracket(const std::string& tag);

/// « Chef », « Officier », « Membre » (role "leader", "officer", "member").
std::string roleLabel(const std::string& role);
/// « Sur invitation », « Sur candidature », « Ouvert à tous » (policy "invite", "request", "open").
std::string policyLabel(const std::string& policy);
/// The policies in the order of the forms, and their ids.
const std::vector<std::string>& policyIds();

/// A best result of the clan on an EXTRÊME level: « Vérifié · 1ᵉʳ » (finished), « 78,4 % · 3ᵉ »,
/// without the rank when it is unknown (0).
std::string resultLabel(double score, bool finished, int rank);
/// « 1ᵉʳ », « 2ᵉ », « 12ᵉ ».
std::string ordinal(int rank);
/// « 512,4 pts » (one decimal only when needed).
std::string pointsLabel(double points);
/// « Solo », « Duo », « Trio » (team size 1, 2, 3).
std::string modeLabel(int teamSize);

/// The rules of a clan's name as the server applies them, the name already cleaned
/// (cleanName); the French message, "" when it is valid.
std::string nameError(const std::string& name);
/// Spaces collapsed and trimmed, control characters removed (what the server keeps).
std::string cleanName(const std::string& raw);
/// Characters of a UTF-8 text (code points: the server counts the same for these alphabets).
int textLength(const std::string& s);
/// 2 to 5 letters or digits, never a reserved tag; "" when valid.
std::string tagError(const std::string& tag);

// ------------------------------------------------------------------ rights

/// The chief and the officers invite, answer applications and exclude.
bool canManage(const std::string& myRole);
/// Whether `myRole` may exclude a member of role `targetRole` (the chief: officers and members;
/// an officer: members; nobody the chief).
bool canExclude(const std::string& myRole, const std::string& targetRole);
/// The member who becomes chief when the chief leaves: the oldest officer, else the oldest
/// member (the server's rule), from the page's member list (chief first, then the officers,
/// then the members, each by seniority). Their username, "" when the chief is alone.
std::string successorName(const Json& members, const std::string& leaderId);

// ------------------------------------------------------------------ emblem

/// The emblem of a clan: a shape and two colours (the shape, the border and background).
struct Emblem {
    std::string shape = "orb";
    Color primary{255, 210, 122, 255};
    Color secondary{166, 107, 255, 255};
};
/// The shapes the game draws, in the order of the forms, and their French names.
const std::vector<std::string>& emblemShapes();
std::string shapeLabel(const std::string& shape);
/// {shape, primary, secondary} of the server (unknown values: the defaults).
Emblem emblemFromJson(const Json& j);
Json emblemToJson(const Emblem& e);
/// "#rrggbb" ↔ colour (an unreadable text gives `def`).
std::string colorHex(Color c);
Color colorFromHex(const std::string& hex, Color def);

/// The colours offered by the forms (like the frames of the Collection), with their names.
struct NamedColor {
    const char* name;
    Color color;
};
const std::vector<NamedColor>& palette();
/// The palette's colour nearest to `c` (its index).
int nearestPaletteIndex(Color c);

/// The outline of a shape (convex polygons, in order) of radius `r` around `centre`: the star
/// is its points (triangles from the centre), the crescent a disc (the game cuts it with the
/// background colour). Used to draw the emblem.
std::vector<Vec2> shapeOutline(const std::string& shape, Vec2 centre, float r);

}  // namespace clans

}  // namespace cosmic
