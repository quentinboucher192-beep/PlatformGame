// Suits created on Cosmic Online (ADMINISTRATION › Créations): the run-time recolour of Nova's
// sheet is a port of recolor() in tools/assetgen/characters/nova_suits.py. Given the colours of
// a catalogue suit it must give back that suit's PNG exactly (every channel of every pixel, the
// transparent ones included: no tolerance needed, the port follows Python's colorsys in double
// precision with the same rounding). Then what the game reads of the server: colours, the
// public list kept on disk, the suits won, worn and drawn by others.

#include <cstring>

#include "engine/core/FileUtil.h"
#include "game/cosmetics/Collection.h"
#include "game/cosmetics/SuitRecolor.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

struct CatalogueSuit {
    const char* id;
    Rgb8 suit, accent, scarf;
};

// SUITS of tools/assetgen/characters/nova_suits.py (id -> suit, accent, scarf).
const CatalogueSuit kSuits[] = {
    {"givre", {200, 225, 255}, {120, 200, 255}, {235, 245, 255}},
    {"braise", {230, 200, 185}, {255, 150, 60}, {220, 40, 40}},
    {"seve", {205, 235, 205}, {120, 255, 150}, {176, 90, 255}},
    {"noyau", {70, 80, 120}, {255, 210, 90}, {255, 200, 80}},
    {"spectre", {205, 190, 250}, {190, 120, 255}, {120, 230, 255}},
    {"or", {255, 225, 140}, {255, 255, 235}, {200, 30, 60}},
    {"echo", {60, 60, 75}, {255, 70, 100}, {255, 70, 100}},
    {"nebuleuse", {235, 150, 215}, {90, 230, 255}, {150, 80, 255}},
    {"abysse", {55, 125, 150}, {80, 255, 190}, {255, 120, 100}},
    {"eruption", {105, 70, 65}, {255, 120, 30}, {255, 215, 60}},
    {"sanctuaire", {236, 233, 240}, {190, 150, 255}, {255, 200, 70}},
    {"champion", {140, 20, 48}, {255, 205, 80}, {245, 245, 255}},
    {"obsidienne", {52, 44, 60}, {255, 110, 40}, {190, 40, 110}},  // 2.48-2.53: the giga bosses of Les Confins
    {"granit", {128, 122, 116}, {255, 178, 70}, {110, 150, 80}},
    {"encre", {60, 52, 112}, {110, 235, 255}, {40, 210, 180}},
    {"aube", {52, 47, 103}, {255, 208, 74}, {255, 110, 96}},  // 2.56: the Phénix
    {"linceul", {58, 46, 78}, {180, 245, 102}, {226, 214, 184}},  // 2.57: the Liche
};

fs::path playerSprites() { return Paths::assetsDir() / "sprites" / "player"; }

}  // namespace

TEST_CASE("suit recolour: the colours of « givre » give nova_givre.png back, to the bit") {
    std::vector<uint8_t> base, want;
    int w = 0, h = 0, w2 = 0, h2 = 0;
    CHECK(loadRgbaImage(playerSprites() / "nova.png", base, w, h));
    CHECK(loadRgbaImage(playerSprites() / "suits" / "nova_givre.png", want, w2, h2));
    CHECK(w == w2 && h == h2 && base.size() == want.size());
    const CatalogueSuit& g = kSuits[0];
    std::vector<uint8_t> got(base.size());
    recolorSuitPixels(base.data(), got.data(), base.size() / 4, SuitColors{g.suit, g.accent, g.scarf});
    size_t differ = 0;
    for (size_t i = 0; i < got.size(); ++i) differ += got[i] != want[i] ? 1 : 0;
    CHECK(differ == 0);
    // In place too (the same buffer as source and destination).
    std::vector<uint8_t> inPlace = base;
    recolorSuitPixels(inPlace.data(), inPlace.data(), inPlace.size() / 4, SuitColors{g.suit, g.accent, g.scarf});
    CHECK(inPlace == want);
}

TEST_CASE("suit recolour: every suit of the catalogue, exactly as nova_suits.py made it") {
    std::vector<uint8_t> base;
    int w = 0, h = 0;
    CHECK(loadRgbaImage(playerSprites() / "nova.png", base, w, h));
    for (const CatalogueSuit& s : kSuits) {
        std::vector<uint8_t> want;
        int w2 = 0, h2 = 0;
        CHECK(loadRgbaImage(playerSprites() / "suits" / (std::string("nova_") + s.id + ".png"), want, w2, h2));
        std::vector<uint8_t> got(base.size());
        recolorSuitPixels(base.data(), got.data(), base.size() / 4, SuitColors{s.suit, s.accent, s.scarf});
        if (got != want) std::printf("    combinaison %s : différente de son PNG\n", s.id);
        CHECK(got == want);
    }
}

TEST_CASE("suit recolour: the sheets the game draws (texture cache) are the same pixels") {
    SuitTextureCache cache;
    CHECK(cache.pixels(SuitColors{}).empty());  // nothing before Nova's sheet is read
    CHECK(cache.loadBase(playerSprites() / "nova.png"));
    for (const CatalogueSuit& s : {kSuits[0], kSuits[3], kSuits[6], kSuits[11]}) {
        std::vector<uint8_t> want;
        int w = 0, h = 0;
        CHECK(loadRgbaImage(playerSprites() / "suits" / (std::string("nova_") + s.id + ".png"), want, w, h));
        CHECK(cache.pixels(SuitColors{s.suit, s.accent, s.scarf}) == want);
    }
    // Any other colours: the same as the whole-sheet recolour.
    std::vector<uint8_t> base;
    int w = 0, h = 0;
    CHECK(loadRgbaImage(playerSprites() / "nova.png", base, w, h));
    const SuitColors odd{{3, 250, 128}, {0, 0, 0}, {128, 128, 128}};
    std::vector<uint8_t> whole(base.size());
    recolorSuitPixels(base.data(), whole.data(), base.size() / 4, odd);
    CHECK(cache.pixels(odd) == whole);
}

TEST_CASE("suit recolour: colours, references, cache key") {
    CHECK(parseHexColor("#c8e1ff") == (Rgb8{200, 225, 255}));
    CHECK(parseHexColor("#C8E1FF") == (Rgb8{200, 225, 255}));
    CHECK(!parseHexColor("c8e1ff"));
    CHECK(!parseHexColor("#c8e1f"));
    CHECK(!parseHexColor("#c8e1fg"));
    CHECK(!parseHexColor("#c8e1ff0"));
    CHECK(hexColor({0, 15, 255}) == "#000fff");
    auto c = suitColorsFromJson(Json{{"suit", "#c8e1ff"}, {"accent", "#78C8FF"}, {"scarf", "#ebf5ff"}});
    CHECK(c && c->accent == (Rgb8{120, 200, 255}));
    CHECK(c->key() == "#c8e1ff#78c8ff#ebf5ff");
    CHECK(suitColorsFromJson(suitColorsToJson(*c)) == c);
    CHECK(!suitColorsFromJson(Json{{"suit", "#c8e1ff"}, {"accent", "#78c8ff"}}));
    CHECK(!suitColorsFromJson(Json::array()));
    CHECK(isServerSuitRef("srvsuit:1"));
    CHECK(isServerSuitRef("srvsuit:1234567890"));
    CHECK(!isServerSuitRef("srvsuit:0"));
    CHECK(!isServerSuitRef("srvsuit:01"));
    CHECK(!isServerSuitRef("srvsuit:"));
    CHECK(!isServerSuitRef("srvsuit:12345678901"));
    CHECK(!isServerSuitRef("srvsuit:1a"));
    CHECK(!isServerSuitRef("srv:1"));
    CHECK(!isServerSuitRef("../srvsuit:1"));
}

TEST_CASE("suit recolour: the public list, checked, kept on disk") {
    const fs::path dir = fs::temp_directory_path() / "cosmic_test_server_suits";
    std::error_code ec;
    fs::remove_all(dir, ec);
    ServerSuitBook book;
    book.setFile(dir / "creations.json");
    book.load();  // no file yet
    CHECK(!book.known());
    Json list = Json::array({
        {{"id", 3}, {"ref", "srvsuit:3"}, {"kind", "suit"}, {"name", "Aurore"}, {"colors", {{"suit", "#c8e1ff"}, {"accent", "#78c8ff"}, {"scarf", "#ebf5ff"}}},
         {"updated_at", "2026-11-16T10:00:00.000Z"}},
        {{"ref", "srvsuit:4"}, {"kind", "suit"}, {"name", "Sans couleurs"}},                                               // malformed
        {{"ref", "srvsuit:0"}, {"name", "Zéro"}, {"colors", {{"suit", "#000000"}, {"accent", "#000000"}, {"scarf", "#000000"}}}},  // bad ref
        {{"ref", "srvsuit:5"}, {"kind", "hat"}, {"name", "Chapeau"}, {"colors", {{"suit", "#000000"}, {"accent", "#000000"}, {"scarf", "#000000"}}}},
        {{"ref", "srvsuit:6"}, {"name", std::string("Ligne\x01") + std::string(40, 'x')}, {"colors", {{"suit", "#010203"}, {"accent", "#040506"}, {"scarf", "#070809"}}}},
    });
    CHECK(book.applyList(list));
    CHECK(book.known());
    CHECK(book.all().size() == 2);
    const ServerSuit* a = book.find("srvsuit:3");
    CHECK(a && a->name == "Aurore" && a->colors.suit == (Rgb8{200, 225, 255}));
    const ServerSuit* b = book.find("srvsuit:6");
    CHECK(b && b->name.size() == 32 && b->name.find('\x01') == std::string::npos);
    CHECK(!book.applyList(list));  // the same list: nothing changed
    CHECK(book.save());
    ServerSuitBook again;
    again.setFile(dir / "creations.json");
    again.load();
    CHECK(again.known() && again.all().size() == 2 && again.find("srvsuit:3")->colors == a->colors);
    // A recoloured suit is a change.
    list[0]["colors"]["suit"] = "#ffd700";
    CHECK(again.applyList(list));
    CHECK(again.find("srvsuit:3")->colors.suit == (Rgb8{255, 215, 0}));
    fs::remove_all(dir, ec);
}

TEST_CASE("suit recolour: a suit won at a challenge, equipped, and others' looks") {
    CosmeticCatalog cat;
    std::string err;
    CHECK(cat.loadDefault(&err));
    Json rewards = Json::array({
        {{"challenge", {{"id", "daily-2026-11-20"}, {"kind", "daily"}, {"period", "2026-11-20"}, {"date", "2026-11-20"}}},
         {"kind", "suit"},
         {"ref", "srvsuit:7"},
         {"item", nullptr},
         {"title", nullptr},
         {"suit", {{"id", "srvsuit:7"}, {"name", "Aurore"}, {"colors", {{"suit", "#c8e1ff"}, {"accent", "#78c8ff"}, {"scarf", "#ebf5ff"}}}}},
         {"granted_at", "2026-11-21T01:00:00.000Z"}},
        // Malformed: no colours, or a suit that is not "srvsuit:<n>".
        {{"challenge", {{"id", "daily-2026-11-21"}}}, {"kind", "suit"}, {"ref", "srvsuit:8"}, {"suit", {{"id", "srvsuit:8"}, {"name", "X"}}}},
        {{"challenge", {{"id", "daily-2026-11-22"}}}, {"kind", "suit"}, {"ref", "suit_givre"},
         {"suit", {{"id", "suit_givre"}, {"name", "X"}, {"colors", {{"suit", "#000000"}, {"accent", "#000000"}, {"scarf", "#000000"}}}}}},
    });
    auto won = WonRewards::fromJson(rewards);
    CHECK(won && won->known);
    CHECK(won->list.size() == 1 && won->suits.size() == 1);
    const CosmeticItem* s = won->item("srvsuit:7");
    CHECK(s && s->category == "suit" && s->serverSuit() && s->look == "srvsuit:7" && s->name == "Aurore");
    CHECK(s->suitColors && s->suitColors->accent == (Rgb8{120, 200, 255}));
    // Kept on this computer in the same form.
    auto back = WonRewards::fromJson(won->toJson());
    CHECK(back && *back == *won);
    // Unlocked only won; equipped like any suit.
    CosmeticProgress p;
    CHECK(!cosmeticUnlocked(*s, p));
    p.won = &*won;
    CHECK(cosmeticUnlocked(*s, p));
    CollectionBook book;
    book.equip("suit", "srvsuit:7");
    CHECK(book.equipped(cat, "suit", p) == s);
    CHECK(book.look(cat, "suit", p) == "srvsuit:7");
    CHECK(book.equippedIds(cat, p)["suit"] == "srvsuit:7");
    // Without the reward (another account): the default suit.
    CosmeticProgress none;
    CHECK(book.equipped(cat, "suit", none)->isDefault);
    // Another player's look (a board, a ghost, a profile): a server suit passes as it is, a
    // malformed one gives the default.
    CHECK(cat.lookOr("suit", "srvsuit:12") == "srvsuit:12");
    CHECK(cat.lookOr("suit", "srvsuit:0") == "standard");
    CHECK(cat.lookOr("frame", "srvsuit:12") == "simple");
}
