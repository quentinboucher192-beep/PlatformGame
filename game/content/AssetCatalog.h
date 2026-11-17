#pragma once
// Asset catalogue (2.63, ADMINISTRATION › Assets): every file installed under assets/, sorted in
// a logical tree, with what can be known of it without decoding it and above all who uses it.
// Read only: nothing here writes to assets/ or data/.
//
// ── What is read ─────────────────────────────────────────────────────────────────────────────
//  · the whole of assets/ (size of each file); the size of the PNGs from their header (IHDR),
//    the frames and clips of the animations (assets/animations/*.json, parsed as the game
//    parses them), the length of sounds and music (WAV header, last Ogg page);
//  · who uses them: the native enemies and bosses (data/enemies: sheet, phases, sounds,
//    attacks), the game's levels (data/levels: music, backgrounds, objects, decor, materials,
//    enemies placed, tablets, effects, events — counted object by object), the planets, the
//    powers, the stories of the giga bosses, the suits of the Vestiaire, the particle effects,
//    the jukebox (data/music/tracks.json), what every run loads
//    (DependencyCollector::coreRuntimeAssets) and the files named by the game's code
//    (data/assets/code_refs.json, written by tools/assets/code_refs.py). A sheet or a texture
//    described by a JSON (animation, decor atlas, tiles) inherits the users of that JSON. An
//    asset none of these names is « unused ».
//
// ── API (also for the Studio de Créations, which uses it as its library of models) ───────────
//    // Each frame of a screen: nullptr while the catalogue is read (on a worker thread, never
//    // on the main thread), then always the same catalogue.
//    std::shared_ptr<const AssetCatalog> cat = sharedAssetCatalog(g.content);
//    // List: cat->assets() (in the order of the tree), cat->folders() (0 = the root).
//    // Filter: text (words, accents and case ignored), types, unused, folder, user.
//    AssetQuery q;
//    q.text = "golem";
//    q.types = assetTypeBit(AssetType::Image) | assetTypeBit(AssetType::Animation);
//    std::vector<int> hits = cat->filter(q);          // indices into cat->assets()
//    const AssetEntry& e = cat->assets()[hits[0]];    // path, type, size, uses…
//    fs::path file = cat->absolutePath(e);            // the file on disk
//  Thumbnails and previews (textures, sounds): ui/widgets/AssetPreview.h (AssetImageLoader,
//  AssetPreview), built on this catalogue.
//
// A catalogue once built is immutable: it is shared between threads without a lock.

#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/core/Paths.h"

namespace cosmic {

class ContentDatabase;

enum class AssetType : uint8_t { Image, Animation, Sound, Music, Font, Data, Count };
/// « Image », « Animation », « Son », « Musique », « Police », « Données » (French: translated when drawn).
const char* assetTypeLabel(AssetType t);
constexpr uint32_t assetTypeBit(AssetType t) { return 1u << static_cast<unsigned>(t); }
constexpr uint32_t kAllAssetTypes = (1u << static_cast<unsigned>(AssetType::Count)) - 1u;

/// Who uses an asset.
enum class AssetUseKind : uint8_t {
    Core,        // loaded by every run (player, effects, interface, fonts…)
    Code,        // named by the game's code (id: the source file)
    Cosmetic,    // an item of the Vestiaire (id: the item)
    Enemy,       // a native enemy (id: the enemy)
    Boss,        // a native boss (id: the boss)
    Level,       // a level of the game (id: the level)
    Planet,      // a planet (id: the planet)
    Power,       // a power (id: the power)
    Story,       // the story of a giga boss (id: the boss)
    Effect,      // a particle effect (id: the preset)
    Jukebox,     // a track of the jukebox (id: its file)
    Descriptor,  // a JSON describing it: animation, decor atlas, tiles (id: its path)
    Count
};
/// « Moteur du jeu », « Code du jeu », « Vestiaire », « Ennemi », « Boss », « Niveau »… (French).
const char* assetUseKindLabel(AssetUseKind k);

struct AssetUse {
    AssetUseKind kind = AssetUseKind::Code;
    std::string id;
    std::string name;      // as the game shows it (French, translated when drawn)
    bool literal = false;  // `name` is an id or a path: shown as it is
    int count = 1;         // references: objects of a level, phases, attacks…
};

struct AssetClipInfo {
    std::string name;
    int frames = 0;
    float seconds = 0.0f;  // one loop
    bool loop = true;
};

struct AssetEntry {
    std::string path;  // relative to assets/, forward slashes (« sprites/enemies/golem.png »)
    std::string name;  // file name (« golem.png »)
    AssetType type = AssetType::Data;
    int folder = 0;    // its folder (AssetCatalog::folders())
    uint64_t bytes = 0;
    int width = 0, height = 0;  // images: PNG header; animations: one frame of the sheet
    // Animations
    std::string sheet;          // its sheet (path under assets/)
    int frames = 0;             // frames of all its clips
    std::vector<AssetClipInfo> clips;
    // Sounds and music
    int64_t durationMs = 0;     // 0: unknown
    int sampleRate = 0, channels = 0;
    std::vector<AssetUse> uses;  // by kind, then the most numerous first
    std::string search;          // searched text (lowercase, accents removed)

    /// Nothing of the game uses it: no use but the JSON describing it (a sheet inherits the
    /// users of its animation, so a sheet whose animation is unused is unused too).
    bool unused() const;
    int useCount() const;
    bool usedBy(AssetUseKind kind, std::string_view id) const;
};

struct AssetFolder {
    std::string name;  // French (translated when drawn); a proper name (planet, boss) too
    std::string key;   // stable: « boss/confins/kraken »
    int parent = -1;
    int depth = 0;
    std::vector<int> children;
    int begin = 0, end = 0;  // its assets and those of its subfolders: [begin, end) of assets()
    int own = 0;             // assets filed directly here (the first ones of the range)
    uint64_t bytes = 0;      // weight of the range
    int count() const { return end - begin; }
};

struct AssetQuery {
    std::string text;                 // words, all required (name, path, folder, type, users)
    uint32_t types = kAllAssetTypes;  // assetTypeBit()
    bool unusedOnly = false;
    int folder = 0;                   // this folder and its subfolders (0: everything)
    /// Only what this « user » (an enemy, a level…) uses: its kind and id.
    std::optional<AssetUseKind> userKind;
    std::string userId;
};

class AssetCatalog {
public:
    /// Reads `assetsDir` and the native definitions of `content` (`dataDir`: code_refs.json,
    /// stories, Vestiaire, jukebox). Slow: never on the main thread.
    static AssetCatalog build(const fs::path& assetsDir, const fs::path& dataDir, const ContentDatabase& content);

    const std::vector<AssetEntry>& assets() const { return assets_; }
    const std::vector<AssetFolder>& folders() const { return folders_; }
    const fs::path& root() const { return root_; }
    fs::path absolutePath(const AssetEntry& e) const { return root_ / Paths::fromUtf8(e.path); }
    /// Index of `path` (relative to assets/), -1 when it is not catalogued.
    int indexOf(std::string_view path) const;
    const AssetEntry* find(std::string_view path) const;
    /// Indices of the assets kept, in the order of the tree.
    std::vector<int> filter(const AssetQuery& q) const;
    /// The path of a folder from the root (excluded): « Boss et giga-boss › Les Confins ».
    std::string folderTrail(int folder) const;
    int unusedCount() const { return unused_; }
    uint64_t totalBytes() const { return folders_.empty() ? 0 : folders_.front().bytes; }
    /// A « user » (enemy, level…) as it appears among the uses (nullptr: it uses nothing).
    const AssetUse* userOf(AssetUseKind kind, std::string_view id) const;

private:
    fs::path root_;
    std::vector<AssetEntry> assets_;
    std::vector<AssetFolder> folders_;
    std::unordered_map<std::string, int> index_;
    int unused_ = 0;
};

/// The shared catalogue: built once per session on a worker thread (from its own copy of the
/// content) at the first call; nullptr while it is not ready.
std::shared_ptr<const AssetCatalog> sharedAssetCatalog(const ContentDatabase& content);

// ── File headers (nothing decoded) ────────────────────────────────────────────────────────────
/// Width and height of a PNG (IHDR chunk), nullopt when it is not one.
std::optional<std::pair<int, int>> readPngSize(const fs::path& file);
struct AudioHeader {
    int64_t durationMs = 0;
    int sampleRate = 0;
    int channels = 0;
};
/// WAV (RIFF: fmt + data) or Ogg (Vorbis, Opus: the granule position of the last page).
std::optional<AudioHeader> readAudioHeader(const fs::path& file);

/// Searched text: lowercase, accented letters brought back to their base (« Écaille » → « ecaille »).
std::string foldForSearch(std::string_view s);
/// « Natural » order (kraken_p2 before kraken_p10), case ignored.
bool naturalLess(std::string_view a, std::string_view b);
/// « 12,4 Mo », « 640 Ko », « 512 o ».
std::string formatBytes(uint64_t bytes);
/// « 1:23 », « 0,8 s ».
std::string formatDuration(int64_t ms);

/// LRU cache: `get` moves the entry to the front; past `capacity`, the oldest one goes (its
/// resources are released by its destructor, on the calling thread).
template <typename K, typename V>
class LruCache {
public:
    explicit LruCache(size_t capacity) : capacity_(capacity < 1 ? 1 : capacity) {}
    V* get(const K& key) {
        auto it = map_.find(key);
        if (it == map_.end()) return nullptr;
        order_.splice(order_.begin(), order_, it->second);
        return &it->second->second;
    }
    bool contains(const K& key) const { return map_.count(key) > 0; }
    /// Adds or replaces; returns the number of entries evicted.
    size_t put(const K& key, V value) {
        if (auto it = map_.find(key); it != map_.end()) {
            it->second->second = std::move(value);
            order_.splice(order_.begin(), order_, it->second);
            return 0;
        }
        order_.emplace_front(key, std::move(value));
        map_[key] = order_.begin();
        return trim();
    }
    bool erase(const K& key) {
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        order_.erase(it->second);
        map_.erase(it);
        return true;
    }
    void setCapacity(size_t capacity) {
        capacity_ = capacity < 1 ? 1 : capacity;
        trim();
    }
    void clear() {
        map_.clear();
        order_.clear();
    }
    size_t size() const { return map_.size(); }
    size_t capacity() const { return capacity_; }
    /// The most recent / the oldest key (cache not empty).
    const K& newest() const { return order_.front().first; }
    const K& oldest() const { return order_.back().first; }

private:
    size_t trim() {
        size_t n = 0;
        while (map_.size() > capacity_) {
            map_.erase(order_.back().first);
            order_.pop_back();
            ++n;
        }
        return n;
    }
    size_t capacity_;
    std::list<std::pair<K, V>> order_;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> map_;
};

}  // namespace cosmic
