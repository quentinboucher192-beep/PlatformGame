#include "game/ghost/ReplayLibrary.h"

#include <algorithm>
#include <chrono>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/content/ContentDatabase.h"
#include "game/online/LevelFingerprint.h"

namespace cosmic {

namespace {

constexpr const char* kIndexFormat = "cosmic.downloaded_replays";
constexpr uint64_t kMaxIndexBytes = 2ull << 20;

/// `s` cut to at most `n` bytes, never inside a UTF-8 character.
std::string clip(std::string s, size_t n) {
    if (s.size() <= n) return s;
    size_t cut = n;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    s.resize(cut);
    return s;
}

bool safeChars(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

uint64_t fileSize(const fs::path& p) {
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(n);
}

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string megabytes(uint64_t bytes) { return std::to_string((bytes + (1u << 19)) >> 20); }

}  // namespace

// ------------------------------------------------------------------ description

Json DownloadedReplay::toJson() const {
    return Json{{"id", id},
                {"source", source},
                {"player", player},
                {"player_id", playerId},
                {"level_id", levelId},
                {"level_hash", levelHash},
                {"level_name", levelName},
                {"category", category},
                {"time_ms", timeMs},
                {"achieved_at", achievedAt},
                {"downloaded_at", downloadedAt},
                {"game_version", gameVersion},
                {"bytes", bytes},
                {"file", file}};
}

std::optional<DownloadedReplay> DownloadedReplay::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    DownloadedReplay r;
    r.id = jstr(j, "id");
    // The file is always named after the id: an index edited by hand never points elsewhere.
    if (r.id.size() > 96 || !safeChars(r.id) || jstr(j, "file") != r.id + ".cprz") return std::nullopt;
    r.file = r.id + ".cprz";
    r.source = clip(jstr(j, "source"), 16);
    r.player = clip(jstr(j, "player"), 64);
    r.playerId = clip(jstr(j, "player_id"), 64);
    r.levelId = clip(jstr(j, "level_id"), 128);
    r.levelHash = clip(jstr(j, "level_hash"), 64);
    r.levelName = clip(jstr(j, "level_name"), 120);
    r.category = clip(jstr(j, "category"), 120);
    r.timeMs = std::clamp<int64_t>(jget<int64_t>(j, "time_ms", 0), 0, int64_t{24} * 3600 * 1000);
    r.achievedAt = clip(jstr(j, "achieved_at"), 40);
    r.downloadedAt = std::max<int64_t>(0, jget<int64_t>(j, "downloaded_at", 0));
    r.gameVersion = clip(jstr(j, "game_version"), 24);
    r.bytes = jget<uint64_t>(j, "bytes", 0);
    if (r.levelId.empty()) return std::nullopt;
    return r;
}

// ------------------------------------------------------------------ level version

ReplayLevelMatch matchReplayLevel(const std::string& replayHash, bool installed, const std::vector<std::string>& installedHashes) {
    if (!installed) return ReplayLevelMatch::NotInstalled;
    if (!replayHash.empty() && std::find(installedHashes.begin(), installedHashes.end(), replayHash) != installedHashes.end())
        return ReplayLevelMatch::Same;
    return ReplayLevelMatch::OtherRevision;
}

std::optional<std::vector<std::string>> installedLevelVersions(const ContentDatabase& content, const std::string& levelId) {
    const LevelEntry* entry = content.level(levelId);
    if (!entry) return std::nullopt;
    std::vector<std::string> out;
    // The same two versions as the replay viewer: the ranked fingerprint and the gameplay one.
    if (auto text = FileUtil::readText(entry->path)) {
        Json j = Json::parse(*text, nullptr, false);
        if (!j.is_discarded())
            if (auto fp = fingerprintRankedLevel(content, j)) out.push_back(fp->hash);
    }
    if (auto level = content.loadLevel(levelId)) out.push_back(fingerprintGameplay(content, *level));
    return out;
}

// ------------------------------------------------------------------ library

void ReplayLibrary::open(fs::path root) {
    root_ = std::move(root);
    entries_.clear();
    fs::path index = root_ / "index.json";
    if (fileSize(index) == 0 || fileSize(index) > kMaxIndexBytes) return;
    auto j = FileUtil::loadJson(index);
    if (!j || jstr(*j, "format") != kIndexFormat) return;
    for (const auto& e : jfield(*j, "replays", Json::array())) {
        if (entries_.size() >= kMaxCount) break;
        auto r = DownloadedReplay::fromJson(e);
        if (!r || find(r->id)) continue;
        // Its file must still be there (it may have been removed by hand).
        uint64_t n = fileSize(pathOf(*r));
        if (n == 0 || n > kMaxFileBytes) continue;
        r->bytes = n;
        entries_.push_back(std::move(*r));
    }
    sort();
}

void ReplayLibrary::sort() {
    std::stable_sort(entries_.begin(), entries_.end(), [](const DownloadedReplay& a, const DownloadedReplay& b) { return a.downloadedAt > b.downloadedAt; });
}

const DownloadedReplay* ReplayLibrary::find(const std::string& id) const {
    for (const auto& e : entries_)
        if (e.id == id) return &e;
    return nullptr;
}

uint64_t ReplayLibrary::totalBytes() const {
    uint64_t n = 0;
    for (const auto& e : entries_) n += e.bytes;
    return n;
}

std::string ReplayLibrary::roomFor(uint64_t bytes) const {
    if (entries_.size() >= kMaxCount)
        return "Limite atteinte : " + std::to_string(kMaxCount) + " replays téléchargés au plus. Supprimez-en dans Collection › Replays.";
    if (totalBytes() + bytes > kMaxTotalBytes)
        return "Limite atteinte : " + megabytes(kMaxTotalBytes) + " Mo de replays téléchargés au plus. Supprimez-en dans Collection › Replays.";
    return {};
}

std::string ReplayLibrary::makeId(const std::string& source, const std::string& remoteId) {
    std::string id = source + "-" + remoteId;
    if (!safeChars(source) || !safeChars(remoteId) || id.size() > 96) return {};
    return id;
}

std::optional<DownloadedReplay> ReplayLibrary::check(const std::vector<uint8_t>& bytes, DownloadedReplay meta, std::string* error) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return std::nullopt;
    };
    if (meta.id.size() > 96 || !safeChars(meta.id)) return fail("Replay inconnu.");
    if (bytes.empty()) return fail("Replay vide.");
    if (bytes.size() > kMaxFileBytes) return fail("Replay trop volumineux.");
    std::string why;
    auto r = ReplayData::decode(bytes, &why);
    if (!r) return fail("Replay illisible (" + why + ").");
    if (r->levelHash.empty() || r->frames.size() < 2) return fail("Replay incomplet : il ne peut pas être regardé.");
    // What identifies the run comes from the replay, not from the server's answer.
    meta.levelHash = clip(r->levelHash, 64);
    if (meta.levelId.empty()) meta.levelId = clip(r->levelId, 128);
    if (meta.levelId.empty()) return fail("Replay sans niveau.");
    if (meta.timeMs <= 0) meta.timeMs = r->durationMs;
    meta.gameVersion = clip(r->gameVersion, 24);
    meta.bytes = bytes.size();
    meta.file = meta.id + ".cprz";
    meta.player = clip(meta.player, 64);
    meta.playerId = clip(meta.playerId, 64);
    meta.levelName = clip(meta.levelName, 120);
    meta.category = clip(meta.category, 120);
    meta.achievedAt = clip(meta.achievedAt, 40);
    if (meta.downloadedAt <= 0) meta.downloadedAt = nowSeconds();
    return meta;
}

bool ReplayLibrary::writeFile(const fs::path& root, const DownloadedReplay& meta, const std::vector<uint8_t>& bytes, std::string* error) {
    std::error_code ec;
    fs::create_directories(root, ec);
    if (meta.file != meta.id + ".cprz" || !safeChars(meta.id) || !FileUtil::writeBinary(root / meta.file, bytes)) {
        if (error) *error = "Écriture impossible sur ce PC.";
        return false;
    }
    return true;
}

bool ReplayLibrary::commit(DownloadedReplay meta, std::string* error) {
    fs::path file = pathOf(meta);
    uint64_t n = fileSize(file);
    auto drop = [&](const std::string& m) {
        std::error_code ec;
        if (!find(meta.id)) fs::remove(file, ec);
        if (error) *error = m;
        return false;
    };
    if (meta.file != meta.id + ".cprz" || !safeChars(meta.id) || n == 0 || n > kMaxFileBytes) return drop("Écriture impossible sur ce PC.");
    meta.bytes = n;
    // The same replay downloaded again replaces its entry (not counted twice).
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const DownloadedReplay& e) { return e.id == meta.id; }), entries_.end());
    if (std::string why = roomFor(n); !why.empty()) return drop(why);
    entries_.push_back(std::move(meta));
    sort();
    if (!saveIndex()) {
        if (error) *error = "Index des replays non enregistré.";
        return false;
    }
    return true;
}

bool ReplayLibrary::add(const std::vector<uint8_t>& bytes, DownloadedReplay meta, std::string* error) {
    if (!has(meta.id))
        if (std::string why = roomFor(bytes.size()); !why.empty()) {
            if (error) *error = why;
            return false;
        }
    auto checked = check(bytes, std::move(meta), error);
    if (!checked || !writeFile(root_, *checked, bytes, error)) return false;
    return commit(std::move(*checked), error);
}

bool ReplayLibrary::remove(const std::string& id, std::string* error) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const DownloadedReplay& e) { return e.id == id; });
    if (it == entries_.end()) {
        if (error) *error = "Ce replay n'est plus sur ce PC.";
        return false;
    }
    std::error_code ec;
    fs::remove(pathOf(*it), ec);
    if (ec && fs::exists(pathOf(*it))) {
        if (error) *error = "Suppression impossible : fichier en cours d'utilisation ?";
        return false;
    }
    entries_.erase(it);
    if (!saveIndex()) {
        if (error) *error = "Index des replays non enregistré.";
        return false;
    }
    return true;
}

std::optional<ReplayData> ReplayLibrary::loadFile(const fs::path& file, const std::string& expectedHash, std::string* error) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return std::nullopt;
    };
    uint64_t n = fileSize(file);
    if (n == 0) return fail("Fichier du replay introuvable sur ce PC.");
    if (n > kMaxFileBytes) return fail("Fichier du replay trop volumineux.");
    auto bytes = FileUtil::readBinary(file);
    if (!bytes) return fail("Lecture du replay impossible.");
    std::string why;
    auto r = ReplayData::decode(*bytes, &why);
    if (!r) return fail("Replay illisible (" + why + ") : supprimez-le puis téléchargez-le de nouveau.");
    if (!expectedHash.empty() && r->levelHash != expectedHash) return fail("Le fichier du replay a été modifié : supprimez-le puis téléchargez-le de nouveau.");
    return r;
}

std::optional<ReplayData> ReplayLibrary::load(const std::string& id, std::string* error) const {
    const DownloadedReplay* e = find(id);
    if (!e) {
        if (error) *error = "Ce replay n'est plus sur ce PC.";
        return std::nullopt;
    }
    auto r = loadFile(pathOf(*e), e->levelHash, error);
    if (r) r->levelId = e->levelId;
    return r;
}

bool ReplayLibrary::saveIndex() const {
    Json list = Json::array();
    for (const auto& e : entries_) list.push_back(e.toJson());
    std::error_code ec;
    fs::create_directories(root_, ec);
    bool ok = FileUtil::writeTextAtomic(root_ / "index.json", Json{{"format", kIndexFormat}, {"version", 1}, {"replays", list}}.dump(1));
    if (!ok) Log::warn("Index des replays téléchargés non enregistré : {}", Paths::toUtf8(root_));
    return ok;
}

}  // namespace cosmic
