#include "game/content/BossStories.h"

#include <algorithm>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/records/BossStats.h"

namespace cosmic {

namespace {
Color hexOr(const Json& j, const char* key, Color def) {
    std::string h = jstr(j, key);
    return h.empty() ? def : Color::fromHex(h, def);
}
}  // namespace

std::optional<BossStory> BossStory::fromJson(const Json& j, std::string* error) {
    auto fail = [&](const std::string& why) -> std::optional<BossStory> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (!j.is_object()) return fail("not an object");
    BossStory s;
    s.bossId = jstr(j, "boss");
    if (s.bossId.empty()) return fail("no boss");
    s.title = jstr(j, "title");
    s.color = hexOr(j, "color", Color::white());
    s.accent = hexOr(j, "accent", s.color);
    s.clip = jstr(j, "clip", "roar");
    if (!j.contains("shots") || !j["shots"].is_array()) return fail("no shots");
    for (const auto& x : j["shots"]) {
        StoryShot shot;
        shot.scene = jstr(x, "scene");
        shot.image = jstr(x, "image");
        shot.text = jstr(x, "text");
        if (shot.text.empty()) return fail("a shot without text");
        if (x.contains("focus")) shot.focus = vec2FromJson(x["focus"], shot.focus);
        shot.focus.x = std::clamp(shot.focus.x, 0.0f, 1.0f);
        shot.focus.y = std::clamp(shot.focus.y, 0.0f, 1.0f);
        shot.zoom = std::clamp(jget<float>(x, "zoom", shot.zoom), 1.0f, 1.5f);
        shot.particles = jstr(x, "particles", shot.particles);
        shot.glow = hexOr(x, "glow", s.color);
        s.shots.push_back(std::move(shot));
    }
    if (s.shots.empty()) return fail("no shots");
    return s;
}

namespace story {

size_t characters(std::string_view text) {
    size_t n = 0;
    for (unsigned char c : text) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

float shotDuration(std::string_view text) {
    return std::max(kMinShot, kShotBase + static_cast<float>(characters(text)) / kReadRate);
}

float duration(const BossStory& s) {
    float t = kTitleCard;
    for (const auto& shot : s.shots) t += shotDuration(shot.text);
    return t;
}

}  // namespace story

bool isGigaBoss(const EnemyDefinition& d) {
    return d.isBoss && d.variantOf.empty() && static_cast<int>(d.phases.size()) >= story::kGigaPhases;
}

bool BossStories::load(const std::filesystem::path& dir, std::string* error) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return true;  // no story: nothing to tell
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    bool ok = true;
    for (const auto& f : files) {
        std::string why = "unreadable";
        std::optional<BossStory> s;
        if (auto j = FileUtil::loadJson(f)) s = BossStory::fromJson(*j, &why);
        if (!s) {
            ok = false;
            if (error) *error += (error->empty() ? "" : "; ") + Paths::toUtf8(f.filename()) + ": " + why;
            continue;
        }
        add(std::move(*s));
    }
    return ok;
}

bool BossStories::loadDefault(std::string* error) { return load(Paths::dataDir() / "stories", error); }

void BossStories::add(BossStory s) {
    std::string id = s.bossId;
    stories_[id] = std::move(s);
}

const BossStory* BossStories::find(const std::string& bossId) const {
    auto it = stories_.find(bossId);
    return it == stories_.end() ? nullptr : &it->second;
}

bool storyDue(const EnemyDefinition& boss, const BossStories& stories, const BossStats& stats, int setting, const StoryContext& ctx) {
    if (setting == static_cast<int>(StorySetting::Never)) return false;
    if (ctx.bossTraining || ctx.editorTest || ctx.scripted) return false;
    if (!isGigaBoss(boss) || !stories.find(boss.id)) return false;
    return !stats.storySeen(boss.id);
}

}  // namespace cosmic
