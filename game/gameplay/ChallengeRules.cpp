#include "game/gameplay/ChallengeRules.h"

#include <algorithm>
#include <set>

#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "packaging/Sha256.h"

namespace cosmic {

namespace {

void fail(std::string* error, const std::string& why) {
    if (error) *error = why;
}

/// Identifiers are plain ASCII (no escaping ever needed in the canonical form, the same bytes
/// in every JSON library): a challenge "daily-2026-11-16", a power "air_dash".
bool plainId(const std::string& s, bool allowDashDot) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || (allowDashDot && (c == '-' || c == '.' || (c >= 'A' && c <= 'Z')));
        if (!ok) return false;
    }
    return true;
}

}  // namespace

std::vector<std::string> ChallengeRules::sortedPowers() const {
    std::vector<std::string> out = powers;
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string ChallengeRules::canonical() const {
    std::string s = "{\"challenge\":\"" + challengeId + "\",\"hearts\":" + std::to_string(hearts) + ",\"slots\":" + std::to_string(slots) + ",\"powers\":[";
    bool first = true;
    for (const std::string& p : sortedPowers()) {
        if (!first) s += ',';
        first = false;
        s += "\"" + p + "\"";
    }
    s += "],\"stations\":";
    s += stations ? "true" : "false";
    s += ",\"checkpoints\":";
    s += checkpoints ? "true" : "false";
    s += "}";
    return s;
}

std::string ChallengeRules::hash() const { return Sha256::hex(canonical()); }

std::optional<ChallengeRules> ChallengeRules::fromJson(const std::string& challengeId, const Json& j, std::string* error) {
    if (!plainId(challengeId, true)) {
        fail(error, "identifiant de défi invalide");
        return std::nullopt;
    }
    if (!j.is_object()) {
        fail(error, "règles absentes");
        return std::nullopt;
    }
    auto intIn = [&](const char* key, int lo, int hi, int& out) {
        if (!j.contains(key) || !j[key].is_number_integer()) return false;
        int64_t v = j[key].get<int64_t>();
        if (v < lo || v > hi) return false;
        out = static_cast<int>(v);
        return true;
    };
    ChallengeRules r;
    r.challengeId = challengeId;
    if (!intIn("hearts", kMinHearts, kMaxHearts, r.hearts)) {
        fail(error, "cœurs hors limites");
        return std::nullopt;
    }
    if (!intIn("slots", kMinSlots, kMaxSlots, r.slots)) {
        fail(error, "emplacements hors limites");
        return std::nullopt;
    }
    if (!j.contains("stations") || !j["stations"].is_boolean() || !j.contains("checkpoints") || !j["checkpoints"].is_boolean()) {
        fail(error, "règles des stations ou des points de passage absentes");
        return std::nullopt;
    }
    r.stations = j["stations"].get<bool>();
    r.checkpoints = j["checkpoints"].get<bool>();
    const Json& p = j.contains("powers") ? j["powers"] : Json();
    if (!p.is_array()) {
        fail(error, "liste des pouvoirs absente");
        return std::nullopt;
    }
    std::set<std::string> seen;
    for (const Json& v : p) {
        if (!v.is_string() || !plainId(v.get<std::string>(), false) || !seen.insert(v.get<std::string>()).second) {
            fail(error, "pouvoir invalide ou en double");
            return std::nullopt;
        }
        r.powers.push_back(v.get<std::string>());
    }
    if (static_cast<int>(r.powers.size()) > r.slots) {
        fail(error, "plus de pouvoirs que d'emplacements");
        return std::nullopt;
    }
    r.powers = r.sortedPowers();
    return r;
}

std::optional<ChallengeRules> ChallengeRules::fromCanonical(const std::string& text, std::string* error) {
    if (text.empty() || text.size() > kMaxCanonicalBytes) {
        fail(error, "section des règles de taille invalide");
        return std::nullopt;
    }
    Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("challenge") || !j["challenge"].is_string() || j.size() != 6) {
        fail(error, "section des règles illisible");
        return std::nullopt;
    }
    auto r = fromJson(j["challenge"].get<std::string>(), j, error);
    if (!r) return std::nullopt;
    if (r->canonical() != text) {
        fail(error, "section des règles hors de la forme canonique");
        return std::nullopt;
    }
    return r;
}

void applyChallengeStart(GameWorld& w, const ChallengeRules& rules) {
    Player& p = w.player();
    // Hearts: exactly the challenge's (the player's own maximum moved by the bonus), full.
    int base = p.maxHealth() - p.bonusHealth;
    p.bonusHealth = std::clamp(rules.hearts, ChallengeRules::kMinHearts, ChallengeRules::kMaxHearts) - base;
    p.health = p.maxHealth();
    // Slots and powers: the imposed ones only, granted in canonical order with the usual rule
    // (Player::grantPower), never more than the slots.
    int slots = std::clamp(rules.slots, ChallengeRules::kMinSlots, ChallengeRules::kMaxSlots);
    p.clearPowers();
    p.setPowerSlots(slots);
    for (const std::string& id : rules.sortedPowers()) {
        if (static_cast<int>(p.powers().size()) >= slots) break;
        if (const PowerDefinition* d = w.content().power(id)) p.grantPower(*d);
    }
    p.refreshPassivePowers();
    p.restoreAirJumps();
}

}  // namespace cosmic
