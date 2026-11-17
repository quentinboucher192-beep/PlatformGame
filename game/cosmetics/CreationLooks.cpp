#include "game/cosmetics/CreationLooks.h"

#include <algorithm>

#include "engine/core/FileUtil.h"

namespace cosmic {

namespace {

struct StyleName {
    const char* id;
    const char* name;
};

// The same lists as the server's (server/src/creations.ts), in the same order. Each style is
// drawn like the catalogue item named in its comment, in the creation's two colours.
const StyleName kTrailStyles[] = {
    {"comet", "Comète"},                  // « Comète »: a bright head, a tail that fades
    {"stardust", "Poussière d'étoiles"},  // « Poussière d'étoiles »: scattered sparks
    {"prism", "Ruban"},                   // « Prisme »: a ribbon from one colour to the other
    {"flame", "Flammèches"},              // « Flammèches »: embers rising
    {"aurora", "Aurore"},                 // « Aurore »: a waving glow
    {"bubbles", "Bulles"},                // « Bulles des abysses »: rising bubbles of light
};
const StyleName kFrameStyles[] = {
    {"metal", "Anneau poli"},       // « Bronze », « Argent », « Or »: a polished ring and its halo
    {"emerald", "Gemmes serties"},  // « Émeraude »: eight cut gems in the ring
    {"orbit", "Orbite"},            // « Orbite »: two moons going round
    {"nebula", "Nébuleuse"},        // « Nébuleuse »: turning clouds of colour
    {"constellation", "Constellation"},  // « Constellation »: three rings, stars around
    {"runic", "Cercle runique"},    // « Cercle runique »: runes lighting up one after the other
};
const StyleName kVictoryStyles[] = {
    {"fireworks", "Feu d'artifice"},    // three bursts
    {"starfall", "Pluie d'étoiles"},    // stars falling around Nova
    {"supernova", "Supernova"},         // a flash and a ring of light
    {"vortex", "Vortex"},               // three arms of sparks turning
    {"borealis", "Aurore boréale"},     // curtains of light
    {"corona", "Couronne solaire"},     // an eclipse and its rays
};

template <size_t N>
std::vector<std::string> ids(const StyleName (&list)[N]) {
    std::vector<std::string> out;
    for (const StyleName& s : list) out.emplace_back(s.id);
    return out;
}

template <size_t N>
const char* nameIn(const StyleName (&list)[N], const std::string& id) {
    for (const StyleName& s : list)
        if (id == s.id) return s.name;
    return nullptr;
}

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

CreationLookResolver& resolverSlot() {
    static CreationLookResolver r;
    return r;
}

}  // namespace

bool isCreationLookKind(const std::string& kind) { return kind == "trail" || kind == "frame" || kind == "victory"; }

const std::vector<std::string>& creationStyles(const std::string& kind) {
    static const std::vector<std::string> trail = ids(kTrailStyles), frame = ids(kFrameStyles), victory = ids(kVictoryStyles), none;
    return kind == "trail" ? trail : kind == "frame" ? frame : kind == "victory" ? victory : none;
}

bool isCreationStyle(const std::string& kind, const std::string& style) {
    const auto& l = creationStyles(kind);
    return std::find(l.begin(), l.end(), style) != l.end();
}

std::string creationStyleName(const std::string& kind, const std::string& style) {
    const char* n = kind == "trail" ? nameIn(kTrailStyles, style) : kind == "frame" ? nameIn(kFrameStyles, style) : kind == "victory" ? nameIn(kVictoryStyles, style) : nullptr;
    return n ? n : "";
}

std::pair<std::string, std::string> creationColorKeys(const std::string& kind) {
    if (kind == "trail") return {"start", "end"};
    if (kind == "frame") return {"border", "glow"};
    if (kind == "victory") return {"main", "second"};
    return {"", ""};
}

std::optional<CreationLook> creationLookFromJson(const std::string& kind, const Json& j) {
    if (!isCreationLookKind(kind) || !j.is_object()) return std::nullopt;
    CreationLook l;
    l.kind = kind;
    l.style = jstr(j, "style");
    if (!isCreationStyle(kind, l.style)) return std::nullopt;
    const Json& c = jfield(j, "colors", Json());
    const auto [ka, kb] = creationColorKeys(kind);
    auto a = parseHexColor(jstr(c, ka.c_str())), b = parseHexColor(jstr(c, kb.c_str()));
    if (!a || !b) return std::nullopt;
    l.a = *a;
    l.b = *b;
    if (kind == "trail") {
        const Json& w = jfield(j, "width", Json());
        const Json& life = jfield(j, "life_ms", Json());
        const Json& p = jfield(j, "particles", Json());
        if (!w.is_number_integer() || !life.is_number_integer() || !p.is_boolean()) return std::nullopt;
        l.width = w.get<int>();
        l.lifeMs = life.get<int>();
        l.particles = p.get<bool>();
        if (l.width < kTrailWidthMin || l.width > kTrailWidthMax || l.lifeMs < kTrailLifeMin || l.lifeMs > kTrailLifeMax) return std::nullopt;
    }
    return l;
}

Json creationLookToJson(const CreationLook& l) {
    const auto [ka, kb] = creationColorKeys(l.kind);
    Json j = {{"colors", {{ka, hexColor(l.a)}, {kb, hexColor(l.b)}}}, {"style", l.style}};
    if (l.kind == "trail") {
        j["width"] = l.width;
        j["life_ms"] = l.lifeMs;
        j["particles"] = l.particles;
    }
    return j;
}

CreationLook defaultCreationLook(const std::string& kind) {
    CreationLook l;
    l.kind = kind;
    if (kind == "trail") {
        // Gold by Nova, violet at the tail: the colours of « Comète du mois ».
        l.style = "comet";
        l.a = {255, 214, 90};
        l.b = {150, 70, 255};
    } else if (kind == "frame") {
        l.style = "metal";
        l.a = {200, 210, 225};
        l.b = {90, 210, 255};
    } else {
        l.kind = "victory";
        l.style = "fireworks";
        l.a = {255, 118, 150};
        l.b = {110, 214, 255};
    }
    return l;
}

bool isServerLookRef(const std::string& ref, std::string* kind) {
    for (const char* k : {"trail", "frame", "victory"}) {
        const std::string prefix = std::string("srv") + k + ":";
        if (ref.size() <= prefix.size() || ref.size() > prefix.size() + 10 || ref.compare(0, prefix.size(), prefix) != 0) continue;
        if (ref[prefix.size()] == '0') return false;
        for (size_t i = prefix.size(); i < ref.size(); ++i)
            if (ref[i] < '0' || ref[i] > '9') return false;
        if (kind) *kind = k;
        return true;
    }
    return false;
}

bool isServerCreationRef(const std::string& ref) { return isServerSuitRef(ref) || isServerLookRef(ref); }

// ------------------------------------------------------------------ the looks known

std::optional<ServerLook> ServerLookBook::fromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    ServerLook s;
    s.ref = jstr(j, "ref");
    std::string kind;
    if (!isServerLookRef(s.ref, &kind) || jstr(j, "kind") != kind) return std::nullopt;
    s.name = cleanName(jstr(j, "name"));
    s.updatedAt = jstr(j, "updated_at").substr(0, 40);
    auto look = creationLookFromJson(kind, j);
    if (s.name.empty() || !look) return std::nullopt;
    s.look = *look;
    return s;
}

Json ServerLookBook::toJson(const ServerLook& s) {
    Json j = creationLookToJson(s.look);
    j["ref"] = s.ref;
    j["kind"] = s.look.kind;
    j["name"] = s.name;
    j["updated_at"] = s.updatedAt;
    return j;
}

void ServerLookBook::load() {
    if (file_.empty()) return;
    std::error_code ec;
    if (!fs::exists(file_, ec)) return;
    auto j = FileUtil::loadJson(file_);
    if (!j || !j->is_object()) return;
    looks_.clear();
    for (const Json& e : jfield(*j, "creations", Json::array()))
        if (auto s = fromJson(e); s && !find(s->ref) && looks_.size() < 1000) looks_.push_back(std::move(*s));
    known_ = true;
}

bool ServerLookBook::save() const {
    if (file_.empty()) return false;
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    Json list = Json::array();
    for (const ServerLook& s : looks_) list.push_back(toJson(s));
    return FileUtil::writeTextAtomic(file_, Json{{"version", 1}, {"creations", list}}.dump(1));
}

bool ServerLookBook::applyList(const Json& creations) {
    if (!creations.is_array()) return false;
    std::vector<ServerLook> next;
    for (const Json& e : creations) {
        if (next.size() >= 1000) break;
        auto s = fromJson(e);
        if (!s) continue;
        bool dup = false;
        for (const ServerLook& o : next) dup = dup || o.ref == s->ref;
        if (!dup) next.push_back(std::move(*s));
    }
    bool changed = !known_ || next.size() != looks_.size();
    for (size_t i = 0; !changed && i < next.size(); ++i)
        changed = next[i].ref != looks_[i].ref || next[i].name != looks_[i].name || next[i].look != looks_[i].look || next[i].updatedAt != looks_[i].updatedAt;
    looks_ = std::move(next);
    known_ = true;
    return changed;
}

const ServerLook* ServerLookBook::find(const std::string& ref) const {
    for (const ServerLook& s : looks_)
        if (s.ref == ref) return &s;
    return nullptr;
}

// ------------------------------------------------------------------ resolver

void setCreationLookResolver(CreationLookResolver resolver) { resolverSlot() = std::move(resolver); }

std::optional<CreationLook> resolveCreationLook(const std::string& ref) {
    const CreationLookResolver& r = resolverSlot();
    if (!r || !isServerLookRef(ref)) return std::nullopt;
    return r(ref);
}

}  // namespace cosmic
