#include "game/raid/RaidBots.h"

#include <algorithm>
#include <cmath>

#include "engine/physics/PhysicsWorld.h"
#include "game/enemies/BossHazards.h"
#include "game/enemies/Enemy.h"
#include "game/enemies/HitParts.h"
#include "game/gameplay/Projectile.h"
#include "game/gameplay/entities/Entities.h"
#include "game/powers/Power.h"
#include "game/raid/GigabossRoutine.h"

namespace cosmic::raid {

namespace {

constexpr int kLeft = static_cast<int>(Action::MoveLeft);
constexpr int kRight = static_cast<int>(Action::MoveRight);
constexpr int kUp = static_cast<int>(Action::MoveUp);
constexpr int kDown = static_cast<int>(Action::MoveDown);
constexpr int kJump = static_cast<int>(Action::Jump);
constexpr int kAttack = static_cast<int>(Action::Attack);
constexpr int kInteract = static_cast<int>(Action::Interact);

uint64_t mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}

bool playing(const GameWorld& w, int i) {
    return i >= 0 && i < w.teamSize() && w.member(i).presence == GameWorld::Presence::Playing && w.playerAt(i).alive() &&
           !w.playerAt(i).respawning();
}

/// A deterministic number in [0, 1) for a seat at a step (the bots' precision; no random generator).
float noise(int seat, uint32_t tick) {
    uint32_t x = static_cast<uint32_t>(seat) * 0x9E3779B9u ^ tick * 0x85EBCA6Bu;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFFu) / 16777216.0f;
}

/// The level's reaction (steps before it moves out of a blow it sees) and precision (share of its
/// strikes and shots that go off at the right moment).
struct Level {
    int reaction;
    float precision;
};
Level levelOf(BotSkill s) {
    switch (s) {
        case BotSkill::Beginner: return {36, 0.6f};
        case BotSkill::Expert: return {6, 1.0f};
        default: return {18, 0.85f};
    }
}

/// The power it shoots with (the generators, the enemies from afar): the wave, else the boomerang.
const Power* shooter(const Player& p) {
    for (const auto& pw : p.powers())
        if (pw->id() == "wave") return pw.get();
    for (const auto& pw : p.powers())
        if (pw->id() == "boomerang") return pw.get();
    return nullptr;
}

int keyBit(const Power* p) {
    if (!p) return -1;
    const int k = static_cast<int>(p->key());
    return k >= 0 && k < InputFrame::kActions ? k : -1;
}

/// 2.76.2 (bosses by parts): where to strike `e` in its pose now — the box around its weak points and normal
/// parts (else its armour), never a part that only hurts (its fingers, its claws: a blow there does nothing).
/// Its box when it does not fight by parts.
Rect strikeBox(const Enemy& e, const GameWorld& w) {
    if (!fightsByParts(e, w)) return e.box;
    const AnimationSetPtr& set = e.animator().set();
    if (!set || set->frameWidth <= 0 || set->frameHeight <= 0) return e.box;
    const HitPartsSheet* sheet = HitPartsSheet::of(set->sourcePath);
    if (!sheet) return e.box;
    const Rect src = e.animator().sourceRect();
    const std::vector<HitPartPiece>* pieces = sheet->cell(static_cast<int>(src.y) / set->frameHeight, static_cast<int>(src.x) / set->frameWidth);
    if (!pieces) return e.box;
    const PartRules& rules = PartRules::of(e.def().id);
    Rect best[2];
    bool have[2] = {false, false};
    for (const HitPartPiece& piece : *pieces) {
        if (piece.part < 0 || piece.part >= static_cast<int>(sheet->parts.size())) continue;
        const PartRule rule = rules.ruleOr(sheet->parts[static_cast<size_t>(piece.part)]);
        if (rule.role == PartRole::Hurt) continue;
        const int k = rule.role == PartRole::Armour ? 1 : 0;
        const std::vector<Vec2> poly = placePiece(piece, *sheet, e.box.bottomCenter(), e.def().scale, e.facing < 0.0f);
        if (poly.empty()) continue;
        float x0 = poly[0].x, x1 = poly[0].x, y0 = poly[0].y, y1 = poly[0].y;
        for (const Vec2& p : poly) {
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        }
        const Rect r{x0, y0, x1 - x0, y1 - y0};
        if (!have[k]) {
            best[k] = r;
            have[k] = true;
        } else {
            const float l = std::min(best[k].x, r.x), t = std::min(best[k].y, r.y);
            best[k] = {l, t, std::max(best[k].right(), r.right()) - l, std::max(best[k].bottom(), r.bottom()) - t};
        }
    }
    return have[0] ? best[0] : have[1] ? best[1] : e.box;
}

const Enemy* enemyOf(const GameWorld& w, uint32_t uid) {
    if (uid == 0) return nullptr;
    const Enemy* e = w.findEnemy(uid);
    return e && e->alive() && !e->dormant() ? e : nullptr;
}

std::string shortLabel(const std::string& s) { return s.substr(0, s.find(" (")); }

}  // namespace

const char* botSkillName(BotSkill s) {
    switch (s) {
        case BotSkill::Beginner: return "Débutant";
        case BotSkill::Expert: return "Expert";
        default: return "Confirmé";
    }
}

void TestBots::reset(int players) {
    players_ = std::clamp(players, 1, kMaxInstancePlayers);
    bots_.assign(static_cast<size_t>(kMaxInstancePlayers), Bot{});
    nav_.reset();
    navSig_ = 0;
    navZone_ = -1;
    tick_ = 0;
}

uint64_t TestBots::hash() const {
    uint64_t h = 1469598103934665603ULL;
    for (const Bot& b : bots_) {
        h = mix(h, b.prevHeld);
        h = mix(h, static_cast<uint64_t>(b.jumpHold) | (static_cast<uint64_t>(b.airJump) << 16) | (static_cast<uint64_t>(b.task) << 24) |
                       (static_cast<uint64_t>(b.act) << 32));
        h = mix(h, static_cast<uint64_t>(b.still) | (static_cast<uint64_t>(b.mechanismSteps) << 20));
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(b.goalNode)) << 32) | b.target);
        h = mix(h, (static_cast<uint64_t>(b.at) << 32) | static_cast<uint32_t>(b.lost));
    }
    return mix(h, static_cast<uint64_t>(skill_));
}

std::string TestBots::wish(int seat) {
    if (seat <= 0) return {};
    return seat % 2 == 1 ? "triple_jump" : "boomerang";
}

uint8_t TestBots::choice(const GameWorld& w, int seat) const {
    const auto& offer = w.powerOffer();
    if (!offer || offer->player != seat || seat < 0 || seat >= w.teamSize()) return InputFrame::kChoiceLeave;
    if (offer->powerId != wish(seat)) return InputFrame::kChoiceLeave;
    // Its wish, in place of the impulse (else of the last power that is not the wave, the dash or the shield).
    const auto& powers = w.playerAt(seat).powers();
    int slot = -1;
    for (size_t k = 0; k < powers.size(); ++k)
        if (powers[k]->id() == "impulse") slot = static_cast<int>(k);
    if (slot < 0)
        for (size_t k = powers.size(); k-- > 0;)
            if (powers[k]->id() != "wave" && powers[k]->id() != "dash" && powers[k]->id() != "shield") {
                slot = static_cast<int>(k);
                break;
            }
    if (offer->conflictSlot >= 0) slot = offer->conflictSlot;
    return slot < 0 || slot >= 8 ? InputFrame::kChoiceLeave : static_cast<uint8_t>(slot + 1);
}

int TestBots::nextNode(int seat) const {
    if (seat < 0 || seat >= static_cast<int>(bots_.size())) return -1;
    const Bot& b = bots_[static_cast<size_t>(seat)];
    if (b.path.empty()) return -1;
    return b.path[std::min(b.at + 1, b.path.size() - 1)];
}

int TestBots::chooseExit(const RaidRun& run, int* toZone) {
    const Branch& br = run.branch();
    const uint32_t mask = run.mask();
    const int here = run.zone();
    if (toZone) *toZone = 0;
    if (Branch::isArmory(here)) return armoryExit(run, toZone);  // 2.78: the lair's armory too
    // The zones by the passages open from here (breadth first, the lowest zone first): the first hop.
    const int nz = static_cast<int>(br.zones.size());
    std::vector<int> dist(static_cast<size_t>(nz + 1), -1), first(static_cast<size_t>(nz + 1), 0);
    std::vector<int> todo{here};
    if (here >= 1 && here <= nz) dist[static_cast<size_t>(here)] = 0;
    for (size_t q = 0; q < todo.size(); ++q) {
        const int z = todo[q];
        std::vector<int> next;
        for (const EdgeInfo& e : br.edges) {
            if ((e.a != z && e.b != z) || !br.lockOpen(e.lock, mask)) continue;
            next.push_back(e.a == z ? e.b : e.a);
        }
        std::sort(next.begin(), next.end());
        for (int o : next) {
            if (o < 1 || o > nz || dist[static_cast<size_t>(o)] >= 0) continue;
            dist[static_cast<size_t>(o)] = dist[static_cast<size_t>(z)] + 1;
            first[static_cast<size_t>(o)] = z == here ? o : first[static_cast<size_t>(z)];
            todo.push_back(o);
        }
    }
    // What this group can still do in a zone: an objective not done whose pair it did not do itself.
    auto useful = [&](int z, bool optional) {
        for (const ObjectiveInfo& o : br.objectives)
            if (o.zone == z && o.optional == optional && !Branch::done(mask, o.index) && br.pairDone(o, run.localDone()).empty()) return true;
        return false;
    };
    int best = -1;
    int bestRank = 1 << 30;
    for (const ZoneInfo& z : br.zones) {
        if (z.id == here || z.id < 1 || z.id > nz || dist[static_cast<size_t>(z.id)] < 0) continue;
        int rank;
        if (!z.boss && !z.secret && useful(z.id, false)) rank = 0;                    // the zones to finish
        else if (z.secret && useful(z.id, true)) rank = 1;                             // a relic (it weakens the heart)
        else if (z.boss && useful(z.id, false)) rank = 2;                              // the lair, last
        else continue;
        const int key = rank * 1000 + dist[static_cast<size_t>(z.id)] * 32 + z.id;
        if (key < bestRank) {
            bestRank = key;
            best = z.id;
        }
    }
    if (best < 0) return -1;
    const int hop = first[static_cast<size_t>(best)];
    for (size_t k = 0; k < run.rules().passages.size(); ++k)
        if (run.rules().passages[k].to == hop && run.passageOpen(run.rules().passages[k])) {
            if (toZone) *toZone = hop;
            return static_cast<int>(k);
        }
    return -1;
}

int TestBots::armoryExit(const RaidRun& run, int* toZone) {
    // 2.78: an armory (the first one, or the lair's): the way on (the passage of its « Prêts » plate first,
    // never the way back to the zone before the lair).
    const std::vector<Passage>& ps = run.rules().passages;
    int pick = -1;
    for (int pass = 0; pass < 2 && pick < 0; ++pass)
        for (size_t k = 0; k < ps.size() && pick < 0; ++k)
            if ((pass == 1 || !ps[k].plate.empty()) && run.passageOpen(ps[k])) pick = static_cast<int>(k);
    if (pick >= 0 && toZone) *toZone = ps[static_cast<size_t>(pick)].to;
    return pick;
}

void TestBots::ensureNav(const RaidRun& run) {
    const GameWorld& w = run.world();
    // The graph of the zone: made again when another world is loaded, or a door opened or closed
    // (checked every half second: the signature only reads the colliders).
    if (navZone_ != run.zoneSerial() || !nav_ || tick_ % 60 == 0) {
        const std::string periodic = run.rules().magma.present ? run.rules().magma.zone : std::string();
        const uint64_t sig = NavGraph::signature(w, periodic);
        if (navZone_ != run.zoneSerial() || sig != navSig_ || !nav_) {
            nav_ = NavGraph::of(w, periodic);
            navSig_ = sig;
            navZone_ = run.zoneSerial();
            for (Bot& b : bots_) {
                b.path.clear();
                b.goalNode = -1;
                b.fleeNode = -1;
            }
        }
    }
}

void TestBots::think(RaidRun& run, InputFrame* frames, int count, uint32_t seats, bool adminActive) {
    GameWorld& w = run.world();
    const int n = std::min({kMaxInstancePlayers, count, w.teamSize()});
    ensureNav(run);
    // What is announced to strike this step (a simulated player keeps out of it): the fists marked
    // under the players, the vents about to throw their jet, a gigaboss's blows and dangers announced.
    dangers_.clear();
    const Gigaboss& gb = run.gigaboss();
    if (gb.present()) {
        for (const auto& f : gb.fists())
            if (f.left > 0) dangers_.push_back(gb.fistColumn(f));
        for (int k = 0; k < static_cast<int>(gb.vents().size()); ++k)
            if (gb.jetWarning(k) || gb.jetting(k)) {
                const Rect v = gb.vents()[static_cast<size_t>(k)].area;
                const float h = gb.rules() ? gb.rules()->jetHeight : 112.0f;
                dangers_.push_back({v.x, v.y - h, v.w, h + v.h});
            }
        if (const GigabossRoutine* rt = gb.routine()) {
            std::vector<BossMark> marks;
            rt->marks(w, gb, marks);
            for (const BossMark& m : marks)
                if (m.kind == BossMark::Kind::Target || (m.kind == BossMark::Kind::Zone && (m.strong || m.progress > 0.0f)))
                    dangers_.push_back(m.area);
        }
    }
    // The bosses' own blows announced in the world (BossHazards): a flood rising over the floor, cracks and
    // rocks marked on the ground, walls, burning clouds, presses about to fall, armed runes.
    {
        const BossHazards& hz = w.hazards();
        for (const auto& fl : hz.floods()) {
            const float top = fl.floorY - fl.height;
            dangers_.push_back({fl.left, top, fl.right - fl.left, fl.floorY - top + 8.0f});
        }
        for (const auto& c : hz.cracks())
            if (!c.done()) dangers_.push_back({c.at.x - c.half - 4.0f, c.at.y - std::max(c.height, 40.0f), c.half * 2.0f + 8.0f, std::max(c.height, 40.0f) + 4.0f});
        for (const auto& wl : hz.walls()) dangers_.push_back(wl.rect);
        for (const auto& cl : hz.clouds()) dangers_.push_back(cl.current());
        for (const auto& pr : hz.presses())
            if (!pr.safe && !pr.done()) dangers_.push_back(pr.column());
        for (const auto& r : hz.runes())
            if (r.armed() || r.bursting()) dangers_.push_back(r.pillar());
    }
    // The enemies' shots: where each one will be in the next half second (its blast around it).
    projectiles_ = dangers_.size();
    for (const Projectile& pr : w.projectiles()) {
        if (!pr.alive || pr.fromPlayer || pr.reflected) continue;
        const float r = pr.radius + 4.0f + pr.explosionRadius * 0.6f;
        for (int k = 1; k <= 6; ++k) {
            const float t = 0.075f * static_cast<float>(k);
            const Vec2 at{pr.pos.x + pr.vel.x * t, pr.pos.y + pr.vel.y * t + 0.5f * pr.gravity * t * t};
            dangers_.push_back({at.x - r, at.y - r, r * 2.0f, r * 2.0f});
        }
    }
    if (tick_ % 15 == 0 || navZone_ != run.zoneSerial()) plan(run, seats, adminActive);
    for (int s = 0; s < n; ++s) {
        Bot& b = bots_[static_cast<size_t>(s)];
        if (!((seats >> s) & 1u)) continue;
        if (b.task != Task::Mechanism) b.mechanismSteps = 0;
        else if (playing(w, s) && b.mechanism.intersects(w.playerAt(s).box)) b.mechanismSteps = 0;
        else ++b.mechanismSteps;
    }
    // The players of a mechanism struck together (two restraints within a second and a half): all
    // within reach of their targets, or none strikes.
    syncReady_ = true;
    for (int s = 0; s < n; ++s) {
        const Bot& b = bots_[static_cast<size_t>(s)];
        if (!((seats >> s) & 1u) || !b.sync || !playing(w, s)) continue;
        const Enemy* e = w.findEnemy(b.target);
        if (!e || !e->alive()) continue;
        const Vec2 d = e->center() - w.playerAt(s).center();
        if (std::fabs(d.x) > e->box.w * 0.5f + 22.0f || std::fabs(d.y) > e->box.h * 0.5f + 18.0f) syncReady_ = false;
    }
    for (int s = 0; s < n; ++s)
        if ((seats >> s) & 1u) frames[s] = drive(run, s, bots_[static_cast<size_t>(s)]);
    ++tick_;
}

// ----------------------------------------------------------------------- the tasks of the group

void TestBots::plan(RaidRun& run, uint32_t seats, bool adminActive) {
    GameWorld& w = run.world();
    const NavGraph* nav = nav_.get();
    const int n = std::min(kMaxInstancePlayers, w.teamSize());
    std::vector<int> drivers;
    for (int s = 0; s < n; ++s)
        if ((seats >> s) & 1u) {
            Bot& b = bots_[static_cast<size_t>(s)];
            const Task was = b.task;
            b.task = Task::Follow;
            b.target = 0;
            b.act = None;
            b.thing.clear();
            b.sync = false;
            b.mechanism = {};
            if (was == Task::Flee) b.task = Task::Flee;  // it finishes sheltering (drive)
            if (playing(w, s)) drivers.push_back(s);
        }
    std::vector<bool> busy(static_cast<size_t>(kMaxInstancePlayers), false);
    std::vector<std::string> said;
    auto nearestFree = [&](Vec2 p, const std::function<bool(int)>& ok = nullptr) {
        int best = -1;
        float bestD = 1e30f;
        for (int s : drivers) {
            if (busy[static_cast<size_t>(s)] || (ok && !ok(s))) continue;
            const float d = (w.playerAt(s).feet() - p).lengthSq();
            if (d < bestD) {
                bestD = d;
                best = s;
            }
        }
        return best;
    };
    auto freeCount = [&] {
        int k = 0;
        for (int s : drivers) k += busy[static_cast<size_t>(s)] ? 0 : 1;
        return k;
    };
    auto give = [&](int s, Task t, Vec2 goal, int act, uint32_t target = 0, const std::string& thing = {}) {
        Bot& b = bots_[static_cast<size_t>(s)];
        b.task = t;
        b.goal = goal;
        b.act = act;
        b.target = target;
        b.thing = thing;
        busy[static_cast<size_t>(s)] = true;
    };
    auto standNode = [&](const Rect& r) {
        if (!nav) return -1;
        int k = nav->nearest(r.bottomCenter(), std::max(48.0f, r.w), [&](const NavGraph::Node& nd) {
            return nd.mover < 0 && Rect{nd.feet.x - 5.0f, nd.feet.y - 22.0f, 10.0f, 22.0f}.intersects(r);
        });
        if (k < 0) k = nav->nearest(r.bottomCenter(), 160.0f, [](const NavGraph::Node& nd) { return nd.mover < 0; });
        return k;
    };
    // The players down: the nearest simulated player goes to each soul (the administrator's too).
    if (w.canRevive())
        for (int d = 0; d < w.teamSize(); ++d) {
            if (w.member(d).presence != GameWorld::Presence::Down) continue;
            const Vec2 soul = w.member(d).soul;
            const int s = nearestFree(soul);
            if (s < 0) break;
            give(s, Task::Revive, soul, Stand);
            bots_[static_cast<size_t>(s)].goalNode = nav ? nav->locate(soul, 96.0f) : -1;
            said.push_back("Réanimer");
        }
    // Someone not played by the bots (the administrator) standing on a rectangle.
    auto heldByOthers = [&](const Rect& r) {
        int k = 0;
        for (int s = 0; s < n; ++s)
            if (!((seats >> s) & 1u) && playing(w, s) && r.intersects(w.playerAt(s).box)) ++k;
        return k;
    };
    // ------------------------------------------------------------------ a gigaboss's fight
    const Gigaboss& gb = run.gigaboss();
    const Enemy* boss = gb.present() ? gb.boss(w) : nullptr;
    bool fight = false;
    if (boss && boss->alive() && gb.stage() != Gigaboss::Stage::Fallen) {
        fight = gb.engaged();
        const Rect arena = run.rules().arena.present ? run.rules().arena.rect : boss->box.expanded(600.0f);
        for (int s = 0; s < n && !fight; ++s) fight = playing(w, s) && arena.intersects(w.playerAt(s).box);
    }
    if (fight) {
        // Armoured (« Blindé » on its sheet): its plates or vents, all held together (one each); and the
        // plates its routine lights now whatever its state (the Cœur's anchors against its supernova,
        // Skullmaw's mirrors while it charges: lit, named or strong).
        {
            const bool armoured = gb.stage() == Gigaboss::Stage::Carapace;
            std::vector<Rect> plates;
            if (armoured)
                for (const auto& v : gb.vents())
                    if (!v.closed) plates.push_back(v.area);
            if (const GigabossRoutine* rt = gb.routine()) {
                std::vector<BossMark> marks;
                rt->marks(w, gb, marks);
                for (const BossMark& m : marks) {
                    if (m.kind != BossMark::Kind::Plate) continue;
                    if (!armoured && !m.strong && m.label.empty() && m.clip != "anchor") continue;
                    bool known = false;
                    for (const Rect& p : plates) known = known || p.intersects(m.area);
                    if (!known) plates.push_back(m.area);
                }
            }
            for (const Rect& p : plates) {
                if (heldByOthers(p) > 0) continue;
                const int s = nearestFree(p.bottomCenter());
                if (s < 0) break;
                give(s, Task::Mechanism, {p.center().x, p.bottom()}, Stand);
                Bot& b = bots_[static_cast<size_t>(s)];
                b.mechanism = p;
                b.goalNode = standNode(p);
            }
            if (!plates.empty()) said.push_back("Mécanisme du Gigaboss : " + std::to_string(plates.size()) + " plaques");
        }
        // The targets of its mechanism (restraints, claws…), two players each, striking together. A routine
        // that names none: armoured, what is stuck to its body (its restraints at its ankles, its plates).
        if (const GigabossRoutine* rt = gb.routine()) {
            std::vector<uint32_t> uids;
            rt->targets(uids);
            std::vector<uint32_t> live;
            for (uint32_t u : uids)
                if (enemyOf(w, u)) live.push_back(u);
            if (live.empty() && gb.stage() == Gigaboss::Stage::Carapace)
                for (const auto& e : w.enemies())
                    if (e->alive() && !e->dormant() && e->uid() != boss->uid() && e->box.intersects(boss->box.expanded(48.0f))) live.push_back(e->uid());
            std::sort(live.begin(), live.end());
            for (uint32_t u : live)
                for (int k = 0; k < 2; ++k) {
                    const Enemy* e = enemyOf(w, u);
                    const int s = nearestFree(e->box.bottomCenter());
                    if (s < 0) break;
                    give(s, Task::Fight, e->box.bottomCenter(), Strike, u);
                    bots_[static_cast<size_t>(s)].sync = live.size() > 1;
                }
            if (!live.empty()) said.push_back("Cibles du mécanisme : " + std::to_string(live.size()));
        }
        // Bare (« À nu »): its weak points, shared out.
        std::vector<uint32_t> weak;
        if (gb.stage() == Gigaboss::Stage::Exposed)
            for (uint32_t u : gb.weakUids())
                if (enemyOf(w, u)) weak.push_back(u);
        // One player per weak point it can reach (a place in reach of its blade, or at its height for the
        // wave); the others strike its body, bare too (it takes more damage meanwhile).
        for (uint32_t u : weak) {
            const Enemy* e = enemyOf(w, u);
            if (!e || freeCount() <= 1) break;
            const Vec2 c = e->center();
            const bool reachable = nav && nav->nearest(c, 260.0f, [&](const NavGraph::Node& nd) {
                                              const bool blade = std::fabs(nd.feet.x - c.x) < e->box.w * 0.5f + 20.0f && std::fabs(nd.feet.y - 11.0f - c.y) < e->box.h * 0.5f + 16.0f;
                                              const bool wave = std::fabs(nd.feet.y - 11.0f - c.y) <= e->box.h * 0.5f + 4.0f;
                                              return nd.mover < 0 && (blade || wave);
                                          }) >= 0;
            if (!reachable) continue;
            const int s = nearestFree(e->box.bottomCenter());
            if (s < 0) break;
            give(s, Task::Fight, e->box.bottomCenter(), Strike, u);
        }
        if (!weak.empty()) said.push_back("Points faibles à nu");
        // Its body (« Combat » or « À nu »), else its creatures while it is armoured.
        while (freeCount() > 0) {
            uint32_t target = gb.stage() == Gigaboss::Stage::Carapace ? 0u : boss->uid();
            if (target == 0) {
                float best = 1e30f;
                for (const auto& e : w.enemies())
                    if (e->alive() && !e->dormant() && e->uid() != boss->uid() && (e->center() - boss->center()).lengthSq() < best &&
                        (e->center() - boss->center()).lengthSq() < 700.0f * 700.0f) {
                        best = (e->center() - boss->center()).lengthSq();
                        target = e->uid();
                    }
            }
            const Enemy* e = enemyOf(w, target);
            if (!e) break;
            const int s = nearestFree(e->box.bottomCenter());
            if (s < 0) break;
            give(s, Task::Fight, e->box.bottomCenter(), Strike, target);
        }
        said.push_back(gb.stage() == Gigaboss::Stage::Carapace ? "Gigaboss blindé" : "Frapper le Gigaboss");
    }
    // ------------------------------------------------------------------ the armory's tablets
    if (Branch::isArmory(run.zone()))  // 2.78: the lair's armory too
        for (int s : drivers) {
            if (busy[static_cast<size_t>(s)]) continue;
            const std::string want = wish(s);
            if (want.empty() || w.playerAt(s).hasPower(want)) continue;
            for (const auto& e : w.entities())
                if (auto* t = dynamic_cast<const PowerUnlockEntity*>(e.get()); t && !e->removed && t->power() == want) {
                    give(s, Task::Tablet, e->box.bottomCenter(), Enter, 0, e->id());
                    bots_[static_cast<size_t>(s)].goalNode = standNode(e->box);
                    said.push_back("Pouvoirs");
                    break;
                }
        }
    // ------------------------------------------------------------------ the ambushes under way
    for (const auto& e : w.entities()) {
        auto* cz = dynamic_cast<CombatZoneEntity*>(e.get());
        if (!cz || !cz->fighting()) continue;
        const auto foes = cz->fightingEnemies(w);
        if (foes.empty()) continue;
        for (int s : drivers) {
            if (busy[static_cast<size_t>(s)] || !e->box.intersects(w.playerAt(s).box)) continue;
            const Enemy* best = nullptr;
            for (const Enemy* f : foes)
                if (!best || (f->center() - w.playerAt(s).center()).lengthSq() < (best->center() - w.playerAt(s).center()).lengthSq()) best = f;
            give(s, Task::Fight, best->box.bottomCenter(), Strike, best->uid());
        }
        said.push_back("Embuscade");
    }
    // ------------------------------------------------------------------ the plates held together
    bool platesLeft = false;
    if (!fight)
        for (size_t k = 0; k < run.rules().coops.size(); ++k) {
            if (run.coop(k).done) continue;
            platesLeft = true;
            const CoopPlate& c = run.rules().coops[k];
            std::vector<size_t> group{k};
            if (!c.group.empty())
                for (size_t j = 0; j < run.rules().coops.size(); ++j)
                    if (j != k && run.rules().coops[j].group == c.group) group.push_back(j);
            int sent = 0;
            for (size_t g : group) {
                const RaidRun::CoopView v = run.coop(g);
                const Rect r = run.rules().coops[g].rect;
                int need = std::max(0, v.need - heldByOthers(r));
                for (int slot = 0; need > 0; ++slot) {
                    const int s = nearestFree(r.bottomCenter());
                    if (s < 0) break;
                    // Spread over the plate (never all on one pixel), feet on its floor.
                    const float span = std::max(0.0f, r.w - 16.0f);
                    const float off = span <= 0.0f ? 0.0f : std::fmod(static_cast<float>(slot) * 13.0f + 6.0f, span) - span * 0.5f;
                    give(s, Task::Mechanism, {r.center().x + off, r.bottom()}, Stand);
                    Bot& b = bots_[static_cast<size_t>(s)];
                    b.mechanism = r;
                    b.goalNode = nav ? nav->nearest({r.center().x + off, r.bottom()}, std::max(40.0f, r.w),
                                                    [&](const NavGraph::Node& nd) { return nd.mover < 0 && r.contains({nd.feet.x, nd.feet.y - 4.0f}); })
                                     : -1;
                    if (b.goalNode < 0) b.goalNode = standNode(r);
                    --need;
                    ++sent;
                }
            }
            said.push_back(shortLabel(c.label) + " : " + std::to_string(sent));
            break;  // the first plate of the zone not done (the order of the route)
        }
    // ------------------------------------------------------------------ the zone's objectives
    struct Job {
        int act;
        Vec2 at;
        uint32_t target;
        std::string thing, label;
        int quota;
        Rect area;
    };
    std::vector<Job> light, heavy;
    const Branch& br = run.branch();
    for (const ZoneObjective& zo : run.rules().objectives) {
        if (zo.index < 0 || zo.index >= static_cast<int>(br.objectives.size())) continue;
        const ObjectiveInfo& info = br.objectives[static_cast<size_t>(zo.index)];
        if (Branch::done(run.mask(), info.index) || !br.pairDone(info, run.localDone()).empty()) continue;
        LevelEntity* e = w.entity(zo.source);
        if (!e || e->removed) continue;
        const std::string label = shortLabel(info.label);
        if (zo.trigger == "cleared") {
            auto* cz = dynamic_cast<CombatZoneEntity*>(e);
            if (!cz || cz->cleared()) continue;
            heavy.push_back({Enter, e->box.center(), 0, e->id(), label, 8, e->box});
        } else if (zo.trigger == "killed") {
            auto* sp = dynamic_cast<EnemySpawnEntity*>(e);
            Enemy* en = sp ? sp->enemy(w) : nullptr;
            if (!en || !en->alive()) continue;
            heavy.push_back({Strike, en->box.bottomCenter(), en->uid(), e->id(), label, 8, en->box});
        } else if (zo.trigger == "activate") {
            if (dynamic_cast<ReceptorEntity*>(e)) light.push_back({Shoot, e->box.center(), 0, e->id(), label, 2, e->box});
            else light.push_back({Interact, e->box.bottomCenter(), 0, e->id(), label, 1, e->box});
        } else if (zo.trigger == "collected") {
            light.push_back({Enter, e->box.bottomCenter(), 0, e->id(), label, 1, e->box});
        }
    }
    const bool jobs = !light.empty() || !heavy.empty();
    for (const Job& j : light) {
        int sent = 0;
        for (int q = 0; q < j.quota; ++q) {
            // A generator needs a shot (the wave or the boomerang): only those who carry one.
            const int s = nearestFree(j.at, [&](int seat) { return j.act != Shoot || shooter(w.playerAt(seat)) != nullptr; });
            if (s < 0 || (freeCount() <= 1 && !heavy.empty() && sent > 0)) break;
            give(s, Task::Objective, j.at, j.act, 0, j.thing);
            Bot& b = bots_[static_cast<size_t>(s)];
            if (j.act == Shoot && nav) {
                // Where the wave reaches it: its height, from 30 to 220 px, nothing between.
                const Vec2 c = j.area.center();
                const float half = j.area.h * 0.5f + 5.0f;
                b.goalNode = nav->nearest(c, 230.0f, [&](const NavGraph::Node& nd) {
                    const float dy = (nd.feet.y - 11.0f) - c.y;
                    const float dx = std::fabs(nd.feet.x - c.x);
                    return nd.mover < 0 && std::fabs(dy) <= half && dx >= 30.0f && dx <= 220.0f &&
                           w.physics().lineOfSight({nd.feet.x, nd.feet.y - 11.0f}, c);
                });
                if (b.goalNode < 0) b.goalNode = standNode(j.area.expanded(24.0f));
            } else {
                b.goalNode = standNode(j.area.expanded(4.0f));
            }
            ++sent;
        }
        if (sent > 0) said.push_back(j.label + " : " + std::to_string(sent));
    }
    for (size_t k = 0; !heavy.empty() && freeCount() > 0; ++k) {
        const Job& j = heavy[k % heavy.size()];
        const int s = nearestFree(j.at);
        if (s < 0) break;
        give(s, j.act == Strike ? Task::Fight : Task::Objective, j.at, j.act, j.target, j.thing);
        Bot& b = bots_[static_cast<size_t>(s)];
        b.goalNode = nav ? (j.act == Strike ? nav->locate(j.at, 120.0f) : standNode(j.area.expanded(-16.0f))) : -1;
        if (k < heavy.size()) said.push_back(j.label);
    }
    // ------------------------------------------------------------------ the way out
    if (!fight && !platesLeft && !jobs && freeCount() > 0) {
        int to = 0;
        const int exit = chooseExit(run, &to);
        if (exit >= 0) {
            const Passage& p = run.rules().passages[static_cast<size_t>(exit)];
            // The administrator there and playing: they lead the way out (the group waits near it);
            // away or AFK: the simulated players go on alone.
            const bool wait = adminActive && !((seats >> 0) & 1u) && playing(w, 0);
            const ZoneInfo* z = run.branch().zone(to);
            said.push_back((wait ? "Attend à la sortie vers " : "Sortie vers ") + std::string(z ? z->name : "?"));
            for (int s : drivers) {
                if (busy[static_cast<size_t>(s)]) continue;
                give(s, Task::Exit, p.rect.bottomCenter(), wait ? Stand : Enter);
                Bot& b = bots_[static_cast<size_t>(s)];
                if (!nav) continue;
                if (wait)
                    b.goalNode = nav->nearest(p.rect.center(), 220.0f, [&](const NavGraph::Node& nd) {
                        const float d = std::fabs(nd.feet.x - p.rect.center().x);
                        return nd.mover < 0 && d >= 40.0f + 8.0f * static_cast<float>(s) && !p.rect.expanded(8.0f).contains({nd.feet.x, nd.feet.y - 8.0f});
                    });
                else b.goalNode = standNode(p.rect);
            }
        }
    }
    // ------------------------------------------------------------------ the others: help, or keep together
    for (int s : drivers) {
        if (busy[static_cast<size_t>(s)]) continue;
        Bot& b = bots_[static_cast<size_t>(s)];
        if (!heavy.empty()) {
            give(s, heavy.front().act == Strike ? Task::Fight : Task::Objective, heavy.front().at, heavy.front().act, heavy.front().target);
            b.goalNode = nav ? nav->locate(heavy.front().at, 160.0f) : -1;
            continue;
        }
        // Next to the nearest player it does not play (the administrator), else where it is.
        int mate = -1;
        float best = 1e30f;
        for (int o = 0; o < n; ++o)
            if (!((seats >> o) & 1u) && playing(w, o) && (w.playerAt(o).feet() - w.playerAt(s).feet()).lengthSq() < best) {
                best = (w.playerAt(o).feet() - w.playerAt(s).feet()).lengthSq();
                mate = o;
            }
        const float side = (s % 2) != 0 ? -1.0f : 1.0f;
        const Vec2 at = mate >= 0 ? w.playerAt(mate).feet() + Vec2{side * (24.0f + 14.0f * static_cast<float>(s / 2)), 0.0f} : w.playerAt(s).feet();
        give(s, Task::Follow, at, None);
        b.goalNode = nav && mate >= 0 ? nav->locate(at, 80.0f) : -1;
    }
    std::sort(said.begin(), said.end());
    said.erase(std::unique(said.begin(), said.end()), said.end());
    plan_.clear();
    for (const std::string& t : said) plan_ += (plan_.empty() ? "" : " · ") + t;
}

// ----------------------------------------------------------------------- one seat's frame

void TestBots::steer(const GameWorld& w, int seat, Bot& b, InputFrame& f, uint32_t& held, int& dir, float& stop) {
    const Player& me = w.playerAt(seat);
    const Vec2 feet = me.feet();
    const NavGraph* nav = nav_.get();
    const bool footed = me.grounded() || me.onLadder;
    const int now = static_cast<int>(tick_);
    // A path to its goal on the graph (again when the goal changed, every 2 s, or when it stalls).
    if (nav && b.goalNode >= 0 && footed) {
        const bool stale = b.path.empty() || b.path.back() != b.goalNode || now - b.planned > 240 || b.noProgress > 360;
        if (stale && now - b.planned > 30) {
            const int here = nav->locate(feet);
            b.planned = now;
            b.best = 1e30f;
            b.noProgress = 0;
            b.at = 0;
            if (here < 0 || !nav->path(here, b.goalNode, b.path, 20000)) b.path.clear();
        }
    }
    auto reached = [&](int i) {
        const NavGraph::Node& nd = nav->node(i);
        if (nd.mover >= 0) {
            const NavGraph::Mover& m = nav->movers()[static_cast<size_t>(nd.mover)];
            return me.grounded() && me.groundCollider() == m.collider && (nav->moverFeet(w, nd.mover) - nd.feet).length() < 24.0f;
        }
        return std::fabs(feet.x - nd.feet.x) < 7.0f && std::fabs(feet.y - nd.feet.y) < 10.0f && footed;
    };
    if (!nav || b.path.size() < 2) {
        // No path: straight toward the goal, jumping the walls and the gaps (as before 2.77).
        const float dx = b.goal.x - feet.x;
        dir = std::fabs(dx) > stop ? (dx > 0.0f ? 1 : -1) : 0;
        if (dir != 0 && me.grounded()) {
            Rect ahead = me.box;
            ahead.x += static_cast<float>(dir) * 6.0f;
            ahead.y -= 2.0f;
            MoveOptions opt;
            const bool wall = w.physics().overlapsSolid(ahead, opt);
            const Vec2 probe{feet.x + static_cast<float>(dir) * 16.0f, feet.y - 2.0f};
            const bool gap = !w.physics().raycast(probe, {probe.x, probe.y + 120.0f}, true).has_value();
            if ((wall || gap || b.still > 18) && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump))) b.jumpHold = 18;
        }
        if (me.grounded() && feet.y - b.goal.y > 28.0f && std::fabs(dx) < 150.0f && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump))) b.jumpHold = 18;
        if (!me.grounded() && me.vel.y > -40.0f && !b.airJump && (feet.y - b.goal.y > 20.0f || b.still > 6)) {
            b.airJump = true;
            if (!(b.prevHeld & (1u << kJump))) {
                held |= 1u << kJump;
                b.jumpHold = 14;
            }
        }
        return;
    }
    // Along the path: past the nodes reached (a few ahead: it may have gone further).
    for (size_t k = std::min(b.path.size() - 1, b.at + 6); k > b.at; --k)
        if (reached(b.path[k])) {
            b.at = k;
            b.best = 1e30f;
            b.noProgress = 0;
            break;
        }
    if (b.at + 1 >= b.path.size()) {
        // At its goal node: the last pixels toward the goal itself.
        const float dx = b.goal.x - feet.x;
        dir = std::fabs(dx) > stop && std::fabs(dx) < 40.0f ? (dx > 0.0f ? 1 : -1) : 0;
        return;
    }
    const int src = b.path[b.at], dst = b.path[b.at + 1];
    const NavGraph::Node& S = nav->node(src);
    const NavGraph::Node& T = nav->node(dst);
    const NavGraph::Move move = nav->moveBetween(src, dst);
    Vec2 to = T.mover >= 0 ? nav->moverFeet(w, T.mover) : T.feet;
    const float dx = to.x - feet.x, up = feet.y - to.y;
    const float dist = std::fabs(dx) + std::fabs(up);
    if (dist < b.best - 2.0f) {
        b.best = dist;
        b.noProgress = 0;
    } else {
        ++b.noProgress;
    }
    const bool onMover = S.mover >= 0 && me.grounded() && me.groundCollider() == nav->movers()[static_cast<size_t>(S.mover)].collider;
    switch (move) {
        case NavGraph::Move::Walk:
            dir = std::fabs(dx) > 2.0f ? (dx > 0.0f ? 1 : -1) : 0;
            break;
        case NavGraph::Move::Climb:
            dir = std::fabs(dx) > 3.0f ? (dx > 0.0f ? 1 : -1) : 0;
            if (std::fabs(up) > 4.0f && (me.onLadder || S.ladder || T.ladder)) {
                held |= 1u << (up > 0.0f ? kUp : kDown);
                f.moveY = static_cast<int8_t>(up > 0.0f ? -127 : 127);
                if (!me.onLadder && up > 4.0f && me.grounded() && std::fabs(dx) < 8.0f) held |= 1u << kUp;
            }
            break;
        case NavGraph::Move::Ride:
            if (onMover) {
                const float mx = nav->moverFeet(w, S.mover).x - feet.x;
                dir = std::fabs(mx) > 6.0f ? (mx > 0.0f ? 1 : -1) : 0;
            } else {
                dir = std::fabs(dx) > 4.0f ? (dx > 0.0f ? 1 : -1) : 0;
            }
            break;
        case NavGraph::Move::Drop: {
            const Collider* ground = me.grounded() ? w.physics().get(me.groundCollider()) : nullptr;
            if (me.grounded() && std::fabs(dx) < 12.0f && ground && ground->kind == ColliderKind::OneWay) {
                held |= 1u << kDown;
                f.moveY = 127;
                if (!(b.prevHeld & (1u << kJump))) held |= 1u << kJump;
                dir = 0;
                break;
            }
            if (std::fabs(dx) < 28.0f || !me.grounded()) {
                dir = std::fabs(dx) > 2.0f ? (dx > 0.0f ? 1 : -1) : 0;
                if (!me.grounded() && me.vel.y > 20.0f && !b.airJump && std::fabs(dx) > 16.0f && feet.y > to.y - 30.0f) {
                    b.airJump = true;  // short of the far side: the air jump
                    if (!(b.prevHeld & (1u << kJump))) {
                        held |= 1u << kJump;
                        b.jumpHold = 12;
                    }
                }
                break;
            }
            [[fallthrough]];  // across a gap to a lower floor: a jump, not a walk off the edge
        }
        case NavGraph::Move::Jump: {
            dir = std::fabs(dx) > 2.0f ? (dx > 0.0f ? 1 : -1) : 0;
            if (me.grounded() || onMover) {
                // A moving platform: when it is at that place of its route, not before.
                bool go = true;
                if (T.mover >= 0) go = (nav->moverFeet(w, T.mover) - T.feet).length() < 28.0f && std::fabs(dx) < 120.0f;
                if (S.mover >= 0 && !onMover) go = true;
                const float fromSrc = std::fabs(feet.x - S.feet.x);
                if (!go) {
                    dir = std::fabs(S.feet.x - feet.x) > 4.0f && S.mover < 0 ? (S.feet.x > feet.x ? 1 : -1) : 0;
                } else if (fromSrc < 18.0f || std::fabs(dx) < 90.0f || b.noProgress > 40) {
                    // A full jump for a gap (its airtime carries it across), a short one for a step.
                    if (b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump)))
                        b.jumpHold = up > 30.0f ? 24 : up > 8.0f ? 18 : std::fabs(dx) > 36.0f ? 20 : 10;
                }
            } else if (me.vel.y > -40.0f && !b.airJump && (up > 6.0f || (std::fabs(dx) > 12.0f && me.vel.y > 20.0f && up > -24.0f))) {
                b.airJump = true;
                if (!(b.prevHeld & (1u << kJump))) {
                    held |= 1u << kJump;
                    b.jumpHold = 14;
                }
            }
            break;
        }
    }
    // Stalled on the way: a hop, then a new path.
    if (b.noProgress > 120 && me.grounded() && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump)) && b.noProgress % 60 == 0) b.jumpHold = 18;
    if (me.grounded()) b.airJump = false;
}

InputFrame TestBots::drive(RaidRun& run, int seat, Bot& b) {
    InputFrame f;
    GameWorld& w = run.world();
    if (!playing(w, seat)) {
        b.prevHeld = 0;
        b.jumpHold = 0;
        b.path.clear();
        return f;  // down (its soul waits), or not in play
    }
    const Player& me = w.playerAt(seat);
    const Vec2 feet = me.feet();
    const Level lv = levelOf(skill_);
    const NavGraph* nav = nav_.get();
    float stop = b.task == Task::Follow ? 14.0f : 6.0f;
    uint32_t held = 0;
    // The rising liquid of the zone, announced or rising, and it under its level — or a danger announced
    // over a whole floor (a gigaboss's tide: wider than a dodge can leave): up to a place above it.
    bool fleeing = false;
    Rect flood;
    if (run.rules().magma.present)
        if (auto* dz = dynamic_cast<DamageZoneEntity*>(w.entity(run.rules().magma.zone))) {
            const float in = run.magmaIn();
            const bool coming = dz->warning() || dz->magmaLevel() > 0.0f || run.magmaActive() || (in >= 0.0f && in <= run.rules().magma.warn + 1.0f);
            if (coming) flood = dz->object().rect;
        }
    if (flood.w <= 0.0f)
        for (const Rect& d : dangers_)
            if (d.w > 260.0f && d.expanded(6.0f).intersects(me.box) && (flood.w <= 0.0f || d.w > flood.w)) flood = d;
    const bool under = flood.w > 0.0f && feet.x > flood.x - 6.0f && feet.x < flood.right() + 6.0f && feet.y > flood.y - 6.0f;
    if (under && nav) {
        auto safe = [&](int i) {
            const NavGraph::Node& nd = nav->node(i);
            return nd.mover < 0 && (nd.feet.y < flood.y - 8.0f || nd.feet.x < flood.x - 14.0f || nd.feet.x > flood.right() + 14.0f);
        };
        if (b.fleeNode < 0 || !safe(b.fleeNode)) {
            const int here = nav->locate(feet);
            b.fleeNode = here >= 0 ? nav->search(here, safe, 4000.0f) : -1;
        }
        if (b.fleeNode >= 0) {
            fleeing = true;
            b.task = Task::Flee;
            b.goal = nav->node(b.fleeNode).feet;
            b.goalNode = b.fleeNode;
            stop = 4.0f;
        }
    } else if (b.task == Task::Flee) {
        b.task = Task::Follow;
        b.fleeNode = -1;
    }
    // Its goal moves with what it strikes. A boss, or anything during a gigaboss's fight, from afar with the
    // wave when it carries one (its body hurts on contact, its blows fall around it): at its height, in sight.
    const Enemy* foe = enemyOf(w, b.target);
    // 2.76.2: a boss by parts is struck where a blow counts (its weak points and normal parts in its pose).
    const Rect fb = foe ? strikeBox(*foe, w) : Rect{};
    const Vec2 fc = fb.center();
    const bool bossFight = run.gigaboss().present() && run.gigaboss().engaged();
    const bool ranged = foe && shooter(me) && (foe->def().isBoss || fb.w >= 56.0f || fb.h >= 72.0f || bossFight);
    if (!fleeing && foe) {
        const float side = fc.x < me.center().x ? 1.0f : -1.0f;
        const float off = ranged ? fb.w * 0.5f + (foe->def().isBoss || fb.w >= 56.0f ? 200.0f : 90.0f) : fb.w * 0.5f + 12.0f;
        b.goal = {fc.x + side * off, ranged ? fc.y + 11.0f : fb.bottom()};
        if (nav && (b.goalNode < 0 || (nav->node(b.goalNode).feet - b.goal).lengthSq() > 64.0f * 64.0f)) {
            b.goalNode = -1;
            if (ranged) {
                const Vec2 c = fc;
                const float half = fb.h * 0.5f + 4.0f;
                const float minDx = fb.w * 0.5f + 40.0f;
                b.goalNode = nav->nearest(b.goal, 180.0f, [&](const NavGraph::Node& nd) {
                    const float dx = std::fabs(nd.feet.x - c.x);
                    return nd.mover < 0 && std::fabs(nd.feet.y - 11.0f - c.y) <= half && dx >= minDx && dx <= fb.w * 0.5f + 250.0f &&
                           w.physics().lineOfSight({nd.feet.x, nd.feet.y - 11.0f}, c);
                });
            }
            if (b.goalNode < 0) b.goalNode = nav->locate(b.goal, 160.0f);
        }
        stop = 6.0f;
    } else if (!fleeing && b.act == Strike && !foe) {
        b.act = None;  // beaten: the next plan gives it another task
    }
    // A blow announced on it (a fist, a jet, a gigaboss's mark): out of it by the nearest side, once it
    // has seen it (its level's reaction; the players holding a mechanism or reviving stay).
    bool dodging = false;
    // A blow or a shot about to land on it: the shield first, when it carries one ready (it stops them).
    for (size_t k = 0; k < dangers_.size(); ++k)
        if (dangers_[k].intersects(me.box.expanded(2.0f))) {
            for (const auto& pw : me.powers())
                if (pw->id() == "shield" && keyBit(pw.get()) >= 0 && pw->ready(me) && !(b.prevHeld & (1u << keyBit(pw.get()))) &&
                    noise(seat, tick_ + 29) < lv.precision)
                    held |= 1u << keyBit(pw.get());
            break;
        }
    if (!fleeing && b.task != Task::Revive && b.task != Task::Mechanism) {
        const Rect* hit = nullptr;
        for (const Rect& d : dangers_)
            if (d.expanded(6.0f).intersects(me.box)) {
                hit = &d;
                break;
            }
        if (hit) {
            if (b.react <= 0) b.react = lv.reaction + 1;
            if (--b.react <= 1 && noise(seat, tick_ / 30) < std::max(0.55f, lv.precision)) {
                const float left = me.center().x - hit->x, right = hit->right() - me.center().x;
                const float x = left < right ? hit->x - 22.0f : hit->right() + 22.0f;
                const float dx = x - feet.x;
                const int dir = std::fabs(dx) > 4.0f ? (dx > 0.0f ? 1 : -1) : 0;
                held |= dir > 0 ? 1u << kRight : dir < 0 ? 1u << kLeft : 0u;
                f.moveX = static_cast<int8_t>(dir * 127);
                dodging = true;
                b.react = 1;
            }
        } else {
            b.react = 0;
        }
    }
    int dir = 0;
    if (!dodging) {
        steer(w, seat, b, f, held, dir, stop);
        // What it does once there.
        if (foe && !fleeing) {
            const Vec2 d = fc - me.center();
            const bool inReach = std::fabs(d.x) <= fb.w * 0.5f + 22.0f && std::fabs(d.y) <= fb.h * 0.5f + 18.0f;
            if (inReach) {
                dir = 0;
                b.goalBest = 0.0f;  // fighting where it should: not lost
                b.lost = 0;
                const bool facing = (d.x >= 0.0f) == (me.facing >= 0.0f) || std::fabs(d.x) < 3.0f;
                if (!facing) dir = d.x > 0.0f ? 1 : -1;  // turn toward it (one step)
                else if (b.attackCool <= 0 && (!b.sync || syncReady_) && noise(seat, tick_) < lv.precision + 0.1f) {
                    held |= 1u << kAttack;
                    b.attackCool = 22;
                }
                if (d.y < -30.0f && me.grounded() && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump))) b.jumpHold = 12;  // up to it
            } else if (const Power* sh = shooter(me); sh && keyBit(sh) >= 0 && std::fabs(d.y) < fb.h * 0.5f + 6.0f && std::fabs(d.x) > 30.0f &&
                       std::fabs(d.x) - fb.w * 0.5f < 280.0f && !b.sync && w.physics().lineOfSight(me.center(), fc)) {
                // The wave from afar (energy kept for the generators, unless it fights a boss).
                const bool facing = (d.x >= 0.0f) == (me.facing >= 0.0f);
                if (ranged && me.grounded() && std::fabs(b.goal.x - feet.x) < 40.0f) {
                    dir = facing ? 0 : (d.x > 0.0f ? 1 : -1);  // in place, turned toward it
                    b.goalBest = 0.0f;
                    b.lost = 0;
                }
                if (facing && sh->ready(me) && me.energy >= (ranged ? 10.0f : 40.0f) && !(b.prevHeld & (1u << keyBit(sh))) &&
                    noise(seat, tick_ + 7) < lv.precision + 0.1f)
                    held |= 1u << keyBit(sh);
            }
        }
        if (b.act == Shoot && !fleeing) {
            // A generator: from its place, facing it, the wave (or the boomerang).
            LevelEntity* e = w.entity(b.thing);
            const Power* sh = shooter(me);
            if (e && sh && keyBit(sh) >= 0 && nav && b.goalNode >= 0 && b.path.size() >= 1 && b.at + 1 >= b.path.size() && me.grounded()) {
                const float dx = e->box.center().x - me.center().x;
                dir = 0;
                if ((dx >= 0.0f) != (me.facing >= 0.0f)) dir = dx > 0.0f ? 1 : -1;
                else if (sh->ready(me) && !(b.prevHeld & (1u << keyBit(sh))) && noise(seat, tick_ + 13) < lv.precision + 0.1f) held |= 1u << keyBit(sh);
            }
        }
        if (b.act == Interact && !fleeing) {
            if (LevelEntity* e = w.entity(b.thing); e && e->box.expanded(8.0f).intersects(me.box)) {
                dir = 0;
                if (!(b.prevHeld & (1u << kInteract))) held |= 1u << kInteract;
            }
        }
        if (b.task == Task::Revive && !fleeing && std::fabs(b.goal.x - feet.x) <= GameWorld::kReviveReachX - 6.0f &&
            std::fabs(b.goal.y - feet.y) <= GameWorld::kReviveReachY - 6.0f) {
            // 2.77: a revival is held (« Interagir » kept down for 2 s near the soul).
            dir = 0;
            held |= 1u << kInteract;
        }
        if (b.act == Stand && !fleeing) {
            const Rect target = b.task == Task::Mechanism ? b.mechanism : Rect{b.goal.x - 8.0f, b.goal.y - 24.0f, 16.0f, 26.0f};
            if (target.w > 0.0f && target.intersects(me.box) && me.grounded() && std::fabs(b.goal.x - feet.x) < 10.0f) dir = 0;
        }
    }
    // Lost on the way (no nearer to its goal): counted for the rescue of a test (TestSession).
    const float gd = (b.goal - feet).length();
    if (b.goalBest <= 0.0f && gd >= 24.0f) b.goalBest = gd;  // left its fight: measured again from here
    if (gd < b.goalBest - 8.0f || gd < 24.0f || b.task == Task::Follow) {
        b.goalBest = gd;
        b.lost = 0;
    } else {
        ++b.lost;
    }
    if (dir > 0) held |= 1u << kRight;
    if (dir < 0) held |= 1u << kLeft;
    if (dir != 0) f.moveX = static_cast<int8_t>(dir * 127);
    // Stuck against something while wanting to move.
    if (dir != 0 && std::fabs(me.box.x - b.lastX) < 0.25f) ++b.still;
    else b.still = 0;
    b.lastX = me.box.x;
    if (b.jumpHold > 0) {
        held |= 1u << kJump;
        --b.jumpHold;
    }
    if (b.attackCool > 0) --b.attackCool;
    f.held = held;
    f.pressed = held & ~b.prevHeld;
    f.released = b.prevHeld & ~held;
    b.prevHeld = held;
    if (const auto& offer = w.powerOffer(); offer && offer->player == seat) f.choice = choice(w, seat);
    return f;
}

}  // namespace cosmic::raid
