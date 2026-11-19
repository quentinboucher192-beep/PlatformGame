#include "game/enemies/HitParts.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "engine/animation/Animation.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "engine/physics/Collision.h"
#include "game/enemies/Enemy.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

namespace {

std::string baseName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

PartRole roleOf(const std::string& id) {
    if (id == "weak") return PartRole::Weak;
    if (id == "armour") return PartRole::Armour;
    if (id == "hurt") return PartRole::Hurt;
    return PartRole::Normal;
}

/// The projections of a polygon on an axis.
void project(const std::vector<Vec2>& poly, Vec2 axis, float& lo, float& hi) {
    lo = hi = poly[0].x * axis.x + poly[0].y * axis.y;
    for (size_t i = 1; i < poly.size(); ++i) {
        const float d = poly[i].x * axis.x + poly[i].y * axis.y;
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
}

/// Its current cell's pieces placed in the world, by the pose the simulation gives it.
struct Pose {
    const HitPartsSheet* sheet = nullptr;
    const std::vector<HitPartPiece>* pieces = nullptr;
};

Pose poseOf(const Enemy& e) {
    Pose p;
    const AnimationSetPtr& set = e.animator().set();
    if (!set) return p;
    p.sheet = HitPartsSheet::of(set->sourcePath);
    if (!p.sheet || set->frameWidth <= 0 || set->frameHeight <= 0) return p;
    const Rect src = e.animator().sourceRect();
    p.pieces = p.sheet->cell(static_cast<int>(src.y) / set->frameHeight, static_cast<int>(src.x) / set->frameWidth);
    return p;
}

/// The part of `e` a shape touches, by priority (weak > normal > armour), its rule.
template <typename Touch>
std::optional<PartHit> bestPart(const Enemy& e, Touch touches) {
    const Pose pose = poseOf(e);
    if (!pose.pieces) return std::nullopt;
    const PartRules& rules = PartRules::of(e.def().id);
    const Vec2 feet = e.box.bottomCenter();
    const bool flip = e.facing < 0.0f;
    std::optional<PartHit> best;
    auto rank = [](PartRole r) { return r == PartRole::Weak ? 0 : r == PartRole::Normal ? 1 : 2; };
    for (const HitPartPiece& piece : *pose.pieces) {
        if (piece.part < 0 || piece.part >= static_cast<int>(pose.sheet->parts.size())) continue;
        const PartRule rule = rules.ruleOr(pose.sheet->parts[static_cast<size_t>(piece.part)]);
        if (rule.role == PartRole::Hurt) continue;  // it only hurts: never hit
        if (best && rank(best->role) <= rank(rule.role)) continue;
        if (!touches(placePiece(piece, *pose.sheet, feet, e.def().scale, flip))) continue;
        best = PartHit{rule.name, rule.role, rule.damage};
    }
    return best;
}

}  // namespace

// ------------------------------------------------------------------ the sheets

const std::vector<HitPartPiece>* HitPartsSheet::cell(int row, int column) const {
    const auto it = cells.find({row, column});
    return it == cells.end() ? nullptr : &it->second;
}

const std::vector<HitPartPiece>* HitPartsSheet::frame(const AnimationSet& set, const AnimationClip& clip, int f) const {
    const Rect src = set.frameRect(clip, f);
    if (set.frameWidth <= 0 || set.frameHeight <= 0) return nullptr;
    return cell(static_cast<int>(src.y) / set.frameHeight, static_cast<int>(src.x) / set.frameWidth);
}

std::shared_ptr<HitPartsSheet> HitPartsSheet::parse(const Json& j) {
    if (!j.is_object() || jstr(j, "format") != "cosmic.hitparts") return nullptr;
    auto s = std::make_shared<HitPartsSheet>();
    s->sheet = jstr(j, "sheet");
    s->profile = jstr(j, "profile");
    s->frameWidth = jget<int>(j, "frameWidth", 0);
    s->frameHeight = jget<int>(j, "frameHeight", 0);
    s->pivot = vec2FromJson(j.value("pivot", Json()), {s->frameWidth * 0.5f, static_cast<float>(s->frameHeight)});
    for (const Json& n : j.value("parts", Json::array()))
        if (n.is_string()) s->parts.push_back(n.get<std::string>());
    const Json cells = j.value("cells", Json::object());
    if (!cells.is_object()) return s;
    for (auto& [key, list] : cells.items()) {
        const size_t comma = key.find(',');
        if (comma == std::string::npos || !list.is_array()) continue;
        const int row = std::atoi(key.substr(0, comma).c_str()), col = std::atoi(key.substr(comma + 1).c_str());
        std::vector<HitPartPiece>& out = s->cells[{row, col}];
        for (const Json& item : list) {
            if (!item.is_array() || item.size() != 2 || !item[0].is_number_integer() || !item[1].is_array()) continue;
            HitPartPiece p;
            p.part = item[0].get<int>();
            const Json& v = item[1];
            for (size_t k = 0; k + 1 < v.size(); k += 2)
                if (v[k].is_number() && v[k + 1].is_number()) p.pts.push_back({v[k].get<float>(), v[k + 1].get<float>()});
            if (p.pts.size() >= 3) out.push_back(std::move(p));
        }
    }
    return s;
}

const HitPartsSheet* HitPartsSheet::of(const std::string& sheetPath) {
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<HitPartsSheet>> loaded;
    std::lock_guard<std::mutex> lock(mutex);
    const std::string name = baseName(sheetPath);
    if (name.empty()) return nullptr;
    auto it = loaded.find(name);
    if (it == loaded.end()) {
        std::shared_ptr<HitPartsSheet> s;
        if (auto j = FileUtil::loadJson(Paths::assetsDir() / "hitparts" / (name + ".json"))) {
            // A phase sheet with the very masks of the boss's own: its parts are those of that sheet.
            const std::string same = jstr(*j, "sameAs");
            if (!same.empty() && same != name + ".json") {
                if (auto base = FileUtil::loadJson(Paths::assetsDir() / "hitparts" / same)) {
                    s = parse(*base);
                    if (s) s->sheet = jstr(*j, "sheet");
                }
            } else {
                s = parse(*j);
            }
        }
        it = loaded.emplace(name, std::move(s)).first;
    }
    return it->second.get();
}

// ------------------------------------------------------------------ the rules

const PartRule* PartRules::rule(const std::string& name) const {
    for (const PartRule& r : rules)
        if (r.name == name) return &r;
    return nullptr;
}

PartRule PartRules::ruleOr(const std::string& name) const {
    if (const PartRule* r = rule(name)) return *r;
    PartRule r;
    r.name = name;
    return r;
}

PartRules PartRules::parse(const Json& parts, int fromVersion) {
    PartRules out;
    out.fromVersion = std::max(0, fromVersion);
    if (!parts.is_object()) return out;
    out.profile = jstr(parts, "profile");
    const Json roles = parts.value("roles", Json::object());
    if (!roles.is_object()) return out;
    for (auto& [name, r] : roles.items()) {
        if (!r.is_object()) continue;
        PartRule rule;
        rule.name = name;
        rule.role = roleOf(jstr(r, "role", "normal"));
        rule.damage = std::clamp(jget<float>(r, "damage", rule.role == PartRole::Hurt ? 0.0f : 1.0f), 0.0f, 4.0f);
        for (const Json& a : r.value("hurtDuring", Json::array()))
            if (a.is_string()) rule.hurtDuring.push_back(a.get<std::string>());
        out.rules.push_back(std::move(rule));
    }
    std::sort(out.rules.begin(), out.rules.end(), [](const PartRule& a, const PartRule& b) { return a.name < b.name; });
    return out;
}

const PartRules& PartRules::of(const std::string& enemyId) {
    static std::mutex mutex;
    static std::map<std::string, PartRules> loaded;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = loaded.find(enemyId);
    if (it == loaded.end()) {
        PartRules r;
        if (!enemyId.empty() && enemyId.find_first_of("/\\.") == std::string::npos)
            if (auto j = FileUtil::loadJson(Paths::dataDir() / "enemies" / (enemyId + ".json")))
                r = parse(j->value("parts", Json()), jget<int>(*j, "partsFromVersion", 0));
        it = loaded.emplace(enemyId, std::move(r)).first;
    }
    return it->second;
}

const char* partRoleId(PartRole r) {
    switch (r) {
        case PartRole::Weak: return "weak";
        case PartRole::Armour: return "armour";
        case PartRole::Hurt: return "hurt";
        case PartRole::Normal: break;
    }
    return "normal";
}

const char* partRoleLabel(PartRole r) {
    switch (r) {
        case PartRole::Weak: return "Point faible";
        case PartRole::Armour: return "Blindée";
        case PartRole::Hurt: return "Blesse";
        case PartRole::Normal: break;
    }
    return "Partie normale";
}

std::string partDamageLabel(const PartRule& r) {
    if (r.role == PartRole::Hurt) return "—";
    const int t = static_cast<int>(std::lround(r.damage * 100.0f));
    std::string s = std::to_string(t / 100);
    if (t % 100) {
        s += "," + std::to_string((t % 100) / 10);
        if (t % 10) s += std::to_string(t % 10);
    }
    return "× " + s;
}

// ------------------------------------------------------------------ geometry

std::vector<Vec2> placePiece(const HitPartPiece& piece, const HitPartsSheet& sheet, Vec2 feet, float scale, bool flip) {
    std::vector<Vec2> out;
    out.reserve(piece.pts.size());
    for (const Vec2& v : piece.pts) {
        const float dx = (v.x - sheet.pivot.x) * scale;
        out.push_back({flip ? feet.x - dx : feet.x + dx, feet.y + (v.y - sheet.pivot.y) * scale});
    }
    return out;
}

bool convexOverlapsRect(const std::vector<Vec2>& poly, const Rect& r) {
    if (poly.size() < 3) return false;
    float lo = 0.0f, hi = 0.0f;
    // The box's own axes: its bounds.
    project(poly, {1.0f, 0.0f}, lo, hi);
    if (hi < r.x || lo > r.right()) return false;
    project(poly, {0.0f, 1.0f}, lo, hi);
    if (hi < r.y || lo > r.bottom()) return false;
    // The polygon's edges.
    const std::vector<Vec2> box = {{r.x, r.y}, {r.right(), r.y}, {r.right(), r.bottom()}, {r.x, r.bottom()}};
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
        const Vec2 axis{a.y - b.y, b.x - a.x};
        if (axis.x == 0.0f && axis.y == 0.0f) continue;
        float plo = 0.0f, phi = 0.0f, blo = 0.0f, bhi = 0.0f;
        project(poly, axis, plo, phi);
        project(box, axis, blo, bhi);
        if (phi < blo || plo > bhi) return false;
    }
    return true;
}

bool convexContains(const std::vector<Vec2>& poly, Vec2 p) {
    if (poly.size() < 3) return false;
    int sign = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
        const float c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (c == 0.0f) continue;
        const int s = c > 0.0f ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return true;
}

bool convexOverlapsCircle(const std::vector<Vec2>& poly, Vec2 c, float radius) {
    if (poly.size() < 3) return false;
    if (convexContains(poly, c)) return true;
    const float r2 = radius * radius;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
        const Vec2 ab{b.x - a.x, b.y - a.y};
        const float len2 = ab.x * ab.x + ab.y * ab.y;
        float t = len2 > 0.0f ? ((c.x - a.x) * ab.x + (c.y - a.y) * ab.y) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float dx = a.x + ab.x * t - c.x, dy = a.y + ab.y * t - c.y;
        if (dx * dx + dy * dy <= r2) return true;
    }
    return false;
}

// ------------------------------------------------------------------ an enemy

bool fightsByParts(const Enemy& e, const GameWorld& w) {
    const int from = PartRules::of(e.def().id).fromVersion;
    if (from <= 0 || w.simulationVersion() < from) return false;  // drawn only, or a replay of before: its box
    const AnimationSetPtr& set = e.animator().set();
    return set && HitPartsSheet::of(set->sourcePath) != nullptr;
}

std::optional<PartHit> partHitBy(const Enemy& e, const Rect& area) {
    return bestPart(e, [&](const std::vector<Vec2>& poly) { return convexOverlapsRect(poly, area); });
}

std::optional<PartHit> partHitBy(const Enemy& e, Vec2 c, float radius) {
    return bestPart(e, [&](const std::vector<Vec2>& poly) { return convexOverlapsCircle(poly, c, radius); });
}

bool reaches(const Enemy& e, const GameWorld& w, const Rect& area, float* mult) {
    if (mult) *mult = 1.0f;
    if (!fightsByParts(e, w)) return e.box.intersects(area);
    const auto hit = partHitBy(e, area);
    if (hit && mult) *mult = hit->damage;
    return hit.has_value();
}

bool reaches(const Enemy& e, const GameWorld& w, Vec2 c, float radius, float* mult) {
    if (mult) *mult = 1.0f;
    if (!fightsByParts(e, w)) return Collision::circleIntersectsRect(c, radius, e.box);
    const auto hit = partHitBy(e, c, radius);
    if (hit && mult) *mult = hit->damage;
    return hit.has_value();
}

bool hurtsWithParts(const Enemy& e, const Rect& box) {
    const Pose pose = poseOf(e);
    if (!pose.pieces) return false;
    const PartRules& rules = PartRules::of(e.def().id);
    const AttackDef* attack = e.state() == EnemyState::Attack ? e.currentAttack() : nullptr;
    const Vec2 feet = e.box.bottomCenter();
    const bool flip = e.facing < 0.0f;
    for (const HitPartPiece& piece : *pose.pieces) {
        if (piece.part < 0 || piece.part >= static_cast<int>(pose.sheet->parts.size())) continue;
        const PartRule rule = rules.ruleOr(pose.sheet->parts[static_cast<size_t>(piece.part)]);
        const bool now = rule.role == PartRole::Hurt ||
                         (attack && std::find(rule.hurtDuring.begin(), rule.hurtDuring.end(), attack->id) != rule.hurtDuring.end());
        if (now && convexOverlapsRect(placePiece(piece, *pose.sheet, feet, e.def().scale, flip), box)) return true;
    }
    return false;
}

}  // namespace cosmic
