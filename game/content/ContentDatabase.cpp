#include "game/content/ContentDatabase.h"

#include <algorithm>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"

namespace cosmic {

const char* contentSourceLabel(ContentSource s) {
    switch (s) {
        case ContentSource::Builtin: return "Officiel";
        case ContentSource::User: return "Mes créations";
        case ContentSource::Package: return "Importé";
    }
    return "";
}

namespace {
std::vector<fs::path> jsonFiles(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && FileUtil::extensionLower(e.path()) == ".json") out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}
}  // namespace

void ContentDatabase::reload() {
    planets_.clear();
    levels_.clear();
    enemies_.clear();
    powers_.clear();
    packages_.clear();
    nativePowers_.clear();
    nativeEffects_.clear();
    effects_ = Json::object();

    const fs::path data = Paths::dataDir();
    loadPlanetsFrom(data / "planets", {});
    for (const auto& f : jsonFiles(data / "levels")) loadLevelFile(f, ContentSource::Builtin, "", {});
    loadEnemiesFrom(data / "enemies", ContentSource::Builtin, {});
    loadPowersFrom(data / "powers", true);
    loadEffectsFrom(data / "effects", true);
    if (auto pj = FileUtil::loadJson(data / "player.json")) playerConfig_ = *pj;

    for (const auto& f : jsonFiles(Paths::userLevelsDir())) loadLevelFile(f, ContentSource::User, "", {});
    loadEnemiesFrom(Paths::userEnemiesDir(), ContentSource::User, {});

    std::error_code ec;
    if (fs::is_directory(Paths::packagesDir(), ec)) {
        std::vector<fs::path> roots;
        for (const auto& e : fs::directory_iterator(Paths::packagesDir(), ec))
            if (e.is_directory(ec)) roots.push_back(e.path());
        std::sort(roots.begin(), roots.end());
        for (const auto& r : roots) loadPackage(r);
    }
    for (auto& [id, e] : levels_) resolveAssetPackage(e);
    // 2.45: the campaign revisions downloaded from Cosmic Online stay in use.
    for (const auto& [id, file] : campaignRevisions_)
        if (auto it = levels_.find(id); it != levels_.end() && it->second.source == ContentSource::Builtin) applyCampaignRevision(it->second, file);
    Log::info("Content: {} planets, {} levels, {} enemies, {} powers, {} packages", planets_.size(), levels_.size(),
              enemies_.size(), powers_.size(), packages_.size());
}

void ContentDatabase::loadPlanetsFrom(const fs::path& dir, const fs::path& assetRoot) {
    for (const auto& f : jsonFiles(dir)) {
        auto j = FileUtil::loadJson(f);
        if (!j) continue;
        PlanetData p = PlanetData::fromJson(*j);
        if (p.id.empty()) continue;
        p.sourcePath = Paths::toUtf8(f);
        p.assetRoot = assetRoot;
        auto it = planets_.find(p.id);
        if (it != planets_.end() && !assetRoot.empty()) {
            // A package planet with an existing id (a level exported from the campaign
            // carries its planet for the look) never changes that planet's campaign: its
            // levels stay in the WORKSHOP. Only a new planet brings its own campaign.
            continue;
        }
        planets_[p.id] = std::move(p);
    }
}

void ContentDatabase::loadLevelFile(const fs::path& file, ContentSource src, const std::string& pkg,
                                    const fs::path& assetRoot) {
    auto j = FileUtil::loadJson(file);
    if (!j) return;
    if (jstr(*j, "format") != "cosmic.level") return;
    LevelEntry e;
    e.id = jstr(*j, "id");
    if (e.id.empty()) return;
    e.name = jstr(*j, "name", e.id);
    e.description = jstr(*j, "description");
    e.planet = jstr(*j, "planet", "sylvanox");
    e.author = jstr(*j, "author");
    e.difficulty = jget<int>(*j, "difficulty", 1);
    e.path = file;
    e.source = src;
    e.packageId = pkg;
    e.assetRoot = assetRoot;
    if (auto m = j->find("metadata"); m != j->end() && m->is_object()) e.assetPackage = jstr(*m, "assetPackage");
    if (levels_.count(e.id)) {
        Log::warn("Duplicate level id '{}' ({}) ignored", e.id, Paths::toUtf8(file));
        return;
    }
    levels_[e.id] = std::move(e);
}

void ContentDatabase::loadEnemiesFrom(const fs::path& dir, ContentSource src, const fs::path& assetRoot) {
    for (const auto& f : jsonFiles(dir)) {
        auto j = FileUtil::loadJson(f);
        if (!j || jstr(*j, "format") != "cosmic.enemy") continue;
        EnemyDefinition d = EnemyDefinition::fromJson(*j);
        if (d.id.empty()) continue;
        d.sourcePath = Paths::toUtf8(f);
        d.assetRoot = assetRoot;
        d.builtin = src == ContentSource::Builtin;
        if (!d.builtin) {
            if (auto it = enemies_.find(d.id); it != enemies_.end() && it->second.builtin) {
                Log::warn("Enemy '{}' ({}) ignored: native entities cannot be replaced", d.id, Paths::toUtf8(f));
                continue;
            }
        }
        enemies_[d.id] = std::move(d);
    }
}

void ContentDatabase::loadPowersFrom(const fs::path& dir, bool native) {
    for (const auto& f : jsonFiles(dir)) {
        auto j = FileUtil::loadJson(f);
        if (!j || jstr(*j, "format") != "cosmic.power") continue;
        PowerDefinition d = PowerDefinition::fromJson(*j);
        if (d.id.empty()) continue;
        if (!native && nativePowers_.count(d.id)) {
            Log::warn("Power '{}' ({}) ignored: native powers cannot be replaced", d.id, Paths::toUtf8(f));
            continue;
        }
        if (native) nativePowers_.insert(d.id);
        d.sourcePath = Paths::toUtf8(f);
        powers_[d.id] = std::move(d);
    }
}

void ContentDatabase::loadEffectsFrom(const fs::path& dir, bool native) {
    for (const auto& f : jsonFiles(dir)) {
        auto j = FileUtil::loadJson(f);
        if (!j || !j->is_object()) continue;
        const Json& presets = j->contains("presets") ? (*j)["presets"] : *j;
        if (!presets.is_object()) continue;
        for (auto& [k, v] : presets.items()) {
            if (!v.is_object()) continue;
            if (!native && nativeEffects_.count(k)) continue;  // native presets stay as shipped
            if (native) nativeEffects_.insert(k);
            effects_[k] = v;
        }
    }
}

void ContentDatabase::loadPackage(const fs::path& root) {
    auto manifest = FileUtil::loadJson(root / "manifest.json");
    if (!manifest) return;
    PackageInfo info;
    info.id = jstr(*manifest, "packageId");
    info.name = jstr(*manifest, "name");
    info.author = jstr(*manifest, "author");
    info.version = jstr(*manifest, "version");
    info.levelId = jstr(*manifest, "levelId");
    info.installedAt = jstr(*manifest, "installedAt");
    info.root = root;
    if (info.id.empty()) return;
    const fs::path assets = root / "assets";
    loadPlanetsFrom(root / "planets", assets);
    loadEnemiesFrom(root / "enemies", ContentSource::Package, assets);
    loadPowersFrom(root / "powers", false);
    loadEffectsFrom(root / "effects", false);
    loadLevelFile(root / "level.json", ContentSource::Package, info.id, assets);
    packages_.push_back(std::move(info));
}

std::vector<const PlanetData*> ContentDatabase::planets() const {
    std::vector<const PlanetData*> v;
    for (const auto& [id, p] : planets_) v.push_back(&p);
    std::sort(v.begin(), v.end(), [](const auto* a, const auto* b) { return a->orbitRadius < b->orbitRadius; });
    return v;
}

const PlanetData* ContentDatabase::planet(const std::string& id) const {
    auto it = planets_.find(id);
    return it == planets_.end() ? nullptr : &it->second;
}

std::vector<const PlanetData*> ContentDatabase::campaignPlanets() const {
    std::vector<const PlanetData*> left;  // nearest to the star first (planets())
    for (const PlanetData* p : planets())
        if (!p->teaser && !levelsForPlanet(p->id).empty()) left.push_back(p);
    // What each planet's `unlock` waits for: "planet:<id>", "planets:<id>,<id>…", "level:<id>".
    auto needs = [this](const PlanetData& p) {
        std::vector<std::string> ids;
        const std::string& rule = p.unlock;
        if (rule.rfind("planets:", 0) == 0) {
            for (size_t start = 8; start <= rule.size();) {
                const size_t end = rule.find(',', start);
                ids.push_back(rule.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (end == std::string::npos) break;
                start = end + 1;
            }
        } else if (rule.rfind("planet:", 0) == 0) {
            ids.push_back(rule.substr(7));
        } else if (rule.rfind("level:", 0) == 0) {
            ids.push_back(campaignPlanetOf(rule.substr(6)));
        }
        return ids;
    };
    std::vector<const PlanetData*> out;
    while (!left.empty()) {
        // The planets open once those already listed are done (one waiting for a planet outside
        // the list, or for itself, is open: never left out).
        auto open = [&](const PlanetData* p) {
            for (const std::string& id : needs(*p)) {
                if (id.empty() || id == p->id) continue;
                const bool waits = std::any_of(left.begin(), left.end(), [&](const PlanetData* q) { return q->id == id; });
                if (waits) return false;
            }
            return true;
        };
        const PlanetData* next = nullptr;
        for (const PlanetData* p : left)
            if (open(p) && (!next || p->difficulty < next->difficulty)) next = p;
        if (!next) next = left.front();  // a loop between unlock rules: nearest first
        out.push_back(next);
        left.erase(std::find(left.begin(), left.end(), next));
    }
    return out;
}

std::vector<const LevelEntry*> ContentDatabase::levels() const {
    std::vector<const LevelEntry*> v;
    for (const auto& [id, l] : levels_) v.push_back(&l);
    return v;
}

std::vector<const LevelEntry*> ContentDatabase::levelsForPlanet(const std::string& planetId) const {
    std::vector<const LevelEntry*> v;
    if (const PlanetData* p = planet(planetId)) {
        for (const auto& id : p->levels)
            if (const LevelEntry* l = level(id)) v.push_back(l);
    }
    return v;
}

std::string ContentDatabase::campaignPlanetOf(const std::string& levelId) const {
    for (const auto& [id, p] : planets_)
        if (std::find(p.levels.begin(), p.levels.end(), levelId) != p.levels.end() && level(levelId)) return id;
    return {};
}

bool ContentDatabase::applyCampaignRevision(LevelEntry& e, const fs::path& file) {
    auto j = FileUtil::loadJson(file);
    if (!j || jstr(*j, "format") != "cosmic.level" || jstr(*j, "id") != e.id) return false;
    if (e.bundledPath.empty()) e.bundledPath = e.path;
    e.path = file;
    e.name = jstr(*j, "name", e.id);
    e.description = jstr(*j, "description");
    e.difficulty = jget<int>(*j, "difficulty", 1);
    return true;
}

bool ContentDatabase::setCampaignRevision(const std::string& levelId, const fs::path& file) {
    auto it = levels_.find(levelId);
    if (it == levels_.end() || it->second.source != ContentSource::Builtin) return false;
    LevelEntry& e = it->second;
    if (file.empty()) {
        campaignRevisions_.erase(levelId);
        if (e.bundledPath.empty()) return true;
        fs::path bundled = e.bundledPath;
        e.bundledPath.clear();
        e.path = bundled;
        if (auto j = FileUtil::loadJson(bundled)) {
            e.name = jstr(*j, "name", e.id);
            e.description = jstr(*j, "description");
            e.difficulty = jget<int>(*j, "difficulty", 1);
        }
        return true;
    }
    if (!applyCampaignRevision(e, file)) return false;
    campaignRevisions_[levelId] = file;
    return true;
}

fs::path ContentDatabase::bundledLevelPath(const std::string& levelId) const {
    const LevelEntry* e = level(levelId);
    if (!e || e->source != ContentSource::Builtin) return {};
    return e->bundledPath.empty() ? e->path : e->bundledPath;
}

const LevelEntry* ContentDatabase::level(const std::string& id) const {
    auto it = levels_.find(id);
    return it == levels_.end() ? nullptr : &it->second;
}

std::optional<LevelData> ContentDatabase::loadLevel(const std::string& id, std::string* error) const {
    const LevelEntry* e = level(id);
    if (!e) {
        if (error) *error = "niveau introuvable : " + id;
        return std::nullopt;
    }
    auto j = FileUtil::loadJson(e->path);
    if (!j) {
        if (error) *error = "fichier illisible";
        return std::nullopt;
    }
    return LevelData::fromJson(*j, error);
}

bool ContentDatabase::saveUserLevel(const LevelData& level) {
    fs::path p = Paths::userLevelsDir() / (FileUtil::slugify(level.id) + ".json");
    if (!FileUtil::saveJson(p, level.toJson())) return false;
    auto it = levels_.find(level.id);
    if (it == levels_.end() || it->second.source == ContentSource::User) {
        levels_.erase(level.id);
        loadLevelFile(p, ContentSource::User, "", {});
        if (auto saved = levels_.find(level.id); saved != levels_.end()) resolveAssetPackage(saved->second);
    }
    return true;
}

void ContentDatabase::resolveAssetPackage(LevelEntry& e) const {
    if (e.source != ContentSource::User || e.assetPackage.empty()) return;
    for (const auto& p : packages_) {
        if (p.id != e.assetPackage) continue;
        std::error_code ec;
        if (fs::is_directory(p.root / "assets", ec)) e.assetRoot = p.root / "assets";
        return;
    }
    Log::warn("Level '{}' uses assets of package '{}', which is not installed", e.id, e.assetPackage);
}

bool ContentDatabase::deleteUserLevel(const std::string& id) {
    auto it = levels_.find(id);
    if (it == levels_.end() || it->second.source != ContentSource::User) return false;
    std::error_code ec;
    fs::remove(it->second.path, ec);
    levels_.erase(it);
    return !ec;
}

std::vector<const EnemyDefinition*> ContentDatabase::enemies() const {
    std::vector<const EnemyDefinition*> v;
    for (const auto& [id, e] : enemies_) v.push_back(&e);
    std::sort(v.begin(), v.end(), [](const auto* a, const auto* b) {
        if (a->isBoss != b->isBoss) return !a->isBoss;
        return a->name < b->name;
    });
    return v;
}

const EnemyDefinition* ContentDatabase::enemy(const std::string& id) const {
    auto it = enemies_.find(id);
    return it == enemies_.end() ? nullptr : &it->second;
}

bool ContentDatabase::isNativeEnemy(const std::string& id) const {
    auto it = enemies_.find(id);
    return it != enemies_.end() && it->second.builtin;
}

std::string ContentDatabase::freeEnemyId(const std::string& base) const {
    std::string b = FileUtil::slugify(base);
    if (b.empty()) b = "ennemi";
    std::string id = b;
    for (int n = 2; enemies_.count(id); ++n) id = b + "_" + std::to_string(n);
    return id;
}

bool ContentDatabase::setEnemyRevision(int revision, EnemyDefinition def) {
    if (revision < 1 || !isNativeEnemy(def.id)) return false;
    // A native's sheets and sounds: those of the game (never a package's).
    def.builtin = true;
    def.assetRoot.clear();
    std::lock_guard<std::mutex> lock(*enemyRevisionsMutex_);
    auto& revs = enemyRevisions_[def.id];
    if (auto it = revs.find(revision); it != revs.end()) it->second = std::move(def);  // same node: pointers stay valid
    else revs.emplace(revision, std::move(def));
    return true;
}

void ContentDatabase::removeEnemyRevision(const std::string& id, int revision) {
    std::lock_guard<std::mutex> lock(*enemyRevisionsMutex_);
    auto it = enemyRevisions_.find(id);
    if (it == enemyRevisions_.end()) return;
    it->second.erase(revision);
    if (it->second.empty()) enemyRevisions_.erase(it);
}

const EnemyDefinition* ContentDatabase::enemyRevision(const std::string& id, int revision) const {
    std::lock_guard<std::mutex> lock(*enemyRevisionsMutex_);
    auto it = enemyRevisions_.find(id);
    if (it == enemyRevisions_.end()) return nullptr;
    auto r = it->second.find(revision);
    return r == it->second.end() ? nullptr : &r->second;
}

std::map<std::string, int> ContentDatabase::enemyBindingOf(const std::string& levelId, const Json& metadata) const {
    std::map<std::string, int> out;
    const LevelEntry* e = level(levelId);
    // Only the campaign plays revisions: a community level, or one of the player's, never.
    if (!e || e->source != ContentSource::Builtin || !metadata.is_object()) return out;
    auto b = metadata.find("enemyRevisions");
    if (b == metadata.end() || !b->is_object()) return out;
    for (const auto& [id, rev] : b->items())
        if (rev.is_number_integer() && rev.get<int64_t>() >= 1 && rev.get<int64_t>() <= 1000000 && isNativeEnemy(id)) out[id] = rev.get<int>();
    return out;
}

const EnemyDefinition* ContentDatabase::enemyFor(const std::string& id, const std::map<std::string, int>& binding) const {
    auto b = binding.find(id);
    if (b == binding.end()) return enemy(id);
    return enemyRevision(id, b->second);
}

bool ContentDatabase::saveUserEnemy(const EnemyDefinition& def) {
    if (def.id.empty() || isNativeEnemy(def.id) || isNativeEnemy(FileUtil::slugify(def.id))) {
        Log::warn("Refusing to save enemy '{}': native entities are read-only", def.id);
        return false;
    }
    fs::path p = Paths::userEnemiesDir() / (FileUtil::slugify(def.id) + ".json");
    if (!FileUtil::saveJson(p, def.toJson())) return false;
    EnemyDefinition copy = def;
    copy.sourcePath = Paths::toUtf8(p);
    copy.builtin = false;
    // A user enemy that was itself loaded from a package keeps that package's assets.
    if (auto it = enemies_.find(def.id); it != enemies_.end()) copy.assetRoot = it->second.assetRoot;
    enemies_[def.id] = std::move(copy);
    return true;
}

bool ContentDatabase::deleteUserEnemy(const std::string& id) {
    auto it = enemies_.find(id);
    if (it == enemies_.end() || it->second.builtin) return false;
    std::error_code ec;
    fs::remove(Paths::fromUtf8(it->second.sourcePath), ec);
    enemies_.erase(it);
    return true;
}

std::vector<const PowerDefinition*> ContentDatabase::powers() const {
    std::vector<const PowerDefinition*> v;
    for (const auto& [id, p] : powers_) v.push_back(&p);
    std::sort(v.begin(), v.end(), [](const auto* a, const auto* b) { return a->icon < b->icon; });
    return v;
}

const PowerDefinition* ContentDatabase::power(const std::string& id) const {
    auto it = powers_.find(id);
    return it == powers_.end() ? nullptr : &it->second;
}

const PackageInfo* ContentDatabase::package(const std::string& id) const {
    for (const auto& p : packages_)
        if (p.id == id) return &p;
    return nullptr;
}

bool ContentDatabase::removePackage(const std::string& id) {
    const PackageInfo* p = package(id);
    if (!p) return false;
    std::error_code ec;
    fs::remove_all(p->root, ec);
    reload();
    return !ec;
}

}  // namespace cosmic
