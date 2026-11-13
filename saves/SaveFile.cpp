#include "saves/SaveFile.h"

#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"

namespace cosmic {

std::optional<Json> SaveFile::migrate(const Json& envelope, int* fromVersion) const {
    if (!envelope.is_object()) return std::nullopt;
    std::string format = jstr(envelope, "format");
    if (format != "cosmic." + kind_) return std::nullopt;
    int v = jget<int>(envelope, "version", 0);
    if (fromVersion) *fromVersion = v;
    if (v < 1) return std::nullopt;
    if (v > version_) {
        Log::warn("{} save is from a newer version ({} > {}); reading what is understood", kind_, v, version_);
        return envelope.value("data", Json::object());
    }
    Json data = envelope.value("data", Json::object());
    for (int cur = v; cur < version_; ++cur) {
        size_t idx = static_cast<size_t>(cur - 1);
        if (idx >= migrations_.size() || !migrations_[idx]) {
            Log::error("No migration for {} v{} -> v{}", kind_, cur, cur + 1);
            return std::nullopt;
        }
        data = migrations_[idx](data);
        Log::info("Migrated {} save v{} -> v{}", kind_, cur, cur + 1);
    }
    return data;
}

std::optional<Json> SaveFile::load(const fs::path& path, bool* migrated) const {
    if (migrated) *migrated = false;
    if (!FileUtil::exists(path)) return std::nullopt;
    auto text = FileUtil::readText(path);
    Json env;
    bool ok = false;
    if (text) {
        try {
            env = Json::parse(*text);
            ok = true;
        } catch (const Json::exception& e) {
            Log::error("Corrupt save {}: {}", Paths::toUtf8(path), e.what());
        }
    }
    int from = 0;
    std::optional<Json> data = ok ? migrate(env, &from) : std::nullopt;
    if (!data) {
        std::error_code ec;
        fs::path bad = path;
        bad += ".corrupt";
        fs::rename(path, bad, ec);
        Log::error("Save {} unreadable; moved to {}", Paths::toUtf8(path), Paths::toUtf8(bad));
        return std::nullopt;
    }
    if (from < version_) {
        std::error_code ec;
        fs::path bak = path;
        bak += ".v" + std::to_string(from) + ".bak";
        fs::copy_file(path, bak, fs::copy_options::overwrite_existing, ec);
        if (migrated) *migrated = true;
    }
    return data;
}

bool SaveFile::save(const fs::path& path, const Json& data) const {
    Json env;
    env["format"] = "cosmic." + kind_;
    env["version"] = version_;
    env["savedAt"] = FileUtil::nowString();
    env["engine"] = COSMIC_VERSION_STRING;
    env["data"] = data;
    return FileUtil::saveJson(path, env);
}

}  // namespace cosmic
