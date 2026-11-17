#include "game/cosmetics/CreationImages.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/rendering/Renderer.h"
#include "game/cosmetics/CreationLooks.h"
#include "game/cosmetics/SuitRecolor.h"
#include "packaging/Sha256.h"

namespace cosmic {

// ------------------------------------------------------------------ the emitter

namespace {

const std::vector<EmitterBound> kBounds = {
    {"rate", 1, 120},   {"life_ms", 100, 1000}, {"size", 25, 400},   {"speed", 0, 400},
    {"spread", 0, 360}, {"gravity", -600, 600}, {"spin", -720, 720}, {"fade", 0, 2},
};

int* fieldOf(ImageEmitter& e, const std::string& key) {
    if (key == "rate") return &e.rate;
    if (key == "life_ms") return &e.lifeMs;
    if (key == "size") return &e.size;
    if (key == "speed") return &e.speed;
    if (key == "spread") return &e.spread;
    if (key == "gravity") return &e.gravity;
    if (key == "spin") return &e.spin;
    if (key == "fade") return &e.fade;
    return nullptr;
}

bool isHash(const std::string& h) {
    if (h.size() != 64) return false;
    for (char c : h)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint32_t crc32(const uint8_t* data, size_t n) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

CreationImageResolver& resolverSlot() {
    static CreationImageResolver r;
    return r;
}

}  // namespace

const std::vector<EmitterBound>& emitterBounds() { return kBounds; }

Json ImageEmitter::toJson() const {
    return Json{{"rate", rate}, {"life_ms", lifeMs}, {"size", size},   {"speed", speed},       {"spread", spread},
                {"gravity", gravity}, {"spin", spin}, {"fade", fade}, {"sparkles", sparkles}};
}

std::optional<ImageEmitter> ImageEmitter::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    ImageEmitter e;
    for (const EmitterBound& b : kBounds) {
        auto it = j.find(b.key);
        if (it == j.end() || !it->is_number_integer()) return std::nullopt;
        const int64_t v = it->get<int64_t>();
        if (v < b.min || v > b.max) return std::nullopt;
        *fieldOf(e, b.key) = static_cast<int>(v);
    }
    auto sp = j.find("sparkles");
    if (sp == j.end() || !sp->is_boolean()) return std::nullopt;
    e.sparkles = sp->get<bool>();
    return e;
}

ImageEmitter ImageEmitter::clamped() const {
    ImageEmitter e = *this;
    for (const EmitterBound& b : kBounds) {
        int* f = fieldOf(e, b.key);
        *f = std::clamp(*f, b.min, b.max);
    }
    return e;
}

// ------------------------------------------------------------------ layouts

std::string imageLayoutProblem(const std::string& kind, int width, int height, int frameW, int frameH, int frames, int fps) {
    if (kind == "suit") {
        if (width != kSuitSheetW || height != kSuitSheetH)
            return "Une combinaison est la planche entière de Nova : " + std::to_string(kSuitSheetW) + " × " + std::to_string(kSuitSheetH) +
                   " pixels (ici " + std::to_string(width) + " × " + std::to_string(height) + ").";
        return "";
    }
    if (kind != "trail" && kind != "victory" && kind != "frame") return "Type de création inconnu.";
    if (frames < 1 || frames > kImageFramesMax) return "De 1 à " + std::to_string(kImageFramesMax) + " images.";
    if (fps < kImageFpsMin || fps > kImageFpsMax)
        return "De " + std::to_string(kImageFpsMin) + " à " + std::to_string(kImageFpsMax) + " images par seconde.";
    if (kind == "frame") {
        if (frameW != frameH) return "Les images d'un cadre sont carrées.";
        if (frameW < kRingFrameMin || frameW > kRingFrameMax)
            return "Un cadre mesure de " + std::to_string(kRingFrameMin) + " à " + std::to_string(kRingFrameMax) + " pixels de côté.";
    } else if (frameW < kParticleFrameMin || frameW > kParticleFrameMax || frameH < kParticleFrameMin || frameH > kParticleFrameMax) {
        return "Une particule mesure de " + std::to_string(kParticleFrameMin) + " à " + std::to_string(kParticleFrameMax) + " pixels de côté.";
    }
    if (width != frameW * frames || height != frameH)
        return "La planche doit mesurer " + std::to_string(frameW * frames) + " × " + std::to_string(frameH) + " pixels (ses images côte à côte).";
    return "";
}

// ------------------------------------------------------------------ PNG

bool readPngHeader(const uint8_t* d, size_t n, PngHeader& out, std::string* why) {
    auto fail = [why](const char* m) {
        if (why) *why = m;
        return false;
    };
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (!d || n < 8 || std::memcmp(d, kSig, 8) != 0) return fail("Pas un fichier PNG.");
    // IHDR first: length 13, its CRC.
    if (n < 8 + 25) return fail("Fichier PNG tronqué.");
    const uint8_t* c = d + 8;
    if (be32(c) != 13 || std::memcmp(c + 4, "IHDR", 4) != 0) return fail("Fichier PNG invalide (en-tête IHDR absent).");
    if (crc32(c + 4, 17) != be32(c + 21)) return fail("Fichier PNG abîmé (en-tête IHDR).");
    out.width = static_cast<int>(std::min<uint32_t>(be32(c + 8), 0x7FFFFFFF));
    out.height = static_cast<int>(std::min<uint32_t>(be32(c + 12), 0x7FFFFFFF));
    out.bitDepth = c[16];
    out.colorType = c[17];
    if (out.width <= 0 || out.height <= 0) return fail("Image vide.");
    const int ct = out.colorType, bd = out.bitDepth;
    const bool depthOk = (ct == 0 && (bd == 1 || bd == 2 || bd == 4 || bd == 8 || bd == 16)) || (ct == 3 && (bd == 1 || bd == 2 || bd == 4 || bd == 8)) ||
                         ((ct == 2 || ct == 4 || ct == 6) && (bd == 8 || bd == 16));
    if (!depthOk || c[18] != 0 || c[19] != 0 || c[20] > 1) return fail("Fichier PNG invalide (format de couleur).");
    out.alpha = ct == 4 || ct == 6;
    // The chunks one after the other up to IEND (a tRNS gives transparency too).
    size_t at = 8;
    bool end = false, data = false;
    while (at + 12 <= n) {
        const uint32_t len = be32(d + at);
        if (len > n - at - 12) return fail("Fichier PNG tronqué.");
        const uint8_t* type = d + at + 4;
        if (std::memcmp(type, "tRNS", 4) == 0) out.alpha = true;
        if (std::memcmp(type, "IDAT", 4) == 0) data = true;
        at += 12 + len;
        if (std::memcmp(type, "IEND", 4) == 0) {
            end = true;
            break;
        }
    }
    if (!end || !data) return fail("Fichier PNG tronqué.");
    return true;
}

bool decodeImage(const uint8_t* data, size_t size, int& w, int& h, std::vector<uint8_t>& rgba) {
    if (!data || size == 0) return false;
    SDL_IOStream* io = SDL_IOFromConstMem(data, size);
    if (!io) return false;
    SDL_Surface* s = IMG_Load_IO(io, true);
    if (!s) return false;
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!conv) return false;
    w = conv->w;
    h = conv->h;
    bool ok = w > 0 && h > 0 && SDL_LockSurface(conv);
    if (ok) {
        rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
        for (int y = 0; y < h; ++y)
            std::memcpy(rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4,
                        static_cast<const uint8_t*>(conv->pixels) + static_cast<size_t>(y) * static_cast<size_t>(conv->pitch), static_cast<size_t>(w) * 4);
        SDL_UnlockSurface(conv);
    }
    SDL_DestroySurface(conv);
    return ok;
}

std::vector<uint8_t> encodePng(int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> out;
    if (w <= 0 || h <= 0 || rgba.size() != static_cast<size_t>(w) * static_cast<size_t>(h) * 4) return out;
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(rgba.data()), w * 4);
    if (!s) return out;
    SDL_IOStream* io = SDL_IOFromDynamicMem();
    if (io && IMG_SavePNG_IO(s, io, false)) {
        const Sint64 size = SDL_GetIOSize(io);
        SDL_PropertiesID props = SDL_GetIOProperties(io);
        auto* mem = static_cast<uint8_t*>(SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
        if (mem && size > 0) out.assign(mem, mem + size);
    }
    if (io) SDL_CloseIO(io);
    SDL_DestroySurface(s);
    return out;
}

// ------------------------------------------------------------------ the images listed

int CreationImageInfo::creationId() const {
    const size_t c = ref.find(':');
    return c == std::string::npos ? 0 : std::atoi(ref.c_str() + c + 1);
}

std::string CreationImageInfo::path() const { return "/api/v1/creations/" + std::to_string(creationId()) + "/image/" + hash; }

std::optional<CreationImageInfo> CreationImageInfo::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    CreationImageInfo i;
    i.ref = jstr(j, "ref");
    i.kind = jstr(j, "kind");
    i.hash = jstr(j, "hash");
    std::string k;
    const bool refOk = i.kind == "suit" ? isServerSuitRef(i.ref) : isServerLookRef(i.ref, &k) && k == i.kind;
    if (!refOk || !isHash(i.hash)) return std::nullopt;
    auto num = [&j](const char* key, int& v) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_number_integer()) return false;
        const int64_t x = it->get<int64_t>();
        if (x < 0 || x > 100000) return false;
        v = static_cast<int>(x);
        return true;
    };
    if (!num("width", i.width) || !num("height", i.height)) return std::nullopt;
    if (i.kind == "suit") {
        i.frameW = i.frameH = kSuitFrame;
        i.frames = 1;
        i.fps = kImageFpsMin;
    } else if (!num("frame_w", i.frameW) || !num("frame_h", i.frameH) || !num("frames", i.frames) || !num("fps", i.fps)) {
        return std::nullopt;
    }
    if (!imageLayoutProblem(i.kind, i.width, i.height, i.frameW, i.frameH, i.frames, i.fps).empty()) return std::nullopt;
    if (i.kind == "trail" || i.kind == "victory") {
        i.emitter = ImageEmitter::fromJson(jfield(j, "emitter"));
        if (!i.emitter) return std::nullopt;
    }
    i.updatedAt = jstr(j, "updated_at");
    return i;
}

Json CreationImageInfo::toJson() const {
    Json j{{"ref", ref}, {"kind", kind}, {"hash", hash}, {"width", width}, {"height", height}, {"updated_at", updatedAt}};
    if (kind != "suit") {
        j["frame_w"] = frameW;
        j["frame_h"] = frameH;
        j["frames"] = frames;
        j["fps"] = fps;
    }
    if (emitter) j["emitter"] = emitter->toJson();
    return j;
}

void setCreationImageResolver(CreationImageResolver resolver) { resolverSlot() = std::move(resolver); }

std::optional<CreationImageDraw> resolveCreationImage(const std::string& ref) {
    if (!resolverSlot() || ref.rfind("srv", 0) != 0) return std::nullopt;
    return resolverSlot()(ref);
}

// ------------------------------------------------------------------ the book

CreationImageBook::~CreationImageBook() {
    // Decodes still running finish before their slots go (their futures wait).
    slots_.clear();
}

void CreationImageBook::load() {
    list_.clear();
    auto j = FileUtil::loadJson(dir_ / "list.json");
    if (!j) return;
    for (const Json& e : jfield(*j, "images", Json::array()))
        if (auto i = CreationImageInfo::fromJson(e)) list_.push_back(std::move(*i));
}

bool CreationImageBook::save() const {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    Json arr = Json::array();
    std::set<std::string> keep;
    for (const CreationImageInfo& i : list_) {
        arr.push_back(i.toJson());
        keep.insert(i.hash + ".png");
    }
    // The files of images no longer listed go (only this folder's PNG files).
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        const std::string name = Paths::toUtf8(e.path().filename());
        if (e.is_regular_file(ec) && FileUtil::extensionLower(e.path()) == ".png" && !keep.count(name)) fs::remove(e.path(), ec);
    }
    return FileUtil::writeTextAtomic(dir_ / "list.json", Json{{"format", "cosmic.creation_images"}, {"images", arr}}.dump(1));
}

bool CreationImageBook::applyList(const Json& images) {
    if (!images.is_array()) return false;
    std::vector<CreationImageInfo> next;
    for (const Json& e : images)
        if (auto i = CreationImageInfo::fromJson(e)) next.push_back(std::move(*i));
    bool changed = next.size() != list_.size();
    for (size_t k = 0; !changed && k < next.size(); ++k) changed = next[k].toJson() != list_[k].toJson();
    list_ = std::move(next);
    // A slot whose image changed (another hash) or went is dropped: drawn again from the new one.
    for (auto it = slots_.begin(); it != slots_.end();) {
        const CreationImageInfo* i = find(it->first);
        if (!i || i->hash != it->second.hash) {
            if (it->second.decoding.valid()) it->second.decoding.wait();
            it = slots_.erase(it);
        } else {
            ++it;
        }
    }
    return changed;
}

const CreationImageInfo* CreationImageBook::find(const std::string& ref) const {
    for (const CreationImageInfo& i : list_)
        if (i.ref == ref) return &i;
    return nullptr;
}

std::string CreationImageBook::checkBytes(const CreationImageInfo& info, const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return "fichier vide";
    if (bytes.size() > kCreationImageMaxBytes) return "fichier trop grand";
    if (Sha256::hex(bytes) != info.hash) return "fichier différent de celui du serveur (SHA-256)";
    PngHeader h;
    std::string why;
    if (!readPngHeader(bytes.data(), bytes.size(), h, &why)) return why;
    if (h.width != info.width || h.height != info.height) return "dimensions différentes de celles annoncées";
    return "";
}

void CreationImageBook::startDecode(Slot& slot, const CreationImageInfo& info) {
    const fs::path file = fileOf(info.hash);
    const CreationImageInfo copy = info;
    slot.decoding = std::async(std::launch::async, [file, copy]() -> std::optional<Decoded> {
        auto bytes = FileUtil::readBinary(file);
        if (!bytes || !checkBytes(copy, *bytes).empty()) return std::nullopt;
        Decoded d;
        if (!decodeImage(bytes->data(), bytes->size(), d.w, d.h, d.rgba) || d.w != copy.width || d.h != copy.height) return std::nullopt;
        return d;
    });
}

void CreationImageBook::update(Renderer& renderer) {
    for (auto& [ref, slot] : slots_) {
        if (!slot.decoding.valid() || slot.decoding.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
        std::optional<Decoded> d = slot.decoding.get();
        if (!d) {
            // Unreadable on disk: removed, downloaded again later.
            std::error_code ec;
            fs::remove(fileOf(slot.hash), ec);
            slot.failed = true;
            slot.retryAtMs = SDL_GetTicks() + 60000;
            continue;
        }
        SDL_Surface* s = SDL_CreateSurfaceFrom(d->w, d->h, SDL_PIXELFORMAT_RGBA32, d->rgba.data(), d->w * 4);
        if (!s) continue;
        slot.texture = renderer.textureFromSurface(s, "<image " + ref + ">", true);
        SDL_DestroySurface(s);
    }
}

TexturePtr CreationImageBook::texture(const std::string& ref, Renderer& renderer) {
    const CreationImageInfo* info = find(ref);
    if (!info) return nullptr;
    Slot& slot = slots_[ref];
    if (slot.hash != info->hash) {
        if (slot.decoding.valid()) slot.decoding.wait();
        slot = Slot{};
        slot.hash = info->hash;
    }
    if (slot.texture) return slot.texture;
    if (slot.decoding.valid()) {
        update(renderer);
        return slot.texture;
    }
    if (slot.downloading) return nullptr;
    if (slot.failed && SDL_GetTicks() < slot.retryAtMs) return nullptr;
    slot.failed = false;
    std::error_code ec;
    if (fs::exists(fileOf(info->hash), ec)) {
        startDecode(slot, *info);
        return nullptr;
    }
    if (!fetch_) return nullptr;
    slot.downloading = true;
    const CreationImageInfo copy = *info;
    std::weak_ptr<int> alive = life_;
    fetch_(info->path(), [this, alive, copy](bool ok, std::vector<uint8_t> bytes) {
        if (alive.expired()) return;
        auto it = slots_.find(copy.ref);
        if (it == slots_.end() || it->second.hash != copy.hash) return;
        Slot& s = it->second;
        s.downloading = false;
        const std::string why = ok ? checkBytes(copy, bytes) : std::string("téléchargement impossible");
        if (!why.empty()) {
            if (ok) Log::warn("Image de la création {} refusée : {}", copy.ref, why);
            s.failed = true;
            s.retryAtMs = SDL_GetTicks() + 120000;
            return;
        }
        std::error_code e2;
        fs::create_directories(dir_, e2);
        if (!FileUtil::writeBinary(fileOf(copy.hash), bytes)) {
            s.failed = true;
            s.retryAtMs = SDL_GetTicks() + 120000;
            return;
        }
        startDecode(s, copy);
    });
    return nullptr;
}

size_t CreationImageBook::busy() const {
    size_t n = 0;
    for (const auto& [ref, slot] : slots_) n += (slot.downloading || slot.decoding.valid()) ? 1 : 0;
    return n;
}

}  // namespace cosmic
