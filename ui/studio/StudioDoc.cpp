#include "ui/studio/StudioDoc.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>

#include "engine/core/FileUtil.h"

namespace cosmic::studio {

namespace {

struct KindWords {
    DocKind kind;
    const char* id;
    const char* name;
    const char* word;
};
const KindWords kKinds[kDocKinds] = {
    {DocKind::Suit, "suit", "Combinaison", "combinaison"},
    {DocKind::Trail, "trail", "Traînée", "traînée"},
    {DocKind::Frame, "frame", "Cadre", "cadre"},
    {DocKind::Victory, "victory", "Effet de victoire", "effet de victoire"},
};

/// Nova's animations in French (the clips of animations/nova.json).
struct ClipName {
    const char* id;
    const char* name;
};
const ClipName kClipNames[] = {
    {"idle", "Attente"},          {"walk", "Marche"},
    {"run", "Course"},            {"jump", "Saut"},
    {"double_jump", "Double saut"}, {"fall", "Chute"},
    {"land", "Atterrissage"},     {"slide", "Glissade"},
    {"wall_slide", "Glissade au mur"}, {"climb", "Escalade"},
    {"attack", "Attaque"},        {"air_attack", "Attaque en l'air"},
    {"cast", "Sort"},             {"dash", "Ruée"},
    {"hurt", "Blessure"},         {"death", "Chute fatale"},
    {"respawn", "Retour"},        {"victory", "Victoire"},
    {"interact", "Interaction"},  {"glide", "Vol plané"},
    {"pound_dive", "Piqué"},      {"pound_land", "Impact au sol"},
    {"charge", "Charge"},         {"shoot", "Tir"},
    {"grapple", "Grappin"},       {"grapple_pull", "Traction"},
    {"crouch", "Accroupi"},
};

size_t codepoints(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

}  // namespace

const char* kindId(DocKind k) { return kKinds[static_cast<int>(k)].id; }
const char* kindName(DocKind k) { return kKinds[static_cast<int>(k)].name; }
const char* kindWord(DocKind k) { return kKinds[static_cast<int>(k)].word; }
std::optional<DocKind> kindFromId(const std::string& id) {
    for (const KindWords& k : kKinds)
        if (id == k.id) return k.kind;
    return std::nullopt;
}

std::string animName(const std::string& clipId) {
    for (const ClipName& c : kClipNames)
        if (clipId == c.id) return c.name;
    return clipId;
}

std::vector<DocAnim> suitAnims(const Json& nova) {
    std::vector<DocAnim> out;
    const Json clips = jfield(nova, "clips", Json::object());
    if (!clips.is_object()) return out;
    for (auto it = clips.begin(); it != clips.end(); ++it) {
        const Json& c = it.value();
        DocAnim a;
        a.id = it.key();
        a.name = animName(a.id);
        a.row = jget<int>(c, "row", 0);
        a.count = std::clamp(jget<int>(c, "count", 1), 1, kSuitSheetW / kSuitFrame);
        if (a.row < 0 || a.row >= kSuitSheetH / kSuitFrame) continue;
        const Json d = jfield(c, "durations", Json());
        const float fps = jget<float>(c, "fps", 10.0f);
        for (int i = 0; i < a.count; ++i) {
            float v = d.is_array() && static_cast<size_t>(i) < d.size() && d[static_cast<size_t>(i)].is_number() ? d[static_cast<size_t>(i)].get<float>()
                                                                                                                : 1.0f / std::max(1.0f, fps);
            a.durations.push_back(std::clamp(v, 0.01f, 2.0f));
        }
        out.push_back(std::move(a));
    }
    std::sort(out.begin(), out.end(), [](const DocAnim& a, const DocAnim& b) { return a.row < b.row; });
    return out;
}

// ------------------------------------------------------------------ the document

std::vector<DocAnim> StudioDoc::anims(const std::vector<DocAnim>& novaAnims) const {
    if (kind == DocKind::Suit) return novaAnims;
    DocAnim a;
    a.id = kind == DocKind::Frame ? "ring" : "particle";
    a.name = kind == DocKind::Frame ? "Cadre" : "Particule";
    a.count = std::max(1, stripFrames());
    a.durations.assign(static_cast<size_t>(a.count), 1.0f / static_cast<float>(std::max(1, fps)));
    return {a};
}

IRect StudioDoc::frameRect(const DocAnim& anim, int index) const {
    if (kind == DocKind::Suit) return {index * kSuitFrame, anim.row * kSuitFrame, kSuitFrame, kSuitFrame};
    return {index * frameW, 0, frameW, frameH};
}

int StudioDoc::drawnFrames(const DocAnim& anim) const {
    int n = 0;
    for (int i = 0; i < anim.count; ++i) n += image.transparent(frameRect(anim, i)) ? 0 : 1;
    return n;
}

bool StudioDoc::insertFrame(int at, bool copy) {
    if (kind == DocKind::Suit) return false;
    const int n = stripFrames();
    if (n >= kImageFramesMax || at < -1 || at >= n) return false;
    PixelImage next((n + 1) * frameW, frameH);
    // Frames 0..at stay, the new one after `at`, the others one place further.
    next.paste(image.crop({0, 0, (at + 1) * frameW, frameH}), 0, 0);
    if (copy && at >= 0) next.paste(image.crop({at * frameW, 0, frameW, frameH}), (at + 1) * frameW, 0);
    next.paste(image.crop({(at + 1) * frameW, 0, (n - at - 1) * frameW, frameH}), (at + 2) * frameW, 0);
    image = std::move(next);
    return true;
}

bool StudioDoc::removeFrame(int at) {
    if (kind == DocKind::Suit) return false;
    const int n = stripFrames();
    if (n <= 1 || at < 0 || at >= n) return false;
    PixelImage next((n - 1) * frameW, frameH);
    next.paste(image.crop({0, 0, at * frameW, frameH}), 0, 0);
    next.paste(image.crop({(at + 1) * frameW, 0, (n - at - 1) * frameW, frameH}), at * frameW, 0);
    image = std::move(next);
    return true;
}

Json StudioDoc::metaJson() const {
    Json pal = Json::array();
    for (const Px& p : palette) pal.push_back(hexOf(p));
    return Json{{"frame_w", frameW}, {"frame_h", frameH}, {"fps", fps}, {"emitter", emitter.toJson()}, {"palette", pal},
                {"look", creationLookToJson(look)}, {"colors", suitColorsToJson(colors)}};
}

void StudioDoc::applyMeta(const Json& m) {
    if (!m.is_object()) return;
    frameW = std::max(1, jget<int>(m, "frame_w", frameW));
    frameH = std::max(1, jget<int>(m, "frame_h", frameH));
    fps = std::clamp(jget<int>(m, "fps", fps), kImageFpsMin, kImageFpsMax);
    if (auto e = ImageEmitter::fromJson(jfield(m, "emitter"))) emitter = *e;
    palette.clear();
    for (const Json& p : jfield(m, "palette", Json::array())) {
        Px c;
        if (p.is_string() && parseHex(p.get<std::string>(), c)) palette.push_back(c);
    }
    if (kind != DocKind::Suit)
        if (auto l = creationLookFromJson(kindId(kind), jfield(m, "look"))) look = *l;
    if (auto c = suitColorsFromJson(jfield(m, "colors"))) colors = *c;
}

std::string StudioDoc::sendProblem(const std::vector<DocAnim>& novaAnims) const {
    if (name.find_first_not_of(" \t") == std::string::npos) return "Donnez-lui un nom.";
    if (codepoints(name) > 32) return "Nom : 32 caractères au plus.";
    if (archived) return "Archivée sur le serveur : restaurez-la d'abord (Bibliothèque).";
    if (kind == DocKind::Suit) {
        if (const std::string p = imageLayoutProblem("suit", image.w(), image.h(), kSuitFrame, kSuitFrame, 1, 1); !p.empty()) return p;
        for (const DocAnim& a : novaAnims) {
            const int drawn = drawnFrames(a);
            if (drawn < a.count)
                return "« " + a.name + " » : " + std::to_string(a.count - drawn) + " image(s) vide(s) sur " + std::to_string(a.count) +
                       ". Dessinez-les, ou « Compléter avec Nova ».";
        }
        return "";
    }
    if (const std::string p = imageLayoutProblem(kindId(kind), image.w(), image.h(), frameW, frameH, stripFrames(), fps); !p.empty()) return p;
    if (image.transparent(image.bounds())) return "L'image est vide : dessinez au moins une image.";
    return "";
}

void StudioDoc::paletteFromImage(size_t max) {
    palette.clear();
    for (const auto& [c, n] : colorCounts(image, image.bounds())) {
        if (palette.size() >= max) break;
        // Several alphas of the same colour count once.
        const Px opaque{c.r, c.g, c.b, 255};
        if (std::find(palette.begin(), palette.end(), opaque) == palette.end()) palette.push_back(opaque);
    }
}

Json StudioDoc::toJson() const {
    Json j{{"format", "cosmic.studio.doc"}, {"version", 1}, {"uid", uid}, {"kind", kindId(kind)}, {"name", name}, {"saved_at", savedAt}};
    j["server"] = Json{{"id", serverId}, {"updated_at", serverUpdatedAt}, {"image_hash", serverImageHash}, {"archived", archived}};
    Json m = metaJson();
    for (auto it = m.begin(); it != m.end(); ++it) j[it.key()] = it.value();
    return j;
}

std::optional<StudioDoc> StudioDoc::fromJson(const Json& j, std::string* why) {
    auto fail = [why](const char* m) -> std::optional<StudioDoc> {
        if (why) *why = m;
        return std::nullopt;
    };
    if (!j.is_object() || jstr(j, "format") != "cosmic.studio.doc") return fail("Fichier de brouillon inconnu.");
    auto k = kindFromId(jstr(j, "kind"));
    if (!k) return fail("Type de création inconnu.");
    StudioDoc d;
    d.kind = *k;
    d.uid = jstr(j, "uid");
    d.name = jstr(j, "name");
    if (codepoints(d.name) > 64) d.name = d.name.substr(0, 64);
    d.savedAt = jstr(j, "saved_at");
    const Json s = jfield(j, "server", Json::object());
    d.serverId = std::max(0, jget<int>(s, "id", 0));
    d.serverUpdatedAt = jstr(s, "updated_at");
    d.serverImageHash = jstr(s, "image_hash");
    d.archived = jget<bool>(s, "archived", false);
    d.look = defaultCreationLook(d.kind == DocKind::Suit ? "trail" : kindId(d.kind));
    d.frameW = d.frameH = d.kind == DocKind::Suit ? kSuitFrame : 16;
    d.applyMeta(j);
    if (d.kind == DocKind::Suit) d.frameW = d.frameH = kSuitFrame;
    return d;
}

bool StudioDoc::saveDraft(const fs::path& dir, std::string* why) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::vector<uint8_t> png = encodePng(image.w(), image.h(), image.data());
    if (png.empty()) {
        if (why) *why = "Image impossible à écrire en PNG.";
        return false;
    }
    const std::string before = savedAt;
    savedAt = nowUtcIso();
    if (!FileUtil::writeBinary(dir / "image.png", png) || !FileUtil::writeTextAtomic(dir / "doc.json", toJson().dump(1))) {
        savedAt = before;
        if (why) *why = "Écriture impossible dans " + Paths::toUtf8(dir) + ".";
        return false;
    }
    return true;
}

std::optional<StudioDoc> StudioDoc::loadDraft(const fs::path& dir, std::string* why) {
    auto j = FileUtil::loadJson(dir / "doc.json");
    if (!j) {
        if (why) *why = "doc.json illisible.";
        return std::nullopt;
    }
    auto d = fromJson(*j, why);
    if (!d) return std::nullopt;
    auto bytes = FileUtil::readBinary(dir / "image.png");
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!bytes || !decodeImage(bytes->data(), bytes->size(), w, h, rgba)) {
        if (why) *why = "image.png illisible.";
        return std::nullopt;
    }
    d->image = PixelImage::fromRgba(w, h, std::move(rgba));
    const bool ok = d->kind == DocKind::Suit ? (w == kSuitSheetW && h == kSuitSheetH)
                                             : (d->frameW > 0 && d->frameH == h && w % d->frameW == 0 && w / d->frameW >= 1 && w / d->frameW <= kImageFramesMax);
    if (!ok) {
        if (why) *why = "Image du brouillon aux mauvaises dimensions.";
        return std::nullopt;
    }
    if (d->uid.empty()) d->uid = Paths::toUtf8(dir.filename());
    return d;
}

StudioDoc StudioDoc::make(DocKind kind, const std::string& uid, const std::string& name, const PixelImage& start, int frames) {
    StudioDoc d;
    d.kind = kind;
    d.uid = uid;
    d.name = name;
    d.look = defaultCreationLook(kind == DocKind::Suit ? "trail" : kindId(kind));
    if (kind == DocKind::Suit) {
        d.frameW = d.frameH = kSuitFrame;
        d.image = PixelImage(kSuitSheetW, kSuitSheetH);
        d.image.paste(start, 0, 0);
    } else if (kind == DocKind::Frame) {
        const int side = start.empty() ? 64 : std::clamp(start.h(), kRingFrameMin, kRingFrameMax);
        d.frameW = d.frameH = side;
        const int n = start.empty() ? 1 : std::clamp(start.w() / side, 1, kImageFramesMax);
        d.image = PixelImage(side * n, side);
        d.image.paste(start, 0, 0);
        d.fps = 8;
    } else {
        const int side = start.empty() ? 16 : std::clamp(start.h(), kParticleFrameMin, kParticleFrameMax);
        d.frameW = d.frameH = side;
        const int n = start.empty() ? std::clamp(frames, 1, kImageFramesMax) : std::clamp(start.w() / side, 1, kImageFramesMax);
        d.image = PixelImage(side * n, side);
        d.image.paste(start, 0, 0);
    }
    d.paletteFromImage();
    return d;
}

std::string nowUtcIso() {
    const std::time_t t = std::time(nullptr);
    std::tm m{};
#ifdef _WIN32
    gmtime_s(&m, &t);
#else
    gmtime_r(&t, &m);
#endif
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02dZ", m.tm_year + 1900, m.tm_mon + 1, m.tm_mday, m.tm_hour, m.tm_min, m.tm_sec);
    return b;
}

std::string newDraftUid() {
    const std::time_t t = std::time(nullptr);
    std::tm m{};
#ifdef _WIN32
    gmtime_s(&m, &t);
#else
    gmtime_r(&t, &m);
#endif
    std::random_device rd;
    char b[48];
    std::snprintf(b, sizeof b, "b%04d%02d%02d-%02d%02d%02d-%04x", m.tm_year + 1900, m.tm_mon + 1, m.tm_mday, m.tm_hour, m.tm_min, m.tm_sec,
                  static_cast<unsigned>(rd() & 0xFFFF));
    return b;
}

}  // namespace cosmic::studio
