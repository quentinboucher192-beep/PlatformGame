#include "game/online/Clans.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace cosmic::clans {

namespace {

/// The code points of a UTF-8 text (an invalid byte counts as U+FFFD).
std::vector<char32_t> codePoints(const std::string& s) {
    std::vector<char32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        bool ok = true;
        for (int k = 1; k < n; ++k) {
            unsigned char d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80) ok = false;
            cp = (cp << 6) | (d & 0x3F);
        }
        out.push_back(ok ? cp : 0xFFFD);
        i += ok ? n : 1;
    }
    return out;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// What the server removes from a line (control, zero-width and bidi characters).
bool invisible(char32_t c) {
    return c < 0x20 || (c >= 0x7F && c <= 0x9F) || (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) ||
           c == 0xFEFF;
}

bool space(char32_t c) { return c == ' ' || c == 0xA0 || c == 0x2007 || c == 0x202F || (c >= 0x2000 && c <= 0x200A) || c == 0x3000; }

/// Letters of the game's fonts (Latin with accents) and digits: the server's NAME_CHARS.
bool nameLetter(char32_t c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) ||
           (c >= 0xF8 && c <= 0x17F);
}

bool namePunct(char32_t c) { return c == ' ' || c == '\'' || c == 0x2019 || c == '-' || c == '_' || c == '.' || c == '!' || c == '&'; }

/// "78,4" / "78" (French decimal comma, one decimal only when needed).
std::string decimal(double v) {
    double r = std::round(v * 10.0) / 10.0;
    char buf[32];
    if (std::fabs(r - std::round(r)) < 1e-9) std::snprintf(buf, sizeof buf, "%.0f", r);
    else std::snprintf(buf, sizeof buf, "%.1f", r);
    std::string s = buf;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

}  // namespace

std::string bracket(const std::string& tag) { return tag.empty() ? std::string() : "[" + tag + "]"; }

std::string tagged(const std::string& username, const std::string& tag) { return tag.empty() ? username : bracket(tag) + " " + username; }

std::string taggedName(const Json& player) {
    if (!player.is_object()) return {};
    return tagged(jstr(player, "username"), jstr(player, "clan_tag"));
}

std::string roleLabel(const std::string& role) {
    if (role == "leader") return "Chef";
    if (role == "officer") return "Officier";
    return "Membre";
}

std::string policyLabel(const std::string& policy) {
    if (policy == "invite") return "Sur invitation";
    if (policy == "open") return "Ouvert à tous";
    return "Sur candidature";
}

const std::vector<std::string>& policyIds() {
    static const std::vector<std::string> ids = {"invite", "request", "open"};
    return ids;
}

std::string ordinal(int rank) { return rank > 0 ? "n°" + std::to_string(rank) : std::string(); }

std::string resultLabel(double score, bool finished, int rank) {
    std::string s = finished ? std::string("Vérifié") : decimal(score) + " %";
    if (rank > 0) s += " · " + ordinal(rank);
    return s;
}

std::string pointsLabel(double points) { return decimal(points) + " pts"; }

std::string modeLabel(int teamSize) { return teamSize >= 3 ? "Trio" : teamSize == 2 ? "Duo" : "Solo"; }

std::string cleanName(const std::string& raw) {
    std::string out;
    bool pendingSpace = false;
    for (char32_t c : codePoints(raw)) {
        if (invisible(c) || space(c) || c == '\t') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) out.push_back(' ');
        pendingSpace = false;
        appendUtf8(out, c);
    }
    return out;
}

int textLength(const std::string& s) { return static_cast<int>(codePoints(s).size()); }

std::string nameError(const std::string& name) {
    std::vector<char32_t> cps = codePoints(name);
    const int n = static_cast<int>(cps.size());
    if (n < kNameMin || n > kNameMax) return "Nom : de " + std::to_string(kNameMin) + " à " + std::to_string(kNameMax) + " caractères.";
    if (!nameLetter(cps.front())) return "Nom : il commence par une lettre ou un chiffre.";
    for (char32_t c : cps)
        if (!nameLetter(c) && !namePunct(c)) return "Nom : lettres, chiffres, espaces et ' - _ . ! & seulement.";
    return {};
}

std::string tagError(const std::string& tag) {
    if (tag.size() < 2 || tag.size() > 5) return "Tag : de 2 à 5 lettres ou chiffres.";
    std::string up;
    for (char c : tag) {
        if (!std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80) return "Tag : lettres et chiffres seulement (sans accent).";
        up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    static const char* reserved[] = {"ADMIN", "MODO", "MOD", "STAFF", "ROOT", "NULL", "DEV", "SYS"};
    for (const char* r : reserved)
        if (up == r) return "Tag : ce tag est réservé.";
    return {};
}

bool canManage(const std::string& myRole) { return myRole == "leader" || myRole == "officer"; }

bool canExclude(const std::string& myRole, const std::string& targetRole) {
    if (targetRole == "leader") return false;
    if (myRole == "leader") return targetRole == "officer" || targetRole == "member";
    if (myRole == "officer") return targetRole == "member";
    return false;
}

std::string successorName(const Json& members, const std::string& leaderId) {
    std::string firstMember;
    if (!members.is_array()) return {};
    for (const auto& m : members) {
        const Json& p = jfield(m, "player", Json());
        std::string id = jstr(p, "id"), role = jstr(m, "role");
        if (id == leaderId || role == "leader") continue;
        if (role == "officer") return jstr(p, "username");
        if (firstMember.empty()) firstMember = jstr(p, "username");
    }
    return firstMember;
}

const std::vector<std::string>& emblemShapes() {
    static const std::vector<std::string> shapes = {"orb", "star", "shield", "diamond", "hexagon", "crescent"};
    return shapes;
}

std::string shapeLabel(const std::string& shape) {
    if (shape == "star") return "Étoile";
    if (shape == "shield") return "Bouclier";
    if (shape == "diamond") return "Losange";
    if (shape == "hexagon") return "Hexagone";
    if (shape == "crescent") return "Croissant";
    return "Orbe";
}

JoinOffer joinOffer(const Json& clan, const Json& me, bool loggedIn, bool myClanKnown, const std::string& myClanId) {
    JoinOffer o;
    if (!jstr(me, "role").empty()) return o;
    if (!loggedIn) return {"login", "Se connecter pour rejoindre", true, {}};
    if (!myClanKnown) return o;
    if (!myClanId.empty() && myClanId != jstr(clan, "id")) {
        o.note = "Vous êtes déjà dans un clan.";
        return o;
    }
    const bool full = jget<int>(clan, "members", 0) >= jget<int>(clan, "max_members", kMaxMembers);
    const std::string kind = jstr(jfield(me, "request", Json()), "kind");
    const std::string policy = jstr(clan, "join_policy");
    if (kind == "apply") return {"withdraw", "Retirer ma candidature", true, "Candidature envoyée : réponse du chef ou d'un officier."};
    if (kind == "invite") o = {"accept", "Accepter l'invitation", !full, {}};
    else if (policy == "open") o = {"join", "Rejoindre", !full, {}};
    else if (policy == "request") o = {"apply", "Postuler", !full, {}};
    else {
        o.note = "Ce clan recrute sur invitation.";
        return o;
    }
    if (full) o.note = "Ce clan est complet.";
    return o;
}

std::string joinHint(const JoinOffer& o) {
    if (o.action == "login") return "Connectez-vous à Cosmic Online pour rejoindre ce clan.";
    if (!o.note.empty() && (!o.enabled || o.action.empty() || o.action == "withdraw")) return o.note;
    if (o.action == "join") return "Ouvert à tous : « Rejoindre » sur la fiche du clan.";
    if (o.action == "apply") return "Sur candidature : « Postuler » sur la fiche du clan.";
    if (o.action == "accept") return "Vous êtes invité dans ce clan : répondez depuis sa fiche.";
    return {};
}

std::vector<PendingRequest> pendingRequests(const Json& requests, const std::string& kind) {
    std::vector<PendingRequest> out;
    if (!requests.is_array()) return out;
    for (const auto& q : requests) {
        if (!q.is_object() || jstr(q, "kind") != kind) continue;
        const Json& p = jfield(q, "player", Json());
        PendingRequest r;
        r.kind = kind;
        r.playerId = jstr(p, "id");
        if (r.playerId.empty()) continue;
        r.username = jstr(p, "username");
        r.avatar = jstr(p, "avatar", "avatar_01");
        r.message = jstr(q, "message");
        r.createdAt = jstr(q, "created_at");
        const Json& by = jfield(q, "by", Json());
        r.byId = jstr(by, "id");
        r.byName = jstr(by, "username");
        out.push_back(std::move(r));
    }
    return out;
}

InviteOffer inviteOffer(const Json& mine, const Json& myPage, const Json& target, const std::string& targetId, const std::string& myId,
                        bool blocked) {
    InviteOffer o;
    const Json& clan = jfield(mine, "clan", Json());
    if (!clan.is_object() || !canManage(jstr(clan, "role")) || targetId.empty() || targetId == myId) return o;
    o.shown = true;
    const std::string id = jstr(clan, "id");
    const int max = jget<int>(clan, "max_members", kMaxMembers);
    if (blocked) {
        o.note = "Vous avez bloqué ce joueur : débloquez-le pour l'inviter.";
        return o;
    }
    if (target.is_object()) {
        o.note = jstr(target, "id") == id ? "Ce joueur est déjà membre de votre clan." : "Ce joueur est déjà dans un clan.";
        return o;
    }
    if (jget<int>(clan, "members", 0) >= max) {
        o.note = "Votre clan est complet (" + std::to_string(max) + " membres).";
        return o;
    }
    bool applied = false;
    // Only the page of the player's own clan says what waits (another clan's answer says nothing).
    const Json& requests = jfield(myPage, "requests", Json());
    if (jstr(jfield(myPage, "clan", Json()), "id") == id && requests.is_array()) {
        for (const auto& q : requests) {
            if (jstr(jfield(q, "player", Json()), "id") != targetId) continue;
            if (jstr(q, "kind") == "invite") {
                o.note = "Invitation déjà envoyée : elle attend sa réponse.";
                return o;
            }
            applied = applied || jstr(q, "kind") == "apply";
        }
        if (!applied && static_cast<int>(requests.size()) >= kMaxPending) {
            o.note = "Votre clan a trop de demandes en attente : traitez-en d'abord.";
            return o;
        }
    }
    o.enabled = true;
    o.note = applied ? "Ce joueur a postulé : l'inviter l'accepte aussitôt dans votre clan." : "Il recevra une invitation, à accepter depuis l'écran CLANS.";
    return o;
}

std::string clanIdForTag(const Json& list, const std::string& tag) {
    if (!list.is_array() || tag.empty()) return {};
    auto upper = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string want = upper(tag);
    for (const auto& c : list)
        if (upper(jstr(c, "tag")) == want && !jstr(c, "id").empty()) return jstr(c, "id");
    return {};
}

std::string colorHex(Color c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

Color colorFromHex(const std::string& hex, Color def) {
    if (hex.size() != 7 || hex[0] != '#') return def;
    unsigned v[3];
    for (int i = 0; i < 3; ++i) {
        unsigned x = 0;
        for (int k = 0; k < 2; ++k) {
            char c = hex[1 + i * 2 + k];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return def;
            x = x * 16 + static_cast<unsigned>(d);
        }
        v[i] = x;
    }
    return Color{static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2]), 255};
}

Emblem emblemFromJson(const Json& j) {
    Emblem e;
    if (!j.is_object()) return e;
    const std::string shape = jstr(j, "shape");
    for (const auto& s : emblemShapes())
        if (s == shape) e.shape = shape;
    e.primary = colorFromHex(jstr(j, "primary"), e.primary);
    e.secondary = colorFromHex(jstr(j, "secondary"), e.secondary);
    return e;
}

Json emblemToJson(const Emblem& e) { return {{"shape", e.shape}, {"primary", colorHex(e.primary)}, {"secondary", colorHex(e.secondary)}}; }

const std::vector<NamedColor>& palette() {
    static const std::vector<NamedColor> colors = {
        {"Or", {255, 210, 122, 255}},        {"Braise", {255, 106, 58, 255}},    {"Rubis", {255, 77, 109, 255}},
        {"Rose", {255, 122, 184, 255}},      {"Améthyste", {166, 107, 255, 255}}, {"Saphir", {107, 139, 255, 255}},
        {"Turquoise", {62, 230, 214, 255}},  {"Émeraude", {120, 230, 140, 255}}, {"Argent", {232, 238, 252, 255}},
        {"Acier", {143, 155, 189, 255}},     {"Nuit", {42, 22, 80, 255}},        {"Abysse", {18, 48, 74, 255}},
    };
    return colors;
}

int nearestPaletteIndex(Color c) {
    int best = 0;
    long bestD = -1;
    const auto& p = palette();
    for (size_t i = 0; i < p.size(); ++i) {
        long dr = static_cast<long>(c.r) - p[i].color.r, dg = static_cast<long>(c.g) - p[i].color.g, db = static_cast<long>(c.b) - p[i].color.b;
        long d = dr * dr + dg * dg + db * db;
        if (bestD < 0 || d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::vector<Vec2> shapeOutline(const std::string& shape, Vec2 c, float r) {
    std::vector<Vec2> pts;
    constexpr float kPi = 3.14159265358979f;
    auto ring = [&](int n, float start, float radius) {
        for (int i = 0; i < n; ++i) {
            float a = start + 2.0f * kPi * static_cast<float>(i) / static_cast<float>(n);
            pts.push_back({c.x + std::cos(a) * radius, c.y + std::sin(a) * radius});
        }
    };
    if (shape == "star") {
        // The ten points of a five-pointed star, the first one up.
        for (int i = 0; i < 10; ++i) {
            float a = -kPi * 0.5f + kPi * static_cast<float>(i) / 5.0f;
            float radius = (i % 2 == 0) ? r : r * 0.45f;
            pts.push_back({c.x + std::cos(a) * radius, c.y + std::sin(a) * radius});
        }
    } else if (shape == "shield") {
        pts = {{c.x - r * 0.8f, c.y - r * 0.85f}, {c.x + r * 0.8f, c.y - r * 0.85f}, {c.x + r * 0.8f, c.y + r * 0.1f},
               {c.x, c.y + r * 0.95f},           {c.x - r * 0.8f, c.y + r * 0.1f}};
    } else if (shape == "diamond") {
        pts = {{c.x, c.y - r}, {c.x + r * 0.72f, c.y}, {c.x, c.y + r}, {c.x - r * 0.72f, c.y}};
    } else if (shape == "hexagon") {
        ring(6, -kPi * 0.5f, r);
    } else {
        ring(32, 0.0f, r);  // orb, crescent: a disc
    }
    return pts;
}

}  // namespace cosmic::clans
