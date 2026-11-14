#include "packaging/PackageExporter.h"

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "packaging/Sha256.h"
#include "packaging/ZipArchive.h"

namespace cosmic {

size_t ExportPlan::countKind(const std::string& kind) const {
    size_t n = 0;
    for (const auto& i : items) n += i.kind == kind ? 1 : 0;
    return n;
}

std::string PackageExporter::makePackageId(const LevelData& level, const std::string& author) {
    std::string a = FileUtil::slugify(author.empty() ? "anonyme" : author);
    return FileUtil::slugify(level.id) + "-" + a;
}

namespace {
std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }
}  // namespace

ExportPlan PackageExporter::plan(const LevelData& level, const ExportOptions& opt, const fs::path& assetRoot) {
    ExportPlan p;
    p.packageId = makePackageId(level, opt.author);
    if (!assetRoot.empty()) res_.pushMount(assetRoot);
    DependencyCollector collector(content_, res_);
    DependencySet deps = collector.collect(level);
    p.warnings = deps.warnings;

    // Level (with the export metadata).
    LevelData exported = level;
    if (!opt.author.empty()) exported.author = opt.author;
    if (!opt.description.empty()) exported.description = opt.description;
    exported.version = opt.version;
    p.items.push_back({"level.json", {}, bytes(exported.toJson().dump(1)), "level"});

    if (const PlanetData* planet = content_.planet(level.planet)) {
        Json pj = planet->toJson();
        pj["levels"] = Json::array({level.id});
        p.items.push_back({"planet.json", {}, bytes(pj.dump(1)), "planet"});
    } else {
        p.warnings.push_back("Planète inconnue : " + level.planet + " (le niveau sera rattaché à Sylvanox)");
    }
    for (const auto& id : deps.enemies)
        if (const EnemyDefinition* d = content_.enemy(id))
            p.items.push_back({"enemies/" + FileUtil::slugify(id) + ".json", {}, bytes(d->toJson().dump(1)), "enemy", 0, id});
    for (const auto& id : deps.powers)
        if (const PowerDefinition* d = content_.power(id))
            p.items.push_back({"powers/" + FileUtil::slugify(id) + ".json", {}, bytes(d->toJson().dump(1)), "power", 0, id});
    Json effects = {{"format", "cosmic.effects"}, {"presets", content_.effects()}};
    p.items.push_back({"effects/effects.json", {}, bytes(effects.dump(1)), "effects"});

    // Assets: resolved through the mounts (package enemies have their own root).
    for (const auto& rel : deps.assets) {
        std::optional<fs::path> src = res_.resolve(rel);
        if (!src) {
            for (const auto& id : deps.enemies) {
                const EnemyDefinition* d = content_.enemy(id);
                if (d && !d->assetRoot.empty() && FileUtil::exists(d->assetRoot / Paths::fromUtf8(rel))) {
                    src = d->assetRoot / Paths::fromUtf8(rel);
                    break;
                }
            }
        }
        if (!src) {
            p.missing.push_back(rel);
            continue;
        }
        if (!PackageFormat::allowedExtension(rel)) {
            p.warnings.push_back("Type de fichier non autorisé ignoré : " + rel);
            continue;
        }
        if (opt.skipInstalledAssets) {
            // Installed with the game (installed files always win over packages anyway).
            std::error_code ec;
            fs::path rel2 = fs::relative(*src, Paths::assetsDir(), ec);
            if (!ec && !rel2.empty() && rel2.native().find(Paths::fromUtf8("..").native()) != 0) continue;
        }
        ExportItem item;
        item.archivePath = "assets/" + rel;
        item.source = *src;
        item.kind = "asset";
        std::error_code ec;
        item.size = fs::file_size(*src, ec);
        p.items.push_back(std::move(item));
    }
    if (!assetRoot.empty()) res_.popMount();

    if (!opt.thumbnailPng.empty()) p.items.push_back({"metadata/thumbnail.png", {}, opt.thumbnailPng, "metadata"});
    Json info = {{"exportedWith", "Cosmic Platformer " + PackageFormat::engineVersion()},
                 {"exportedAt", FileUtil::nowString()},
                 {"objects", level.objects.size()},
                 {"events", level.events.size()}};
    p.items.push_back({"metadata/info.json", {}, bytes(info.dump(1)), "metadata"});

    for (auto& i : p.items) {
        if (!i.data.empty() || i.source.empty()) i.size = i.data.size();
        p.totalSize += i.size;
    }
    return p;
}

bool PackageExporter::write(const LevelData& level, const ExportPlan& plan, const ExportOptions& opt, std::string* error) {
    if (!plan.ok()) {
        if (error) *error = std::to_string(plan.missing.size()) + " fichier(s) manquant(s)";
        return false;
    }
    if (plan.totalSize > PackageFormat::kMaxTotalSize) {
        if (error) *error = "package trop volumineux";
        return false;
    }
    ZipWriter zip;
    Json files = Json::array();
    uint64_t total = 0;
    for (const auto& item : plan.items) {
        std::vector<uint8_t> data = item.data;
        if (!item.source.empty()) {
            auto d = FileUtil::readBinary(item.source);
            if (!d) {
                if (error) *error = "lecture impossible : " + Paths::toUtf8(item.source);
                return false;
            }
            data = std::move(*d);
        }
        // Already-compressed media are stored as is.
        std::string ext = FileUtil::extensionLower(Paths::fromUtf8(item.archivePath));
        bool compress = ext != ".png" && ext != ".ogg" && ext != ".jpg" && ext != ".mp3";
        if (!zip.add(PackageFormat::kRoot + item.archivePath, data, compress)) {
            if (error) *error = "ajout impossible : " + item.archivePath;
            return false;
        }
        files.push_back({{"path", item.archivePath}, {"size", data.size()}, {"sha256", Sha256::hex(data)}, {"kind", item.kind}});
        total += data.size();
    }
    std::vector<std::string> enemies, powers;
    for (const auto& i : plan.items) {
        if (i.kind == "enemy") enemies.push_back(i.id);
        if (i.kind == "power") powers.push_back(i.id);
    }
    Json manifest = {{"format", "cosmic.package"},
                     {"formatVersion", PackageFormat::kFormatVersion},
                     {"packageId", plan.packageId},
                     {"name", opt.packageName.empty() ? level.name : opt.packageName},
                     {"levelId", level.id},
                     {"author", opt.author.empty() ? level.author : opt.author},
                     {"version", opt.version},
                     {"engineVersion", PackageFormat::engineVersion()},
                     {"minEngineVersion", opt.skipInstalledAssets ? PackageFormat::engineVersion() : std::string("1.0.0")},
                     {"description", opt.description.empty() ? level.description : opt.description},
                     {"createdAt", FileUtil::nowString()},
                     {"planet", level.planet},
                     {"dependencies", {{"enemies", enemies}, {"powers", powers}, {"planet", level.planet}}},
                     {"files", files},
                     {"totalSize", total}};
    zip.add(std::string(PackageFormat::kRoot) + "manifest.json", manifest.dump(2));
    std::string err;
    if (!zip.save(opt.destination, &err)) {
        if (error) *error = err;
        return false;
    }
    Log::info("Package exported: {} ({} files, {} bytes)", Paths::toUtf8(opt.destination), files.size(), total);
    return true;
}

}  // namespace cosmic
