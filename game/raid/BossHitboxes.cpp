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
    // 2.75.2: Heartbreaker's plates are the armour of its FIRST phase (HeartbreakerRoutine::stepPlates,
    // gone past it), not of its ribs' phases.
    std::vector<HitPart> targets, threats, firstPlates;
    for (const char* key : {"restraints", "plates"}) {
        const Json t = x.value(key, Json());
        if (!t.is_object()) continue;
        const std::string enemy = t.value("enemy", std::string());
        const Vec2 size = sizeOf(content, enemy, {24.0f, 24.0f});
        std::vector<HitPart>& to = std::string(key) == "plates" && rules.routine == "heartbreaker" ? firstPlates : targets;
        for (const Vec2& s : spotsOf(t.value("spots", Json())))
            to.push_back({HitPart::Kind::Weak, Rect::fromBottomCenter(spotFeet(out.body, 1.0f, s), size), "À briser · " + nameOf(content, enemy), enemy});
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
        if (p == 1 && !firstPlates.empty()) {
            const float window = x.value("plates", Json::object()).value("window", 20.0f);
            HitboxView a;
            a.phase = p;
            a.state = "armour";
            a.stateName = "Blindé (plaques)";
            a.parts.push_back({HitPart::Kind::Armour, out.body, "Corps cuirassé : rien ne le blesse"});
            if (boss.contactDamage > 0.0f) a.parts.push_back(contact);
            for (const HitPart& t : firstPlates) a.parts.push_back(t);
            for (const HitPart& t : threats) a.parts.push_back(t);
            a.hitWhen = "Brisez ses " + std::to_string(firstPlates.size()) + " plaques en vert à moins de " + std::to_string(static_cast<int>(std::lround(window))) +
                        " s d'écart (elles repoussent sinon) : sa cuirasse tombe et son corps prend les coups.";
            out.views.push_back(std::move(a));
            HitboxView f;
            f.phase = p;
            f.state = "fight";
            f.stateName = "Plaques brisées";
            f.parts.push_back({HitPart::Kind::Weak, out.body, "Corps : il prend les coups"});
            if (boss.contactDamage > 0.0f) f.parts.push_back(contact);
            for (const HitPart& t : threats) f.parts.push_back(t);
            f.hitWhen = "Ses plaques brisées, frappez son corps jusqu'à la phase suivante, en évitant son contact et ses coups annoncés.";
            out.views.push_back(std::move(f));
            continue;
        }
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
                               "Point faible · " + nameOf(content, id), id});
        }
        for (const HitPart& t : threats) e.parts.push_back(t);
        e.hitWhen = "Frappez ici quand son blindage éclate : ses points faibles et son corps (dégâts × " + decimal(rules.exposedMult) + ") pendant " + std::to_string(static_cast<int>(std::lround(rules.exposed))) + " s.";
        out.views.push_back(std::move(e));
    }
    // 2.75.2: its attacks, each with the phases that use it.
    for (const AttackDef& a : boss.attacks) {
        HitAttack h = hitAttack(a, boss);
        for (size_t p = 0; p < boss.phases.size(); ++p)
            if (std::find(boss.phases[p].attacks.begin(), boss.phases[p].attacks.end(), a.id) != boss.phases[p].attacks.end())
                h.phases.push_back(static_cast<int>(p) + 1);
        if (boss.phases.empty()) h.phases.push_back(1);
        if (!h.phases.empty()) out.attacks.push_back(std::move(h));
    }
    return out;
}

HitAttack hitAttack(const AttackDef& a, const EnemyDefinition& boss) {
    HitAttack h;
    h.id = a.id;
    h.name = a.name;
    h.telegraphClip = a.telegraphAnim;
    h.attackClip = a.attackAnim;
    const Rect body = Rect::fromBottomCenter({0.0f, 0.0f}, boss.size);
    const Vec2 c = body.center();
    const float w = body.w, ht = body.h;
    auto px = [](float v) { return std::to_string(static_cast<int>(std::lround(v))) + " px"; };
    auto area = [&](const Rect& r) {
        HitShape s;
        s.kind = HitShape::Kind::Area;
        s.rect = r;
        h.shapes.push_back(s);
    };
    auto ring = [&](Vec2 at, float radius, bool hurts) {
        HitShape s;
        s.kind = HitShape::Kind::Ring;
        s.at = at;
        s.radius = radius;
        s.hurts = hurts;
        h.shapes.push_back(s);
    };
    auto line = [&](HitShape::Kind k, Vec2 from, Vec2 to, bool hurts) {
        HitShape s;
        s.kind = k;
        s.at = from;
        s.to = to;
        s.hurts = hurts;
        h.shapes.push_back(s);
    };
    const Vec2 origin = attackOriginOf(a, body, 1.0f, boss.scale);  // where Enemy::attackOrigin puts it
    auto source = [&]() { line(HitShape::Kind::Origin, origin, origin, false); };
    switch (a.type) {
        case AttackType::Melee:  // Enemy::performAttackActive
            area(Rect::fromCenter(c + Vec2{w * 0.5f + a.range * 0.4f, 0.0f}, {a.range * 0.9f, ht + 4.0f}));
            h.reach = "Coup devant lui, sur " + px(a.range * 0.9f) + " (rouge)";
            break;
        case AttackType::Lunge:
        case AttackType::Charge:
            area(body.expanded(2.0f));
            h.reach = "Ruée : tout son corps blesse pendant l'élan (rouge)";
            break;
        case AttackType::Slam:
        case AttackType::Leap: {  // Enemy::slam: its feet, then a shockwave on each side
            area(Rect::fromCenter({0.0f, 0.0f}, {w + 30.0f, 24.0f}));
            for (float d : {-1.0f, 1.0f}) line(HitShape::Kind::Wave, {d * w * 0.5f, -6.0f}, {d * (w * 0.5f + a.range), -6.0f}, true);
            h.reach = "Impact à ses pieds, puis une onde de choc de chaque côté sur " + px(a.range) + " : sautez-la";
            break;
        }
        case AttackType::Skyfall: {  // Enemy::landSkyfall
            const float reach = a.radius > 0.0f ? a.radius : 26.0f;
            area({body.x - reach, body.y, body.w + reach * 2.0f, body.h});
            h.reach = "Il retombe sur un joueur : écrase tout à " + px(reach) + " de son corps (rouge)";
            break;
        }
        case AttackType::Laser:
            source();
            line(HitShape::Kind::Beam, origin, origin + Vec2{a.laserLength, 0.0f}, true);
            h.reach = "Rayon de " + px(a.laserLength) + " depuis le point rose, vers un joueur";
            break;
        case AttackType::Prism: {  // upward diagonals that bounce off walls and ceiling
            source();
            const float len = std::max(200.0f, a.laserLength);
            line(HitShape::Kind::Beam, origin, origin + Vec2{len * 0.7071f, -len * 0.7071f}, true);
            h.reach = "Rayon en diagonale qui rebondit sur les murs, depuis le point rose";
            break;
        }
        case AttackType::Projectile:
        case AttackType::Spread:
        case AttackType::Arc:
            source();
            h.reach = "Ses tirs partent du point rose";
            break;
        case AttackType::Spiral:  // Enemy::fireSpiral: born on a circle around its centre
            ring(c, std::max(w, ht) * 0.4f, false);
            h.reach = "Tirs en spirale, nés sur le cercle rose (" + px(std::max(w, ht) * 0.4f) + " de son centre)";
            break;
        case AttackType::Orbit: {
            const std::pair<float, float> r = orbitRadii(a, body.size(), boss.scale);
            ring(c, r.first, true);
            ring(c, r.second, true);
            h.reach = "Couronne de flammes entre " + px(r.first) + " et " + px(r.second) + " de son centre";
            break;
        }
        case AttackType::Ring: {
            const float r = a.radius > 0.0f ? a.radius : 560.0f;
            ring(c, r, true);
            h.reach = "Onde qui s'étend de son centre jusqu'à " + px(r) + " : passez par son ouverture";
            break;
        }
        default:
            h.reach = "Vise les joueurs dans l'arène : rien ne part de son corps";
            break;
    }
    return h;
}

}  // namespace cosmic::raid
