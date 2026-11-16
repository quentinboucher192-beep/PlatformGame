#include "game/online/EnemyRevisions.h"

#include <algorithm>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/content/ContentDatabase.h"
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
std::string key(const std::string& id, int revision) { return id + ":" + std::to_string(revision); }

/// A revision of the manifest, when it is well formed.
std::optional<EnemyRevisionFile> parseRevision(const std::string& enemyId, const Json& r) {
    if (!r.is_object()) return std::nullopt;
    EnemyRevisionFile f;
    f.enemyId = enemyId;
    f.revision = jget<int>(r, "revision", 0);
    f.sha256 = jstr(r, "sha256");
    f.size = jget<int64_t>(r, "size", 0);
    f.minGameVersion = jstr(r, "min_game_version");
    f.path = jstr(r, "path");
    if (f.revision < 1 || !isHex64(f.sha256) || f.size <= 0 || f.size > 256 * 1024) return std::nullopt;
    // Only a path of this server's enemy files (never another host).
    const std::string prefix = "/api/v1/enemies/" + enemyId + "/revisions/";
    if (f.path.rfind(prefix, 0) != 0 || f.path.find("..") != std::string::npos) return std::nullopt;
    return f;
}
}  // namespace

std::optional<EnemyDefinition> EnemyRevisionStore::parseDefinition(const ContentDatabase& content, const std::string& enemyId, const std::string& text,
                                                                   std::string* error) {
    auto fail = [&](const std::string& why) -> std::optional<EnemyDefinition> {
        if (error) *error = why;
        return std::nullopt;
    };
    Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || jstr(j, "format") != "cosmic.enemy") return fail("ce n'est pas une définition d'ennemi");
    if (jstr(j, "id") != enemyId) return fail("identifiant différent");
    if (!content.isNativeEnemy(enemyId)) return fail("pas une entité native de ce jeu");
    EnemyDefinition d = EnemyDefinition::fromJson(j);
    if (d.id != enemyId) return fail("identifiant différent");
    return d;
}

fs::path EnemyRevisionStore::fileOf(const std::string& enemyId, int revision) const {
    return dir_ / (enemyId + "-" + std::to_string(revision) + ".json");
}

void EnemyRevisionStore::load() {
    index_ = Json::object();
    if (dir_.empty()) return;
    auto j = FileUtil::loadJson(dir_ / "index.json");
    if (j && j->is_object() && jstr(*j, "format") == "cosmic.enemy_revisions_cache") index_ = *j;
}

bool EnemyRevisionStore::save() const {
    if (dir_.empty()) return false;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    Json out = index_;
    out["format"] = "cosmic.enemy_revisions_cache";
    return FileUtil::writeTextAtomic(dir_ / "index.json", out.dump(1));
}

void EnemyRevisionStore::forget(ContentDatabase& content) {
    const Json files = index_.value("files", Json::object());
    for (const auto& [k, f] : files.items()) {
        const std::string id = jstr(f, "enemy_id");
        const int rev = jget<int>(f, "revision", 0);
        content.removeEnemyRevision(id, rev);
        if (safeId(id) && rev > 0) {
            std::error_code ec;
            fs::remove(fileOf(id, rev), ec);
        }
    }
    index_["files"] = Json::object();
    index_["current"] = Json::object();
}

std::vector<EnemyRevisionFile> EnemyRevisionStore::applyManifest(const Json& data, const std::string& server, ContentDatabase& content,
                                                                 const std::string& gameVersion) {
    std::vector<EnemyRevisionFile> todo;
    if (!data.is_object() || jstr(data, "format") != "cosmic.enemy_revisions" || !data.contains("enemies") || !data["enemies"].is_array()) return todo;
    // The revision numbers are the server's own: those of another server are forgotten.
    if (!jstr(index_, "server").empty() && jstr(index_, "server") != server) forget(content);
    Json current = Json::object();
    const Json files = index_.value("files", Json::object());
    for (const Json& e : data["enemies"]) {
        const std::string id = jstr(e, "enemy_id");
        if (!safeId(id) || !content.isNativeEnemy(id)) continue;  // not a native of this game
        const int cur = jget<int>(e, "current", 0);
        if (cur > 0) current[id] = cur;
        for (const Json& r : e.value("revisions", Json::array())) {
            auto f = parseRevision(id, r);
            if (!f) continue;
            if (!f->minGameVersion.empty() && versionNewer(f->minGameVersion, gameVersion)) continue;  // made with a newer game
            auto it = files.find(key(id, f->revision));
            bool have = it != files.end() && jstr(*it, "sha256") == f->sha256;
            if (have) {  // a small file: checked again (a damaged one is downloaded again)
                auto text = FileUtil::readText(fileOf(id, f->revision));
                have = text && Sha256::hex(*text) == f->sha256;
            }
            if (!have) todo.push_back(*f);
        }
    }
    index_["server"] = server;
    index_["current"] = current;
    return todo;
}

bool EnemyRevisionStore::install(ContentDatabase& content, const EnemyRevisionFile& f, const std::string& bytes, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        Log::warn("Ennemis en ligne : révision {} de {} refusée ({})", f.revision, f.enemyId, why);
        return false;
    };
    if (dir_.empty() || !safeId(f.enemyId) || f.revision < 1) return fail("révision invalide");
    if (Sha256::hex(bytes) != f.sha256) return fail("fichier différent de celui du serveur (SHA-256)");
    std::string why;
    auto def = parseDefinition(content, f.enemyId, bytes, &why);
    if (!def) return fail(why);
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (!FileUtil::writeTextAtomic(fileOf(f.enemyId, f.revision), bytes)) return fail("écriture impossible");
    Json& files = index_["files"];
    if (!files.is_object()) files = Json::object();
    files[key(f.enemyId, f.revision)] = {{"enemy_id", f.enemyId},
                                         {"revision", f.revision},
                                         {"sha256", f.sha256},
                                         {"size", static_cast<int64_t>(bytes.size())},
                                         {"min_game_version", f.minGameVersion}};
    def->sourcePath = Paths::toUtf8(fileOf(f.enemyId, f.revision));
    content.setEnemyRevision(f.revision, std::move(*def));
    Log::info("Ennemis en ligne : révision {} de {} installée", f.revision, f.enemyId);
    return true;
}

int EnemyRevisionStore::applyTo(ContentDatabase& content, const std::string& server, const std::string& gameVersion) const {
    if (jstr(index_, "server") != server) return 0;
    int n = 0;
    const Json files = index_.value("files", Json::object());
    for (const auto& [k, f] : files.items()) {
        const std::string id = jstr(f, "enemy_id");
        const int rev = jget<int>(f, "revision", 0);
        if (!safeId(id) || rev < 1) continue;
        const std::string minVersion = jstr(f, "min_game_version");
        if (!minVersion.empty() && versionNewer(minVersion, gameVersion)) continue;
        // Already given from this very file (left as it is: a level may be using it).
        if (const EnemyDefinition* have = content.enemyRevision(id, rev); have && have->sourcePath == Paths::toUtf8(fileOf(id, rev))) {
            ++n;
            continue;
        }
        auto text = FileUtil::readText(fileOf(id, rev));
        // A file changed or damaged on disk: not used (downloaded again at the next reading).
        if (!text || Sha256::hex(*text) != jstr(f, "sha256")) {
            Log::warn("Ennemis en ligne : fichier de {} (révision {}) abîmé", id, rev);
            continue;
        }
        auto def = parseDefinition(content, id, *text);
        if (!def) continue;
        def->sourcePath = Paths::toUtf8(fileOf(id, rev));
        if (content.setEnemyRevision(rev, std::move(*def))) ++n;
    }
    return n;
}

std::map<std::string, int> EnemyRevisionStore::currentRevisions(const std::string& server) const {
    std::map<std::string, int> out;
    if (jstr(index_, "server") != server) return out;
    auto c = index_.find("current");
    if (c == index_.end() || !c->is_object()) return out;
    for (const auto& [id, rev] : c->items())
        if (rev.is_number_integer() && rev.get<int>() > 0) out[id] = rev.get<int>();
    return out;
}

int EnemyRevisionStore::currentRevision(const std::string& enemyId, const std::string& server) const {
    if (jstr(index_, "server") != server) return 0;
    auto c = index_.find("current");
    if (c == index_.end() || !c->is_object()) return 0;
    return std::max(0, jget<int>(*c, enemyId.c_str(), 0));
}

}  // namespace cosmic
