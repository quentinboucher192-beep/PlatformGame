#include "game/online/CampaignRevisions.h"

#include <algorithm>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/content/ContentDatabase.h"
#include "game/online/LevelFingerprint.h"
#include "game/progression/NewsCenter.h"
#include "packaging/Sha256.h"

namespace cosmic {

namespace {
bool isHex64(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool safeId(const std::string& s) {
    return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}
std::string key(const std::string& levelId, int version) { return levelId + ":" + std::to_string(version); }

/// A revision of the manifest (current or pinned), when it is well formed.
std::optional<CampaignRevisionFile> parseRevision(const std::string& levelId, const Json& f) {
    if (!f.is_object()) return std::nullopt;
    CampaignRevisionFile r;
    r.levelId = levelId;
    r.version = jget<int>(f, "version", 0);
    r.contentHash = jstr(f, "content_hash");
    r.sha256 = jstr(f, "sha256");
    r.size = jget<int64_t>(f, "size", 0);
    r.minGameVersion = jstr(f, "min_game_version");
    r.path = jstr(f, "path");
    if (r.version < 1 || !isHex64(r.contentHash) || !isHex64(r.sha256) || r.size <= 0 || r.size > 8 * 1024 * 1024) return std::nullopt;
    // Only a path of this server's campaign files (never another host).
    const std::string prefix = "/api/v1/campaign/levels/" + levelId + "/revisions/";
    if (r.path.rfind(prefix, 0) != 0 || r.path.find("..") != std::string::npos) return std::nullopt;
    return r;
}
}  // namespace

std::string CampaignRevisionStore::rankedHashOf(const ContentDatabase& content, const std::string& text) {
    Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    auto fp = fingerprintRankedLevel(content, j);
    return fp ? fp->hash : std::string();
}

int CampaignRevisionStore::revisionOf(const fs::path& file) {
    auto j = FileUtil::loadJson(file);
    if (!j || !j->is_object()) return 0;
    if (auto m = j->find("metadata"); m != j->end() && m->is_object()) return std::max(1, jget<int>(*m, "revision", 1));
    return 1;
}

fs::path CampaignRevisionStore::fileOf(const std::string& levelId, int version) const {
    return dir_ / (levelId + "-" + std::to_string(version) + ".json");
}

void CampaignRevisionStore::load() {
    index_ = Json::object();
    bundledHash_.clear();
    if (dir_.empty()) return;
    auto j = FileUtil::loadJson(dir_ / "index.json");
    if (j && j->is_object() && jstr(*j, "format") == "cosmic.campaign_revisions_cache") index_ = *j;
}

bool CampaignRevisionStore::save() const {
    if (dir_.empty()) return false;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    Json out = index_;
    out["format"] = "cosmic.campaign_revisions_cache";
    return FileUtil::writeTextAtomic(dir_ / "index.json", out.dump(1));
}

std::vector<CampaignRevisionFile> CampaignRevisionStore::applyManifest(const Json& data, const std::string& server, const ContentDatabase& content,
                                                                       const std::string& gameVersion) {
    std::vector<CampaignRevisionFile> todo;
    if (!data.is_object() || jstr(data, "format") != "cosmic.campaign_revisions" || !data.contains("levels") || !data["levels"].is_array()) return todo;
    Json current = Json::object(), pinned = Json::object();
    const Json files = index_.value("files", Json::object());
    auto have = [&](const CampaignRevisionFile& r) {
        auto it = files.find(key(r.levelId, r.version));
        return it != files.end() && jstr(*it, "sha256") == r.sha256 && FileUtil::exists(fileOf(r.levelId, r.version));
    };
    auto want = [&](const CampaignRevisionFile& r) {
        if (!r.minGameVersion.empty() && versionNewer(r.minGameVersion, gameVersion)) return;  // made with a newer game
        if (!have(r)) todo.push_back(r);
    };
    for (const Json& l : data["levels"]) {
        const std::string id = jstr(l, "level_id");
        if (!safeId(id) || !content.level(id) || content.level(id)->source != ContentSource::Builtin) continue;  // not a level of this game
        Json c = {{"version", jget<int>(l, "version", 0)}, {"content_hash", jstr(l, "content_hash")}, {"max_version", jget<int>(l, "max_version", 0)}};
        if (l.contains("rating") && l["rating"].is_object()) c["rating"] = l["rating"];
        auto file = parseRevision(id, l.value("file", Json()));
        if (file && file->contentHash == jstr(l, "content_hash")) {
            c["file"] = l["file"];
            // A game whose bundled level is newer than every revision of the server keeps it
            // (the server registers the levels of a release after the release itself).
            int bundled = revisionOf(content.bundledLevelPath(id));
            if (bundled <= jget<int>(l, "max_version", 0)) want(*file);
        }
        current[id] = c;
        Json pins = Json::array();
        for (const Json& p : l.value("pinned", Json::array()))
            if (auto r = parseRevision(id, p)) {
                pins.push_back(r->contentHash);
                want(*r);
            }
        if (!pins.empty()) pinned[id] = pins;
    }
    index_["server"] = server;
    index_["current"] = current;
    index_["pinned"] = pinned;
    return todo;
}

bool CampaignRevisionStore::install(const ContentDatabase& content, const CampaignRevisionFile& f, const std::string& bytes, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        Log::warn("Campagne en ligne : révision {} de {} refusée ({})", f.version, f.levelId, why);
        return false;
    };
    if (dir_.empty() || !safeId(f.levelId) || f.version < 1) return fail("révision invalide");
    if (Sha256::hex(bytes) != f.sha256) return fail("fichier différent de celui du serveur (SHA-256)");
    Json j = Json::parse(bytes, nullptr, false);
    if (j.is_discarded() || !j.is_object() || jstr(j, "format") != "cosmic.level") return fail("ce n'est pas un niveau");
    if (jstr(j, "id") != f.levelId) return fail("identifiant différent");
    auto loaded = LevelData::fromJson(j);
    if (!loaded) return fail("niveau illisible par ce jeu");
    auto fp = fingerprintRankedLevel(content, j);
    if (!fp || fp->version != f.version) return fail("numéro de révision différent");
    // The same level as the publisher's game plays it (enemies, powers, player, rules).
    if (fp->hash != f.contentHash) return fail("empreinte différente dans cette version du jeu");
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (!FileUtil::writeTextAtomic(fileOf(f.levelId, f.version), bytes)) return fail("écriture impossible");
    Json& files = index_["files"];
    if (!files.is_object()) files = Json::object();
    files[key(f.levelId, f.version)] = {{"level_id", f.levelId},
                                        {"version", f.version},
                                        {"content_hash", f.contentHash},
                                        {"sha256", f.sha256},
                                        {"size", static_cast<int64_t>(bytes.size())},
                                        {"checked_with", std::string(COSMIC_VERSION_STRING)}};
    Log::info("Campagne en ligne : révision {} de {} installée", f.version, f.levelId);
    return true;
}

std::map<std::string, fs::path> CampaignRevisionStore::currentFiles(const ContentDatabase& content, const std::string& server,
                                                                   const std::string& gameVersion) {
    std::map<std::string, fs::path> out;
    if (jstr(index_, "server") != server || !index_.contains("current") || !index_["current"].is_object()) return out;
    Json& files = index_["files"];
    if (!files.is_object()) return out;
    bool changed = false;
    for (const auto& [id, c] : index_["current"].items()) {
        if (!c.contains("file")) continue;
        auto r = parseRevision(id, c["file"]);
        const LevelEntry* e = content.level(id);
        if (!r || !e || e->source != ContentSource::Builtin) continue;
        auto it = files.find(key(id, r->version));
        if (it == files.end() || jstr(*it, "sha256") != r->sha256) continue;
        fs::path file = fileOf(id, r->version);
        auto text = FileUtil::readText(file);
        // A file changed or damaged on disk: the bundled level is played.
        if (!text || Sha256::hex(*text) != r->sha256) {
            Log::warn("Campagne en ligne : fichier de {} abîmé, niveau d'origine utilisé", id);
            continue;
        }
        // Checked by another version of the game: its fingerprint is checked again here.
        if (jstr(*it, "checked_with") != gameVersion) {
            if (rankedHashOf(content, *text) != r->contentHash) {
                Log::warn("Campagne en ligne : révision {} de {} faite pour une autre version du jeu", r->version, id);
                continue;
            }
            (*it)["checked_with"] = gameVersion;
            changed = true;
        }
        int bundled = revisionOf(content.bundledLevelPath(id));
        if (bundled > jget<int>(c, "max_version", 0)) continue;  // the game is newer than the server's revisions
        out[id] = file;
    }
    if (changed) save();
    return out;
}

std::optional<fs::path> CampaignRevisionStore::fileFor(const ContentDatabase& content, const std::string& levelId, const std::string& hash) const {
    if (!isHex64(hash)) return std::nullopt;
    fs::path bundled = content.bundledLevelPath(levelId);
    if (bundled.empty()) return std::nullopt;
    auto bh = bundledHash_.find(levelId);
    if (bh == bundledHash_.end()) {
        auto text = FileUtil::readText(bundled);
        bh = bundledHash_.emplace(levelId, text ? rankedHashOf(content, *text) : std::string()).first;
    }
    if (bh->second == hash) return bundled;
    const Json files = index_.value("files", Json::object());
    for (const auto& [k, f] : files.items()) {
        if (jstr(f, "level_id") != levelId || jstr(f, "content_hash") != hash) continue;
        fs::path file = fileOf(levelId, jget<int>(f, "version", 0));
        auto text = FileUtil::readText(file);
        if (!text || Sha256::hex(*text) != jstr(f, "sha256")) continue;
        if (jstr(f, "checked_with") != COSMIC_VERSION_STRING && rankedHashOf(content, *text) != hash) continue;
        return file;
    }
    return std::nullopt;
}

const Json* CampaignRevisionStore::ratingOf(const std::string& levelId) const {
    auto c = index_.find("current");
    if (c == index_.end() || !c->is_object()) return nullptr;
    auto l = c->find(levelId);
    if (l == c->end() || !l->contains("file")) return nullptr;
    auto r = l->find("rating");
    return r != l->end() && r->is_object() ? &*r : nullptr;
}

int CampaignRevisionStore::prune() {
    if (!index_.contains("files") || !index_["files"].is_object()) return 0;
    std::set<std::string> keep;  // "<id>:<hash>"
    const Json current = index_.value("current", Json::object()), pinned = index_.value("pinned", Json::object());
    for (const auto& [id, c] : current.items()) keep.insert(id + ":" + jstr(c, "content_hash"));
    for (const auto& [id, pins] : pinned.items())
        for (const auto& h : pins)
            if (h.is_string()) keep.insert(id + ":" + h.get<std::string>());
    int removed = 0;
    Json& files = index_["files"];
    for (auto it = files.begin(); it != files.end();) {
        const std::string id = jstr(*it, "level_id");
        if (keep.count(id + ":" + jstr(*it, "content_hash"))) {
            ++it;
            continue;
        }
        std::error_code ec;
        fs::remove(fileOf(id, jget<int>(*it, "version", 0)), ec);
        it = files.erase(it);
        ++removed;
    }
    if (removed) save();
    return removed;
}

}  // namespace cosmic
