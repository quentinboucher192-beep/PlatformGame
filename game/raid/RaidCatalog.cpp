#include "game/raid/RaidCatalog.h"

#include <algorithm>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"

namespace cosmic::raid {

namespace {

CatalogArt artOf(const Json& j) {
    CatalogArt a;
    a.image = Paths::normalizeSlashes(jstr(j, "image"));
    const Json c = jfield(j, "crop");
    if (c.is_array() && c.size() == 4 && std::all_of(c.begin(), c.end(), [](const Json& v) { return v.is_number(); })) {
        a.crop = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>()};
        if (a.crop.w <= 0.0f || a.crop.h <= 0.0f || a.crop.x < 0.0f || a.crop.y < 0.0f) a.crop = {};
    }
    return a;
}

}  // namespace

int CatalogEntry::formatIndex(const std::string& formatId) const {
    for (size_t i = 0; i < formats.size(); ++i)
        if (formats[i].id == formatId) return static_cast<int>(i);
    return -1;
}

std::string CatalogEntry::formatName(const std::string& formatId) const {
    const int i = formatIndex(formatId);
    return i < 0 ? formatId : formats[static_cast<size_t>(i)].name;
}

CatalogEntry parseCatalogEntry(const Json& j, bool& valid, std::string* problem) {
    CatalogEntry e;
    valid = false;
    auto fail = [&](const std::string& why) {
        if (problem) *problem = why;
        return e;
    };
    if (jstr(j, "format") != "cosmic.raid_body") return fail("format");
    const Json c = jfield(j, "catalog");
    if (!c.is_object()) return fail("catalog");
    e.id = jstr(j, "raid");
    e.title = jstr(c, "title");
    e.name = jstr(c, "name");
    e.subtitle = jstr(c, "subtitle");
    e.pitch = jstr(c, "description");
    e.sheet = jstr(c, "sheet");
    e.order = jget<int>(c, "order", 0);
    e.icon = artOf(jfield(c, "icon"));
    e.poster = artOf(jfield(c, "poster"));
    for (const Json& f : jfield(c, "formats", Json::array())) {
        CatalogFormat cf;
        cf.id = jstr(f, "id");
        cf.name = jstr(f, "name", cf.id);
        const Json m = jfield(f, "minutes");
        if (m.is_array() && m.size() == 2 && m[0].is_number_integer() && m[1].is_number_integer()) {
            cf.minMinutes = m[0].get<int>();
            cf.maxMinutes = m[1].get<int>();
        }
        if (!cf.id.empty() && e.formatIndex(cf.id) < 0) e.formats.push_back(std::move(cf));
    }
    e.defaultFormat = std::max(0, e.formatIndex(jstr(c, "default_format")));
    const Json d = jfield(c, "difficulty");
    e.difficultyMin = std::max(1, jget<int>(d, "lowest", 1));
    e.difficultyMax = std::max(e.difficultyMin, jget<int>(d, "highest", 5));
    e.difficultyDefault = std::clamp(jget<int>(d, "default", e.difficultyMin), e.difficultyMin, e.difficultyMax);
    // Its counts: the parts of the same file (every part, those with a gigaboss, its heart among them).
    const std::string heartPart = jstr(jfield(c, "heart"), "part");
    for (const Json& p : jfield(j, "parts", Json::array())) {
        if (jstr(p, "id").empty()) continue;
        ++e.parts;
        if (!jstr(p, "boss").empty()) {
            ++e.gigaboss;
            e.gigabossParts.push_back(jstr(p, "name", jstr(p, "id")));
        }
        if (!heartPart.empty() && jstr(p, "id") == heartPart) e.hasHeart = true;
    }
    if (e.hasHeart) e.heartLabel = jstr(jfield(c, "heart"), "label");
    if (e.id.empty() || e.title.empty() || e.name.empty()) return fail("raid/title/name");
    if (e.formats.empty()) return fail("formats");
    valid = true;
    return e;
}

std::vector<CatalogEntry> loadCatalog(const std::filesystem::path& dir, std::vector<std::string>* problems) {
    std::vector<CatalogEntry> out;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && FileUtil::extensionLower(it->path()) == ".json") files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        const auto j = FileUtil::loadJson(f);
        if (!j || !j->is_object() || !j->contains("catalog")) continue;  // films, sheets…: not raids of the catalogue
        bool valid = false;
        std::string problem;
        CatalogEntry e = parseCatalogEntry(*j, valid, &problem);
        if (!valid) {
            if (problems) problems->push_back(Paths::toUtf8(f.filename()) + ": " + problem);
            continue;
        }
        if (std::any_of(out.begin(), out.end(), [&](const CatalogEntry& o) { return o.id == e.id; })) {
            if (problems) problems->push_back(Paths::toUtf8(f.filename()) + ": " + e.id);
            continue;
        }
        e.file = "raids/" + Paths::toUtf8(f.filename());
        out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(), [](const CatalogEntry& a, const CatalogEntry& b) { return a.order != b.order ? a.order < b.order : a.id < b.id; });
    return out;
}

const std::vector<CatalogEntry>& catalog() {
    static const std::vector<CatalogEntry> c = loadCatalog(Paths::dataDir() / "raids");
    return c;
}

const CatalogEntry* findRaid(const std::string& id) {
    for (const CatalogEntry& e : catalog())
        if (e.id == id) return &e;
    return nullptr;
}

}  // namespace cosmic::raid
