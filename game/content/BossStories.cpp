#include "game/content/BossStories.h"

#include <algorithm>
#include <cmath>
#include <set>
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

float finiteOr(float v, float def) { return std::isfinite(v) ? v : def; }

std::optional<float> optFloat(const Json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number()) return std::nullopt;
    const float v = j[key].get<float>();
    return std::isfinite(v) ? std::optional<float>(v) : std::nullopt;
}

StoryEase easeOf(const std::string& e) {
    if (e == "linear") return StoryEase::Linear;
    if (e == "in") return StoryEase::In;
    if (e == "out") return StoryEase::Out;
    if (e == "step") return StoryEase::Step;
    return StoryEase::InOut;
}

/// The keys of an element or of the camera, sorted by time (a key without a time is refused).
bool keysFromJson(const Json& j, std::vector<StoryKey>& out) {
    if (!j.is_array()) return false;
    for (const auto& k : j) {
        if (!k.is_object() || !k.contains("t") || !k["t"].is_number()) return false;
        StoryKey key;
        key.t = std::max(0.0f, finiteOr(k["t"].get<float>(), 0.0f));
        if (k.contains("pos") && k["pos"].is_array() && k["pos"].size() == 2) {
            const Vec2 p = vec2FromJson(k["pos"]);
            if (std::isfinite(p.x) && std::isfinite(p.y)) key.pos = p;
        }
        key.scale = optFloat(k, "scale");
        if (!key.scale) key.scale = optFloat(k, "zoom");
        if (key.scale) key.scale = std::clamp(*key.scale, 0.01f, 20.0f);
        key.alpha = optFloat(k, "alpha");
        if (key.alpha) key.alpha = std::clamp(*key.alpha, 0.0f, 1.0f);
        key.rot = optFloat(k, "rot");
        if (k.contains("flip") && k["flip"].is_boolean()) key.flip = k["flip"].get<bool>();
        key.clip = jstr(k, "clip");
        key.offset = std::max(0.0f, finiteOr(jget<float>(k, "offset", 0.0f), 0.0f));
        key.ease = easeOf(jstr(k, "ease", "inout"));
        out.push_back(std::move(key));
    }
    std::stable_sort(out.begin(), out.end(), [](const StoryKey& a, const StoryKey& b) { return a.t < b.t; });
    return true;
}
}  // namespace

float storyEase(StoryEase e, float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    switch (e) {
        case StoryEase::Linear: return u;
        case StoryEase::In: return u * u;
        case StoryEase::Out: return 1.0f - (1.0f - u) * (1.0f - u);
        case StoryEase::Step: return u >= 1.0f ? 1.0f : 0.0f;
        case StoryEase::InOut: break;
    }
    return u * u * (3.0f - 2.0f * u);
}

StoryElementState storyStateAt(const std::vector<StoryKey>& keys, float t) {
    StoryElementState cur;
    auto apply = [](StoryElementState s, const StoryKey& k) {
        if (k.pos) s.pos = *k.pos;
        if (k.scale) s.scale = *k.scale;
        if (k.alpha) s.alpha = *k.alpha;
        if (k.rot) s.rot = *k.rot;
        if (k.flip) s.flip = *k.flip;
        return s;
    };
    int clipKey = -1;
    float start = 0.0f, offset = 0.0f;
    for (size_t i = 0; i < keys.size(); ++i) {
        const StoryKey& k = keys[i];
        StoryElementState next = apply(cur, k);
        if (k.t <= t) {
            if (!k.clip.empty()) {
                clipKey = static_cast<int>(i);
                start = k.t;
                offset = k.offset;
            }
            cur = next;
            continue;
        }
        if (i > 0) {  // between the key before and this one
            const float u = (t - keys[i - 1].t) / std::max(1e-6f, k.t - keys[i - 1].t);
            const float e = storyEase(k.ease, u);
            cur.pos = lerpv(cur.pos, next.pos, e);
            cur.scale = lerpf(cur.scale, next.scale, e);
            cur.alpha = lerpf(cur.alpha, next.alpha, e);
            cur.rot = lerpf(cur.rot, next.rot, e);
        } else {
            cur = next;  // before the first key: its values
        }
        break;
    }
    if (clipKey < 0) {  // before its first clip starts: that clip's first frame
        for (size_t i = 0; i < keys.size(); ++i)
            if (!keys[i].clip.empty()) {
                clipKey = static_cast<int>(i);
                start = t;
                offset = keys[i].offset;
                break;
            }
    }
    cur.clipKey = clipKey;
    if (clipKey >= 0) cur.clip = keys[static_cast<size_t>(clipKey)].clip;
    cur.clipTime = std::max(0.0f, t - start + offset);
    return cur;
}

std::optional<StoryStage> StoryStage::fromJson(const Json& j, std::string* error) {
    auto fail = [&](const std::string& why) -> std::optional<StoryStage> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (!j.is_object()) return fail("a stage that is not an object");
    StoryStage st;
    if (j.contains("size")) st.size = vec2FromJson(j["size"], st.size);
    if (!(st.size.x >= 16.0f && st.size.y >= 16.0f && st.size.x <= 8192.0f && st.size.y <= 8192.0f)) return fail("a stage of a bad size");
    st.backdrop = hexOr(j, "backdrop", st.backdrop);
    if (j.contains("camera") && !keysFromJson(j["camera"], st.camera)) return fail("bad camera keys");
    if (!st.camera.empty() && !st.camera.front().pos) st.camera.front().pos = st.size * 0.5f;  // from the middle
    if (j.contains("shake") && j["shake"].is_array())
        for (const auto& s : j["shake"]) {
            StoryShake sh;
            sh.from = std::max(0.0f, finiteOr(jget<float>(s, "from", 0.0f), 0.0f));
            sh.to = std::max(sh.from, finiteOr(jget<float>(s, "to", sh.from), sh.from));
            sh.amount = std::clamp(finiteOr(jget<float>(s, "amount", 0.0f), 0.0f), 0.0f, 100.0f);
            st.shakes.push_back(sh);
        }
    if (j.contains("flash") && j["flash"].is_array())
        for (const auto& f : j["flash"]) {
            StoryFlash fl;
            fl.t = std::max(0.0f, finiteOr(jget<float>(f, "t", 0.0f), 0.0f));
            fl.in = std::clamp(finiteOr(jget<float>(f, "in", fl.in), fl.in), 0.01f, 10.0f);
            fl.out = std::clamp(finiteOr(jget<float>(f, "out", fl.out), fl.out), 0.01f, 10.0f);
            fl.color = hexOr(f, "color", fl.color);
            fl.alpha = std::clamp(finiteOr(jget<float>(f, "alpha", fl.alpha), fl.alpha), 0.0f, 1.0f);
            st.flashes.push_back(fl);
        }
    if (j.contains("sfx") && j["sfx"].is_array())  // 2.63: its sounds
        for (const auto& s : j["sfx"]) {
            if (!s.is_object()) return fail("a sound that is not an object");
            StorySfx fx;
            fx.sound = jstr(s, "sound");
            if (fx.sound.empty()) return fail("a sound without its file");
            fx.t = std::max(0.0f, finiteOr(jget<float>(s, "t", 0.0f), 0.0f));
            fx.volume = std::clamp(finiteOr(jget<float>(s, "volume", fx.volume), fx.volume), 0.0f, 1.0f);
            fx.pitch = std::clamp(finiteOr(jget<float>(s, "pitch", 1.0f), 1.0f), 0.25f, 4.0f);
            if (s.contains("pan") && s["pan"].is_number()) fx.pan = std::clamp(finiteOr(s["pan"].get<float>(), 0.0f), -1.0f, 1.0f);
            if (s.contains("at") && s["at"].is_array() && s["at"].size() == 2) fx.at = vec2FromJson(s["at"]);
            fx.loop = jget<bool>(s, "loop", false);
            fx.to = std::max(0.0f, finiteOr(jget<float>(s, "to", 0.0f), 0.0f));
            if (fx.to > 0.0f && fx.to < fx.t) return fail("a sound that ends before it starts");
            fx.fade = std::clamp(finiteOr(jget<float>(s, "fade", fx.fade), fx.fade), 0.0f, 10.0f);
            fx.duck = jget<bool>(s, "duck", false);
            st.sounds.push_back(std::move(fx));
        }
    if (!j.contains("elements") || !j["elements"].is_array()) return fail("a stage without elements");
    for (const auto& e : j["elements"]) {
        if (!e.is_object()) return fail("an element that is not an object");
        StoryElement el;
        el.id = jstr(e, "id");
        if (e.contains("layer")) {
            el.kind = StoryElementKind::Layer;
            el.source = jstr(e, "layer");
        } else if (e.contains("sprite")) {
            el.kind = StoryElementKind::Sprite;
            el.source = jstr(e, "sprite");
        } else if (e.contains("glow")) {
            el.kind = StoryElementKind::Glow;
            el.color = hexOr(e, "glow", el.color);
        } else if (e.contains("particles")) {
            el.kind = StoryElementKind::Particles;
            el.source = jstr(e, "particles");
            el.color = hexOr(e, "color", el.color);
        } else {
            return fail("element '" + el.id + "': neither a layer, a sprite, a glow nor particles");
        }
        if ((el.kind == StoryElementKind::Layer || el.kind == StoryElementKind::Sprite || el.kind == StoryElementKind::Particles) &&
            el.source.empty())
            return fail("element '" + el.id + "' without its file");
        el.depth = std::clamp(finiteOr(jget<float>(e, "depth", 1.0f), 1.0f), 0.0f, 4.0f);
        el.additive = jstr(e, "blend") == "add";
        el.tint = hexOr(e, "tint", Color::white());
        el.radius = std::clamp(finiteOr(jget<float>(e, "radius", el.radius), el.radius), 1.0f, 4000.0f);
        if (e.contains("area") && e["area"].is_array() && e["area"].size() == 4) {
            const auto& a = e["area"];
            if (a[0].is_number() && a[1].is_number() && a[2].is_number() && a[3].is_number())
                el.area = {a[0].get<float>(), a[1].get<float>(), std::max(1.0f, a[2].get<float>()), std::max(1.0f, a[3].get<float>())};
        }
        if (e.contains("drift")) el.drift = vec2FromJson(e["drift"]);
        if (!std::isfinite(el.drift.x) || !std::isfinite(el.drift.y)) el.drift = {};
        el.repeat = jget<bool>(e, "repeat", false);
        if (e.contains("pulse") && e["pulse"].is_array() && e["pulse"].size() >= 2 && e["pulse"][0].is_number() &&
            e["pulse"][1].is_number()) {
            el.pulse = std::clamp(finiteOr(e["pulse"][0].get<float>(), 0.0f), 0.0f, 1.0f);
            el.pulsePeriod = std::clamp(finiteOr(e["pulse"][1].get<float>(), 2.0f), 0.05f, 60.0f);
            if (e["pulse"].size() >= 3 && e["pulse"][2].is_number()) el.pulsePhase = finiteOr(e["pulse"][2].get<float>(), 0.0f);
        }
        if (e.contains("shadow") && e["shadow"].is_object()) {
            const Json& s = e["shadow"];
            el.shadow.on = true;
            el.shadow.alpha = std::clamp(finiteOr(jget<float>(s, "alpha", el.shadow.alpha), el.shadow.alpha), 0.0f, 1.0f);
            if (s.contains("size")) el.shadow.size = vec2FromJson(s["size"], el.shadow.size);
            if (!(el.shadow.size.x > 0.0f && el.shadow.size.y > 0.0f && el.shadow.size.x < 4000.0f && el.shadow.size.y < 4000.0f))
                el.shadow.size = {60.0f, 12.0f};
            if (s.contains("light") && s["light"].is_array() && s["light"].size() == 2) el.shadow.light = vec2FromJson(s["light"]);
        }
        if (!e.contains("keys") || !keysFromJson(e["keys"], el.keys) || el.keys.empty())
            return fail("element '" + el.id + "' without keys");
        st.elements.push_back(std::move(el));
    }
    if (st.elements.empty()) return fail("a stage without elements");
    return st;
}

float StoryElement::pulseAt(float t) const {
    if (pulse <= 0.0f) return 1.0f;
    const float u = t / std::max(0.05f, pulsePeriod) + pulsePhase;
    return 1.0f - pulse * (0.5f - 0.5f * std::cos(2.0f * kPi * u));
}

std::pair<Vec2, float> StoryStage::cameraAt(float t) const {
    if (camera.empty()) return {size * 0.5f, 1.0f};
    const StoryElementState s = storyStateAt(camera, t);
    return {s.pos, std::max(0.05f, s.scale)};
}

Vec2 StoryStage::clampCamera(Vec2 pos, Vec2 view) const {
    auto keep = [](float p, float half, float len) { return half * 2.0f >= len ? len * 0.5f : std::clamp(p, half, len - half); };
    return {keep(pos.x, view.x * 0.5f, size.x), keep(pos.y, view.y * 0.5f, size.y)};
}

Vec2 StoryStage::shakeAt(float t) const {
    Vec2 o{};
    for (const StoryShake& s : shakes) {
        if (t < s.from || t > s.to || s.amount <= 0.0f) continue;
        const float env = std::min({1.0f, (t - s.from) / 0.08f, (s.to - t) / 0.25f});
        o.x += s.amount * env * (0.6f * std::sin(t * 53.0f) + 0.4f * std::sin(t * 31.0f + 1.0f));
        o.y += s.amount * env * (0.6f * std::cos(t * 47.0f) + 0.4f * std::sin(t * 29.0f + 2.0f));
    }
    return o;
}

float StoryStage::flashAt(float t, Color* color) const {
    float best = 0.0f;
    for (const StoryFlash& f : flashes) {
        const float u = t - f.t;
        float a = 0.0f;
        if (u < 0.0f && u >= -f.in) a = 1.0f + u / f.in;
        else if (u >= 0.0f && u <= f.out) a = 1.0f - u / f.out;
        a *= f.alpha;
        if (a > best) {
            best = a;
            if (color) *color = f.color;
        }
    }
    return best;
}

float StoryStage::lastTime() const {
    float t = 0.0f;
    for (const StoryKey& k : camera) t = std::max(t, k.t);
    for (const StoryShake& s : shakes) t = std::max(t, s.to);
    for (const StoryFlash& f : flashes) t = std::max(t, f.t);
    for (const StoryElement& e : elements)
        for (const StoryKey& k : e.keys) t = std::max(t, k.t);
    for (const StorySfx& s : sounds) t = std::max({t, s.t, s.to});
    return t;
}

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
    s.kind = jstr(j, "kind", "story");
    s.music = jstr(j, "music");
    s.titleCard = std::clamp(jget<float>(j, "titleCard", story::kTitleCard), 0.0f, 10.0f);
    s.version = jget<int>(j, "version", 1);
    if (s.version < 1 || s.version > story::kVersion) return fail("version " + std::to_string(s.version) + " not known");
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
        if (x.contains("duration")) shot.duration = std::clamp(jget<float>(x, "duration", 0.0f), 0.5f, 20.0f);
        shot.subtitle = jstr(x, "subtitle");
        if (s.version >= 2 && x.contains("stage")) {
            std::string why;
            shot.stage = StoryStage::fromJson(x["stage"], &why);
            if (!shot.stage) return fail("shot " + std::to_string(s.shots.size() + 1) + ": " + why);
        }
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

float shotLength(const StoryShot& shot) { return shot.duration > 0.0f ? shot.duration : shotDuration(shot.text); }

float duration(const BossStory& s) {
    float t = s.titleCard;
    for (const auto& shot : s.shots) t += shotLength(shot);
    return t;
}

std::optional<BossStory> loadTrailer(std::string* error) {
    auto j = FileUtil::loadJson(Paths::dataDir() / "trailer" / "trailer.json");
    if (!j) {
        if (error) *error = "data/trailer/trailer.json introuvable";
        return std::nullopt;
    }
    return BossStory::fromJson(*j, error);
}

StageAssets stageAssets(const BossStory& s) {
    StageAssets a;
    std::set<std::string> seen;
    for (const StoryShot& shot : s.shots) {
        if (!shot.stage) continue;
        for (const StoryElement& e : shot.stage->elements) {
            if (e.kind != StoryElementKind::Layer && e.kind != StoryElementKind::Sprite) continue;
            if (!seen.insert(e.source).second) continue;
            (e.kind == StoryElementKind::Layer ? a.textures : a.animations).push_back(e.source);
        }
        for (const StorySfx& s : shot.stage->sounds)
            if (seen.insert(s.sound).second) a.sounds.push_back(s.sound);
    }
    return a;
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
