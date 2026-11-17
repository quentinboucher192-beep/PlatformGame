#include "game/cosmetics/SuitRecolor.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/core/FileUtil.h"
#include "engine/core/Log.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"

namespace cosmic {

namespace {

// ------------------------------------------------- Python's colorsys, to the bit
//
// The functions of CPython 3.13's Lib/colorsys.py, written in the same order of operations
// (IEEE double, no contraction: see cmake/CompilerWarnings.cmake), and Python's float modulo.

constexpr double kOneThird = 1.0 / 3.0;
constexpr double kOneSixth = 1.0 / 6.0;
constexpr double kTwoThird = 2.0 / 3.0;

/// `x % y` of Python for floats: the result has the sign of `y`.
double pyMod(double x, double y) {
    double m = std::fmod(x, y);
    if (m != 0.0) {
        if ((y < 0.0) != (m < 0.0)) m += y;
    } else {
        m = std::copysign(0.0, y);
    }
    return m;
}

struct Hls {
    double h, l, s;
};

Hls rgbToHls(double r, double g, double b) {
    const double maxc = std::max(r, std::max(g, b));
    const double minc = std::min(r, std::min(g, b));
    const double sumc = maxc + minc;
    const double rangec = maxc - minc;
    const double l = sumc / 2.0;
    if (minc == maxc) return {0.0, l, 0.0};
    double s;
    if (l <= 0.5) s = rangec / sumc;
    else s = rangec / (2.0 - maxc - minc);
    const double rc = (maxc - r) / rangec;
    const double gc = (maxc - g) / rangec;
    const double bc = (maxc - b) / rangec;
    double h;
    if (r == maxc) h = bc - gc;
    else if (g == maxc) h = 2.0 + rc - bc;
    else h = 4.0 + gc - rc;
    h = pyMod(h / 6.0, 1.0);
    return {h, l, s};
}

double hlsV(double m1, double m2, double hue) {
    hue = pyMod(hue, 1.0);
    if (hue < kOneSixth) return m1 + (m2 - m1) * hue * 6.0;
    if (hue < 0.5) return m2;
    if (hue < kTwoThird) return m1 + (m2 - m1) * (kTwoThird - hue) * 6.0;
    return m1;
}

void hlsToRgb(double h, double l, double s, double out[3]) {
    if (s == 0.0) {
        out[0] = out[1] = out[2] = l;
        return;
    }
    double m2;
    if (l <= 0.5) m2 = l * (1.0 + s);
    else m2 = l + s - (l * s);
    const double m1 = 2.0 * l - m2;
    out[0] = hlsV(m1, m2, h + kOneThird);
    out[1] = hlsV(m1, m2, h);
    out[2] = hlsV(m1, m2, h - kOneThird);
}

Hls hlsOf(Rgb8 c) { return rgbToHls(c.r / 255.0, c.g / 255.0, c.b / 255.0); }

/// `int(round(v * 255))` of Python (half to even: nearbyint in the default rounding mode).
uint8_t channel(double v) {
    const double x = std::nearbyint(v * 255.0);
    return static_cast<uint8_t>(std::clamp(x, 0.0, 255.0));
}

/// Python's min(a, b) / max(a, b): the first unless the second is strictly smaller / larger.
double pyMin(double a, double b) { return b < a ? b : a; }
double pyMax(double a, double b) { return b > a ? b : a; }

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

void recolorSuitPixels(const uint8_t* src, uint8_t* dst, size_t count, const SuitColors& c) {
    constexpr double kSuitMix = 0.55;
    const Hls ts = hlsOf(c.suit);
    const Hls targetAccent = hlsOf(c.accent), targetScarf = hlsOf(c.scarf);
    std::unordered_map<uint32_t, uint32_t> cache;  // source RGB → recoloured RGB, as recolor()'s dict
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* p = src + i * 4;
        uint8_t* q = dst + i * 4;
        if (p[3] == 0) {
            if (q != p) std::memcpy(q, p, 4);
            continue;
        }
        const uint32_t key = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
        auto it = cache.find(key);
        if (it == cache.end()) {
            const Hls px = rgbToHls(p[0] / 255.0, p[1] / 255.0, p[2] / 255.0);
            uint32_t out = key;  // the outline (l < 0.13) keeps its colour
            if (!(px.l < 0.13)) {
                double rgb[3];
                const bool accent = px.s > 0.45 && 0.40 < px.h && px.h < 0.55;
                const bool scarf = !accent && px.s > 0.45 && (px.h < 0.08 || px.h > 0.95);
                if (!accent && !scarf) {
                    const double nl = px.l * (1 - kSuitMix) + px.l * ts.l / 0.75 * kSuitMix;
                    hlsToRgb(ts.h, pyMin(0.96, nl), ts.s * 0.9, rgb);
                } else {
                    const Hls& t = accent ? targetAccent : targetScarf;
                    hlsToRgb(t.h, pyMax(0.15, pyMin(0.95, px.l * (t.l / 0.55))), pyMax(t.s, 0.6), rgb);
                }
                out = (static_cast<uint32_t>(channel(rgb[0])) << 16) | (static_cast<uint32_t>(channel(rgb[1])) << 8) | channel(rgb[2]);
            }
            it = cache.emplace(key, out).first;
        }
        q[0] = static_cast<uint8_t>(it->second >> 16);
        q[1] = static_cast<uint8_t>(it->second >> 8);
        q[2] = static_cast<uint8_t>(it->second);
        q[3] = p[3];
    }
}

// ------------------------------------------------------------------ colours

std::string SuitColors::key() const { return hexColor(suit) + hexColor(accent) + hexColor(scarf); }

std::optional<Rgb8> parseHexColor(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return std::nullopt;
    int v[6];
    for (int i = 0; i < 6; ++i)
        if ((v[i] = hexDigit(s[static_cast<size_t>(i) + 1])) < 0) return std::nullopt;
    return Rgb8{static_cast<uint8_t>(v[0] * 16 + v[1]), static_cast<uint8_t>(v[2] * 16 + v[3]), static_cast<uint8_t>(v[4] * 16 + v[5])};
}

std::string hexColor(Rgb8 c) {
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
    return b;
}

std::optional<SuitColors> suitColorsFromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    auto s = parseHexColor(jstr(j, "suit")), a = parseHexColor(jstr(j, "accent")), f = parseHexColor(jstr(j, "scarf"));
    if (!s || !a || !f) return std::nullopt;
    return SuitColors{*s, *a, *f};
}

Json suitColorsToJson(const SuitColors& c) { return {{"suit", hexColor(c.suit)}, {"accent", hexColor(c.accent)}, {"scarf", hexColor(c.scarf)}}; }

bool isServerSuitRef(const std::string& ref) {
    constexpr size_t kPrefix = 8;  // "srvsuit:"
    if (ref.size() <= kPrefix || ref.size() > kPrefix + 10 || ref.compare(0, kPrefix, "srvsuit:") != 0) return false;
    if (ref[kPrefix] == '0') return false;
    for (size_t i = kPrefix; i < ref.size(); ++i)
        if (ref[i] < '0' || ref[i] > '9') return false;
    return true;
}

// ------------------------------------------------------------------ images

bool loadRgbaImage(const fs::path& file, std::vector<uint8_t>& pixels, int& w, int& h) {
    SDL_Surface* s = IMG_Load(Paths::toUtf8(file).c_str());
    if (!s) return false;
    SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!rgba) return false;
    w = rgba->w;
    h = rgba->h;
    pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    bool ok = SDL_LockSurface(rgba);
    if (ok)
        for (int y = 0; y < h; ++y)
            std::memcpy(pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4,
                        static_cast<const uint8_t*>(rgba->pixels) + static_cast<size_t>(y) * static_cast<size_t>(rgba->pitch),
                        static_cast<size_t>(w) * 4);
    if (ok) SDL_UnlockSurface(rgba);
    SDL_DestroySurface(rgba);
    return ok && w > 0 && h > 0;
}

// ------------------------------------------------------------------ the suits known

namespace {

/// A name as shown: at most 32 characters (code points), control characters as spaces.
std::string cleanName(const std::string& s) {
    std::string out;
    size_t n = 0;
    for (size_t i = 0; i < s.size() && n < 32;) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + len > s.size()) break;
        if (c < 0x20 || c == 0x7f) out += ' ';
        else out.append(s, i, len);
        i += len;
        ++n;
    }
    return out;
}

}  // namespace

std::optional<ServerSuit> ServerSuitBook::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    if (j.contains("kind") && jstr(j, "kind") != "suit") return std::nullopt;
    ServerSuit s;
    s.ref = jstr(j, "ref");
    s.name = cleanName(jstr(j, "name"));
    s.updatedAt = jstr(j, "updated_at").substr(0, 40);
    auto colors = suitColorsFromJson(jfield(j, "colors", Json()));
    if (!isServerSuitRef(s.ref) || s.name.empty() || !colors) return std::nullopt;
    s.colors = *colors;
    return s;
}

Json ServerSuitBook::toJson(const ServerSuit& s) {
    return {{"ref", s.ref}, {"kind", "suit"}, {"name", s.name}, {"colors", suitColorsToJson(s.colors)}, {"updated_at", s.updatedAt}};
}

void ServerSuitBook::load() {
    if (file_.empty()) return;
    std::error_code ec;
    if (!fs::exists(file_, ec)) return;
    auto j = FileUtil::loadJson(file_);
    if (!j || !j->is_object()) return;
    suits_.clear();
    for (const Json& e : jfield(*j, "creations", Json::array()))
        if (auto s = fromJson(e); s && !find(s->ref) && suits_.size() < 1000) suits_.push_back(std::move(*s));
    known_ = true;
}

bool ServerSuitBook::save() const {
    if (file_.empty()) return false;
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    Json list = Json::array();
    for (const ServerSuit& s : suits_) list.push_back(toJson(s));
    return FileUtil::writeTextAtomic(file_, Json{{"version", 1}, {"creations", list}}.dump(1));
}

bool ServerSuitBook::applyList(const Json& creations) {
    if (!creations.is_array()) return false;
    std::vector<ServerSuit> next;
    for (const Json& e : creations) {
        if (next.size() >= 1000) break;
        auto s = fromJson(e);
        if (!s) continue;
        bool dup = false;
        for (const ServerSuit& o : next) dup = dup || o.ref == s->ref;
        if (!dup) next.push_back(std::move(*s));
    }
    bool changed = !known_ || next.size() != suits_.size();
    for (size_t i = 0; !changed && i < next.size(); ++i)
        changed = next[i].ref != suits_[i].ref || next[i].name != suits_[i].name || next[i].colors != suits_[i].colors ||
                  next[i].updatedAt != suits_[i].updatedAt;
    // The same list: what was found before stays valid (no new vector under the callers' pointers).
    if (changed) suits_ = std::move(next);
    known_ = true;
    return changed;
}

const ServerSuit* ServerSuitBook::find(const std::string& ref) const {
    for (const ServerSuit& s : suits_)
        if (s.ref == ref) return &s;
    return nullptr;
}

// ------------------------------------------------------------------ textures

bool SuitTextureCache::loadBase(const fs::path& novaSheet) {
    baseTried_ = true;
    distinct_.clear();
    indexOf_.clear();
    clear();
    if (!loadRgbaImage(novaSheet, base_, w_, h_)) {
        base_.clear();
        return false;
    }
    // Each opaque pixel's colour among the distinct ones (recolor() depends on the colour only).
    std::unordered_map<uint32_t, uint32_t> seen;
    indexOf_.assign(base_.size() / 4, kNone);
    for (size_t i = 0; i < indexOf_.size(); ++i) {
        const uint8_t* p = base_.data() + i * 4;
        if (p[3] == 0) continue;
        const uint32_t key = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
        auto [it, fresh] = seen.emplace(key, static_cast<uint32_t>(distinct_.size() / 4));
        if (fresh) distinct_.insert(distinct_.end(), {p[0], p[1], p[2], 255});
        indexOf_[i] = it->second;
    }
    return true;
}

std::vector<uint8_t> SuitTextureCache::pixels(const SuitColors& c) const {
    if (base_.empty()) return {};
    std::vector<uint8_t> mapped(distinct_.size());
    recolorSuitPixels(distinct_.data(), mapped.data(), distinct_.size() / 4, c);
    std::vector<uint8_t> out = base_;  // transparent pixels and every alpha as they are
    for (size_t i = 0; i < indexOf_.size(); ++i)
        if (indexOf_[i] != kNone) std::memcpy(out.data() + i * 4, mapped.data() + static_cast<size_t>(indexOf_[i]) * 4, 3);
    return out;
}

TexturePtr SuitTextureCache::get(ResourceManager& resources, Renderer& renderer, const SuitColors& c) {
    const std::string key = c.key();
    if (auto it = byKey_.find(key); it != byKey_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second);  // the most recent first
        return it->second->second;
    }
    if (!baseTried_) {
        baseTried_ = true;
        auto path = resources.resolve("sprites/player/nova.png");
        if (!path || !loadBase(*path)) Log::error("Combinaisons créées en ligne : sprites/player/nova.png illisible");
    }
    if (base_.empty()) return nullptr;
    std::vector<uint8_t> px = pixels(c);
    SDL_Surface* s = SDL_CreateSurfaceFrom(w_, h_, SDL_PIXELFORMAT_RGBA32, px.data(), w_ * 4);
    if (!s) return nullptr;
    TexturePtr t = renderer.textureFromSurface(s, "<combinaison " + key + ">", true);
    SDL_DestroySurface(s);
    if (!t) return nullptr;
    lru_.emplace_front(key, t);
    byKey_[key] = lru_.begin();
    while (lru_.size() > kMaxTextures) {
        byKey_.erase(lru_.back().first);
        lru_.pop_back();
    }
    return t;
}

void SuitTextureCache::clear() {
    lru_.clear();
    byKey_.clear();
}

}  // namespace cosmic
