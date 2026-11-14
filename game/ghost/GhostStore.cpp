#include "game/ghost/GhostStore.h"

#include <algorithm>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"

namespace cosmic {

namespace {

fs::path sidecar(const fs::path& replayFile) {
    fs::path p = replayFile;
    p.replace_extension(".json");
    return p;
}

uint64_t fileSize(const fs::path& p) {
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(n);
}

/// A description built from the replay itself (a file without its ".json").
GhostMeta metaFromReplay(const ReplayData& r, const std::string& name) {
    GhostMeta m;
    m.source = "file";
    m.label = name.substr(0, 48);
    m.levelId = r.levelId.substr(0, 128);
    m.levelHash = r.levelHash.substr(0, 64);
    m.timeMs = r.durationMs;
    m.gameVersion = r.gameVersion.substr(0, 24);
    return m;
}

constexpr size_t kMaxListed = 200;  // files read per folder (a folder full of junk stays cheap)

}  // namespace

void GhostStore::open(fs::path root) {
    root_ = std::move(root);
    loadFavorites();
}

std::string GhostStore::safeName(const std::string& s) {
    std::string out;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += c;
        if (out.size() >= 64) break;
    }
    return out;
}

std::string GhostStore::levelKey(const std::string& levelId, const std::string& levelHash) {
    std::string slug = FileUtil::slugify(levelId);
    if (slug.size() > 64) slug.resize(64);
    std::string h = safeName(levelHash);
    if (h.size() > 12) h.resize(12);
    return (slug.empty() ? std::string("niveau") : slug) + "-" + (h.empty() ? std::string("local") : h);
}

std::optional<ReplayData> GhostStore::loadReplay(const fs::path& file, std::string* error) {
    uint64_t n = fileSize(file);
    if (n == 0 || n > kMaxReplayBytes) {
        if (error) *error = n == 0 ? "Fichier vide ou introuvable." : "Fichier trop volumineux.";
        return std::nullopt;
    }
    auto bytes = FileUtil::readBinary(file);
    if (!bytes) {
        if (error) *error = "Lecture impossible.";
        return std::nullopt;
    }
    std::string why;
    auto r = ReplayData::decode(*bytes, &why);
    if (!r) {
        if (error) *error = "Replay illisible (" + why + ").";
        return std::nullopt;
    }
    return r;
}

std::optional<GhostEntry> GhostStore::readEntry(const fs::path& replayFile) {
    std::error_code ec;
    if (!fs::is_regular_file(replayFile, ec)) return std::nullopt;
    GhostEntry e;
    e.file = replayFile;
    e.bytes = fileSize(replayFile);
    if (e.bytes == 0 || e.bytes > kMaxReplayBytes) return std::nullopt;
    fs::path side = sidecar(replayFile);
    if (fs::exists(side, ec) && fileSize(side) < 64 * 1024) {
        if (auto j = FileUtil::loadJson(side); j && jstr(*j, "format") == "cosmic.ghost") {
            e.meta = GhostMeta::fromJson(*j);
            return e;
        }
    }
    // No description: read it from the replay.
    auto r = loadReplay(replayFile);
    if (!r) return std::nullopt;
    e.meta = metaFromReplay(*r, Paths::toUtf8(replayFile.stem()));
    return e;
}

bool GhostStore::writeEntry(const fs::path& replayFile, const std::vector<uint8_t>& bytes, const GhostMeta& meta) {
    std::error_code ec;
    fs::create_directories(replayFile.parent_path(), ec);
    return FileUtil::writeBinary(replayFile, bytes) && FileUtil::writeTextAtomic(sidecar(replayFile), meta.toJson().dump(1));
}

bool GhostStore::recordRun(const ReplayData& replay, GhostMeta meta, bool newBest) {
    if (replay.frames.empty()) return false;
    fs::path dir = root_ / "local" / levelKey(meta.levelId, meta.levelHash);
    std::vector<uint8_t> bytes = replay.encode();
    if (bytes.empty() || bytes.size() > kMaxReplayBytes) return false;
    bool ok = true;
    if (newBest) {
        // The best run so far becomes the previous record.
        std::error_code ec;
        if (fs::exists(dir / "pb.cprz", ec)) {
            fs::rename(dir / "pb.cprz", dir / "prev.cprz", ec);
            if (!ec) {
                if (auto j = FileUtil::loadJson(dir / "pb.json")) {
                    GhostMeta prev = GhostMeta::fromJson(*j);
                    prev.source = "prev";
                    FileUtil::writeTextAtomic(dir / "prev.json", prev.toJson().dump(1));
                }
            }
        }
        GhostMeta pb = meta;
        pb.source = "pb";
        ok = writeEntry(dir / "pb.cprz", bytes, pb);
    }
    meta.source = "last";
    ok = writeEntry(dir / "last.cprz", bytes, meta) && ok;
    if (!ok) Log::warn("Fantôme non enregistré : {}", Paths::toUtf8(dir));
    return ok;
}

std::vector<GhostEntry> GhostStore::localGhosts(const std::string& levelId) const {
    std::vector<GhostEntry> out;
    std::error_code ec;
    fs::path base = root_ / "local";
    if (!fs::is_directory(base, ec)) return out;
    std::string prefix = levelKey(levelId, "");
    prefix.resize(prefix.size() - std::string("local").size());  // "<slug>-": every version
    size_t seen = 0;
    for (const auto& d : fs::directory_iterator(base, ec)) {
        if (++seen > kMaxListed * 4) break;
        if (!d.is_directory(ec)) continue;
        std::string name = Paths::toUtf8(d.path().filename());
        if (name.rfind(prefix, 0) != 0) continue;
        for (const char* which : {"pb", "prev", "last"}) {
            auto e = readEntry(d.path() / (std::string(which) + ".cprz"));
            if (!e || e->meta.levelId != levelId) continue;
            e->meta.source = which;
            out.push_back(std::move(*e));
        }
    }
    return out;
}

bool GhostStore::storeDownload(const std::vector<uint8_t>& bytes, GhostMeta meta, std::string* error, bool sameVersionOnly) {
    std::string key = safeName(meta.recordId);
    if (key.empty()) {
        if (error) *error = "Record inconnu.";
        return false;
    }
    if (bytes.empty() || bytes.size() > kMaxReplayBytes) {
        if (error) *error = "Replay vide ou trop volumineux.";
        return false;
    }
    std::string why;
    auto r = ReplayData::decode(bytes, &why);
    if (!r) {
        if (error) *error = "Replay illisible (" + why + ").";
        return false;
    }
    // What the description says about the run must be what the replay contains.
    if (sameVersionOnly && !meta.levelHash.empty() && r->levelHash != meta.levelHash) {
        if (error) *error = "Le replay ne correspond pas à ce record.";
        return false;
    }
    meta.levelHash = r->levelHash;
    if (meta.levelId.empty()) meta.levelId = r->levelId;
    if (!writeEntry(root_ / "cache" / (key + ".cprz"), bytes, meta)) {
        if (error) *error = "Écriture impossible.";
        return false;
    }
    return true;
}

std::optional<GhostEntry> GhostStore::cached(const std::string& recordId) const {
    std::string key = safeName(recordId);
    if (key.empty()) return std::nullopt;
    auto e = readEntry(root_ / "cache" / (key + ".cprz"));
    if (e && e->meta.recordId != recordId) return std::nullopt;
    return e;
}

std::vector<GhostEntry> GhostStore::cachedFor(const std::string& levelId) const {
    std::vector<GhostEntry> out;
    std::error_code ec;
    fs::path dir = root_ / "cache";
    if (!fs::is_directory(dir, ec)) return out;
    size_t seen = 0;
    for (const auto& f : fs::directory_iterator(dir, ec)) {
        if (f.path().extension() != ".cprz") continue;
        if (++seen > kMaxListed) break;
        // Only the description is read here (the replay is decoded when chosen).
        fs::path side = sidecar(f.path());
        if (fileSize(side) == 0 || fileSize(side) > 64 * 1024) continue;
        auto j = FileUtil::loadJson(side);
        if (!j || jstr(*j, "format") != "cosmic.ghost") continue;
        GhostEntry e;
        e.meta = GhostMeta::fromJson(*j);
        if (e.meta.levelId != levelId) continue;
        e.file = f.path();
        e.bytes = fileSize(f.path());
        out.push_back(std::move(e));
    }
    return out;
}

uint64_t GhostStore::cacheBytes() const {
    uint64_t total = 0;
    std::error_code ec;
    fs::path dir = root_ / "cache";
    if (!fs::is_directory(dir, ec)) return 0;
    for (const auto& f : fs::directory_iterator(dir, ec)) total += fileSize(f.path());
    return total;
}

void GhostStore::trimCache(uint64_t maxBytes) {
    std::error_code ec;
    fs::path dir = root_ / "cache";
    if (!fs::is_directory(dir, ec)) return;
    struct Item {
        fs::path file;
        fs::file_time_type time;
        uint64_t bytes;
    };
    std::vector<Item> items;
    uint64_t total = 0;
    for (const auto& f : fs::directory_iterator(dir, ec)) {
        if (f.path().extension() != ".cprz") continue;
        uint64_t n = fileSize(f.path()) + fileSize(sidecar(f.path()));
        total += n;
        std::string id;
        if (auto j = FileUtil::loadJson(sidecar(f.path()))) id = jstr(*j, "record_id");
        if (!id.empty() && isFavorite(id)) continue;  // kept
        items.push_back({f.path(), f.last_write_time(ec), n});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.time < b.time; });
    for (const Item& it : items) {
        if (total <= maxBytes) break;
        fs::remove(it.file, ec);
        fs::remove(sidecar(it.file), ec);
        total -= std::min(total, it.bytes);
    }
}

std::optional<GhostEntry> GhostStore::importFile(const fs::path& source, std::string* error) {
    auto r = loadReplay(source, error);
    if (!r) return std::nullopt;
    if (r->frames.size() < 2) {
        if (error) *error = "Ce replay ne contient pas de trajectoire.";
        return std::nullopt;
    }
    std::string name = safeName(Paths::toUtf8(source.stem()));
    if (name.empty()) name = "replay";
    std::error_code ec;
    fs::path dir = root_ / "files";
    fs::path dst = dir / (name + ".cprz");
    for (int i = 2; fs::exists(dst, ec) && i < 1000; ++i) dst = dir / (name + "_" + std::to_string(i) + ".cprz");
    auto bytes = FileUtil::readBinary(source);
    if (!bytes || !writeEntry(dst, *bytes, metaFromReplay(*r, name))) {
        if (error) *error = "Copie impossible.";
        return std::nullopt;
    }
    return readEntry(dst);
}

std::vector<GhostEntry> GhostStore::files() const {
    std::vector<GhostEntry> out;
    std::error_code ec;
    fs::path dir = root_ / "files";
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& f : fs::directory_iterator(dir, ec)) {
        if (FileUtil::extensionLower(f.path()) != ".cprz") continue;
        if (out.size() >= kMaxListed) break;
        if (auto e = readEntry(f.path())) {
            e->meta.source = "file";
            out.push_back(std::move(*e));
        }
    }
    std::sort(out.begin(), out.end(), [](const GhostEntry& a, const GhostEntry& b) { return a.meta.label < b.meta.label; });
    return out;
}

bool GhostStore::isFavorite(const std::string& recordId) const {
    return std::find(favorites_.begin(), favorites_.end(), recordId) != favorites_.end();
}

void GhostStore::setFavorite(const std::string& recordId, bool on) {
    if (safeName(recordId).empty() || safeName(recordId) != recordId) return;
    auto it = std::find(favorites_.begin(), favorites_.end(), recordId);
    if (on && it == favorites_.end()) favorites_.push_back(recordId);
    if (!on && it != favorites_.end()) favorites_.erase(it);
    saveFavorites();
}

void GhostStore::loadFavorites() {
    favorites_.clear();
    fs::path p = root_ / "favorites.json";
    if (fileSize(p) == 0 || fileSize(p) > 256 * 1024) return;
    auto j = FileUtil::loadJson(p);
    if (!j || !j->contains("records") || !(*j)["records"].is_array()) return;
    for (const Json& v : (*j)["records"]) {
        if (!v.is_string() || favorites_.size() >= 500) continue;
        std::string id = v.get<std::string>();
        if (!id.empty() && safeName(id) == id) favorites_.push_back(id);
    }
}

void GhostStore::saveFavorites() const {
    std::error_code ec;
    fs::create_directories(root_, ec);
    FileUtil::writeTextAtomic(root_ / "favorites.json", Json{{"format", "cosmic.ghost_favorites"}, {"records", favorites_}}.dump(1));
}

}  // namespace cosmic
