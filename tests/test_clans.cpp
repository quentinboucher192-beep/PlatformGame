// Clans (2.66): what the game shows of them and the checks of its forms (game/online/Clans), the
// same rules as the server (server/src/clans.ts), which decides every one of them again.

#include <cmath>

#include "TestFramework.h"
#include "game/online/Clans.h"

using namespace cosmic;

TEST_CASE("clans: the tag before the username, the roles and the joining") {
    CHECK(clans::tagged("Nova", "AUBE") == "[AUBE] Nova");
    CHECK(clans::tagged("Nova", "") == "Nova");
    CHECK(clans::bracket("Vega") == "[Vega]");
    CHECK(clans::bracket("").empty());
    CHECK(clans::taggedName(Json{{"username", "Lyra"}, {"clan_tag", "NOVA"}}) == "[NOVA] Lyra");
    CHECK(clans::taggedName(Json{{"username", "Lyra"}}) == "Lyra");
    CHECK(clans::taggedName(Json{{"username", "Lyra"}, {"clan_tag", nullptr}}) == "Lyra");
    CHECK(clans::taggedName(Json()) == "");
    CHECK(clans::roleLabel("leader") == "Chef");
    CHECK(clans::roleLabel("officer") == "Officier");
    CHECK(clans::roleLabel("member") == "Membre");
    CHECK(clans::policyLabel("invite") == "Sur invitation");
    CHECK(clans::policyLabel("request") == "Sur candidature");
    CHECK(clans::policyLabel("open") == "Ouvert à tous");
    CHECK(clans::policyIds().size() == 3);
}

TEST_CASE("clans: results, ranks and points as the page shows them") {
    CHECK(clans::resultLabel(78.4, false, 3) == "78,4 % · n°3");
    CHECK(clans::resultLabel(100.0, true, 1) == "Vérifié · n°1");
    CHECK(clans::resultLabel(55.0, false, 0) == "55 %");
    CHECK(clans::resultLabel(49.96, false, 12) == "50 % · n°12");
    CHECK(clans::ordinal(0).empty());
    CHECK(clans::pointsLabel(512.4) == "512,4 pts");
    CHECK(clans::pointsLabel(170.0) == "170 pts");
    CHECK(clans::pointsLabel(0.0) == "0 pts");
    CHECK(clans::modeLabel(1) == "Solo");
    CHECK(clans::modeLabel(2) == "Duo");
    CHECK(clans::modeLabel(3) == "Trio");
}

TEST_CASE("clans: names and tags checked like the server does") {
    CHECK(clans::cleanName("  Étoiles   du\x07 Nord ") == "Étoiles du Nord");
    CHECK(clans::cleanName("Ensemble‮ jusqu'au bout") == "Ensemble jusqu'au bout");
    CHECK(clans::cleanName("\t\n") == "");
    CHECK(clans::textLength("Étoile") == 6);
    CHECK(clans::nameError("Les Gardiens de l'Aube").empty());
    CHECK(clans::nameError("Étoiles du Nord").empty());
    CHECK(clans::nameError("Œuvre & Cie !").empty());
    CHECK(!clans::nameError("ab").empty());
    CHECK(clans::nameError("abc").empty());
    CHECK(clans::nameError(std::string(32, 'x')).empty());
    CHECK(!clans::nameError(std::string(33, 'x')).empty());
    // 32 accented letters are 32 characters (64 bytes).
    std::string accents;
    for (int i = 0; i < 32; ++i) accents += "é";
    CHECK(clans::nameError(accents).empty());
    CHECK(!clans::nameError("-commence mal").empty());
    CHECK(!clans::nameError("Clan <script>").empty());
    CHECK(!clans::nameError("Clan 東京").empty());
    CHECK(clans::tagError("AUBE").empty());
    CHECK(clans::tagError("aube").empty());
    CHECK(clans::tagError("C9").empty());
    CHECK(clans::tagError("ABCDE").empty());
    CHECK(!clans::tagError("A").empty());
    CHECK(!clans::tagError("TROPLG").empty());
    CHECK(!clans::tagError("A-B").empty());
    CHECK(!clans::tagError("ÉTÉ").empty());
    CHECK(!clans::tagError("admin").empty());
    CHECK(!clans::tagError("Staff").empty());
}

TEST_CASE("clans: the rights of each role and the next chief") {
    CHECK(clans::canManage("leader"));
    CHECK(clans::canManage("officer"));
    CHECK(!clans::canManage("member"));
    CHECK(!clans::canManage(""));
    // The chief excludes officers and members; an officer, members only; nobody, the chief.
    CHECK(clans::canExclude("leader", "officer"));
    CHECK(clans::canExclude("leader", "member"));
    CHECK(!clans::canExclude("leader", "leader"));
    CHECK(clans::canExclude("officer", "member"));
    CHECK(!clans::canExclude("officer", "officer"));
    CHECK(!clans::canExclude("officer", "leader"));
    CHECK(!clans::canExclude("member", "member"));
    CHECK(!clans::canExclude("", "member"));
    auto m = [](const char* id, const char* name, const char* role) { return Json{{"player", {{"id", id}, {"username", name}}}, {"role", role}}; };
    // The page lists the chief, then the officers, then the members, each by seniority.
    Json members = Json::array({m("a", "Chef", "leader"), m("b", "Lyra", "officer"), m("c", "Astra", "officer"), m("d", "Orion", "member")});
    CHECK(clans::successorName(members, "a") == "Lyra");
    Json noOfficer = Json::array({m("a", "Chef", "leader"), m("d", "Orion", "member"), m("e", "Kael", "member")});
    CHECK(clans::successorName(noOfficer, "a") == "Orion");
    CHECK(clans::successorName(Json::array({m("a", "Chef", "leader")}), "a").empty());
    CHECK(clans::successorName(Json(), "a").empty());
}

TEST_CASE("clans: the emblem (a shape, two colours) read, written and drawn") {
    clans::Emblem e = clans::emblemFromJson(Json{{"shape", "star"}, {"primary", "#FFD27A"}, {"secondary", "#a66bff"}});
    CHECK(e.shape == "star");
    CHECK(e.primary == (Color{255, 210, 122, 255}));
    CHECK(e.secondary == (Color{166, 107, 255, 255}));
    Json back = clans::emblemToJson(e);
    CHECK(back["shape"] == "star");
    CHECK(back["primary"] == "#ffd27a");
    CHECK(back["secondary"] == "#a66bff");
    // Unknown or damaged values: the defaults, never a crash.
    clans::Emblem d = clans::emblemFromJson(Json{{"shape", "cube"}, {"primary", "rouge"}, {"secondary", 42}});
    CHECK(d.shape == "orb");
    CHECK(d.primary == clans::Emblem{}.primary);
    CHECK(clans::emblemFromJson(Json()).shape == "orb");
    CHECK(clans::colorHex(Color{1, 2, 255, 255}) == "#0102ff");
    CHECK(clans::colorFromHex("#zz0000", Color{9, 9, 9, 255}) == (Color{9, 9, 9, 255}));
    // Each shape has its name; each colour of the palette is its own nearest.
    for (const auto& s : clans::emblemShapes()) CHECK(!clans::shapeLabel(s).empty());
    for (size_t i = 0; i < clans::palette().size(); ++i) CHECK(clans::nearestPaletteIndex(clans::palette()[i].color) == static_cast<int>(i));
    // Outlines: inside the radius, the expected number of points.
    const Vec2 c{100.0f, 50.0f};
    const float r = 20.0f;
    CHECK(clans::shapeOutline("star", c, r).size() == 10);
    CHECK(clans::shapeOutline("hexagon", c, r).size() == 6);
    CHECK(clans::shapeOutline("diamond", c, r).size() == 4);
    CHECK(clans::shapeOutline("shield", c, r).size() == 5);
    for (const auto& s : clans::emblemShapes())
        for (const Vec2& p : clans::shapeOutline(s, c, r)) CHECK(std::hypot(p.x - c.x, p.y - c.y) <= r * 1.2f + 1e-3f);
}
