#include "game/online/Gifts.h"

#include <chrono>
#include <cstdio>
#include <ctime>

#include "game/online/OnlineSession.h"

namespace cosmic::gifts {

namespace {

struct WrapDef {
    const char* id;
    const char* name;
    Color light;
};
// The order and the light of tools/assetgen/environment/gift_wraps.py (tests/test_gifts.cpp compares them with wraps.json).
const WrapDef kWraps[kWrapCount] = {
    {"or", "Or", {255, 205, 110, 255}},
    {"amethyste", "Améthyste", {190, 150, 255, 255}},
    {"azur", "Azur", {94, 224, 255, 255}},
    {"rubis", "Rubis", {255, 112, 136, 255}},
    {"emeraude", "Émeraude", {110, 230, 160, 255}},
    {"nebuleuse", "Nébuleuse", {240, 140, 240, 255}},
    {"givre", "Givre", {180, 226, 255, 255}},
    {"braise", "Braise", {255, 154, 48, 255}},
    {"cosmos", "Cosmos", {122, 184, 255, 255}},
    {"rouage", "Rouage", {232, 196, 136, 255}},
    {"asterion", "Asterion", {255, 90, 120, 255}},
};

/// The code point at `i` of `s` and its length in bytes (an invalid byte: itself, one byte).
uint32_t codeAt(const std::string& s, size_t i, size_t* len) {
    const auto c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (i + n > s.size()) n = 1;
    uint32_t cp = n == 1 ? c : n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
    *len = n;
    return cp;
}

bool removed(uint32_t cp) {
    return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
           (cp >= 0x2066 && cp <= 0x2069) || cp == 0xFEFF;
}

}  // namespace

const std::vector<std::string>& wrapIds() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> v;
        for (const auto& w : kWraps) v.emplace_back(w.id);
        return v;
    }();
    return ids;
}

int wrapIndex(const std::string& id) {
    for (int i = 0; i < kWrapCount; ++i)
        if (id == kWraps[i].id) return i;
    return -1;
}

std::string wrapName(const std::string& id) {
    const int i = wrapIndex(id);
    return i >= 0 ? kWraps[i].name : std::string("Or");
}

Color wrapLight(const std::string& id) {
    const int i = wrapIndex(id);
    return kWraps[i >= 0 ? i : 0].light;
}

int textLength(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        size_t len = 1;
        codeAt(s, i, &len);
        i += len;
        ++n;
    }
    return n;
}

std::string cleanMessage(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        size_t len = 1;
        const uint32_t cp = codeAt(s, i, &len);
        if (!removed(cp)) out.append(s, i, len);
        i += len;
    }
    const size_t a = out.find_first_not_of(" ");
    if (a == std::string::npos) return {};
    const size_t b = out.find_last_not_of(" ");
    return out.substr(a, b - a + 1);
}

std::string cutText(const std::string& s, int n) {
    size_t i = 0;
    for (int k = 0; k < n && i < s.size(); ++k) {
        size_t len = 1;
        codeAt(s, i, &len);
        i += len;
    }
    return s.substr(0, i);
}

const std::vector<std::string>& rewardKinds() {
    static const std::vector<std::string> k = {"suit", "trail", "frame", "title", "victory", "badge"};
    return k;
}

std::string kindName(const std::string& kind) {
    if (kind == "suit") return "Combinaison";
    if (kind == "trail") return "Traînée";
    if (kind == "frame") return "Cadre d'avatar";
    if (kind == "title") return "Titre";
    if (kind == "victory") return "Effet de victoire";
    if (kind == "badge") return "Badge de profil";
    return "Récompense";
}

std::optional<Gift> parseGift(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    Gift g;
    g.id = jget<int64_t>(j, "id", 0);
    if (g.id <= 0) return std::nullopt;
    const Json& from = jfield(j, "from", Json());
    g.fromId = jstr(from, "id");
    g.from = jstr(from, "username", "Cosmic Online");
    g.wrap = wrapIndex(jstr(j, "wrap")) >= 0 ? jstr(j, "wrap") : std::string("or");
    g.message = cutText(cleanMessage(jstr(j, "message")), kMaxMessage);
    g.createdAt = jstr(j, "created_at");
    g.openedAt = jstr(j, "opened_at");
    g.opened = jget<bool>(j, "opened", false);
    if (const Json& r = jfield(j, "reward", Json()); g.opened && r.is_object()) {
        Reward w{jstr(r, "kind"), jstr(r, "id"), jstr(r, "name")};
        bool known = false;
        for (const auto& k : rewardKinds()) known = known || k == w.kind;
        if (known && !w.id.empty() && w.id.size() <= 64) g.reward = w;
    }
    return g;
}

std::vector<Gift> parseList(const Json& data) {
    std::vector<Gift> out;
    const Json& list = jfield(data, "gifts", Json::array());
    if (!list.is_array()) return out;
    for (const Json& j : list)
        if (auto g = parseGift(j)) out.push_back(std::move(*g));
    return out;
}

std::vector<const Gift*> filtered(const std::vector<Gift>& all, Filter f) {
    std::vector<const Gift*> out;
    for (const Gift& g : all)
        if (f == Filter::All || (f == Filter::ToOpen) == !g.opened) out.push_back(&g);
    return out;
}

int unopenedCount(const std::vector<Gift>& all) {
    int n = 0;
    for (const Gift& g : all) n += g.opened ? 0 : 1;
    return n;
}

const Gift* nextToOpen(const std::vector<Gift>& all, int64_t current) {
    size_t at = 0;
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].id == current) at = i + 1;
    for (size_t k = 0; k < all.size(); ++k) {
        const Gift& g = all[(at + k) % all.size()];
        if (!g.opened && g.id != current) return &g;
    }
    return nullptr;
}

void applyOpened(std::vector<Gift>& all, const Gift& opened) {
    for (Gift& g : all)
        if (g.id == opened.id) g = opened;
}

std::string dateText(const std::string& iso) {
    auto t = parseIsoUtc(iso);
    if (!t) return {};
    std::time_t tt = std::chrono::system_clock::to_time_t(*t);
    std::tm m{};
#ifdef _WIN32
    localtime_s(&m, &tt);
#else
    localtime_r(&tt, &m);
#endif
    char b[24];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d", m.tm_mday, m.tm_mon + 1, m.tm_year + 1900);
    return b;
}

}  // namespace cosmic::gifts
