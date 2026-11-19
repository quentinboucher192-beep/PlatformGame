#include "game/raid/BossHitboxes.h"

#include <algorithm>
#include <cmath>

#include "game/content/ContentDatabase.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/levels/LevelData.h"
#include "game/raid/RaidBranch.h"

namespace cosmic::raid {

namespace {

std::vector<Rect> rectsOf(const Json& list) {
    std::vector<Rect> out;
    if (!list.is_array()) return out;
    for (const Json& m : list) {
        const Json r = m.is_object() ? m.value("rect", Json()) : Json();
        if (r.is_array() && r.size() == 4 && r[0].is_number() && r[1].is_number() && r[2].is_number() && r[3].is_number())
            out.push_back({r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()});
    }
    return out;
}

std::vector<Vec2> spotsOf(const Json& list) {
    std::vector<Vec2> out;
    if (!list.is_array()) return out;
    for (const Json& s : list)
        if (s.is_array() && s.size() >= 2 && s[0].is_number() && s[1].is_number()) out.push_back({s[0].get<float>(), s[1].get<float>()});
    return out;
}

Vec2 sizeOf(const ContentDatabase& content, const std::string& id, Vec2 fallback) {
    const EnemyDefinition* d = content.enemy(id);
    return d ? d->size : fallback;
}

std::string nameOf(const ContentDatabase& content, const std::string& id) {
    const EnemyDefinition* d = content.enemy(id);
    return d ? d->name : id;
}

/// What the plates of a routine are called (its fight's words).
std::string plateName(const std::string& routine, const std::string& key) {
    if (key == "mirrors") return "Miroirs";
    if (key == "generators") return "Générateurs de gravité";
    if (routine == "voidworm") return "Crochets";
    if (routine == "dreadfang") return "Ancres";
    if (routine == "heartbreaker") return "Côtes";
    if (routine == "coeur") return "Artères";
    return "Évents";
}

/// « 2 », « 2,5 » (French decimals).
std::string decimal(float v) {
    const int t = static_cast<int>(std::lround(v * 10.0f));
    return std::to_string(t / 10) + (t % 10 ? "," + std::to_string(t % 10) : std::string());
}

}  // namespace

Vec2 spotFeet(const Rect& bossBox, float facing, Vec2 spot) { return {bossBox.center().x + spot.x * facing, bossBox.bottom() + spot.y}; }

BossHitboxes bossHitboxes(const GigabossRules& rules, const EnemyDefinition& boss, const ContentDatabase& content, const LevelData* lair, int phaseCount) {
    BossHitboxes out;
    out.body = Rect::fromBottomCenter({0.0f, 0.0f}, boss.size);  // as Enemy places its box at its feet
    const Json& x = rules.extra;
    // Its plates in its arena (the collective mechanism), and their name.
    std::vector<Rect> plates = rules.vents;
    std::string plateKey;
    for (const char* key : {"mirrors", "generators"})
        if (plates.empty()) {
            plates = rectsOf(x.value(key, Json()));
            if (!plates.empty()) plateKey = key;
        }
    // The targets of its mechanism on its body (restraints, plates to break), what threatens from it.
    std::vector<HitPart> targets, threats;
    for (const char* key : {"restraints", "plates"}) {
        const Json t = x.value(key, Json());
        if (!t.is_object()) continue;
        const std::string enemy = t.value("enemy", std::string());
        const Vec2 size = sizeOf(content, enemy, {24.0f, 24.0f});
        for (const Vec2& s : spotsOf(t.value("spots", Json())))
            targets.push_back({HitPart::Kind::Weak, Rect::fromBottomCenter(spotFeet(out.body, 1.0f, s), size), "À briser · " + nameOf(content, enemy)});
    }
    for (const Vec2& e : spotsOf(x.value("eyes", Json())))
        threats.push_back({HitPart::Kind::Hurt, Rect::fromCenter(spotFeet(out.body, 1.0f, e), {28.0f, 28.0f}), "Regard (laser)"});
    out.mechanism = plates.empty() ? std::string() : plateName(rules.routine, plateKey);
    // Its armour's phases: [from, until) of its rules (Skullmaw: its veil), when it has a mechanism.
    int from = rules.carapaceFrom, until = rules.carapaceUntil;
    if (const Json v = x.value("veil", Json()); v.is_array() && v.size() == 2 && v[0].is_number_integer() && v[1].is_number_integer()) {
        from = v[0].get<int>();
        until = v[1].get<int>();
    }
    const bool mechanism = !plates.empty() || !targets.empty();
    // Where it stands, and its arena.
    const LevelObject* o = lair ? lair->findObject(rules.boss) : nullptr;
    out.feet = o ? Vec2{o->rect.center().x, o->rect.bottom()} : Vec2{0.0f, 0.0f};
    Rect bounds = Rect::fromBottomCenter(out.feet, boss.size);
    for (const Rect& p : plates) bounds = bounds.united(p);
    out.arena = bounds;
    if (lair)
        if (const Json a = lair->metadata.value("raid", Json::object()).value("arena", Json()); a.is_object())
            if (const Json r = a.value("rect", Json()); r.is_array() && r.size() == 4) out.arena = {r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};
    const HitPart contact{HitPart::Kind::Hurt, out.body, "Contact : il blesse qui le touche"};
    for (int p = 1; p <= std::max(1, phaseCount); ++p) {
        const bool armour = mechanism && p >= from && p < until;
        if (!armour) {
            HitboxView v;
            v.phase = p;
            v.state = "fight";
            v.stateName = "Combat";
            v.parts.push_back({HitPart::Kind::Weak, out.body, "Corps : il prend les coups"});
            if (boss.contactDamage > 0.0f) v.parts.push_back(contact);
            for (const HitPart& t : threats) v.parts.push_back(t);
            v.hitWhen = "Frappez son corps à tout moment de cette phase, en évitant son contact et ses coups annoncés.";
            out.views.push_back(std::move(v));
            continue;
        }
        HitboxView a;
        a.phase = p;
        a.state = "armour";
        a.stateName = "Blindé";
        a.parts.push_back({HitPart::Kind::Armour, out.body, "Corps blindé : rien ne le blesse"});
        if (boss.contactDamage > 0.0f) a.parts.push_back(contact);
        for (const HitPart& t : targets) a.parts.push_back(t);
        for (const HitPart& t : threats) a.parts.push_back(t);
        for (size_t k = 0; k < plates.size(); ++k)
            a.plates.push_back({HitPart::Kind::Mechanism, plates[k], out.mechanism + " " + std::to_string(k + 1)});
        a.hitWhen = !targets.empty() ? "Frappez ses cibles en vert ensemble : son blindage tombe quand elles sont brisées ; son corps ne prend rien."
                                     : "Ne frappez pas son corps : tenez ses " + std::to_string(plates.size()) + " " + out.mechanism +
                                           " (en or, dans l'arène) ensemble pour briser son blindage.";
        out.views.push_back(std::move(a));
        HitboxView e;
        e.phase = p;
        e.state = "exposed";
        e.stateName = "À nu";
        e.parts.push_back({HitPart::Kind::Weak, out.body, "Corps à nu : dégâts × " + decimal(rules.exposedMult)});
        if (boss.contactDamage > 0.0f) e.parts.push_back(contact);
        for (size_t k = 0; k < rules.weakSpots.size(); ++k) {
            const std::string id = rules.weakEnemyAt(k);
            e.parts.push_back({HitPart::Kind::Weak, Rect::fromBottomCenter(spotFeet(out.body, 1.0f, rules.weakSpots[k]), sizeOf(content, id, {24.0f, 24.0f})),
                               "Point faible · " + nameOf(content, id)});
        }
        for (const HitPart& t : threats) e.parts.push_back(t);
        e.hitWhen = "Frappez ici quand son blindage éclate : ses points faibles et son corps (dégâts × " + decimal(rules.exposedMult) + ") pendant " + std::to_string(static_cast<int>(std::lround(rules.exposed))) + " s.";
        out.views.push_back(std::move(e));
    }
    return out;
}

}  // namespace cosmic::raid
