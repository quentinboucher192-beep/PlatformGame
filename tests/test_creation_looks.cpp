// 2.59 ADMINISTRATION › Créations: trails, avatar frames and victory effects created on Cosmic
// Online. What the game reads of the server is checked (two #rrggbb colours, a style of the
// same lists as the server's, a trail's bounds), kept on this computer for offline play, won
// at a challenge, equipped in the Vestiaire, drawn for another player (boards, ghosts), and
// never part of a run (display only: GhostMeta keeps it beside the replay).

#include <fstream>
#include <regex>
#include <sstream>

#include "engine/core/FileUtil.h"
#include "game/cosmetics/Collection.h"
#include "game/cosmetics/CreationLooks.h"
#include "game/ghost/Ghost.h"
#include "game/online/DailyChallenges.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

Json trailJson() {
    return {{"colors", {{"start", "#FFD75A"}, {"end", "#9646ff"}}}, {"style", "comet"}, {"width", 140}, {"life_ms", 700}, {"particles", true}};
}
Json frameJson() { return {{"colors", {{"border", "#c0c8d8"}, {"glow", "#5ad2ff"}}}, {"style", "orbit"}}; }
Json victoryJson() { return {{"colors", {{"main", "#ff7696"}, {"second", "#6ed6ff"}}}, {"style", "fireworks"}}; }

/// The list `name = [...] as const` of server/src/creations.ts.
std::vector<std::string> serverList(const std::string& source, const std::string& name) {
    std::smatch m;
    std::vector<std::string> out;
    if (!std::regex_search(source, m, std::regex(name + R"( = \[([^\]]*)\] as const)"))) return out;
    const std::string body = m[1].str();
    std::regex item(R"re("([a-z_]+)")re");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), item); it != std::sregex_iterator(); ++it) out.push_back((*it)[1].str());
    return out;
}

Json wonEntry(const std::string& challenge, const std::string& kind, const std::string& ref, const std::string& name, Json look) {
    Json c = std::move(look);
    c["id"] = ref;
    c["kind"] = kind;
    c["name"] = name;
    return {{"challenge", {{"id", challenge}, {"kind", "daily"}, {"period", challenge.substr(6)}, {"date", challenge.substr(6)}}},
            {"kind", kind},
            {"ref", ref},
            {"item", nullptr},
            {"title", nullptr},
            {"creation", c},
            {"granted_at", "2026-11-21T01:00:00.000Z"}};
}

}  // namespace

TEST_CASE("creation looks: the same styles and bounds as the server") {
    std::ifstream f("server/src/creations.ts");
    CHECK(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();
    CHECK(serverList(src, "TRAIL_STYLES") == creationStyles("trail"));
    CHECK(serverList(src, "FRAME_STYLES") == creationStyles("frame"));
    CHECK(serverList(src, "VICTORY_STYLES") == creationStyles("victory"));
    CHECK(creationStyles("suit").empty());
    CHECK(src.find("TRAIL_WIDTH = { min: " + std::to_string(kTrailWidthMin) + ", max: " + std::to_string(kTrailWidthMax) + " }") != std::string::npos);
    CHECK(src.find("TRAIL_LIFE_MS = { min: " + std::to_string(kTrailLifeMin) + ", max: " + std::to_string(kTrailLifeMax) + " }") != std::string::npos);
    // Every style has its French name (the editor's buttons).
    for (const char* kind : {"trail", "frame", "victory"})
        for (const std::string& s : creationStyles(kind)) CHECK(!creationStyleName(kind, s).empty());
    CHECK(creationStyleName("trail", "rainbow").empty());
}

TEST_CASE("creation looks: read strictly, written back the same") {
    auto t = creationLookFromJson("trail", trailJson());
    CHECK(t && t->kind == "trail" && t->style == "comet" && t->a == (Rgb8{255, 215, 90}) && t->b == (Rgb8{150, 70, 255}));
    CHECK(t->width == 140 && t->lifeMs == 700 && t->particles && t->life() > 0.69f && t->life() < 0.71f);
    CHECK(creationLookFromJson("trail", creationLookToJson(*t)) == t);
    CHECK(creationLookToJson(*t)["colors"]["start"] == "#ffd75a");  // lower case, as the server keeps it
    auto fr = creationLookFromJson("frame", frameJson());
    CHECK(fr && fr->style == "orbit" && fr->b == (Rgb8{90, 210, 255}));
    CHECK(!creationLookToJson(*fr).contains("width"));
    auto v = creationLookFromJson("victory", victoryJson());
    CHECK(v && v->style == "fireworks" && creationLookFromJson("victory", creationLookToJson(*v)) == v);
    // Refused: another kind's colours or style, a style unknown, colours malformed, a trail out of its bounds.
    CHECK(!creationLookFromJson("frame", trailJson()));
    CHECK(!creationLookFromJson("victory", frameJson()));
    CHECK(!creationLookFromJson("suit", frameJson()));
    Json j = frameJson();
    j["style"] = "crown";
    CHECK(!creationLookFromJson("frame", j));
    j = frameJson();
    j["colors"]["glow"] = "#5ad2f";
    CHECK(!creationLookFromJson("frame", j));
    j = victoryJson();
    j["colors"] = "#ff7696";
    CHECK(!creationLookFromJson("victory", j));
    for (auto [key, value] : std::vector<std::pair<std::string, Json>>{
             {"width", 49}, {"width", 201}, {"width", 100.5}, {"life_ms", 199}, {"life_ms", 1001}, {"particles", "oui"}, {"particles", nullptr}}) {
        Json k = trailJson();
        k[key] = value;
        CHECK(!creationLookFromJson("trail", k));
    }
    // A new one starts from a valid look of its kind.
    for (const char* kind : {"trail", "frame", "victory"}) {
        CreationLook d = defaultCreationLook(kind);
        CHECK(d.kind == kind && isCreationStyle(kind, d.style));
        CHECK(creationLookFromJson(kind, creationLookToJson(d)) == d);
    }
}

TEST_CASE("creation looks: references") {
    std::string kind;
    CHECK(isServerLookRef("srvtrail:1", &kind) && kind == "trail");
    CHECK(isServerLookRef("srvframe:1234567890", &kind) && kind == "frame");
    CHECK(isServerLookRef("srvvictory:42", &kind) && kind == "victory");
    for (const char* bad : {"srvtrail:0", "srvtrail:01", "srvtrail:", "srvframe:12345678901", "srvvictory:4a", "srvsuit:3", "srv:3", "trail_comet", "../srvtrail:1"})
        CHECK(!isServerLookRef(bad));
    CHECK(isServerCreationRef("srvsuit:3") && isServerCreationRef("srvframe:3") && !isServerCreationRef("srv:3"));
}

TEST_CASE("creation looks: the public list, checked, kept on disk beside the suits") {
    const fs::path dir = fs::temp_directory_path() / "cosmic_test_creation_looks";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto entry = [](int id, const std::string& kind, const std::string& name, Json look) {
        look["id"] = id;
        look["ref"] = "srv" + kind + ":" + std::to_string(id);
        look["kind"] = kind;
        look["name"] = name;
        look["updated_at"] = "2026-11-17T10:00:00.000Z";
        return look;
    };
    Json list = Json::array({
        {{"id", 1}, {"ref", "srvsuit:1"}, {"kind", "suit"}, {"name", "Givre"}, {"colors", {{"suit", "#c8e1ff"}, {"accent", "#78c8ff"}, {"scarf", "#ebf5ff"}}}},
        entry(2, "trail", "Comète dorée", trailJson()),
        entry(3, "frame", "Orbite polaire", frameJson()),
        entry(4, "victory", "Gerbe", victoryJson()),
        entry(5, "frame", "Mauvais style", Json{{"colors", {{"border", "#000000"}, {"glow", "#ffffff"}}}, {"style", "crown"}}),
        entry(6, "trail", "", trailJson()),
    });
    list[3]["kind"] = "frame";  // a victory effect named as a frame: refused
    ServerLookBook book;
    book.setFile(dir / "creation_looks.json");
    book.load();
    CHECK(!book.known());
    CHECK(book.applyList(list));
    CHECK(book.all().size() == 2);
    CHECK(book.find("srvtrail:2") && book.find("srvtrail:2")->look.width == 140);
    CHECK(book.find("srvframe:3") && book.find("srvframe:3")->name == "Orbite polaire");
    CHECK(!book.find("srvvictory:4") && !book.find("srvframe:5") && !book.find("srvtrail:6") && !book.find("srvsuit:1"));
    CHECK(!book.applyList(list));
    CHECK(book.save());
    ServerLookBook again;
    again.setFile(dir / "creation_looks.json");
    again.load();
    CHECK(again.known() && again.all().size() == 2 && again.find("srvtrail:2")->look == book.find("srvtrail:2")->look);
    // The suits keep their own book (a game before 2.59 reads only that one).
    ServerSuitBook suits;
    CHECK(suits.applyList(list) && suits.all().size() == 1);
    // Redrawn on the server: a change.
    list[1]["style"] = "prism";
    CHECK(again.applyList(list) && again.find("srvtrail:2")->look.style == "prism");
    fs::remove_all(dir, ec);
}

TEST_CASE("creation looks: won at a challenge, in their category of the Vestiaire, equipped, drawn for others") {
    CosmeticCatalog cat;
    std::string err;
    CHECK(cat.loadDefault(&err));
    Json rewards = Json::array({
        wonEntry("daily-2026-11-20", "trail", "srvtrail:2", "Comète dorée", trailJson()),
        wonEntry("daily-2026-11-21", "frame", "srvframe:3", "Orbite polaire", frameJson()),
        wonEntry("daily-2026-11-22", "victory", "srvvictory:4", "Gerbe", victoryJson()),
        // Malformed: a ref of another kind, an unknown style, no name.
        wonEntry("daily-2026-11-23", "trail", "srvframe:9", "X", trailJson()),
        wonEntry("daily-2026-11-24", "frame", "srvframe:10", "X", Json{{"colors", {{"border", "#000000"}, {"glow", "#ffffff"}}}, {"style", "crown"}}),
        wonEntry("daily-2026-11-25", "victory", "srvvictory:11", "", victoryJson()),
    });
    auto won = WonRewards::fromJson(rewards);
    CHECK(won && won->list.size() == 3 && won->looks.size() == 3);
    const CosmeticItem* trail = won->item("srvtrail:2");
    CHECK(trail && trail->category == "trail" && trail->serverLook() && trail->serverMade() && trail->look == "srvtrail:2");
    CHECK(trail->creationLook && trail->creationLook->width == 140);
    const CosmeticItem* frame = won->item("srvframe:3");
    CHECK(frame && frame->category == "frame" && frame->creationLook->style == "orbit");
    CHECK(won->item("srvvictory:4") && won->item("srvvictory:4")->category == "victory");
    CHECK(!won->item("srvframe:9") && !won->item("srvframe:10") && !won->item("srvvictory:11"));
    auto back = WonRewards::fromJson(won->toJson());
    CHECK(back && *back == *won && back->looks.size() == 3);
    // Unlocked only once won; equipped in its category only.
    CosmeticProgress none;
    CHECK(!cosmeticUnlocked(*trail, none));
    CosmeticProgress p;
    p.won = &*won;
    CHECK(cosmeticUnlocked(*trail, p));
    CollectionBook book;
    book.equip("trail", "srvtrail:2");
    book.equip("frame", "srvframe:3");
    book.equip("victory", "srvframe:3");  // not a victory effect: the default one
    CHECK(book.equipped(cat, "trail", p) == trail);
    CHECK(book.look(cat, "frame", p) == "srvframe:3");
    CHECK(book.equipped(cat, "victory", p)->isDefault);
    CHECK(book.equippedIds(cat, p)["trail"] == "srvtrail:2");
    CHECK(book.equipped(cat, "trail", none)->isDefault);
    // Another player's look (a board's frame, a ghost's trail): a creation passes in its own category only.
    CHECK(cat.lookOr("frame", "srvframe:3") == "srvframe:3");
    CHECK(cat.lookOr("trail", "srvtrail:12") == "srvtrail:12");
    CHECK(cat.lookOr("frame", "srvtrail:12") == cat.defaultItem("frame")->look);
    CHECK(cat.lookOr("victory", "srvvictory:0") == cat.defaultItem("victory")->look);
}

TEST_CASE("creation looks: rewards of a challenge, the drawing's resolver, the ghosts") {
    Json rewards = Json::array();
    Json c = trailJson();
    c["id"] = "srvtrail:2";
    c["kind"] = "trail";
    c["name"] = "Comète dorée";
    rewards.push_back({{"kind", "trail"}, {"ref", "srvtrail:2"}, {"condition", "tier"}, {"value", 3}, {"item", nullptr}, {"title", nullptr}, {"creation", c}});
    Json f = frameJson();
    f["id"] = "srvframe:3";
    f["kind"] = "frame";
    f["name"] = "Orbite";
    rewards.push_back({{"kind", "frame"}, {"ref", "srvframe:3"}, {"condition", "finished"}, {"creation", f}});
    rewards.push_back({{"kind", "victory"}, {"ref", "srvvictory:4"}, {"condition", "finished"}, {"creation", f}});  // a frame's look: left out
    auto prizes = ChallengePrize::listFromJson(rewards);
    CHECK(prizes.size() == 2);
    CHECK(prizes[0].creation() && prizes[0].serverMade() && prizes[0].category == "trail" && prizes[0].name == "Comète dorée");
    CHECK(prizes[0].creationLook && prizes[0].creationLook->lifeMs == 700);
    CHECK(prizes[1].kind == "frame" && prizes[1].body() == Json({{"kind", "frame"}, {"ref", "srvframe:3"}, {"condition", "finished"}}));

    // The drawing asks the game for the look of a reference (GameServices sets the resolver).
    CHECK(!resolveCreationLook("srvtrail:2"));
    setCreationLookResolver([](const std::string& ref) -> std::optional<CreationLook> {
        if (ref == "srvtrail:2") return creationLookFromJson("trail", trailJson());
        return std::nullopt;
    });
    CHECK(resolveCreationLook("srvtrail:2") && resolveCreationLook("srvtrail:2")->style == "comet");
    CHECK(!resolveCreationLook("srvtrail:3"));
    CHECK(!resolveCreationLook("trail_comet"));  // a catalogue look is never asked
    setCreationLookResolver({});
    CHECK(!resolveCreationLook("srvtrail:2"));

    // A ghost keeps its player's trail beside the replay (never in it), checked when read back.
    GhostMeta m;
    m.source = "world";
    m.suit = "srvsuit:7";
    m.trail = "srvtrail:2";
    GhostMeta back = GhostMeta::fromJson(m.toJson());
    CHECK(back.trail == "srvtrail:2" && back.suit == "srvsuit:7");
    Json j = m.toJson();
    j["trail"] = "srvframe:2";
    CHECK(GhostMeta::fromJson(j).trail.empty());
    j["trail"] = "../../x";
    CHECK(GhostMeta::fromJson(j).trail.empty());
    j["trail"] = "comet";
    CHECK(GhostMeta::fromJson(j).trail == "comet");
}
