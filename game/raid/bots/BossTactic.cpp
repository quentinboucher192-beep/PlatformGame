#include "game/raid/bots/BossTactic.h"

#include <algorithm>
#include <cmath>

#include "game/enemies/BossHazards.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Projectile.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/RaidRun.h"

namespace cosmic::raid::bots {

std::unique_ptr<BossTactic> BossTactic::make(const std::string& routine) {
    if (routine == "skullmaw") return makeSkullmawTactic();
    if (routine == "dreadfang") return makeDreadfangTactic();
    if (routine == "titanus") return makeTitanusTactic();
    if (routine == "voidworm") return makeVoidwormTactic();
    if (routine == "graviton") return makeGravitonTactic();
    if (routine == "heartbreaker") return makeHeartbreakerTactic();
    if (routine == "coeur") return makeHeartTactic();
    return makePyroclastTactic();
}

std::string BossTactic::wish(Role r) const {
    // The armory (2.75): one power in place of the impulse. The reviver covers its allies down with the
    // decoy; the others the triple jump (the high plates, the restraints, the flank towers).
    switch (r) {
        case Role::Reviver: return "decoy";
        default: return "triple_jump";
    }
}

void BossTactic::readShots(const GameWorld& w, BossView& v) {
    for (const Projectile& pr : w.projectiles()) {
        if (!pr.alive || pr.fromPlayer || pr.reflected) continue;
        if (pr.clip == "shockwave" && std::fabs(pr.vel.y) < 1.0f && pr.gravity == 0.0f) {
            // A shockwave of a slam, running along the floor: jumped as it comes (or the shield).
            Danger d;
            d.kind = Danger::Kind::FloorWave;
            d.floorY = pr.pos.y + 6.0f;
            d.height = pr.radius * 2.0f + 8.0f;
            d.speed = pr.vel.x;
            d.area = {pr.pos.x - pr.radius - 2.0f, d.floorY - d.height, pr.radius * 2.0f + 4.0f, d.height};
            d.shot = true;
            v.dangers.push_back(d);
            continue;
        }
        const float r = pr.radius + 5.0f + pr.explosionRadius * 0.6f;
        // Where it will be over the next half second (its blast around it), one box per sixth.
        for (int k = 0; k <= 6; ++k) {
            const float t = 0.075f * static_cast<float>(k);
            const Vec2 at{pr.pos.x + pr.vel.x * t, pr.pos.y + pr.vel.y * t + 0.5f * pr.gravity * t * t};
            Danger d;
            d.kind = Danger::Kind::Area;
            d.area = {at.x - r, at.y - r, r * 2.0f, r * 2.0f};
            d.in = static_cast<int>(t * static_cast<float>(kSimHz));
            d.shot = true;
            d.to = pr.vel;  // a shot: its velocity (the way to leave its path: over it or aside)
            v.dangers.push_back(d);
        }
    }
}

void BossTactic::readBossAttack(const GameWorld& w, const Enemy& b, BossView& v) {
    if (b.skyborne()) {
        // Out of sight (its leap, its meteor fist): it comes down where its shadow is, under it — never there.
        Danger d;
        d.kind = Danger::Kind::Area;
        d.area = {b.box.x - 40.0f, b.box.y, b.box.w + 80.0f, 2400.0f};
        d.in = kSimHz / 3;
        v.dangers.push_back(d);
    }
    const AttackDef* a = b.currentAttack();
    if (!a || (b.state() != EnemyState::Telegraph && b.state() != EnemyState::Attack)) return;
    const bool striking = b.state() == EnemyState::Attack;
    Danger d;
    d.kind = Danger::Kind::Area;
    d.in = striking ? 0 : kSimHz / 4;
    const Rect box = b.box;
    switch (a->type) {
        case AttackType::Slam:
        case AttackType::Melee:
        case AttackType::Contact:
        case AttackType::Orbit:
        case AttackType::Vortex: {
            // Around it: as far as its reach (a slam's shockwave runs further along the floor: a band).
            // (its orbit of flames: close around it; the shots of a spiral are read as they fly, not as an area)
            const float reach = a->type == AttackType::Orbit ? 100.0f : std::clamp(a->range, 40.0f, 220.0f);
            d.area = box.expanded(reach * 0.6f);
            v.dangers.push_back(d);
            if (a->type == AttackType::Slam) {
                Danger f = d;
                f.area = {box.center().x - reach - box.w * 0.5f, box.bottom() - 40.0f, (reach + box.w * 0.5f) * 2.0f, 44.0f};
                v.dangers.push_back(f);
            }
            break;
        }
        case AttackType::Tracker: {
            // A bump running under the ground toward its target, then a spike where it stops: away from the bump.
            const Vec2 m = b.attackMark();
            d.area = {m.x - 44.0f, m.y - 52.0f, 88.0f, 56.0f};
            d.in = striking ? kSimHz / 6 : kSimHz / 2;
            v.dangers.push_back(d);
            break;
        }
        case AttackType::Cage: {
            // Two walls about to rise on both sides of its target: out from between them before they do.
            const std::vector<Vec2>& p = b.attackPath();
            if (!striking && p.size() >= 2) {
                const float x0 = std::min(p[0].x, p[1].x), x1 = std::max(p[0].x, p[1].x);
                d.area = {x0 - 10.0f, std::min(p[0].y, p[1].y) - 140.0f, x1 - x0 + 20.0f, 144.0f};
                d.in = kSimHz / 3;
                v.dangers.push_back(d);
            }
            break;
        }
        case AttackType::Charge:
        case AttackType::Lunge: {
            // Straight ahead along its floor, as far as its run.
            const float run = std::clamp(a->range * (a->type == AttackType::Charge ? 2.5f : 1.0f), 120.0f, 1400.0f);
            const float x0 = b.facing >= 0.0f ? box.x : box.x - run;
            d.area = {x0, box.y, box.w + run, box.h + 8.0f};
            v.dangers.push_back(d);
            break;
        }
        case AttackType::Ring: {
            // A ring growing from it with one opening (Enemy::ringRadius, ringGapAngle): it reaches each player once.
            Danger r;
            r.kind = Danger::Kind::Ring;
            r.from = b.center();
            r.radius = striking ? b.ringRadius() : std::max(b.box.w, b.box.h) * 0.5f * b.def().scale;
            r.reach = a->radius > 0.0f ? a->radius : 560.0f;
            r.speed = std::max(40.0f, a->projectileSpeed);
            r.gap = b.ringGapAngle();
            r.gapHalf = (a->spread > 0.0f ? a->spread : 50.0f) * 0.5f * 0.0174533f;
            r.in = striking ? 0 : kSimHz / 3;
            r.area = {r.from.x - r.reach, r.from.y - r.reach, r.reach * 2.0f, r.reach * 2.0f};
            v.dangers.push_back(r);
            break;
        }
        case AttackType::Spiral: break;  // its shots, read as they fly
        case AttackType::Laser: {
            // A beam from its eye toward the player nearest to it (its target), as long as its reach.
            const Vec2 o = b.attackOrigin();
            int best = -1;
            float bd = 1e30f;
            for (int i = 0; i < w.teamSize(); ++i) {
                if (w.member(i).presence != GameWorld::Presence::Playing || !w.playerAt(i).alive()) continue;
                const float dd = (w.playerAt(i).center() - b.center()).lengthSq();
                if (dd < bd) {
                    bd = dd;
                    best = i;
                }
            }
            if (best < 0) break;
            Vec2 dir = w.playerAt(best).center() - o;
            const float len = dir.length();
            if (len < 1.0f) break;
            dir = dir * (1.0f / len);
            Danger l;
            l.kind = Danger::Kind::Line;
            l.from = o;
            l.to = o + dir * std::max(a->laserLength, len + 60.0f);
            l.width = 26.0f + std::fabs(a->sweepDegrees) * 2.0f;
            l.in = striking ? 0 : kSimHz / 4;
            l.area = {std::min(l.from.x, l.to.x) - l.width, std::min(l.from.y, l.to.y) - l.width, std::fabs(l.to.x - l.from.x) + l.width * 2.0f,
                      std::fabs(l.to.y - l.from.y) + l.width * 2.0f};
            v.dangers.push_back(l);
            break;
        }
        default: break;
    }
}

void BossTactic::readAdds(const GameWorld& w, const Enemy& boss, BossView& v, int priority) {
    for (const auto& e : w.enemies()) {
        if (!e->alive() || e->dormant() || e->uid() == boss.uid()) continue;
        bool known = false;
        for (const Target& t : v.targets) known = known || t.uid == e->uid();
        if (known) continue;
        // Its creatures near the fight (not a weak point nor a piece of its mechanism: those are named).
        if ((e->center() - boss.center()).lengthSq() > 1400.0f * 1400.0f) continue;
        if (e->def().contactDamage <= 0 && e->def().attacks.empty()) continue;  // a stuck piece, harmless
        Target t;
        t.uid = e->uid();
        t.want = 1;
        t.priority = priority;
        t.melee = !e->def().isBoss;
        t.label = e->def().name;
        v.targets.push_back(t);
    }
}

void BossTactic::readCommon(const RaidRun& run, BossView& v, bool vents) {
    const GameWorld& w = run.world();
    const Gigaboss& gb = run.gigaboss();
    const Enemy* boss = gb.boss(w);
    v.exposed = gb.stage() == Gigaboss::Stage::Exposed;
    v.bodyTakesDamage = gb.stage() != Gigaboss::Stage::Carapace;
    // Its fists (marks under the players): the column each one falls on, the seat it was aimed at.
    for (const auto& f : gb.fists()) {
        Danger d;
        d.kind = Danger::Kind::Area;
        d.area = gb.fistColumn(f).expanded(4.0f);
        d.in = std::max(0, f.left);
        v.dangers.push_back(d);
    }
    // Its vents' jets (announced, then throwing).
    for (int k = 0; k < static_cast<int>(gb.vents().size()); ++k)
        if (gb.jetWarning(k) || gb.jetting(k)) {
            const Rect vr = gb.vents()[static_cast<size_t>(k)].area;
            const float h = gb.rules() ? gb.rules()->jetHeight : 112.0f;
            Danger d;
            d.kind = Danger::Kind::Area;
            d.area = {vr.x - 4.0f, vr.y - h, vr.w + 8.0f, h + vr.h};
            d.in = gb.jetting(k) ? 0 : kSimHz / 2;
            v.dangers.push_back(d);
        }
    // The world's hazards (a boss's blows that last): a flood rising, cracks and rocks marked on the ground,
    // walls, burning clouds, presses about to fall, armed runes, wells pulling.
    const BossHazards& hz = w.hazards();
    for (const auto& fl : hz.floods()) {
        Danger d;
        d.kind = Danger::Kind::Flood;
        const float top = fl.floorY - std::max(fl.level(), fl.age < fl.rise ? fl.height : 0.0f);
        d.area = {fl.left, top, fl.right - fl.left, fl.floorY - top + 8.0f};
        v.dangers.push_back(d);
    }
    for (const auto& c : hz.cracks()) {
        if (c.done()) continue;
        Danger d;
        d.kind = Danger::Kind::Area;
        const float h = std::max(c.height, 40.0f);
        d.area = {c.at.x - c.half - 6.0f, c.at.y - h, c.half * 2.0f + 12.0f, h + 4.0f};
        d.in = std::max(0, static_cast<int>((c.delay - c.age) * static_cast<float>(kSimHz)));
        v.dangers.push_back(d);
    }
    for (const auto& wl : hz.walls()) v.dangers.push_back({Danger::Kind::Area, wl.rect.expanded(4.0f)});
    for (const auto& cl : hz.clouds()) v.dangers.push_back({Danger::Kind::Area, cl.current().expanded(4.0f)});
    for (const auto& pr : hz.presses())
        if (!pr.safe && !pr.done()) v.dangers.push_back({Danger::Kind::Area, pr.column().expanded(4.0f)});
    for (const auto& r : hz.runes())
        if (r.armed() || r.bursting()) v.dangers.push_back({Danger::Kind::Area, r.pillar().expanded(4.0f)});
    for (const auto& p : hz.pulls()) {
        Danger d;
        d.kind = Danger::Kind::Pull;
        d.from = p.center;
        const float r = std::max(p.core * 2.5f, 60.0f);
        d.area = {p.center.x - r, p.center.y - r, r * 2.0f, r * 2.0f};
        d.width = p.range;
        v.dangers.push_back(d);
    }
    readShots(w, v);
    if (boss && boss->alive()) readBossAttack(w, *boss, v);
    // Armoured: its vents (the rules' plates), one player on each, held together.
    if (vents && gb.stage() == Gigaboss::Stage::Carapace)
        for (size_t k = 0; k < gb.vents().size(); ++k) {
            const auto& vt = gb.vents()[k];
            if (vt.closed) continue;  // closed, it stays closed a while: free until it lights again
            Station s;
            s.plate = vt.area;
            s.need = 1;
            s.priority = 10;
            s.label = gb.rules() && k < gb.rules()->ventIds.size() ? gb.rules()->ventIds[k] : std::string("évent");
            v.stations.push_back(s);
        }
    // Bare: its weak points first (× 2).
    if (v.exposed)
        for (uint32_t u : gb.weakUids()) {
            const Enemy* e = u ? w.findEnemy(u) : nullptr;
            if (!e || !e->alive()) continue;
            Target t;
            t.uid = u;
            t.want = 2;
            t.priority = 20;
            t.weak = true;
            t.melee = false;
            t.label = e->def().name;
            v.targets.push_back(t);
        }
}

}  // namespace cosmic::raid::bots
