#include "game/content/AssetCatalog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <system_error>

#include "engine/animation/Animation.h"
#include "engine/core/FileUtil.h"
#include "engine/core/I18n.h"
#include "game/content/BossStories.h"
#include "game/content/ContentDatabase.h"
#include "game/cosmetics/Collection.h"
#include "game/levels/ObjectTypes.h"
#include "game/levels/TerrainRenderer.h"
#include "packaging/Package.h"

namespace cosmic {

// ============================================================================ small helpers

const char* assetTypeLabel(AssetType t) {
    switch (t) {
        case AssetType::Image: return "Image";
        case AssetType::Animation: return "Animation";
        case AssetType::Sound: return "Son";
        case AssetType::Music: return "Musique";
        case AssetType::Font: return "Police";
        case AssetType::Data: return "Données";
        case AssetType::Count: break;
    }
    return "Données";
}

const char* assetUseKindLabel(AssetUseKind k) {
    switch (k) {
        case AssetUseKind::Core: return "Moteur du jeu";
        case AssetUseKind::Code: return "Code du jeu";
        case AssetUseKind::Cosmetic: return "Vestiaire";
        case AssetUseKind::Enemy: return "Ennemi";
        case AssetUseKind::Boss: return "Boss";
        case AssetUseKind::Level: return "Niveau";
        case AssetUseKind::Planet: return "Planète";
        case AssetUseKind::Power: return "Pouvoir";
        case AssetUseKind::Story: return "Histoire";
        case AssetUseKind::Effect: return "Effet de particules";
        case AssetUseKind::Jukebox: return "Juke-box";
        case AssetUseKind::Descriptor: return "Décrit par";
        case AssetUseKind::Count: break;
    }
    return "Code du jeu";
}

bool AssetEntry::unused() const {
    for (const AssetUse& u : uses)
        if (u.kind != AssetUseKind::Descriptor) return false;
    return true;
}

int AssetEntry::useCount() const {
    int n = 0;
    for (const AssetUse& u : uses) n += u.count;
    return n;
}

bool AssetEntry::usedBy(AssetUseKind kind, std::string_view id) const {
    for (const AssetUse& u : uses)
        if (u.kind == kind && u.id == id) return true;
    return false;
}

std::string foldForSearch(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char>(std::tolower(c));
            continue;
        }
        // Latin-1 letters (U+00C0..U+00FF: C3 80..C3 BF) and Œ / œ (C5 92 / C5 93).
        if (c == 0xC3 && i + 1 < s.size()) {
            static const char* kBase = "aaaaaaaceeeeiiiidnooooo/ouuuuyts"  // U+00C0..U+00DF
                                       "aaaaaaaceeeeiiiidnooooo/ouuuuyty";  // U+00E0..U+00FF
            unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d >= 0x80 && d <= 0xBF) {
                char b = kBase[d - 0x80];
                if (d == 0x86 || d == 0xA6) out += "ae";
                else if (d == 0x9F) out += "ss";
                else if (b != '/') out += b;
                ++i;
                continue;
            }
        }
        if (c == 0xC5 && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) == 0x92 || static_cast<unsigned char>(s[i + 1]) == 0x93)) {
            out += "oe";
            ++i;
            continue;
        }
        out += static_cast<char>(c);  // other bytes kept as they are (still found when typed the same)
    }
    return out;
}

bool naturalLess(std::string_view a, std::string_view b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = std::isdigit(static_cast<unsigned char>(a[i])) != 0, db = std::isdigit(static_cast<unsigned char>(b[j])) != 0;
        if (da && db) {
            size_t ei = i, ej = j;
            while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
            while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
            // Compare the numbers by value (leading zeros ignored), then by length.
            size_t si = i, sj = j;
            while (si + 1 < ei && a[si] == '0') ++si;
            while (sj + 1 < ej && b[sj] == '0') ++sj;
            if (ei - si != ej - sj) return ei - si < ej - sj;
            for (size_t k = 0; k < ei - si; ++k)
                if (a[si + k] != b[sj + k]) return a[si + k] < b[sj + k];
            i = ei;
            j = ej;
            continue;
        }
        const int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[j]));
        if (ca != cb) return ca < cb;
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

std::string formatBytes(uint64_t bytes) {
    char b[32];
    if (bytes < 1024) std::snprintf(b, sizeof b, "%u o", static_cast<unsigned>(bytes));
    else if (bytes < 1024 * 1024) std::snprintf(b, sizeof b, "%.0f Ko", static_cast<double>(bytes) / 1024.0);
    else std::snprintf(b, sizeof b, "%.1f Mo", static_cast<double>(bytes) / (1024.0 * 1024.0));
    std::string s = b;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::string formatDuration(int64_t ms) {
    if (ms <= 0) return "—";
    char b[32];
    if (ms < 10000) {
        std::snprintf(b, sizeof b, "%.1f s", static_cast<double>(ms) / 1000.0);
        std::string s = b;
        std::replace(s.begin(), s.end(), '.', ',');
        return s;
    }
    const int64_t s = (ms + 500) / 1000;
    std::snprintf(b, sizeof b, "%d:%02d", static_cast<int>(s / 60), static_cast<int>(s % 60));
    return b;
}

// ============================================================================ file headers

std::optional<std::pair<int, int>> readPngSize(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    unsigned char h[24] = {};
    if (!f.read(reinterpret_cast<char*>(h), sizeof h)) return std::nullopt;
    static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(h, kSig, 8) != 0 || std::memcmp(h + 12, "IHDR", 4) != 0) return std::nullopt;
    auto be = [&](int o) { return static_cast<int>((uint32_t(h[o]) << 24) | (uint32_t(h[o + 1]) << 16) | (uint32_t(h[o + 2]) << 8) | uint32_t(h[o + 3])); };
    const int w = be(16), hh = be(20);
    if (w <= 0 || hh <= 0) return std::nullopt;
    return std::make_pair(w, hh);
}

namespace {

uint32_t le32(const unsigned char* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint16_t le16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

std::optional<AudioHeader> readWav(std::ifstream& f) {
    unsigned char riff[12];
    if (!f.read(reinterpret_cast<char*>(riff), 12) || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) return std::nullopt;
    AudioHeader h;
    uint32_t byteRate = 0, dataBytes = 0;
    bool fmt = false, data = false;
    for (int guard = 0; guard < 64 && !(fmt && data); ++guard) {
        unsigned char ch[8];
        if (!f.read(reinterpret_cast<char*>(ch), 8)) break;
        const uint32_t size = le32(ch + 4);
        if (std::memcmp(ch, "fmt ", 4) == 0 && size >= 16) {
            unsigned char b[16];
            if (!f.read(reinterpret_cast<char*>(b), 16)) break;
            h.channels = le16(b + 2);
            h.sampleRate = static_cast<int>(le32(b + 4));
            byteRate = le32(b + 8);
            fmt = true;
            f.seekg(static_cast<std::streamoff>(size - 16 + (size & 1)), std::ios::cur);
        } else {
            if (std::memcmp(ch, "data", 4) == 0) {
                dataBytes = size;
                data = true;
            }
            f.seekg(static_cast<std::streamoff>(size + (size & 1)), std::ios::cur);
        }
    }
    if (!fmt) return std::nullopt;
    if (byteRate > 0 && data) h.durationMs = static_cast<int64_t>(uint64_t(dataBytes) * 1000u / byteRate);
    return h;
}

std::optional<AudioHeader> readOgg(std::ifstream& f, uint64_t fileSize) {
    // First page: the identification header of its stream (Vorbis or Opus).
    unsigned char page[27 + 255];
    if (!f.read(reinterpret_cast<char*>(page), 27) || std::memcmp(page, "OggS", 4) != 0) return std::nullopt;
    const int segments = page[26];
    if (!f.read(reinterpret_cast<char*>(page + 27), segments)) return std::nullopt;
    unsigned char id[32] = {};
    f.read(reinterpret_cast<char*>(id), sizeof id);
    AudioHeader h;
    int64_t preSkip = 0;
    if (id[0] == 0x01 && std::memcmp(id + 1, "vorbis", 6) == 0) {
        h.channels = id[11];
        h.sampleRate = static_cast<int>(le32(id + 12));
    } else if (std::memcmp(id, "OpusHead", 8) == 0) {
        h.channels = id[9];
        preSkip = le16(id + 10);
        h.sampleRate = 48000;  // Opus granules count 48 kHz samples
    } else {
        return std::nullopt;
    }
    // Last page: its granule position is the number of samples of the stream.
    const uint64_t tail = std::min<uint64_t>(fileSize, 65536);
    std::vector<char> buf(static_cast<size_t>(tail));
    f.clear();
    f.seekg(static_cast<std::streamoff>(fileSize - tail), std::ios::beg);
    if (!f.read(buf.data(), static_cast<std::streamsize>(tail)) || h.sampleRate <= 0) return h;
    for (size_t i = buf.size() >= 27 ? buf.size() - 27 : 0;; --i) {
        if (std::memcmp(buf.data() + i, "OggS", 4) == 0) {
            const unsigned char* g = reinterpret_cast<const unsigned char*>(buf.data() + i + 6);
            uint64_t granule = 0;
            for (int k = 7; k >= 0; --k) granule = (granule << 8) | g[k];
            if (granule != ~uint64_t(0)) {
                const int64_t samples = std::max<int64_t>(0, static_cast<int64_t>(granule) - preSkip);
                h.durationMs = samples * 1000 / h.sampleRate;
            }
            break;
        }
        if (i == 0) break;
    }
    return h;
}

}  // namespace

std::optional<AudioHeader> readAudioHeader(const fs::path& file) {
    std::error_code ec;
    const uint64_t size = fs::file_size(file, ec);
    if (ec || size < 12) return std::nullopt;
    std::ifstream f(file, std::ios::binary);
    if (!f) return std::nullopt;
    char magic[4];
    if (!f.read(magic, 4)) return std::nullopt;
    f.seekg(0);
    if (std::memcmp(magic, "RIFF", 4) == 0) return readWav(f);
    if (std::memcmp(magic, "OggS", 4) == 0) return readOgg(f, size);
    return std::nullopt;
}

// ============================================================================ the uses

namespace {

/// Every use found, by asset path; the same user (kind, id) is counted once with its total.
class Uses {
public:
    void add(const std::string& path, AssetUseKind kind, const std::string& id, const std::string& name, bool literal, int count = 1) {
        if (path.empty() || count <= 0) return;
        std::vector<AssetUse>& v = map_[Paths::normalizeSlashes(path)];
        for (AssetUse& u : v)
            if (u.kind == kind && u.id == id) {
                u.count += count;
                return;
            }
        v.push_back({kind, id, name, literal, count});
    }
    /// Adds `u` to `path` unless that user is already there (then the larger count is kept):
    /// a sheet named directly and through its animation is not counted twice.
    void inherit(const std::string& path, const AssetUse& u) {
        std::vector<AssetUse>& v = map_[path];
        for (AssetUse& w : v)
            if (w.kind == u.kind && w.id == u.id) {
                w.count = std::max(w.count, u.count);
                return;
            }
        v.push_back(u);
    }
    std::vector<AssetUse>* find(const std::string& path) {
        auto it = map_.find(path);
        return it == map_.end() ? nullptr : &it->second;
    }

private:
    std::unordered_map<std::string, std::vector<AssetUse>> map_;
};

using Counts = std::map<std::string, int>;  // asset path → references

/// The files one native enemy needs (its sheets, sounds, the visuals of its attacks), with
/// the number of references inside its definition; summoned creatures are its own users'.
Counts enemyFiles(const EnemyDefinition& d) {
    Counts c;
    auto add = [&](const std::string& p) {
        if (!p.empty()) ++c[Paths::normalizeSlashes(p)];
    };
    add(d.animation);
    add(d.hurtSound);
    add(d.deathSound);
    add(d.alertSound);
    for (const AttackDef& a : d.attacks) {
        add(a.sound);
        add(a.telegraphSound);
        // As the packager collects them (2.33): the look of its lasting effects.
        if (a.look == "plasma") add("animations/plasma_flood.json");
        if (a.look == "star") add("animations/star_wall.json");
        if (!a.beamTexture.empty()) add("animations/beams.json");
    }
    for (const BossPhase& p : d.phases) {
        add(p.animation);
        add(p.enterSound);
    }
    return c;
}

/// Name → atlas JSON of every decor sprite of the game (sprites/decor/*.json).
std::map<std::string, std::string> decorSprites(const fs::path& assetsDir) {
    std::map<std::string, std::string> out;
    for (const std::string& atlas : {std::string("sprites/decor/sylvanox_decor.json"), std::string("sprites/decor/pyralis_decor.json"),
                                     std::string("sprites/decor/glacior_decor.json")}) {
        auto j = FileUtil::loadJson(assetsDir / Paths::fromUtf8(atlas));
        if (!j || !j->contains("sprites") || !(*j)["sprites"].is_object()) continue;
        for (auto it = (*j)["sprites"].begin(); it != (*j)["sprites"].end(); ++it) out.emplace(it.key(), atlas);  // first atlas wins, as DecorAtlas::shared()
    }
    return out;
}

/// A world (a planet's campaign) and its rank in the campaign order (the order of the folders).
struct World {
    int rank = 1000;  // 1000: outside the campaigns
    std::string id;
};

}  // namespace

// ============================================================================ build

AssetCatalog AssetCatalog::build(const fs::path& assetsDir, const fs::path& dataDir, const ContentDatabase& content) {
    AssetCatalog cat;
    cat.root_ = assetsDir;
    Uses uses;

    // ------------------------------------------------------------------ who uses what
    // Every run.
    for (const std::string& a : DependencyCollector::coreRuntimeAssets()) uses.add(a, AssetUseKind::Core, "core", "Chargé à chaque partie", false);

    // The game's code (data/assets/code_refs.json, tools/assets/code_refs.py).
    if (auto j = FileUtil::loadJson(dataDir / "assets" / "code_refs.json"); j && j->contains("refs") && (*j)["refs"].is_object())
        for (auto& [asset, files] : (*j)["refs"].items())
            if (files.is_object())
                for (auto& [file, n] : files.items())
                    uses.add(asset, AssetUseKind::Code, file, file, true, n.is_number_integer() ? n.get<int>() : 1);

    // The planets, in campaign order (also the order of the folders by world).
    std::map<std::string, World> planetRank;
    std::map<std::string, std::string> planetName;
    {
        int rank = 0;
        for (const PlanetData* p : content.planets()) {
            planetRank[p->id] = {rank++, p->id};
            planetName[p->id] = p->name;
            uses.add(p->illustration, AssetUseKind::Planet, p->id, p->name, false);
            uses.add(p->banner, AssetUseKind::Planet, p->id, p->name, false);
            uses.add(p->music, AssetUseKind::Planet, p->id, p->name, false);
        }
    }

    // Enemies and bosses (native ones only: the game's own files).
    std::map<std::string, Counts> enemySets;  // id → its files (summons included), for the levels
    std::map<std::string, const EnemyDefinition*> natives;
    for (const EnemyDefinition* d : content.enemies()) {
        if (!d->builtin || !content.isNativeEnemy(d->id)) continue;
        natives[d->id] = d;
        const AssetUseKind kind = d->isBoss ? AssetUseKind::Boss : AssetUseKind::Enemy;
        for (const auto& [file, n] : enemyFiles(*d)) uses.add(file, kind, d->id, d->name, false, n);
    }
    // A placed enemy brings its summons and what it splits into.
    std::function<void(const std::string&, Counts&, std::set<std::string>&)> gather = [&](const std::string& id, Counts& out, std::set<std::string>& seen) {
        if (!seen.insert(id).second) return;
        auto it = natives.find(id);
        if (it == natives.end()) return;
        for (const auto& [file, n] : enemyFiles(*it->second)) out[file] = 1;
        for (const AttackDef& a : it->second->attacks)
            if (a.type == AttackType::Summon) gather(a.summonId, out, seen);
        if (it->second->deathSpawnCount > 0) gather(it->second->deathSpawnId, out, seen);
    };
    auto enemySet = [&](const std::string& id) -> const Counts& {
        auto it = enemySets.find(id);
        if (it != enemySets.end()) return it->second;
        Counts c;
        std::set<std::string> seen;
        gather(id, c, seen);
        return enemySets.emplace(id, std::move(c)).first->second;
    };

    // Powers.
    std::map<std::string, const PowerDefinition*> powers;
    for (const PowerDefinition* p : content.powers()) {
        if (!content.isNativePower(p->id)) continue;
        powers[p->id] = p;
        uses.add(p->sound, AssetUseKind::Power, p->id, p->name, false);
        uses.add(p->soundEnd, AssetUseKind::Power, p->id, p->name, false);
        uses.add("ui/icons_powers.png", AssetUseKind::Power, p->id, p->name, false);  // its icon
    }

    // Particle effects (presets of data/effects).
    const Json& effects = content.effects();
    auto effectTexture = [&](const std::string& name) -> std::string {
        auto it = effects.find(name);
        return it != effects.end() && it->is_object() ? jstr(*it, "texture") : std::string();
    };
    for (auto it = effects.begin(); it != effects.end(); ++it)
        if (it->is_object()) uses.add(jstr(*it, "texture"), AssetUseKind::Effect, it.key(), it.key(), true);

    // Levels of the game, object by object (as DependencyCollector collects them).
    const std::map<std::string, std::string> decor = decorSprites(assetsDir);
    std::map<std::string, World> enemyWorld;  // the first campaign world where an enemy is met
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        std::optional<LevelData> level = content.loadLevel(e->id);
        if (!level) continue;
        Counts c;
        auto add = [&](const std::string& p, int n = 1) {
            if (!p.empty()) c[Paths::normalizeSlashes(p)] += n;
        };
        auto power = [&](const std::string& id) {
            auto it = powers.find(id);
            if (it == powers.end()) return;
            add(it->second->sound);
            add(it->second->soundEnd);
        };
        const std::string planet = content.campaignPlanetOf(e->id);
        const World world = planetRank.count(planet) ? planetRank[planet] : World{};
        add(level->music);
        add(level->bossMusic);
        add(level->ambience);
        for (const auto& l : level->background) add(l.texture);
        for (const auto& pw : level->startPowers) power(pw);
        std::map<std::string, int> placed;  // enemy id → placements
        for (const LevelObject& o : level->objects) {
            if (o.type == "power_gate") {
                const std::string pw = o.propStr("power", "dash");
                if (pw == "dash" || pw == "impulse") add("animations/breakable_wall.json");
                else if (pw == "teleport") add("animations/phase_wall.json");
                else if (pw == "ground_pound") add("animations/cracked_floor.json");
                else add("sprites/objects/laser_beam.png");
                add("ui/icons_powers.png");
                continue;
            }
            if (const ObjectTypeInfo* t = ObjectTypes::find(o.type))
                for (const std::string& a : t->assets) add(a);
            if (o.type == "solid") {
                const std::string m = o.propStr("material", "ruin_stone");
                add("textures/tiles/" + m + ".json");
                add("textures/tiles/" + m + ".png");
            } else if (o.type == "crumble_platform") {
                const std::string st = o.propStr("style", "ruins");
                add(st == "ruins" ? std::string("sprites/objects/crumble_platform.png") : "sprites/objects/crumble_platform_" + st + ".png");
            } else if (o.type == "liquid") {
                add("sprites/objects/liquid_" + o.propStr("kind", "acid") + ".png");
            } else if (o.type == "enemy") {
                ++placed[o.propStr("enemy")];
            } else if (o.type == "power_unlock") {
                power(o.propStr("power"));
            } else if (o.type == "combat_zone") {
                add(o.propStr("music"));
            } else if (o.type == "decor" || o.type == "ladder") {
                auto it = decor.find(o.type == "ladder" ? std::string("hanging_vine_long") : o.propStr("sprite"));
                if (it != decor.end()) add(it->second);
            } else if (o.type == "emitter") {
                add(effectTexture(o.propStr("effect")));
            }
        }
        for (const auto& [id, n] : placed) {
            for (const auto& [file, one] : enemySet(id)) add(file, n);
            // Its world, and the worlds of what it brings.
            std::set<std::string> seen;
            Counts dummy;
            gather(id, dummy, seen);
            for (const std::string& met : seen)
                if (natives.count(met) && world.rank < enemyWorld[met].rank) enemyWorld[met] = world;
        }
        auto steps = [&](const Json& p) {
            for (const char* k : {"sound", "music"})
                if (p.contains(k) && p[k].is_string()) add(p[k].get<std::string>());
            if (p.contains("power") && p["power"].is_string()) power(p["power"].get<std::string>());
        };
        for (const auto& ev : level->events)
            for (const auto& a : ev.actions) steps(a.params);
        for (const auto& s : level->intro.steps) steps(s.params);
        for (const auto& s : level->outro.steps) steps(s.params);
        for (const auto& [file, n] : c) uses.add(file, AssetUseKind::Level, e->id, e->name, false, n);
    }

    // Stories of the giga bosses.
    BossStories stories;
    stories.load(dataDir / "stories");
    for (const auto& [bossId, s] : stories.all()) {
        auto d = natives.find(bossId);
        const std::string name = d != natives.end() ? d->second->name : bossId;
        for (const StoryShot& shot : s.shots) {
            uses.add(shot.image, AssetUseKind::Story, bossId, name, d == natives.end());
            // 2.62: a shot played as an animated scene: its layers' images and its sprites' sheets.
            if (shot.stage)
                for (const StoryElement& el : shot.stage->elements)
                    if (el.kind == StoryElementKind::Layer || el.kind == StoryElementKind::Sprite)
                        uses.add(el.source, AssetUseKind::Story, bossId, name, d == natives.end());
        }
    }

    // The suits of the Vestiaire (the standard one is Nova's own sheet).
    CosmeticCatalog cosmetics;
    if (cosmetics.load(dataDir / "cosmetics" / "cosmetics.json"))
        for (const CosmeticItem& it : cosmetics.items()) {
            if (it.category != "suit" || it.look.empty() || it.serverLook()) continue;
            uses.add(it.look == "standard" ? std::string("sprites/player/nova.png") : "sprites/player/suits/nova_" + it.look + ".png",
                     AssetUseKind::Cosmetic, it.id, it.name, false);
        }

    // The jukebox, and its groups (the folders of the music).
    std::map<std::string, std::pair<int, std::string>> trackGroup;  // file → (group rank, group name)
    if (auto j = FileUtil::loadJson(dataDir / "music" / "tracks.json")) {
        std::map<std::string, std::pair<int, std::string>> groups;
        int rank = 0;
        for (const Json& g : jfield(*j, "groups", Json::array()))
            groups[jstr(g, "id")] = {rank++, jstr(g, "name", jstr(g, "id"))};
        for (const Json& t : jfield(*j, "tracks", Json::array())) {
            const std::string file = Paths::normalizeSlashes(jstr(t, "file"));
            if (file.empty()) continue;
            uses.add(file, AssetUseKind::Jukebox, file, jstr(t, "title", file), false);
            auto g = groups.find(jstr(t, "group"));
            if (g != groups.end()) trackGroup[file] = g->second;
        }
    }

    // ------------------------------------------------------------------ the files
    std::error_code ec;
    for (fs::recursive_directory_iterator it(assetsDir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        AssetEntry a;
        a.path = Paths::normalizeSlashes(Paths::toUtf8(fs::relative(it->path(), assetsDir, ec)));
        if (ec || a.path.empty()) {
            ec.clear();
            continue;
        }
        a.name = Paths::toUtf8(it->path().filename());
        a.bytes = it->file_size(ec);
        if (ec) a.bytes = 0, ec.clear();
        const std::string ext = FileUtil::extensionLower(it->path());
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") a.type = AssetType::Image;
        else if (ext == ".json") a.type = a.path.rfind("animations/", 0) == 0 ? AssetType::Animation : AssetType::Data;
        else if (ext == ".ogg" || ext == ".wav" || ext == ".mp3" || ext == ".flac")
            a.type = a.path.rfind("music/", 0) == 0 ? AssetType::Music : AssetType::Sound;
        else if (ext == ".ttf" || ext == ".otf") a.type = AssetType::Font;
        else a.type = AssetType::Data;
        cat.assets_.push_back(std::move(a));
    }

    // Headers, animations and the files they describe.
    std::vector<std::pair<std::string, std::string>> describedBy;  // a texture, a JSON describing it
    for (AssetEntry& a : cat.assets_) {
        const fs::path file = assetsDir / Paths::fromUtf8(a.path);
        if (a.type == AssetType::Image) {
            if (auto s = readPngSize(file)) a.width = s->first, a.height = s->second;
        } else if (a.type == AssetType::Sound || a.type == AssetType::Music) {
            if (auto h = readAudioHeader(file)) a.durationMs = h->durationMs, a.sampleRate = h->sampleRate, a.channels = h->channels;
        } else if (a.type == AssetType::Animation || (a.type == AssetType::Data && FileUtil::extensionLower(file) == ".json")) {
            auto j = FileUtil::loadJson(file);
            if (!j || !j->is_object()) continue;
            const std::string tex = Paths::normalizeSlashes(jstr(*j, "texture"));
            if (!tex.empty()) {
                describedBy.emplace_back(tex, a.path);
                uses.add(tex, AssetUseKind::Descriptor, a.path, a.path, true);
            }
            if (a.type != AssetType::Animation) continue;
            auto set = AnimationSet::fromJson(*j, a.path);
            a.sheet = set->texturePath;
            a.width = set->frameWidth;
            a.height = set->frameHeight;
            for (const auto& [name, clip] : set->clips) {
                a.clips.push_back({name, clip.frameCount(), clip.totalDuration(), clip.loop});
                a.frames += clip.frameCount();
            }
            std::map<std::string, std::pair<int, int>> order;
            for (const auto& [name, clip] : set->clips) order[name] = {clip.row, clip.start};
            std::sort(a.clips.begin(), a.clips.end(), [&](const AssetClipInfo& x, const AssetClipInfo& y) {
                if (order[x.name] != order[y.name]) return order[x.name] < order[y.name];
                return x.name < y.name;
            });
        }
    }
    // A described file inherits the users of its description (a level using golem.json uses
    // golem.png too), with the same count.
    for (const auto& [tex, json] : describedBy)
        if (std::vector<AssetUse>* from = uses.find(json)) {
            const std::vector<AssetUse> copy = *from;
            for (const AssetUse& u : copy)
                if (u.kind != AssetUseKind::Descriptor) uses.inherit(tex, u);
        }
    // The terrain renderer's materials: loaded with every level (TerrainRenderer::loadTilesets;
    // their file names are built from bare names, no literal for code_refs.json to find).
    for (AssetEntry& a : cat.assets_)
        if (a.path.rfind("textures/tiles/", 0) == 0 && a.path.size() > 20 && a.path.compare(a.path.size() - 5, 5, ".json") == 0) {
            const std::string m = a.path.substr(15, a.path.size() - 20);
            if (TerrainRenderer::supportsMaterial(m)) {
                uses.add(a.path, AssetUseKind::Code, "game/levels/TerrainRenderer.cpp", "game/levels/TerrainRenderer.cpp", true);
                uses.inherit("textures/tiles/" + m + ".png", {AssetUseKind::Code, "game/levels/TerrainRenderer.cpp", "game/levels/TerrainRenderer.cpp", true, 1});
            }
        }
    for (AssetEntry& a : cat.assets_)
        if (std::vector<AssetUse>* u = uses.find(a.path)) {
            a.uses = std::move(*u);
            std::stable_sort(a.uses.begin(), a.uses.end(), [](const AssetUse& x, const AssetUse& y) {
                if (x.kind != y.kind) return x.kind < y.kind;
                if (x.count != y.count) return x.count > y.count;
                return naturalLess(x.name, y.name);
            });
        }

    // ------------------------------------------------------------------ the tree
    struct Seg {
        std::string key, name;
        int rank;
    };
    auto stemOf = [](const std::string& path) {
        const size_t slash = path.rfind('/');
        std::string s = slash == std::string::npos ? path : path.substr(slash + 1);
        const size_t dot = s.rfind('.');
        return dot == std::string::npos ? s : s.substr(0, dot);
    };
    auto planetSeg = [&](const std::string& top, const std::string& id) -> Seg {
        auto r = planetRank.find(id);
        auto n = planetName.find(id);
        if (r == planetRank.end()) return {top + "/" + (id.empty() ? std::string("outside") : id), id.empty() ? "Hors campagne" : id, 1000};
        return {top + "/" + id, n != planetName.end() ? n->second : id, r->second.rank};
    };
    // The world of an enemy or boss file: the first world of its users.
    auto worldOf = [&](const AssetEntry& a) {
        World w;
        for (const AssetUse& u : a.uses)
            if (u.kind == AssetUseKind::Enemy || u.kind == AssetUseKind::Boss) {
                auto it = enemyWorld.find(u.id);
                if (it != enemyWorld.end() && it->second.rank < w.rank) w = it->second;
            }
        return w;
    };
    std::map<std::string, std::vector<Seg>> placeOf;  // asset path → its folder (segments)
    auto creatureFolder = [&](const AssetEntry& a) -> std::vector<Seg> {
        bool boss = false;
        const EnemyDefinition* giga = nullptr;
        for (const AssetUse& u : a.uses) {
            if (u.kind != AssetUseKind::Boss) continue;
            boss = true;
            auto d = natives.find(u.id);
            if (d != natives.end() && isGigaBoss(*d->second) && !giga) giga = d->second;
        }
        const World w = worldOf(a);
        if (!boss) return {{"enemies", "Ennemis", 1}, planetSeg("enemies", w.id)};
        std::vector<Seg> v = {{"bosses", "Boss et giga-boss", 2}, planetSeg("bosses", w.id)};
        if (giga) v.push_back({v.back().key + "/" + giga->id, giga->name, 0});
        return v;
    };
    auto soundFolder = [&](const AssetEntry& a) -> Seg {
        bool power = false, enemy = false, boss = false;
        for (const AssetUse& u : a.uses) {
            power |= u.kind == AssetUseKind::Power;
            enemy |= u.kind == AssetUseKind::Enemy;
            boss |= u.kind == AssetUseKind::Boss;
        }
        if (stemOf(a.path).rfind("ui_", 0) == 0) return {"sounds/interface", "Interface", 0};
        if (power) return {"sounds/powers", "Pouvoirs", 1};
        if (enemy) return {"sounds/enemies", "Ennemis", 2};
        if (boss) return {"sounds/bosses", "Boss", 3};
        if (!a.uses.empty()) return {"sounds/game", "Nova et niveaux", 4};
        return {"sounds/other", "Autres sons", 5};
    };
    auto folderOf = [&](const AssetEntry& a) -> std::vector<Seg> {
        const std::string& p = a.path;
        const std::string stem = stemOf(p);
        auto under = [&](const char* prefix) { return p.rfind(prefix, 0) == 0; };
        if (under("sounds/")) return {{"sounds", "Sons", 9}, soundFolder(a)};
        // The pictures of the powers: their effects, their tablet, their icons.
        if (stem.rfind("power_", 0) == 0 || stem == "icons_powers") return {{"powers", "Pouvoirs", 3}};
        if (under("sprites/player/suits/")) return {{"characters", "Personnages", 0}, {"characters/suits", "Combinaisons", 1}};
        if (under("sprites/player/")) return {{"characters", "Personnages", 0}, {"characters/nova", "Nova", 0}};
        if (under("sprites/enemies/")) return creatureFolder(a);
        if (under("sprites/objects/")) return {{"scenery", "Décors et tuiles", 4}, {"scenery/objects", "Objets des niveaux", 2001}};
        if (under("sprites/decor/")) {
            const size_t us = stem.find('_');
            return {{"scenery", "Décors et tuiles", 4}, planetSeg("scenery", stem.substr(0, us))};
        }
        if (under("backgrounds/")) {
            const size_t slash = p.find('/', 12);
            return {{"scenery", "Décors et tuiles", 4}, planetSeg("scenery", slash == std::string::npos ? std::string() : p.substr(12, slash - 12))};
        }
        if (under("textures/tiles/")) return {{"scenery", "Décors et tuiles", 4}, {"scenery/tiles", "Tuiles et matériaux", 2000}};
        if (under("textures/fx/")) return {{"effects", "Effets et particules", 6}, {"effects/particles", "Particules", 0}};
        if (under("sprites/projectiles/")) return {{"effects", "Effets et particules", 6}, {"effects/attacks", "Projectiles et attaques", 1}};
        if (under("ui/planets/")) return {{"interface", "Interface et icônes", 5}, {"interface/planets", "Planètes", 1}};
        if (under("ui/glyphs/")) return {{"interface", "Interface et icônes", 5}, {"interface/glyphs", "Boutons des manettes", 2}};
        if (under("ui/")) return {{"interface", "Interface et icônes", 5}, {"interface/icons", "Icônes et images", 0}};
        if (under("online/")) return {{"interface", "Interface et icônes", 5}, {"interface/online", "En ligne", 3}};
        if (under("stories/")) {
            // stories/<boss>_<n>.png (the paintings), stories/<boss>/<piece>.png (2.62: the pieces of its scenes).
            std::string boss = stem;
            const size_t us = boss.rfind('_');
            if (us != std::string::npos && us + 1 < boss.size() && std::isdigit(static_cast<unsigned char>(boss[us + 1]))) boss.resize(us);
            if (const size_t sub = p.find('/', 8); sub != std::string::npos) boss = p.substr(8, sub - 8);
            auto d = natives.find(boss);
            return {{"stories", "Histoires", 7}, {"stories/" + boss, d != natives.end() ? d->second->name : boss, 0}};
        }
        if (under("music/")) {
            auto g = trackGroup.find(p);
            if (g == trackGroup.end()) return {{"music", "Musiques", 8}, {"music/other", "Hors juke-box", 1000}};
            return {{"music", "Musiques", 8}, {"music/" + foldForSearch(g->second.second), g->second.second, g->second.first}};
        }
        if (under("fonts/")) return {{"fonts", "Polices", 10}};
        return {{"other", "Autres", 11}};
    };
    for (const AssetEntry& a : cat.assets_)
        if (a.type != AssetType::Animation) placeOf[a.path] = folderOf(a);
    // An animation goes with its sheet.
    for (const AssetEntry& a : cat.assets_) {
        if (a.type != AssetType::Animation) continue;
        auto s = placeOf.find(a.sheet);
        placeOf[a.path] = s != placeOf.end() ? s->second : folderOf(a);
    }

    // Folders (rank and name order among siblings), then the assets in the order of the tree.
    struct Node {
        Seg seg;
        int parent;
        std::vector<int> children;
        std::vector<size_t> files;
    };
    std::vector<Node> nodes = {{{"", "Tous les assets", 0}, -1, {}, {}}};
    std::unordered_map<std::string, int> byKey;
    for (size_t i = 0; i < cat.assets_.size(); ++i) {
        int at = 0;
        for (const Seg& s : placeOf[cat.assets_[i].path]) {
            auto it = byKey.find(s.key);
            if (it == byKey.end()) {
                nodes.push_back({s, at, {}, {}});
                const int id = static_cast<int>(nodes.size()) - 1;
                nodes[static_cast<size_t>(at)].children.push_back(id);
                it = byKey.emplace(s.key, id).first;
            }
            at = it->second;
        }
        nodes[static_cast<size_t>(at)].files.push_back(i);
    }
    std::vector<AssetEntry> ordered;
    ordered.reserve(cat.assets_.size());
    std::function<int(int, int, int)> emit = [&](int n, int parent, int depth) {
        Node& node = nodes[static_cast<size_t>(n)];
        AssetFolder f;
        f.name = node.seg.name;
        f.key = node.seg.key;
        f.parent = parent;
        f.depth = depth;
        const int id = static_cast<int>(cat.folders_.size());
        cat.folders_.push_back(f);
        std::sort(node.files.begin(), node.files.end(), [&](size_t x, size_t y) { return naturalLess(cat.assets_[x].name, cat.assets_[y].name); });
        cat.folders_[static_cast<size_t>(id)].begin = static_cast<int>(ordered.size());
        for (size_t i : node.files) {
            ordered.push_back(std::move(cat.assets_[i]));
            ordered.back().folder = id;
        }
        cat.folders_[static_cast<size_t>(id)].own = static_cast<int>(node.files.size());
        std::vector<int> kids = node.children;
        std::sort(kids.begin(), kids.end(), [&](int x, int y) {
            const Seg& a = nodes[static_cast<size_t>(x)].seg;
            const Seg& b = nodes[static_cast<size_t>(y)].seg;
            if (a.rank != b.rank) return a.rank < b.rank;
            return naturalLess(a.name, b.name);
        });
        for (int k : kids) {
            const int child = emit(k, id, depth + 1);
            cat.folders_[static_cast<size_t>(id)].children.push_back(child);
        }
        cat.folders_[static_cast<size_t>(id)].end = static_cast<int>(ordered.size());
        return id;
    };
    emit(0, -1, 0);
    cat.assets_ = std::move(ordered);
    for (AssetFolder& f : cat.folders_)
        for (int i = f.begin; i < f.end; ++i) f.bytes += cat.assets_[static_cast<size_t>(i)].bytes;

    // ------------------------------------------------------------------ index and search
    const bool french = i18n::language() == Language::French;
    auto both = [&](std::string& s, const std::string& fr) {
        s += ' ';
        s += foldForSearch(fr);
        if (!french) {
            s += ' ';
            s += foldForSearch(i18n::tr(fr));
        }
    };
    for (size_t i = 0; i < cat.assets_.size(); ++i) {
        AssetEntry& a = cat.assets_[i];
        cat.index_[a.path] = static_cast<int>(i);
        if (a.unused()) ++cat.unused_;
        std::string s = foldForSearch(a.path);
        both(s, assetTypeLabel(a.type));
        for (int f = a.folder; f > 0; f = cat.folders_[static_cast<size_t>(f)].parent) both(s, cat.folders_[static_cast<size_t>(f)].name);
        if (a.unused()) both(s, "Inutilisé");
        std::set<AssetUseKind> kinds;
        for (const AssetUse& u : a.uses) {
            if (kinds.insert(u.kind).second) both(s, assetUseKindLabel(u.kind));
            s += ' ';
            s += foldForSearch(u.id);
            if (u.literal) s += ' ' + foldForSearch(u.name);
            else both(s, u.name);
        }
        a.search = std::move(s);
    }
    return cat;
}

// ============================================================================ queries

int AssetCatalog::indexOf(std::string_view path) const {
    auto it = index_.find(std::string(path));
    return it == index_.end() ? -1 : it->second;
}

const AssetEntry* AssetCatalog::find(std::string_view path) const {
    const int i = indexOf(path);
    return i < 0 ? nullptr : &assets_[static_cast<size_t>(i)];
}

std::vector<int> AssetCatalog::filter(const AssetQuery& q) const {
    std::vector<int> out;
    if (folders_.empty()) return out;
    const AssetFolder& f = folders_[static_cast<size_t>(std::clamp(q.folder, 0, static_cast<int>(folders_.size()) - 1))];
    std::vector<std::string> words;
    {
        const std::string t = foldForSearch(q.text);
        size_t i = 0;
        while (i < t.size()) {
            while (i < t.size() && t[i] == ' ') ++i;
            size_t j = i;
            while (j < t.size() && t[j] != ' ') ++j;
            if (j > i) words.push_back(t.substr(i, j - i));
            i = j;
        }
    }
    out.reserve(static_cast<size_t>(f.count()));
    for (int i = f.begin; i < f.end; ++i) {
        const AssetEntry& a = assets_[static_cast<size_t>(i)];
        if (!(q.types & assetTypeBit(a.type))) continue;
        if (q.unusedOnly && !a.unused()) continue;
        if (q.userKind && !a.usedBy(*q.userKind, q.userId)) continue;
        bool all = true;
        for (const std::string& w : words)
            if (a.search.find(w) == std::string::npos) {
                all = false;
                break;
            }
        if (all) out.push_back(i);
    }
    return out;
}

std::string AssetCatalog::folderTrail(int folder) const {
    std::vector<const std::string*> names;
    for (int f = folder; f > 0 && f < static_cast<int>(folders_.size()); f = folders_[static_cast<size_t>(f)].parent)
        names.push_back(&folders_[static_cast<size_t>(f)].name);
    std::string s;
    for (auto it = names.rbegin(); it != names.rend(); ++it) s += (s.empty() ? "" : " › ") + **it;
    return s;
}

const AssetUse* AssetCatalog::userOf(AssetUseKind kind, std::string_view id) const {
    for (const AssetEntry& a : assets_)
        for (const AssetUse& u : a.uses)
            if (u.kind == kind && u.id == id) return &u;
    return nullptr;
}

std::shared_ptr<const AssetCatalog> sharedAssetCatalog(const ContentDatabase& content) {
    using Result = std::shared_ptr<const AssetCatalog>;
    static std::mutex mutex;
    static std::shared_future<Result> job;
    std::lock_guard<std::mutex> lock(mutex);
    if (!job.valid()) {
        // The worker reads its own copy: the content may be reloaded meanwhile, the screen
        // that asked may be gone.
        auto copy = std::make_shared<const ContentDatabase>(content);
        job = std::async(std::launch::async, [copy] {
                  return Result(std::make_shared<const AssetCatalog>(AssetCatalog::build(Paths::assetsDir(), Paths::dataDir(), *copy)));
              }).share();
    }
    if (job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    return job.get();
}

}  // namespace cosmic
