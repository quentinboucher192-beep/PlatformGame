#include "packaging/PackageImporter.h"

#include <algorithm>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "game/content/ContentDatabase.h"
#include "packaging/Sha256.h"
#include "packaging/ZipArchive.h"

namespace cosmic {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::optional<Json> parseJson(const std::vector<uint8_t>& data) {
    try {
        return Json::parse(data.begin(), data.end());
    } catch (const Json::exception&) {
        return std::nullopt;
    }
}
}  // namespace

PackageInspection PackageImporter::inspect(const fs::path& file) {
    PackageInspection r;
    r.file = file;
    auto fail = [&](const std::string& e) {
        r.errors.push_back(e);
        r.valid = false;
        return r;
    };
    if (FileUtil::extensionLower(file) != PackageFormat::kExtension)
        r.warnings.push_back("L'extension n'est pas .cplanet");

    ZipReader zip;
    std::string err;
    if (!zip.open(file, &err)) return fail("Archive illisible : " + err);
    r.checks.push_back("Archive ZIP lisible");

    // ------------------------------------------------------- structure
    const auto& entries = zip.entries();
    if (entries.size() > PackageFormat::kMaxEntries) return fail("Trop de fichiers dans l'archive");
    uint64_t declared = 0;
    std::set<std::string> seen;
    const std::string root = PackageFormat::kRoot;
    for (const auto& e : entries) {
        if (e.symlink) return fail("Lien symbolique refusé : " + e.name);
        if (e.name.find('\\') != std::string::npos) return fail("Chemin invalide (barre oblique inverse) : " + e.name);
        if (e.name.rfind(root, 0) != 0) return fail("Fichier hors du dossier COSMIC_LEVEL : " + e.name);
        std::string rel = e.name.substr(root.size());
        if (e.directory || rel.empty()) continue;
        if (!Paths::isSafeRelative(rel)) return fail("Chemin dangereux refusé : " + e.name);
        if (!seen.insert(lower(rel)).second) return fail("Fichier en double : " + e.name);
        if (!PackageFormat::allowedExtension(rel)) return fail("Type de fichier interdit : " + e.name);
        if (e.size > PackageFormat::kMaxFileSize) return fail("Fichier trop volumineux : " + e.name);
        if (e.size > (1u << 20) && e.compressedSize > 0 &&
            static_cast<double>(e.size) / static_cast<double>(e.compressedSize) > PackageFormat::kMaxCompressionRatio)
            return fail("Taux de compression suspect (bombe ZIP) : " + e.name);
        declared += e.size;
    }
    if (declared > PackageFormat::kMaxTotalSize) return fail("Contenu décompressé trop volumineux");
    r.checks.push_back("Chemins sûrs, types de fichiers autorisés, tailles raisonnables");

    // -------------------------------------------------------- manifest
    const ZipEntry* me = zip.find(root + "manifest.json");
    if (!me) return fail("manifest.json manquant : ce n'est pas un package Cosmic Platformer");
    auto mdata = zip.read(*me, 4u << 20);
    if (!mdata) return fail("manifest.json corrompu");
    auto mj = parseJson(*mdata);
    if (!mj || jstr(*mj, "format") != "cosmic.package") return fail("manifest.json invalide");
    r.manifest = *mj;
    int fv = jget<int>(*mj, "formatVersion", 0);
    if (fv < 1 || fv > PackageFormat::kFormatVersion)
        return fail("Version de format non prise en charge (" + std::to_string(fv) + ")");
    r.packageId = FileUtil::slugify(jstr(*mj, "packageId"));
    r.name = jstr(*mj, "name");
    r.author = jstr(*mj, "author");
    r.version = jstr(*mj, "version");
    r.description = jstr(*mj, "description");
    r.levelId = jstr(*mj, "levelId");
    r.engineVersion = jstr(*mj, "engineVersion");
    r.minEngineVersion = jstr(*mj, "minEngineVersion", "1.0.0");
    if (r.packageId.empty() || r.levelId.empty()) return fail("Identifiants manquants dans le manifeste");
    if (PackageFormat::compareVersions(r.minEngineVersion, PackageFormat::engineVersion()) > 0)
        return fail("Ce niveau nécessite Cosmic Platformer " + r.minEngineVersion + " (version installée : " +
                    PackageFormat::engineVersion() + ")");
    r.checks.push_back("Manifeste valide, version du moteur compatible (" + r.minEngineVersion + " requise)");

    // -------------------------------------------- files and fingerprints
    if (!mj->contains("files") || !(*mj)["files"].is_array()) return fail("Liste des fichiers absente du manifeste");
    std::set<std::string> listed;
    for (const auto& f : (*mj)["files"]) {
        std::string path = jstr(f, "path");
        std::string sha = jstr(f, "sha256");
        uint64_t size = jget<uint64_t>(f, "size", 0);
        if (!Paths::isSafeRelative(path)) return fail("Chemin dangereux dans le manifeste : " + path);
        const ZipEntry* e = zip.find(root + path);
        if (!e) return fail("Fichier manquant dans l'archive : " + path);
        if (e->size != size) return fail("Taille incorrecte : " + path);
        auto data = zip.read(*e, PackageFormat::kMaxFileSize);
        if (!data) return fail("Fichier corrompu (CRC) : " + path);
        if (Sha256::hex(*data) != sha) return fail("Empreinte SHA-256 incorrecte : " + path);
        listed.insert(path);
        r.totalSize += data->size();
        r.files[path] = std::move(*data);
    }
    for (const auto& e : entries) {
        if (e.directory) continue;
        std::string rel = e.name.substr(root.size());
        if (rel != "manifest.json" && !listed.count(rel)) r.warnings.push_back("Fichier non déclaré ignoré : " + rel);
    }
    r.fileCount = r.files.size();
    r.checks.push_back(std::to_string(r.fileCount) + " fichiers vérifiés (tailles et empreintes SHA-256)");

    // ---------------------------------------------------- level & defs
    auto lit = r.files.find("level.json");
    if (lit == r.files.end()) return fail("level.json manquant");
    auto lj = parseJson(lit->second);
    std::string lerr;
    if (!lj) return fail("level.json n'est pas du JSON valide");
    r.level = LevelData::fromJson(*lj, &lerr);
    if (!r.level) return fail("Niveau invalide : " + lerr);
    if (r.level->id != r.levelId) return fail("L'identifiant du niveau ne correspond pas au manifeste");
    r.checks.push_back("Niveau valide : " + std::to_string(r.level->objects.size()) + " objets, " +
                       std::to_string(r.level->events.size()) + " événements");
    for (auto& [path, data] : r.files) {
        if (path.rfind("enemies/", 0) == 0) {
            auto j = parseJson(data);
            if (!j || jstr(*j, "format") != "cosmic.enemy") return fail("Définition d'ennemi invalide : " + path);
            r.enemies.push_back(EnemyDefinition::fromJson(*j));
        } else if (path.rfind("powers/", 0) == 0) {
            auto j = parseJson(data);
            if (!j || jstr(*j, "format") != "cosmic.power") return fail("Définition de pouvoir invalide : " + path);
            PowerDefinition pd = PowerDefinition::fromJson(*j);
            r.powers.push_back(pd);
        } else if (path == "planet.json") {
            auto j = parseJson(data);
            if (!j) return fail("planet.json invalide");
            r.planet = PlanetData::fromJson(*j);
        } else if (path == "metadata/thumbnail.png") {
            r.thumbnail = data;
        } else if (path.rfind("assets/", 0) != 0 && path.rfind("effects/", 0) != 0 && path.rfind("metadata/", 0) != 0 &&
                   path != "level.json") {
            r.warnings.push_back("Fichier inattendu : " + path);
        }
    }
    // Every enemy used by the level must be provided (or exist locally).
    for (const auto& o : r.level->objects) {
        if (o.type != "enemy") continue;
        std::string eid = o.propStr("enemy");
        bool provided = std::any_of(r.enemies.begin(), r.enemies.end(), [&](const auto& d) { return d.id == eid; });
        if (!provided && !content_.enemy(eid)) return fail("Ennemi « " + eid + " » absent du package");
    }
    r.checks.push_back("Toutes les dépendances sont présentes (" + std::to_string(r.enemies.size()) + " ennemis, " +
                       std::to_string(r.powers.size()) + " pouvoirs)");

    // -------------------------------------------------------- conflicts
    if (content_.package(r.packageId))
        r.conflicts.push_back({ConflictKind::Package, r.packageId, "Ce package est déjà installé"});
    if (const LevelEntry* le = content_.level(r.levelId); le && le->packageId != r.packageId)
        r.conflicts.push_back({ConflictKind::Level, r.levelId, std::string("Un niveau « ") + le->name + " » utilise déjà cet identifiant"});
    for (const auto& d : r.enemies)
        if (const EnemyDefinition* ex = content_.enemy(d.id); ex && ex->toJson().dump() != d.toJson().dump())
            r.conflicts.push_back({ConflictKind::Enemy, d.id, "Un ennemi différent porte déjà cet identifiant"});
    for (const auto& d : r.powers)
        if (const PowerDefinition* ex = content_.power(d.id); ex && ex->toJson().dump() != d.toJson().dump())
            r.conflicts.push_back({ConflictKind::Power, d.id, "Un pouvoir différent porte déjà cet identifiant"});
    r.valid = true;
    return r;
}

bool PackageImporter::install(PackageInspection& insp, ConflictPolicy policy, std::string* installedLevelId,
                              std::string* error) {
    if (!insp.valid || !insp.level) {
        if (error) *error = "package non valide";
        return false;
    }
    LevelData level = *insp.level;
    std::string pkgId = insp.packageId;
    std::map<std::string, std::string> enemyRename, powerRename;
    std::string suffix = FileUtil::slugify(pkgId).substr(0, 16);

    for (const auto& c : insp.conflicts) {
        switch (c.kind) {
            case ConflictKind::Package:
                if (policy == ConflictPolicy::Rename) {
                    int n = 2;
                    while (content_.package(pkgId + "_" + std::to_string(n))) ++n;
                    pkgId += "_" + std::to_string(n);
                }
                break;
            case ConflictKind::Level: {
                // Built-in and user levels are never overwritten by an import: always rename.
                int n = 2;
                std::string base = level.id + "_import";
                std::string id = base;
                while (content_.levelIdExists(id)) id = base + std::to_string(n++);
                level.id = id;
                break;
            }
            case ConflictKind::Enemy:
                if (policy == ConflictPolicy::Rename) enemyRename[c.id] = c.id + "__" + suffix;
                break;
            case ConflictKind::Power:
                if (policy == ConflictPolicy::Rename) powerRename[c.id] = c.id + "__" + suffix;
                break;
        }
    }
    // Rewrite references to renamed definitions.
    for (auto& o : level.objects) {
        if (o.type == "enemy") {
            auto it = enemyRename.find(o.propStr("enemy"));
            if (it != enemyRename.end()) o.props["enemy"] = it->second;
        }
        if (o.type == "power_unlock") {
            auto it = powerRename.find(o.propStr("power"));
            if (it != powerRename.end()) o.props["power"] = it->second;
        }
    }
    for (auto& p : level.startPowers)
        if (powerRename.count(p)) p = powerRename[p];
    for (auto& e : level.events)
        for (auto& a : e.actions)
            if (a.type == "givePower" && powerRename.count(jstr(a.params, "power")))
                a.params["power"] = powerRename[jstr(a.params, "power")];

    // Extract into a temporary folder, then move it in place (atomic install).
    fs::path dest = Paths::packagesDir() / Paths::fromUtf8(pkgId);
    fs::path tmp = Paths::packagesDir() / Paths::fromUtf8(".installing_" + pkgId);
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    auto writeFile = [&](const std::string& rel, const std::vector<uint8_t>& data) {
        if (!Paths::isSafeRelative(rel)) return false;
        return FileUtil::writeBinary(tmp / Paths::fromUtf8(rel), data);
    };
    auto writeJson = [&](const std::string& rel, const Json& j) {
        std::string s = j.dump(1);
        return writeFile(rel, std::vector<uint8_t>(s.begin(), s.end()));
    };
    bool ok = true;
    for (const auto& [path, data] : insp.files) {
        if (path == "level.json" || path == "planet.json" || path.rfind("enemies/", 0) == 0 || path.rfind("powers/", 0) == 0)
            continue;
        ok = ok && writeFile(path, data);
    }
    std::optional<PlanetData> planet = insp.planet;
    if (planet) {
        if (planet->id.empty()) planet->id = level.planet;
        planet->levels = {level.id};
        level.planet = planet->id;
        ok = ok && writeJson("planets/" + FileUtil::slugify(planet->id) + ".json", planet->toJson());
    }
    ok = ok && writeJson("level.json", level.toJson());
    for (auto d : insp.enemies) {
        if (enemyRename.count(d.id)) d.id = enemyRename[d.id];
        for (auto& a : d.attacks)
            if (enemyRename.count(a.summonId)) a.summonId = enemyRename[a.summonId];
        ok = ok && writeJson("enemies/" + FileUtil::slugify(d.id) + ".json", d.toJson());
    }
    for (auto d : insp.powers) {
        if (powerRename.count(d.id)) d.id = powerRename[d.id];
        ok = ok && writeJson("powers/" + FileUtil::slugify(d.id) + ".json", d.toJson());
    }
    Json manifest = insp.manifest;
    manifest["packageId"] = pkgId;
    manifest["levelId"] = level.id;
    manifest["installedAt"] = FileUtil::nowString();
    manifest["sourceFile"] = Paths::toUtf8(insp.file.filename());
    ok = ok && writeJson("manifest.json", manifest);
    if (!ok) {
        fs::remove_all(tmp, ec);
        if (error) *error = "écriture des fichiers impossible";
        return false;
    }
    if (fs::exists(dest, ec)) fs::remove_all(dest, ec);
    fs::rename(tmp, dest, ec);
    if (ec) {
        fs::remove_all(tmp, ec);
        if (error) *error = "installation impossible : " + ec.message();
        return false;
    }
    if (installedLevelId) *installedLevelId = level.id;
    Log::info("Package installed: {} -> {}", pkgId, Paths::toUtf8(dest));
    return true;
}

}  // namespace cosmic
