// ADMINISTRATION › Assets (2.63): the catalogue of every installed asset (game/content/AssetCatalog.h).
// Every file of assets/ is catalogued once, in a folder of the tree; every animation names a
// sheet that exists; « utilisé par » is right for known examples and agrees with what the
// packager collects for each level; the search and the filters; the LRU cache of the
// thumbnails; data/assets/code_refs.json is up to date with the code.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <thread>

#include <SDL3/SDL.h>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "engine/core/Paths.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/AssetCatalog.h"
#include "game/content/BossStories.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/ObjectTypes.h"
#include "packaging/Package.h"
#include "tests/TestFramework.h"
#include "ui/screens/AdminAssets.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

/// Built once for the whole file (it reads every level).
const AssetCatalog& catalog() {
    static std::unique_ptr<AssetCatalog> cat;
    static ContentDatabase content;
    if (!cat) {
        content.reload();
        const auto t0 = std::chrono::steady_clock::now();
        cat = std::make_unique<AssetCatalog>(AssetCatalog::build(Paths::assetsDir(), Paths::dataDir(), content));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  catalogue des assets : %zu fichiers, %d inutilisés, lu en %.0f ms\n", cat->assets().size(), cat->unusedCount(), ms);
        for (const AssetEntry& e : cat->assets())
            if (e.unused()) std::printf("    inutilisé : %s\n", e.path.c_str());
    }
    return *cat;
}

const ContentDatabase& contentDb() {
    static ContentDatabase content;
    static bool loaded = false;
    if (!loaded) {
        content.reload();
        loaded = true;
    }
    return content;
}

const AssetUse* useOf(const AssetEntry& e, AssetUseKind kind, const std::string& id) {
    for (const AssetUse& u : e.uses)
        if (u.kind == kind && u.id == id) return &u;
    return nullptr;
}

/// The same reading as tools/assets/code_refs.py: ordinary string literals equal to an asset
/// path, uiSound("<name>") → sounds/<name>.wav, sheetOf("<name>") → animations/<name>.json, a
/// bare file name only one asset has; comments and raw strings skipped.
std::map<std::string, std::map<std::string, int>> scanCodeRefs(const fs::path& root, const std::set<std::string>& assets) {
    std::map<std::string, std::map<std::string, int>> refs;
    std::map<std::string, std::vector<std::string>> byName;  // file name → the assets having it
    for (const std::string& a : assets) byName[a.substr(a.rfind('/') + 1)].push_back(a);
    static const std::set<std::string> excluded = {"game/content/AssetCatalog.cpp", "ui/screens/AdminAssets.cpp", "ui/widgets/AssetPreview.cpp"};
    std::vector<fs::path> files;
    std::error_code ec;
    for (const char* d : {"engine", "game", "ui", "editors", "packaging", "saves"})
        for (fs::recursive_directory_iterator it(root / d, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string ext = it->path().extension().string();
            if (it->is_regular_file() && (ext == ".cpp" || ext == ".h")) files.push_back(it->path());
        }
    files.push_back(root / "main.cpp");
    for (const fs::path& f : files) {
        const std::string rel = Paths::normalizeSlashes(Paths::toUtf8(fs::relative(f, root)));
        if (excluded.count(rel)) continue;
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string src = ss.str();
        const size_t n = src.size();
        std::string lastId;
        bool paren = false;
        auto isIdStart = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
        auto isIdChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
        size_t i = 0;
        while (i < n) {
            const char c = src[i];
            if (c == '/' && i + 1 < n && src[i + 1] == '/') {
                const size_t j = src.find('\n', i);
                i = j == std::string::npos ? n : j;
                continue;
            }
            if (c == '/' && i + 1 < n && src[i + 1] == '*') {
                const size_t j = src.find("*/", i + 2);
                i = j == std::string::npos ? n : j + 2;
                continue;
            }
            if (c == '"') {
                size_t j = i + 1;
                std::string lit;
                while (j < n && src[j] != '"' && src[j] != '\n') {
                    if (src[j] == '\\' && j + 1 < n) {
                        lit += src[j + 1];
                        j += 2;
                        continue;
                    }
                    lit += src[j++];
                }
                std::string name = assets.count(lit) ? lit : std::string();
                if (name.empty() && lastId == "uiSound" && paren && assets.count("sounds/" + lit + ".wav")) name = "sounds/" + lit + ".wav";
                if (name.empty() && lastId == "sheetOf" && paren && assets.count("animations/" + lit + ".json")) name = "animations/" + lit + ".json";
                if (name.empty() && lit.find('/') == std::string::npos && lit.find('.') != std::string::npos) {
                    auto b = byName.find(lit);  // a bare file name only one asset has
                    if (b != byName.end() && b->second.size() == 1) name = b->second.front();
                }
                if (!name.empty()) ++refs[name][rel];
                i = j + 1;
                lastId.clear();
                paren = false;
                continue;
            }
            if (c == '\'') {
                size_t j = i + 1;
                while (j < n && src[j] != '\'' && src[j] != '\n') j += src[j] == '\\' ? 2 : 1;
                i = j + 1;
                lastId.clear();
                paren = false;
                continue;
            }
            if (isIdStart(c)) {
                size_t j = i;
                while (j < n && isIdChar(src[j])) ++j;
                const std::string id = src.substr(i, j - i);
                if ((id == "R" || id == "u8R" || id == "uR" || id == "UR" || id == "LR") && j < n && src[j] == '"') {
                    const size_t k = src.find('(', j + 1);
                    if (k == std::string::npos) break;
                    const size_t end = src.find(")" + src.substr(j + 1, k - j - 1) + "\"", k + 1);
                    i = end == std::string::npos ? n : end + (k - j) + 1;
                    lastId.clear();
                    paren = false;
                    continue;
                }
                lastId = id;
                paren = false;
                i = j;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                size_t j = i;
                while (j < n && (isIdChar(src[j]) || src[j] == '.' || src[j] == '\'')) ++j;
                i = j;
                lastId.clear();
                paren = false;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++i;
                continue;
            }
            if (c == '(' && !lastId.empty() && !paren) paren = true;
            else {
                lastId.clear();
                paren = false;
            }
            ++i;
        }
    }
    return refs;
}

}  // namespace

TEST_CASE("asset catalogue: every file of assets/ is catalogued once, in one folder") {
    const AssetCatalog& cat = catalog();
    std::set<std::string> onDisk;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(Paths::assetsDir(), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file()) onDisk.insert(Paths::normalizeSlashes(Paths::toUtf8(fs::relative(it->path(), Paths::assetsDir()))));
    CHECK(onDisk.size() > 500);
    CHECK(cat.assets().size() == onDisk.size());
    std::set<std::string> seen;
    uint64_t bytes = 0;
    for (size_t i = 0; i < cat.assets().size(); ++i) {
        const AssetEntry& e = cat.assets()[i];
        CHECK(onDisk.count(e.path));
        CHECK(seen.insert(e.path).second);
        CHECK(cat.indexOf(e.path) == static_cast<int>(i));
        CHECK(e.bytes == static_cast<uint64_t>(fs::file_size(cat.absolutePath(e))));
        bytes += e.bytes;
        // Its folder's own range holds it.
        const AssetFolder& f = cat.folders()[static_cast<size_t>(e.folder)];
        CHECK(static_cast<int>(i) >= f.begin && static_cast<int>(i) < f.begin + f.own);
    }
    // The tree: ranges nested, counts and weights summed up to the root.
    const AssetFolder& root = cat.folders().front();
    CHECK(root.begin == 0 && root.end == static_cast<int>(cat.assets().size()));
    CHECK(cat.totalBytes() == bytes);
    for (size_t k = 0; k < cat.folders().size(); ++k) {
        const AssetFolder& f = cat.folders()[k];
        int sum = f.own;
        uint64_t b = 0;
        for (int i = f.begin; i < f.end; ++i) b += cat.assets()[static_cast<size_t>(i)].bytes;
        CHECK(b == f.bytes);
        for (int c : f.children) {
            const AssetFolder& child = cat.folders()[static_cast<size_t>(c)];
            CHECK(child.parent == static_cast<int>(k));
            CHECK(child.depth == f.depth + 1);
            CHECK(child.begin >= f.begin && child.end <= f.end);
            sum += child.count();
        }
        CHECK(sum == f.count());
        CHECK(f.count() > 0);
    }
    // The categories asked for, in that order, at the top of the tree.
    std::vector<std::string> top;
    for (int c : root.children) top.push_back(cat.folders()[static_cast<size_t>(c)].name);
    const std::vector<std::string> expected = {"Personnages", "Ennemis", "Boss et giga-boss", "Pouvoirs", "Décors et tuiles", "Interface et icônes",
                                               "Effets et particules", "Histoires", "Musiques", "Sons", "Polices"};
    CHECK(top == expected);
    // Known places.
    auto keyOf = [&](const std::string& path) {
        const AssetEntry* e = cat.find(path);
        return e ? cat.folders()[static_cast<size_t>(e->folder)].key : std::string("?");
    };
    CHECK(keyOf("sprites/player/nova.png") == "characters/nova");
    CHECK(keyOf("sprites/player/suits/nova_givre.png") == "characters/suits");
    CHECK(keyOf("sprites/enemies/kraken_p8.png") == "bosses/confins/kraken");
    CHECK(keyOf("animations/kraken_p8.json") == "bosses/confins/kraken");  // an animation goes with its sheet
    CHECK(keyOf("sprites/enemies/golem.png").rfind("enemies/", 0) == 0);
    CHECK(keyOf("backgrounds/pyralis/sky.png") == "scenery/pyralis");
    CHECK(keyOf("sprites/decor/glacior_decor.png") == "scenery/glacior");
    CHECK(keyOf("stories/titan_3.png") == "stories/titan");
    CHECK(keyOf("music/pyralis_level.ogg") == "music/pyralis");
    CHECK(keyOf("sounds/ui_confirm.wav") == "sounds/interface");
    CHECK(keyOf("sounds/dash.wav") == "sounds/powers");
    CHECK(keyOf("fonts/Orbitron.ttf") == "fonts");
    CHECK(cat.folderTrail(cat.find("sprites/enemies/kraken_p8.png")->folder).rfind("Boss et giga-boss › ", 0) == 0);
    // Musiques: one folder per group of the jukebox, in its order.
    auto tracks = FileUtil::loadJson(Paths::dataDir() / "music" / "tracks.json");
    CHECK(tracks.has_value());
    for (const Json& t : (*tracks)["tracks"]) {
        const AssetEntry* e = cat.find(t["file"].get<std::string>());
        CHECK(e != nullptr);
        CHECK(e->type == AssetType::Music);
        CHECK(cat.folders()[static_cast<size_t>(cat.folders()[static_cast<size_t>(e->folder)].parent)].key == "music");
    }
}

TEST_CASE("asset catalogue: images, animations and sounds are read from their headers") {
    const AssetCatalog& cat = catalog();
    int images = 0, anims = 0, sounds = 0;
    for (const AssetEntry& e : cat.assets()) {
        if (e.type == AssetType::Image) {
            ++images;
            CHECK(e.width > 0 && e.height > 0);
            auto s = readPngSize(cat.absolutePath(e));
            CHECK(s && s->first == e.width && s->second == e.height);
        } else if (e.type == AssetType::Animation) {
            ++anims;
            CHECK(!e.clips.empty());
            CHECK(e.frames > 0 && e.width > 0 && e.height > 0);
        } else if (e.type == AssetType::Sound || e.type == AssetType::Music) {
            ++sounds;
            CHECK(e.durationMs > 0);
            CHECK(e.sampleRate > 0 && e.channels > 0);
        }
    }
    CHECK(images > 250 && anims > 100 && sounds > 120);
    // A known file: Nova's sheet and her animation.
    const AssetEntry* nova = cat.find("animations/nova.json");
    CHECK(nova && nova->sheet == "sprites/player/nova.png");
    CHECK(std::any_of(nova->clips.begin(), nova->clips.end(), [](const AssetClipInfo& c) { return c.name == "idle" && c.frames > 0; }));
    // Formats shown to the player.
    CHECK(formatBytes(512) == "512 o");
    CHECK(formatBytes(640 * 1024) == "640 Ko");
    CHECK(formatBytes(13002342) == "12,4 Mo");
    CHECK(formatDuration(800) == "0,8 s");
    CHECK(formatDuration(83000) == "1:23");
    CHECK(naturalLess("kraken_p2.png", "kraken_p10.png"));
    CHECK(!naturalLess("kraken_p10.png", "kraken_p2.png"));
    CHECK(naturalLess("Abc", "abd"));
    CHECK(!readPngSize(Paths::dataDir() / "music" / "tracks.json"));
}

TEST_CASE("asset catalogue: every animation referenced exists, with its sheet") {
    const AssetCatalog& cat = catalog();
    const ContentDatabase& content = contentDb();
    // Each animation's sheet is installed.
    for (const AssetEntry& e : cat.assets())
        if (e.type == AssetType::Animation) {
            CHECK(!e.sheet.empty());
            const AssetEntry* sheet = cat.find(e.sheet);
            CHECK(sheet != nullptr);
            CHECK(sheet->type == AssetType::Image);
            // A sheet inherits what uses its animation.
            for (const AssetUse& u : e.uses)
                if (u.kind != AssetUseKind::Descriptor) CHECK(useOf(*sheet, u.kind, u.id) != nullptr);
            CHECK(useOf(*sheet, AssetUseKind::Descriptor, e.path) != nullptr);
        }
    // Each animation named by an enemy (its phases too) and by an object type is catalogued.
    for (const EnemyDefinition* d : content.enemies()) {
        if (!d->builtin) continue;
        CHECK(cat.find(d->animation) && cat.find(d->animation)->type == AssetType::Animation);
        for (const BossPhase& p : d->phases)
            if (!p.animation.empty()) CHECK(cat.find(p.animation) != nullptr);
    }
    for (const ObjectTypeInfo& t : ObjectTypes::all())
        for (const std::string& a : t.assets) CHECK(cat.find(a) != nullptr);
    for (const std::string& a : DependencyCollector::coreRuntimeAssets()) {
        const AssetEntry* e = cat.find(a);
        CHECK(e != nullptr);
        CHECK(useOf(*e, AssetUseKind::Core, "core") != nullptr);
    }
}

TEST_CASE("asset catalogue: « utilisé par » is right for known examples") {
    const AssetCatalog& cat = catalog();
    const ContentDatabase& content = contentDb();
    auto uses = [&](const std::string& path, AssetUseKind kind, const std::string& id) {
        const AssetEntry* e = cat.find(path);
        return e ? useOf(*e, kind, id) : nullptr;
    };
    // An enemy and the levels where it is placed.
    CHECK(uses("animations/golem.json", AssetUseKind::Enemy, "golem"));
    CHECK(uses("sprites/enemies/golem.png", AssetUseKind::Enemy, "golem"));
    CHECK(uses("sprites/enemies/golem.png", AssetUseKind::Level, "sylvanox_01"));
    CHECK(!uses("sprites/enemies/golem.png", AssetUseKind::Boss, "golem"));
    // A giga boss: each phase's sheet, its story, the level of its fight (as many as it is placed).
    CHECK(uses("animations/kraken.json", AssetUseKind::Boss, "kraken"));
    CHECK(uses("sprites/enemies/kraken_p8.png", AssetUseKind::Boss, "kraken"));
    CHECK(uses("stories/kraken_1.png", AssetUseKind::Story, "kraken"));
    // 2.62: the pieces of the animated scenes of a story (layers, sprites) are its own too.
    {
        BossStories stories;
        CHECK(stories.loadDefault());
        int pieces = 0;
        for (const auto& [id, s] : stories.all()) {
            const story::StageAssets sa = story::stageAssets(s);
            for (const std::string& t : sa.textures) {
                CHECK(uses(t, AssetUseKind::Story, id));
                ++pieces;
            }
            for (const std::string& a : sa.animations) {
                CHECK(uses(a, AssetUseKind::Story, id));
                const AssetEntry* anim = cat.find(a);
                CHECK(anim && cat.find(anim->sheet) && cat.find(anim->sheet)->usedBy(AssetUseKind::Story, id));
                ++pieces;
            }
        }
        // The story's own pieces are filed under Histoires › Pyrrhax (the boss's sheet, which
        // its scenes use too, stays with the boss).
        if (pieces > 0) {
            int own = 0;
            for (const std::string& a : story::stageAssets(*stories.find("pyrrhax")).animations) {
                const AssetEntry* anim = cat.find(a);
                if (a.rfind("animations/stories/", 0) == 0) {
                    CHECK(cat.folders()[static_cast<size_t>(anim->folder)].key == "stories/pyrrhax");
                    ++own;
                } else if (a == "animations/pyrrhax.json") {
                    CHECK(cat.folders()[static_cast<size_t>(anim->folder)].key.rfind("bosses/", 0) == 0);
                }
            }
            CHECK(own > 10);
        }
    }
    {
        auto level = content.loadLevel("confins_03");
        CHECK(level.has_value());
        int placed = 0;
        for (const LevelObject& o : level->objects) placed += o.type == "enemy" && o.propStr("enemy") == "kraken";
        const AssetUse* u = uses("animations/kraken.json", AssetUseKind::Level, "confins_03");
        CHECK(placed > 0 && u && u->count == placed);
        CHECK(u->name == content.level("confins_03")->name);
    }
    // Counted object by object: the decor atlas of Pyralis in a level of Pyralis.
    {
        auto level = content.loadLevel("pyralis_01");
        CHECK(level.has_value());
        auto atlas = FileUtil::loadJson(Paths::assetsDir() / "sprites" / "decor" / "pyralis_decor.json");
        CHECK(atlas.has_value());
        std::set<std::string> earlier;  // sprites of the atlases read before it win (DecorAtlas::shared order)
        for (const char* a : {"sylvanox_decor.json"})
            if (auto j = FileUtil::loadJson(Paths::assetsDir() / "sprites" / "decor" / a))
                for (auto it = (*j)["sprites"].begin(); it != (*j)["sprites"].end(); ++it) earlier.insert(it.key());
        int decor = 0;
        for (const LevelObject& o : level->objects) {
            const std::string sprite = o.type == "ladder" ? std::string("hanging_vine_long") : o.propStr("sprite");
            if ((o.type == "decor" || o.type == "ladder") && (*atlas)["sprites"].contains(sprite) && !earlier.count(sprite)) ++decor;
        }
        const AssetUse* u = uses("sprites/decor/pyralis_decor.json", AssetUseKind::Level, "pyralis_01");
        CHECK(decor > 0 && u && u->count == decor);
        // Its sheet, through the atlas, the same number of times.
        const AssetUse* p = uses("sprites/decor/pyralis_decor.png", AssetUseKind::Level, "pyralis_01");
        CHECK(p && p->count == decor);
    }
    // Music: the level, its planet's group of the jukebox.
    CHECK(uses("music/pyralis_level.ogg", AssetUseKind::Level, "pyralis_01"));
    CHECK(uses("music/pyralis_level.ogg", AssetUseKind::Jukebox, "music/pyralis_level.ogg"));
    auto anyUse = [&](const std::string& path, AssetUseKind kind) {
        const AssetEntry* e = cat.find(path);
        return e && std::any_of(e->uses.begin(), e->uses.end(), [&](const AssetUse& u) { return u.kind == kind; });
    };
    for (const PlanetData* p : content.planets())
        if (!p->music.empty()) CHECK(uses(p->music, AssetUseKind::Planet, p->id));
    // A power, a suit, a planet, a particle effect, the code.
    CHECK(uses("sounds/dash.wav", AssetUseKind::Power, "dash"));
    CHECK(uses("ui/icons_powers.png", AssetUseKind::Power, "dash"));
    CHECK(uses("sprites/player/suits/nova_givre.png", AssetUseKind::Cosmetic, "suit_givre"));
    CHECK(uses("sprites/player/nova.png", AssetUseKind::Cosmetic, "suit_standard"));
    CHECK(uses("ui/planets/pyralis.png", AssetUseKind::Planet, "pyralis"));
    CHECK(uses("textures/fx/ring.png", AssetUseKind::Effect, "alert_wave"));
    CHECK(anyUse("sounds/ui_confirm.wav", AssetUseKind::Code));  // uiSound("ui_confirm")
    CHECK(uses("sounds/ui_move.wav", AssetUseKind::Code, "ui/widgets/UIContext.cpp"));
    CHECK(anyUse("textures/tiles/basalt.png", AssetUseKind::Level));  // a material, through its tiles' JSON
    CHECK(uses("textures/tiles/basalt.png", AssetUseKind::Descriptor, "textures/tiles/basalt.json"));
    // Agrees with the packager: whatever it collects for a level (beyond what every run
    // loads) is used by that level in the catalogue.
    ResourceManager res;
    res.init(nullptr, nullptr);
    DependencyCollector collector(content, res);
    const std::vector<std::string> core = DependencyCollector::coreRuntimeAssets();
    int checked = 0;
    for (const LevelEntry* l : content.levels()) {
        if (l->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(l->id);
        CHECK(level.has_value());
        const DependencySet deps = collector.collect(*level);
        for (const std::string& a : deps.assets) {
            if (std::find(core.begin(), core.end(), a) != core.end()) continue;
            if (const PlanetData* p = content.planet(level->planet); p && (a == p->illustration || a == p->banner || a == p->music)) continue;
            const AssetEntry* e = cat.find(a);
            if (!e) continue;  // the packager also names files a level may lack (checked by test_content)
            if (!e->usedBy(AssetUseKind::Level, l->id)) std::printf("  %s n'est pas noté pour %s\n", a.c_str(), l->id.c_str());
            CHECK(e->usedBy(AssetUseKind::Level, l->id));
            ++checked;
        }
    }
    CHECK(checked > 500);
    // Unused: nothing of the game names them; a sheet only described by unused JSONs is unused too.
    int unused = 0;
    for (const AssetEntry& e : cat.assets()) {
        if (!e.unused()) continue;
        ++unused;
        for (const AssetUse& u : e.uses) {
            CHECK(u.kind == AssetUseKind::Descriptor);
            const AssetEntry* d = cat.find(u.id);
            CHECK(d && d->unused());
        }
    }
    CHECK(unused == cat.unusedCount());
    CHECK(unused < static_cast<int>(cat.assets().size()) / 10);  // a few, to be tidied
}

TEST_CASE("asset catalogue: search and filters") {
    const AssetCatalog& cat = catalog();
    AssetQuery all;
    CHECK(cat.filter(all).size() == cat.assets().size());
    // Words, all required, accents and case ignored.
    CHECK(foldForSearch("Écaille Œuf ÇA") == "ecaille oeuf ca");
    AssetQuery q;
    q.text = "golem";
    auto hits = cat.filter(q);
    CHECK(std::find(hits.begin(), hits.end(), cat.indexOf("sprites/enemies/golem.png")) != hits.end());
    CHECK(std::find(hits.begin(), hits.end(), cat.indexOf("animations/golem.json")) != hits.end());
    for (int i : hits) CHECK(cat.assets()[static_cast<size_t>(i)].search.find("golem") != std::string::npos);
    q.text = "KRAKEN   p8";
    hits = cat.filter(q);
    CHECK(!hits.empty());
    for (int i : hits) {
        const std::string& s = cat.assets()[static_cast<size_t>(i)].search;
        CHECK(s.find("kraken") != std::string::npos && s.find("p8") != std::string::npos);
    }
    CHECK(std::find(hits.begin(), hits.end(), cat.indexOf("sprites/enemies/kraken_p8.png")) != hits.end());
    // « Utilisé par »: the name of a level finds its music; a folder's name finds its files.
    q.text = "forges englouties";
    hits = cat.filter(q);
    CHECK(std::find(hits.begin(), hits.end(), cat.indexOf("music/pyralis_level.ogg")) != hits.end());
    q.text = "giga-boss";
    CHECK(cat.filter(q).size() > 20);
    q.text = "zzzz introuvable";
    CHECK(cat.filter(q).empty());
    // By type.
    AssetQuery images;
    images.types = assetTypeBit(AssetType::Image);
    int pngs = 0;
    for (const AssetEntry& e : cat.assets()) pngs += e.type == AssetType::Image;
    hits = cat.filter(images);
    CHECK(static_cast<int>(hits.size()) == pngs);
    for (int i : hits) CHECK(cat.assets()[static_cast<size_t>(i)].type == AssetType::Image);
    AssetQuery sound;
    sound.types = assetTypeBit(AssetType::Sound) | assetTypeBit(AssetType::Music);
    for (int i : cat.filter(sound)) CHECK(cat.assets()[static_cast<size_t>(i)].path.rfind("sounds/", 0) == 0 || cat.assets()[static_cast<size_t>(i)].path.rfind("music/", 0) == 0);
    // Unused only.
    AssetQuery unused;
    unused.unusedOnly = true;
    hits = cat.filter(unused);
    CHECK(static_cast<int>(hits.size()) == cat.unusedCount());
    for (int i : hits) CHECK(cat.assets()[static_cast<size_t>(i)].unused());
    // A folder and what is under it, in the order of the tree.
    const AssetEntry* kp8 = cat.find("sprites/enemies/kraken_p8.png");
    int bosses = cat.folders()[static_cast<size_t>(kp8->folder)].parent;
    while (cat.folders()[static_cast<size_t>(bosses)].parent > 0) bosses = cat.folders()[static_cast<size_t>(bosses)].parent;
    AssetQuery folder;
    folder.folder = bosses;
    hits = cat.filter(folder);
    CHECK(static_cast<int>(hits.size()) == cat.folders()[static_cast<size_t>(bosses)].count());
    CHECK(std::is_sorted(hits.begin(), hits.end()));
    for (int i : hits) CHECK(cat.folderTrail(cat.assets()[static_cast<size_t>(i)].folder).rfind("Boss et giga-boss", 0) == 0);
    // What a user uses: the Kraken's sheets and animations, its story.
    AssetQuery kraken;
    kraken.userKind = AssetUseKind::Boss;
    kraken.userId = "kraken";
    hits = cat.filter(kraken);
    CHECK(hits.size() >= 16);
    for (int i : hits) CHECK(cat.assets()[static_cast<size_t>(i)].usedBy(AssetUseKind::Boss, "kraken"));
    const AssetUse* u = cat.userOf(AssetUseKind::Boss, "kraken");
    CHECK(u && !u->name.empty());
    // Filters together.
    kraken.types = assetTypeBit(AssetType::Animation);
    for (int i : cat.filter(kraken)) CHECK(cat.assets()[static_cast<size_t>(i)].type == AssetType::Animation);
}

TEST_CASE("asset catalogue: the LRU cache keeps the most recent and releases the others") {
    auto alive = std::make_shared<int>(0);
    std::weak_ptr<int> watch = alive;
    LruCache<std::string, std::shared_ptr<int>> lru(2);
    CHECK(lru.put("a", alive) == 0);
    alive.reset();
    CHECK(lru.put("b", std::make_shared<int>(2)) == 0);
    CHECK(lru.get("a") != nullptr);  // a is now the most recent
    CHECK(lru.newest() == "a" && lru.oldest() == "b");
    CHECK(lru.put("c", std::make_shared<int>(3)) == 1);  // b goes
    CHECK(!lru.contains("b") && lru.contains("a") && lru.contains("c"));
    CHECK(lru.size() == 2);
    CHECK(lru.put("c", std::make_shared<int>(4)) == 0);  // replaced, not added
    CHECK(**lru.get("c") == 4);
    CHECK(lru.oldest() == "a");
    CHECK(!watch.expired());
    CHECK(lru.put("d", std::make_shared<int>(5)) == 1);  // a goes: its value is released
    CHECK(watch.expired());
    lru.setCapacity(1);
    CHECK(lru.size() == 1 && lru.contains("d"));
    CHECK(lru.erase("d") && !lru.erase("d"));
    CHECK(lru.size() == 0 && lru.get("d") == nullptr);
    LruCache<int, int> big(100);
    for (int i = 0; i < 1000; ++i) big.put(i, i);
    CHECK(big.size() == 100 && big.oldest() == 900 && big.newest() == 999);
}

TEST_CASE("asset catalogue: code_refs.json is up to date with the code") {
    const fs::path root = Paths::installDir();
    if (!fs::exists(root / "engine") || !fs::exists(root / "tools" / "assets")) return;  // sources not here (installed game)
    std::set<std::string> assets;
    for (const AssetEntry& e : catalog().assets()) assets.insert(e.path);
    const auto scanned = scanCodeRefs(root, assets);
    auto j = FileUtil::loadJson(Paths::dataDir() / "assets" / "code_refs.json");
    CHECK(j && j->contains("refs"));
    std::map<std::string, std::map<std::string, int>> stored;
    for (auto it = (*j)["refs"].begin(); it != (*j)["refs"].end(); ++it)
        for (auto f = it->begin(); f != it->end(); ++f) stored[it.key()][f.key()] = f->get<int>();
    if (scanned != stored) std::printf("  data/assets/code_refs.json n'est pas à jour : python3 tools/assets/code_refs.py\n");
    CHECK(scanned == stored);
    CHECK(stored.count("sounds/ui_move.wav"));
}

TEST_CASE("asset catalogue: the preview track plays a file loaded off the main thread, alone") {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return;  // no audio at all on this machine
    AudioSystem audio;
    if (!audio.init()) {
        audio.shutdown();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    const AssetEntry* jump = catalog().find("sounds/jump.wav");
    CHECK(jump != nullptr);
    // MIX_LoadAudio may run on any thread: the tab loads there.
    MIX_Audio* a = nullptr;
    std::thread([&] { a = MIX_LoadAudio(audio.mixer(), Paths::toUtf8(catalog().absolutePath(*jump)).c_str(), true); }).join();
    CHECK(a != nullptr);
    audio.playPreview(a, AudioBus::Effects);
    CHECK(audio.previewPlaying());
    // Its length is the one read from the WAV header.
    CHECK(std::abs(audio.previewDurationMs() - jump->durationMs) <= 2);
    audio.stopPreview(0);
    CHECK(!audio.previewPlaying());
    // A new one replaces it (the previous audio is released), a null one is ignored.
    audio.playPreview(MIX_LoadAudio(audio.mixer(), Paths::toUtf8(catalog().absolutePath(*catalog().find("sounds/land.wav"))).c_str(), true),
                      AudioBus::Effects);
    CHECK(audio.previewPlaying());
    audio.playPreview(nullptr, AudioBus::Music);
    CHECK(audio.previewPlaying());
    audio.stopPreview(0);
    CHECK(!audio.previewPlaying() && audio.previewPositionMs() == 0);
    audio.shutdown();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// ---------------------------------------------------------------------------- the tab itself

namespace {

/// ADMINISTRATION › Assets drawn on the whole window, without the administration around it
/// (which needs an admin session): the tab exactly as it is drawn there.
class AssetsTabScene : public Scene {
public:
    explicit AssetsTabScene(GameServices& g)
        : g_(g), tab(g, [this](const std::string& m, bool) { said = m; }, [this](const std::string& id) { enemyShown = id; }) {}
    void onEnter() override { tab.enter(); }
    void render(float) override {
        UIContext& ui = g_.ui;
        ui.begin(g_.app, g_.app.frameTime() / 1000.0f);
        tab.draw({ui.s(24), ui.s(24), ui.screenW() - ui.s(48), ui.screenH() - ui.s(48)});
        ui.end();
    }
    GameServices& g_;
    AdminAssets tab;
    std::string said, enemyShown;
};

}  // namespace

TEST_CASE("asset catalogue: the Assets tab reads off the main thread, draws only what is visible") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 960;
    cfg.height = 540;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        auto owned = std::make_unique<AssetsTabScene>(g);
        AssetsTabScene* scene = owned.get();
        app.pushScene(std::move(owned));
        // The first frames come at once: the catalogue is read on its worker thread meanwhile.
        int frames = 0;
        while (!scene->tab.catalog() && frames < 3000) {
            app.runFrames(1);
            ++frames;
        }
        CHECK(scene->tab.catalog() != nullptr);
        const AssetCatalog& cat = *scene->tab.catalog();
        app.runFrames(90);
        CHECK(scene->tab.results().size() == cat.assets().size());
        // The grid: only the visible cells are drawn and their thumbnails read.
        const int drawn = scene->tab.drawnItems();
        CHECK(drawn > 0 && drawn < static_cast<int>(cat.assets().size()) / 4);
        CHECK(scene->tab.preview().images().thumbnailsCached() > 0);
        CHECK(scene->tab.preview().images().thumbnailsCached() <= static_cast<size_t>(drawn) * 3);
        // An asset chosen from elsewhere (a sheet's « Retour »): selected, its preview shown.
        for (const char* path : {"animations/golem.json", "sprites/enemies/kraken_p8.png", "sounds/jump.wav", "music/menu_theme.ogg",
                                 "fonts/Orbitron.ttf", "fonts/OFL-Orbitron.txt", "textures/tiles/basalt.json", "stories/kraken_1.png"}) {
            scene->tab.open(path);
            app.runFrames(40);
            CHECK(scene->tab.selected() == path);
            CHECK(scene->tab.preview().shown() == cat.find(path));
        }
        std::printf("  onglet Assets (960x540, grille) : %d vignettes dessinées sur %zu assets, %zu textures en cache\n", drawn, cat.assets().size(),
                    scene->tab.preview().images().thumbnailsCached());
        // Heard (loaded off the main thread), then stopped: the menu's music comes back as it was.
        const float duck = app.audio().musicDuck();
        scene->tab.open("sounds/jump.wav");
        app.runFrames(2);
        scene->tab.preview().playAudio();
        CHECK(scene->tab.preview().playing() == app.audio().available());  // loading, then playing (no device: nothing)
        for (int i = 0; i < 120 && app.audio().available() && !app.audio().previewPlaying(); ++i) app.runFrames(1);
        if (app.audio().available() && app.audio().previewPlaying()) {
            CHECK(app.audio().musicDuck() <= 0.35f);  // a sound only lowers the music
            CHECK(app.audio().previewDurationMs() > 0);
        }
        scene->tab.preview().stopAudio();
        CHECK(!scene->tab.preview().playing());
        CHECK(app.audio().musicDuck() == duck);
        app.audio().playMusic(Paths::toUtf8(Paths::assetsDir() / "music" / "menu_theme.ogg"), 0);
        scene->tab.open("music/victory.ogg");
        app.runFrames(2);
        scene->tab.preview().playAudio();
        for (int i = 0; i < 120 && app.audio().available() && !app.audio().previewPlaying(); ++i) app.runFrames(1);
        if (app.audio().available() && app.audio().previewPlaying()) CHECK(app.audio().musicPaused());  // a music waits
        scene->tab.open("sounds/land.wav");  // another asset: the music stops, the menu's comes back
        app.runFrames(2);
        CHECK(!scene->tab.preview().playing());
        CHECK(!app.audio().musicPaused());
        app.audio().stopMusic(0);
        // The search, in the list view, in English too.
        scene->tab.setSearch("kraken");
        scene->tab.setGrid(false);
        app.runFrames(10);
        CHECK(!scene->tab.results().empty());
        for (int i : scene->tab.results()) CHECK(cat.assets()[static_cast<size_t>(i)].search.find("kraken") != std::string::npos);
        CHECK(scene->tab.drawnItems() > 0);
        i18n::setLanguage(Language::English);
        app.runFrames(10);
        i18n::setLanguage(Language::French);
        scene->tab.setSearch("");
        app.runFrames(10);
        CHECK(scene->tab.results().size() == cat.assets().size());
        CHECK(scene->tab.drawnItems() < static_cast<int>(cat.assets().size()) / 4);
        app.shutdown();  // the scenes first (they use the services)
    }
}
