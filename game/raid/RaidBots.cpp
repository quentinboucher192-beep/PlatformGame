#include "game/raid/RaidBots.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "engine/physics/Collision.h"
#include "engine/physics/PhysicsWorld.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/BossHazards.h"
#include "game/enemies/Enemy.h"
#include "game/enemies/HitParts.h"
#include "game/gameplay/Projectile.h"
#include "game/gameplay/entities/Entities.h"
#include "game/powers/Power.h"
#include "game/raid/BossHitboxes.h"
#include "game/raid/GigabossRoutine.h"

namespace cosmic::raid {

namespace {

using bots::Danger;
using bots::Role;

constexpr int kLeft = static_cast<int>(Action::MoveLeft);
constexpr int kRight = static_cast<int>(Action::MoveRight);
constexpr int kUp = static_cast<int>(Action::MoveUp);
constexpr int kDown = static_cast<int>(Action::MoveDown);
constexpr int kJump = static_cast<int>(Action::Jump);
constexpr int kAttack = static_cast<int>(Action::Attack);
constexpr int kInteract = static_cast<int>(Action::Interact);

/// The eight directions of a stick (the wave's aim): right, up-right, up, up-left, left, down-left, down,
/// down-right. On the ground a shot downward goes straight ahead (WavePower).
constexpr int kDirX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int kDirY[8] = {0, -1, -1, -1, 0, 1, 1, 1};
Vec2 dirOf(int d) {
    const Vec2 v{static_cast<float>(kDirX[d]), static_cast<float>(kDirY[d])};
    return kDirX[d] != 0 && kDirY[d] != 0 ? v * 0.70710678f : v;
}
constexpr float kWaveRange = 285.0f;  // a little short of its 300 px
constexpr float kKeepOff = 120.0f;  // 2.84: the strikers' distance to the boss's body (its shape) when they can

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

/// The level: its reaction (steps before it moves out of a blow it sees), its precision (share of its
/// strikes and shots that go off at the right moment, its aim and its timing) and its choices (2.79: the
/// shield used before a blow, the flash hidden from, the beams read early). Never anything else.
struct Level {
    int reaction;
    float precision;
    float shieldSense;  // share of the unavoidable blows it raises its shield against
    bool hides;         // hides from a flash behind a column
    float timing;       // seconds of error on its jumps over a wave
};
Level levelOf(BotSkill s) {
    switch (s) {
        case BotSkill::Beginner: return {36, 0.6f, 0.35f, false, 0.09f};
        case BotSkill::Expert: return {6, 1.0f, 1.0f, true, 0.0f};
        default: return {18, 0.85f, 0.7f, true, 0.04f};
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
const Power* powerOf(const Player& p, const char* id) {
    for (const auto& pw : p.powers())
        if (pw->id() == id) return pw.get();
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

/// 2.79: the whole shape of `e` in its pose now — every part, those that only hurt too (its body hurts on contact,
/// docs/HITBOX_PARTS.md), and its box: what a player must keep out of.
Rect bodyBox(const Enemy& e, const GameWorld& w, std::vector<Rect>* parts = nullptr) {
    Rect out = e.box;
    if (parts) parts->clear();
    if (!fightsByParts(e, w)) {
        if (parts) parts->push_back(out);
        return out;
    }
    const AnimationSetPtr& set = e.animator().set();
    const auto whole = [&] {
        if (parts) parts->push_back(out);
        return out;
    };
    if (!set || set->frameWidth <= 0 || set->frameHeight <= 0) return whole();
    const HitPartsSheet* sheet = HitPartsSheet::of(set->sourcePath);
    if (!sheet) return whole();
    const Rect src = e.animator().sourceRect();
    const std::vector<HitPartPiece>* pieces = sheet->cell(static_cast<int>(src.y) / set->frameHeight, static_cast<int>(src.x) / set->frameWidth);
    if (!pieces || pieces->empty()) return whole();
    for (const HitPartPiece& piece : *pieces) {
        const std::vector<Vec2> poly = placePiece(piece, *sheet, e.box.bottomCenter(), e.def().scale, e.facing < 0.0f);
        if (poly.empty()) continue;
        Rect r{poly[0].x, poly[0].y, 0.0f, 0.0f};
        for (const Vec2& q : poly) {
            const float l = std::min(out.x, q.x), t = std::min(out.y, q.y);
            out = {l, t, std::max(out.right(), q.x) - l, std::max(out.bottom(), q.y) - t};
            const float rl = std::min(r.x, q.x), rt = std::min(r.y, q.y);
            r = {rl, rt, std::max(r.right(), q.x) - rl, std::max(r.bottom(), q.y) - rt};
        }
        // 2.84: each part's own box (what touches is a part, not the box around all of them: the space under a skull,
        // between two legs, is free).
        if (parts) parts->push_back(r);
    }
    if (parts && parts->empty()) parts->push_back(out);
    return out;
}

const Enemy* enemyOf(const GameWorld& w, uint32_t uid) {
    if (uid == 0) return nullptr;
    const Enemy* e = w.findEnemy(uid);
    return e && e->alive() && !e->dormant() ? e : nullptr;
}

std::string shortLabel(const std::string& s) { return s.substr(0, s.find(" (")); }

Rect bodyAt(Vec2 feet) { return {feet.x - 5.0f, feet.y - 22.0f, 10.0f, 22.0f}; }

/// 2.78: the « thing » of a player sent to hold the plate of a passage of the group (it may step onto it).
const char* const kGroupPlate = "group_plate";

/// 2.87: the steps before a sweeping beam read as it turns (Danger::turn) reaches `box` (its angle from where the beam
/// is now, at its speed); a wall between its origin and `box` keeps it off (1 << 20). Any other danger: its `in`.
int sweepIn(const GameWorld& w, const Danger& d, const Rect& box) {
    if (d.kind != Danger::Kind::Line || d.gapHalf <= 0.0f || d.turn == 0.0f) return d.in;
    if (w.physics().raycast(d.from, box.center()).has_value()) return 1 << 20;
    const Vec2 c = box.center() - d.from;
    float diff = std::atan2(c.y, c.x) - d.angle;
    while (diff > 3.14159265f) diff -= 6.2831853f;
    while (diff < -3.14159265f) diff += 6.2831853f;
    diff *= d.turn > 0.0f ? 1.0f : -1.0f;  // ahead of the beam: positive
    const float margin = std::atan2(std::max(box.w, box.h) * 0.5f + 12.0f, std::max(1.0f, c.length()));
    if (diff < -margin) return 1 << 20;  // already swept
    return d.in + static_cast<int>(std::max(0.0f, diff - margin) / std::fabs(d.turn) * static_cast<float>(kSimHz));
}

bool hitsLine(const Danger& d, const Rect& box) {
    if (d.gapHalf > 0.0f) {
        // A sweeping beam: the sector it turns through (as far as it reaches).
        const Vec2 c = box.center() - d.from;
        if (c.length() > d.reach) return false;
        float diff = std::fabs(std::atan2(c.y, c.x) - d.gap);
        while (diff > 3.14159265f) diff = std::fabs(diff - 6.2831853f);
        return diff <= d.gapHalf;
    }
    return Collision::segmentIntersectsRect(d.from, d.to, box.expanded(d.width * 0.5f));
}

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
    navi_.reset();
    navi_.setSkill(static_cast<int>(skill_));
    navZone_ = -1;
    tick_ = 0;
    order_ = {};
    tactic_.reset();
    tacticFor_ = "-";
    skyLockedAt_ = -1;
    skyHangAt_ = -1;
}

uint64_t TestBots::hash() const {
    uint64_t h = 1469598103934665603ULL;
    for (const Bot& b : bots_) {
        h = mix(h, b.prevHeld);
        h = mix(h, static_cast<uint64_t>(b.jumpHold) | (static_cast<uint64_t>(b.airJump) << 16) | (static_cast<uint64_t>(b.task) << 24) |
                       (static_cast<uint64_t>(b.act) << 32) | (static_cast<uint64_t>(b.role) << 40));
        h = mix(h, static_cast<uint64_t>(b.still) | (static_cast<uint64_t>(b.mechanismSteps) << 20));
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(b.goalNode)) << 32) | b.target);
        h = mix(h, (static_cast<uint64_t>(b.at) << 32) | static_cast<uint32_t>(b.lost));
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(b.spot)) << 32) | (static_cast<uint32_t>(b.spotDir) << 8) |
                       static_cast<uint32_t>(b.escapeDir + 2));
    }
    h = mix(h, (static_cast<uint64_t>(order_.kind) << 56) ^ (static_cast<uint64_t>(std::lround(order_.at.x)) << 24) ^
                   static_cast<uint64_t>(std::lround(order_.at.y)) ^ (static_cast<uint64_t>(order_.tick) << 32));
    h = mix(h, navi_.hash());
    h = mix(h, static_cast<uint64_t>(skill_));
    return seed_ ? mix(h, seed_) : h;
}

std::string TestBots::wish(int seat) {
    if (seat <= 0) return {};
    return seat % 2 == 1 ? "triple_jump" : "boomerang";
}

std::string TestBots::wishOf(int seat) const {
    if (seat < 0 || seat >= static_cast<int>(bots_.size())) return {};
    // 2.79: its role's power for its branch's gigaboss (the reviver: the decoy; the others: the triple jump).
    const Role r = bots_[static_cast<size_t>(seat)].role;
    if (r == Role::None) return wish(seat);
    const std::unique_ptr<bots::BossTactic> t = bots::BossTactic::make(tacticFor_ == "-" ? std::string() : tacticFor_);
    const std::string w = t->wish(r);
    return w.empty() ? wish(seat) : w;
}

uint8_t TestBots::choice(const GameWorld& w, int seat) const {
    const auto& offer = w.powerOffer();
    if (!offer || offer->player != seat || seat < 0 || seat >= w.teamSize()) return InputFrame::kChoiceLeave;
    if (offer->powerId != wishOf(seat)) return InputFrame::kChoiceLeave;
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

void TestBots::order(const bots::Order& o) {
    order_ = o;
    order_.tick = tick_;
    if (o.kind == bots::OrderKind::Free) order_ = {};
    for (Bot& b : bots_) {
        b.spot = -1;
        b.spotAt = -100000;
    }
}

const bots::Thought& TestBots::thought(int seat) const {
    static const bots::Thought none;
    return seat >= 0 && seat < static_cast<int>(bots_.size()) ? bots_[static_cast<size_t>(seat)].thought : none;
}

std::string TestBots::nameOf(int seat) const {
    if (seat >= 0 && seat < static_cast<int>(names_.size()) && !names_[static_cast<size_t>(seat)].empty()) return names_[static_cast<size_t>(seat)];
    return "Joueur " + std::to_string(seat + 1);
}

void TestBots::think(Bot& b, const std::string& intent) {
    b.thought.role = b.role;
    b.thought.intent = intent;
    b.intent = intent;
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
        // 2.85: the relic of the secret zone first, as soon as its passage is open (a detour of a few rooms, before the long
        // zone of the ambushes); then the zones to finish, the zone of the ambushes that open the lair after the others;
        // the lair last.
        int rank;
        if (z.secret && useful(z.id, true)) rank = 0;                                  // a relic (it weakens the heart)
        else if (!z.boss && !z.secret && useful(z.id, false)) rank = br.ambushesOpenLair(z.id) ? 2 : 1;  // the zones to finish
        else if (z.boss && useful(z.id, false)) rank = 3;                              // the lair, last
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
    // 2.79: the graph of the zone (game/raid/nav): asked for when another world is loaded or a door opened
    // or closed, built in the background, used from a step fixed by the zone; the copy of the zone and the
    // blows announced this step, as its tactic reads them (the shots and the beams are read by the
    // navigation itself, with their paths; the waves along a floor are jumped by drive()).
    std::vector<Rect> blows;
    for (const bots::Danger& d : dangers_) {
        // 2.89 (fight plan): a boss whose body stands over the ledges (the Cœur, Pyroclast, Voidworm): its body too, no jump through it.
        if (d.shot || (d.body && !view_.plan.bodyInNav)) continue;
        switch (d.kind) {
            case bots::Danger::Kind::Area:
            case bots::Danger::Kind::Pull:
            case bots::Danger::Kind::Flood:
            case bots::Danger::Kind::Sector: blows.push_back(d.area); break;
            default: break;
        }
    }
    navi_.update(run, tick_, blows);
    // 2.90 (docs/RAIDS.md §36, the escape: Plan::escape): the same blows with their time — where a player may be before
    // one lands or once it is over —, and the boss's body for the choice of a dodge only (never where a dodge ends).
    if (fight_ && view_.plan.escape) {
        std::vector<nav::Blow> timed;
        for (const bots::Danger& d : dangers_) {
            if (d.shot) continue;
            if (d.kind != bots::Danger::Kind::Area && d.kind != bots::Danger::Kind::Pull && d.kind != bots::Danger::Kind::Flood &&
                d.kind != bots::Danger::Kind::Sector)
                continue;
            nav::Blow b{d.area};
            if (d.body) {
                // Its body: for the choice of a dodge only, or for every move when its plan keeps the navigation out of it.
                b.body = true;
                b.dodgeOnly = !view_.plan.bodyInNav;
            } else if (d.kind == bots::Danger::Kind::Area && d.seat < 0) {
                b.from = d.in;
                b.to = d.last >= (1 << 20) ? (1 << 30) : d.in + (d.last >= 0 ? d.last : kSimHz / 2);
            } else if (d.kind == bots::Danger::Kind::Sector) {
                b.from = d.in;  // its gravity turns over then (and stays so): out of it before
            }
            timed.push_back(b);
        }
        // Never ending a dodge in the box around its whole body (10 px on): its pose changes, its parts move (the space between
        // its legs, measured with the part's box only, is where its quake caught them).
        if (bossUid_) {
            nav::Blow whole{bossBody_.expanded(10.0f)};
            whole.dodgeOnly = true;
            whole.body = true;
            timed.push_back(whole);
        }
        navi_.terrain().setBlows(timed);
    }
    if (navi_.graphChanged() || navZone_ != run.zoneSerial()) {
        navZone_ = run.zoneSerial();
        for (Bot& b : bots_) {
            b.path.clear();
            b.goalNode = -1;
            b.fleeNode = -1;
            b.spot = -1;
            b.hideNode = -1;
        }
    }
}

// ----------------------------------------------------------------------- what is coming (2.79)

void TestBots::sense(RaidRun& run) {
    GameWorld& w = run.world();
    view_ = bots::BossView{};
    const Gigaboss& gb = run.gigaboss();
    const Enemy* boss = gb.present() ? gb.boss(w) : nullptr;
    fight_ = false;
    if (boss && boss->alive() && gb.stage() != Gigaboss::Stage::Fallen) {
        fight_ = gb.engaged();
        const Rect arena = run.rules().arena.present ? run.rules().arena.rect : boss->box.expanded(600.0f);
        for (int s = 0; s < w.teamSize() && !fight_; ++s) fight_ = playing(w, s) && arena.intersects(w.playerAt(s).box);
    }
    if (fight_) {
        const std::string routine = gb.rules() ? gb.rules()->routine : std::string();
        if (!tactic_ || tacticFor_ != routine) {
            tactic_ = bots::BossTactic::make(routine);
            tacticFor_ = routine;
        }
        // 2.90 (the escape): the shadow of its leap locked (it falls now), watched as a player watches it.
        if (boss->shadowLocked()) {
            if (skyLockedAt_ < 0) skyLockedAt_ = static_cast<int64_t>(tick_);
            view_.skyLocked = static_cast<int>(static_cast<int64_t>(tick_) - skyLockedAt_);
        } else {
            skyLockedAt_ = -1;
        }
        if (boss->outOfSight()) {
            if (skyHangAt_ < 0) skyHangAt_ = static_cast<int64_t>(tick_);
            view_.skyHang = static_cast<int>(static_cast<int64_t>(tick_) - skyHangAt_);
        } else {
            skyHangAt_ = -1;
        }
        tactic_->read(run, view_);
        bossUid_ = boss->uid();
        // Its body hurts on contact (every part of it): a lasting area to keep out of.
        // 2.84: part by part (the box around them all closed the space between its legs, under its skull, to the
        // players and their shots).
        bossBody_ = bodyBox(*boss, w, &bodyParts_);
        // 2.89 (fight plan): a walker that turns toward its target swings its arm to its other side at once (Pyroclast): its
        // body read on both sides of its box, a little wider (it walks), so that nobody stands where it turns.
        if (view_.plan.bodyBothSides > 0.0f) {
            const float cx = boss->box.center().x, m = view_.plan.bodyBothSides;
            const auto mirror = [&](const Rect& r) { return Rect{2.0f * cx - r.right(), r.y, r.w, r.h}; };
            const auto both = [&](const Rect& r) {
                const Rect o = mirror(r);
                const float l = std::min(r.x, o.x);
                return Rect{l - m, r.y, std::max(r.right(), o.right()) - l + 2.0f * m, r.h};
            };
            bossBody_ = both(bossBody_);
            for (Rect& r : bodyParts_) r = both(r);
        }
        // 2.84: bare (its few seconds to strike its weak points), part by part — the box around them all closes every
        // place near its weak points; otherwise that whole box (closer, the group took more blows: measured).
        // 2.87: its targets of a group finished together too (Titanus's restraints on its legs: the space between them).
        bool parts = view_.exposed;
        for (const bots::Target& t : view_.targets) parts = parts || t.sync > 0;
        // 2.90 (the escape): where its body will be in a third of a second (it walks, it charges): the way it goes too.
        const float lead = view_.plan.escape ? boss->vel.x * 0.3f : 0.0f;
        const auto ahead = [&](Rect r) {
            r.x += std::min(0.0f, lead);
            r.w += std::fabs(lead);
            return r;
        };
        if (parts) {
            for (const Rect& r : bodyParts_) {
                Danger body;
                body.kind = Danger::Kind::Area;
                body.area = ahead(r.expanded(6.0f));
                body.body = true;
                view_.dangers.push_back(body);
            }
        } else {
            Danger body;
            body.kind = Danger::Kind::Area;
            body.area = ahead(bossBody_.expanded(6.0f));
            body.body = true;
            view_.dangers.push_back(body);
        }
    } else {
        bots::BossTactic::readCommon(run, view_, false);
        bossUid_ = 0;
        if (gb.present() && gb.rules() && (tacticFor_ == "-" || tacticFor_ != gb.rules()->routine)) tacticFor_ = gb.rules()->routine;
    }
    // The zone's rising liquid (its periodic flow), announced or rising: everyone above its top.
    if (run.rules().magma.present)
        if (auto* dz = dynamic_cast<DamageZoneEntity*>(w.entity(run.rules().magma.zone))) {
            const float in = run.magmaIn();
            const bool coming = dz->warning() || dz->magmaLevel() > 0.0f || run.magmaActive() || (in >= 0.0f && in <= run.rules().magma.warn + 1.0f);
            if (coming) {
                Danger d;
                d.kind = Danger::Kind::Flood;
                d.area = dz->object().rect;
                view_.dangers.push_back(d);
            }
        }
    // 2.84 (fight plan): bare, all on one weak point at a time (the most hurt first), not two on each — broken one after the
    // other in its few seconds rather than three half-broken.
    if (fight_ && view_.exposed && view_.plan.focusWeak) {
        bots::Target* first = nullptr;
        for (bots::Target& t : view_.targets)
            if (t.weak)
                if (const Enemy* e = enemyOf(w, t.uid); e && (!first || e->hp() < enemyOf(w, first->uid)->hp())) first = &t;
        for (bots::Target& t : view_.targets)
            if (t.weak) {
                t.want = &t == first ? 8 : 1;
                t.priority = &t == first ? 21 : 20;
            }
    }
    dangers_ = view_.dangers;
    for (const Danger& d : dangers_)
        if (d.kind == Danger::Kind::Flood) lastFlood_ = tick_;  // 2.84: the last step a liquid was coming or up
    // The sync groups (targets finished together): each one low enough everywhere?
    syncLow_.assign(4, 1);
    std::vector<uint8_t> any(4, 0);
    for (const bots::Target& t : view_.targets) {
        if (t.sync <= 0 || t.sync >= 4) continue;
        any[static_cast<size_t>(t.sync)] = 1;
        const Enemy* e = enemyOf(w, t.uid);
        if (e && e->hpRatio() > t.holdAt + 0.001f) syncLow_[static_cast<size_t>(t.sync)] = 0;
    }
    for (size_t k = 0; k < any.size(); ++k)
        if (!any[k]) syncLow_[k] = 1;
    // 2.93: what the players' waves already flying will take from each target of a sync group (their path to the end of
    // their range, through what they have not hit yet).
    incoming_.clear();
    for (const bots::Target& t : view_.targets) {
        if (t.sync <= 0 || t.sync >= 4) continue;
        const Enemy* e = enemyOf(w, t.uid);
        if (!e) continue;
        float dmg = 0.0f;
        for (const Projectile& pr : w.projectiles()) {
            if (!pr.alive || !pr.fromPlayer || pr.explosionRadius > 0.0f) continue;
            if (std::find(pr.hitTargets.begin(), pr.hitTargets.end(), static_cast<const void*>(e)) != pr.hitTargets.end()) continue;
            const Vec2 end = pr.pos + pr.vel * std::max(0.0f, pr.life - pr.age);
            if (Collision::segmentIntersectsRect(pr.pos, end, e->box.expanded(pr.radius + 2.0f))) dmg += pr.damage;
        }
        if (dmg > 0.0f) incoming_.push_back({t.uid, dmg});
    }
}

bool TestBots::touchesBody(const Rect& r) const {
    if (!r.intersects(bossBody_)) return false;
    for (const Rect& p : bodyParts_)
        if (p.intersects(r)) return true;
    return false;
}

bool TestBots::threatened(const GameWorld& w, int seat, const Rect& box, int horizon, const Danger** out) const {
    (void)w;
    for (const Danger& d : dangers_) {
        if (d.seat >= 0 && d.seat != seat) continue;
        if (d.in > horizon) continue;
        bool hit = false;
        switch (d.kind) {
            case Danger::Kind::Area: hit = d.area.intersects(box); break;
            case Danger::Kind::Line: hit = hitsLine(d, box); break;
            case Danger::Kind::Pull: hit = d.area.intersects(box); break;
            default: break;
        }
        if (hit) {
            if (out) *out = &d;
            return true;
        }
    }
    return false;
}

bool TestBots::safeSpot(const GameWorld& w, int seat, Vec2 feet, int horizon) const {
    const Rect body = bodyAt(feet).expanded(4.0f);
    if (threatened(w, seat, body, horizon)) return false;
    for (const Danger& d : dangers_)
        if ((d.kind == Danger::Kind::Flood && feet.y > d.area.y - 6.0f && feet.x > d.area.x - 6.0f && feet.x < d.area.right() + 6.0f) ||
            (d.kind == Danger::Kind::Sector && d.area.contains({feet.x, feet.y - 11.0f})))
            return false;
    return true;
}

// ----------------------------------------------------------------------- shooting (2.79)

bool TestBots::shotReaches(const GameWorld& w, Vec2 center, int d, const Rect& target) const {
    const Vec2 dir = dirOf(d);
    const Vec2 m = center + dir * 10.0f + Vec2{0.0f, -2.0f};
    const Vec2 end = m + dir * kWaveRange;
    float t = 0.0f;
    if (!Collision::segmentIntersectsRect(m, end, target.expanded(6.0f), &t)) return false;
    const Vec2 hit = m + (end - m) * std::clamp(t + 0.02f, 0.0f, 1.0f);
    return !w.physics().raycast(m, hit, false, ColliderTag::EnemyOnly | ColliderTag::Teammate).has_value();
}

int TestBots::shotFrom(const GameWorld& w, Vec2 center, const Rect& target, bool grounded) const {
    // Straight ahead first (the most forgiving), then the diagonals, then straight up (and down in the air).
    static const int kOrder[] = {0, 4, 1, 3, 2, 7, 5, 6};
    for (int d : kOrder) {
        if (grounded && kDirY[d] > 0) continue;
        if (shotReaches(w, center, d, target)) return d;
    }
    return -1;
}

bool TestBots::chooseSpot(const GameWorld& w, int seat, Bot& b, const Rect& target, const Enemy* body) {
    const NavGraph* nav = navi_.graph();
    b.spotAt = static_cast<int>(tick_);
    if (!nav) return false;
    const Player& me = w.playerAt(seat);
    const Vec2 tc = target.center();
    const Rect bossBox = body ? bossBody_ : Rect{};
    const float clearance = b.role == Role::Lure ? 40.0f : 60.0f;
    const int here = nav->locate(me.feet(), 96.0f);
    if (here < 0) {
        b.spot = -1;
        return false;
    }
    // The nearest place it can reach on the graph (the cheapest way there, never a place across a wall it cannot
    // pass) from where a wave reaches its target; first a place of its own, at its role's distance (the lure close
    // to the boss, the others a good way off), then any.
    int dir = -1;
    bool jump = false;
    // 2.87: a target of a group finished together is fired at from where its wave crosses no other of its group (Titanus's
    // two restraints side by side on its leg: from its west side, every wave at the east one hit the west one first).
    std::vector<Rect> siblings;
    for (const bots::Target& t : view_.targets)
        if (b.syncGroup > 0 && t.sync == b.syncGroup && t.uid != b.target)
            if (const Enemy* e = enemyOf(w, t.uid)) siblings.push_back(e->box.expanded(4.0f));
    // 2.87: another of its group between it and its target (Titanus's west restraint before its east one): its place may
    // lie on the far side of the boss's body, which it crosses under its shield (drive).
    bool farSide = false;
    for (const Rect& r : siblings)
        farSide = farSide || (r.center().x - me.center().x) * (tc.x - r.center().x) > 0.0f;
    // 2.98 (Plan::pullX): its body far from where its plan wants it (its crown drags it toward its nearest player): its place on
    // the side toward there, first — the way around its body (under it) found by the navigation.
    const float pullSide = body && b.target == bossUid_ && view_.plan.pullX > 0.0f && std::fabs(view_.plan.pullX - body->center().x) > 160.0f
                               ? view_.plan.pullX - body->center().x
                               : 0.0f;
    if (pullSide != 0.0f && (me.center().x - body->center().x) * pullSide < 0.0f) farSide = true;
    for (int pass = 0; pass < 2; ++pass) {
        auto ok = [&](int i) {
            const NavGraph::Node& nd = nav->node(i);
            if (nd.mover >= 0 || nd.ladder) return false;
            // In a wave's reach of its target (2.84: of its box — a big arm's centre is far from its edge).
            if (std::max({0.0f, target.x - nd.feet.x, nd.feet.x - target.right()}) > kWaveRange + 20.0f ||
                std::max({0.0f, target.y - (nd.feet.y - 11.0f), (nd.feet.y - 11.0f) - target.bottom()}) > kWaveRange + 20.0f)
                return false;
            const Vec2 c{nd.feet.x, nd.feet.y - 11.0f};
            const float fromTarget = (c - tc).length();
            // 2.84 (fight plan): above the floor under the boss, whatever the pass (its waves run along that floor).
            if (body && view_.plan.elevate > 0.0f && (b.role != Role::Lure || view_.plan.elevateAll) && nd.feet.y > view_.plan.floorY - view_.plan.elevate)
                return false;
            // 2.84: never on another's place (a group on one pixel is swept, crushed, burnt together).
            for (int o = 0; o < static_cast<int>(bots_.size()) && pass == 1; ++o)
                if (o != seat && bots_[static_cast<size_t>(o)].spot >= 0 && ((seats_ >> o) & 1u) &&
                    (nav->node(bots_[static_cast<size_t>(o)].spot).feet - nd.feet).lengthSq() < 28.0f * 28.0f)
                    return false;
            if (pass == 0) {
                if (b.role == Role::Lure ? fromTarget > 210.0f : fromTarget < 130.0f) return false;
                if (pullSide != 0.0f && (nd.feet.x - body->center().x) * pullSide < 0.0f) return false;  // 2.98: Plan::pullX
                // 2.84: the others a good way off its body (most of its blows are near it), at the far end of a wave's
                // reach; only a weak point bare is worth coming close.
                if (body && b.role != Role::Lure && !(view_.exposed && b.target != bossUid_)) {
                    const float ex = std::max({0.0f, bossBox.x - c.x, c.x - bossBox.right()});
                    const float ey = std::max({0.0f, bossBox.y - c.y, c.y - bossBox.bottom()});
                    const float keep = view_.plan.keepOff >= 0.0f ? view_.plan.keepOff : kKeepOff;
                    if (ex * ex + ey * ey < keep * keep) return false;
                }
                if (body && b.role == Role::Lure && view_.plan.lureOnFloor && nd.feet.y < bossBox.bottom() - 40.0f) return false;
                for (int o = 0; o < static_cast<int>(bots_.size()); ++o)
                    if (o != seat && bots_[static_cast<size_t>(o)].spot >= 0 && ((seats_ >> o) & 1u) &&
                        (nav->node(bots_[static_cast<size_t>(o)].spot).feet - nd.feet).lengthSq() < 52.0f * 52.0f)
                        return false;  // another's place
            }
            // 2.84: a weak point bare is struck from as close as its parts allow (the space under a skull, between two
            // legs); otherwise the box around them all is kept out of.
            const bool close = (view_.exposed || !siblings.empty()) && b.target != bossUid_;  // 2.87: a group finished together too
            // 2.84 (fight plan): bare, all in front of it (it turns toward the players: on both sides, its weak points
            // swap sides under their shots every second).
            if (close && view_.plan.frontOnly && body && (nd.feet.x - body->center().x) * body->facing < 0.0f) return false;
            if (body && (close ? touchesBody(bodyAt(nd.feet).expanded(clearance)) : bodyAt(nd.feet).intersects(bossBox.expanded(clearance)))) return false;
            // On its side of the boss's body (under its top): the way to the other side goes through it.
            // 2.84: a part of it between (at that height), not the box around them all.
            if (body && !farSide && nd.feet.y > bossBox.y && nd.feet.y - 22.0f < bossBox.bottom() &&
                (close ? touchesBody(Rect{std::min(me.center().x, nd.feet.x), nd.feet.y - 22.0f, std::fabs(nd.feet.x - me.center().x), 20.0f})
                       : (me.center().x < bossBox.center().x ? nd.feet.x > bossBox.x : nd.feet.x < bossBox.right())))
                return false;
            // From the ground, else from the top of its double jump (the target high over the floor: it jumps and
            // fires at the top, as a player does).
            int d = -1;
            bool high = false;
            const float apex = me.config().jumpHeight + me.config().doubleJumpHeight - 6.0f;
            for (int lift = 0; lift < 2 && d < 0; ++lift) {
                const Vec2 from = c - Vec2{0.0f, lift ? apex : 0.0f};
                for (int k : {0, 4, 1, 3, 2}) {
                    const Vec2 v = dirOf(k);
                    const Vec2 m = from + v * 10.0f + Vec2{0.0f, -2.0f};
                    bool crosses = false;
                    for (const Rect& r : siblings) crosses = crosses || Collision::segmentIntersectsRect(m, m + v * kWaveRange, r);
                    if (!crosses && Collision::segmentIntersectsRect(m, m + v * kWaveRange, target.expanded(5.0f)) && shotReaches(w, from, k, target)) {
                        d = k;
                        high = lift != 0;
                        break;
                    }
                }
            }
            if (d < 0 || !safeSpot(w, seat, nd.feet, pass == 0 ? 90 : 36)) return false;
            dir = d;
            jump = high;
            return true;
        };
        std::vector<int> path;
        const int found = nav->search(here, ok, 5000.0f, &path);
        if (found < 0) continue;
        b.spot = found;
        b.spotDir = dir;
        b.spotFor = tc;
        b.jumpShot = jump;
        b.goalNode = found;
        b.goal = nav->node(found).feet;
        // Its way there, as found (the steering follows it).
        if (!path.empty() && path.back() == found) {
            b.path = std::move(path);
            b.at = 0;
            b.planned = static_cast<int>(tick_);
            b.best = 1e30f;
            b.noProgress = 0;
        }
        return true;
    }
    b.spot = -1;
    return false;
}

// ----------------------------------------------------------------------- the step

void TestBots::think(RaidRun& run, InputFrame* frames, int count, uint32_t seats, bool adminActive) {
    GameWorld& w = run.world();
    const int n = std::min({kMaxInstancePlayers, count, w.teamSize()});
    seats_ = seats;
    const bool newZone = navZone_ != run.zoneSerial();
    sense(run);
    ensureNav(run);
    if ((tick_ + seed_ * 7u) % 15u == 0 || newZone || navi_.graphChanged()) plan(run, seats, adminActive);  // 2.84: the seed shifts its plans
    for (int s = 0; s < n; ++s) {
        Bot& b = bots_[static_cast<size_t>(s)];
        if (!((seats >> s) & 1u)) continue;
        if (b.task != Task::Mechanism) b.mechanismSteps = 0;
        else if (playing(w, s) && b.mechanism.intersects(w.playerAt(s).box)) b.mechanismSteps = 0;
        else ++b.mechanismSteps;
    }
    // The players of a mechanism struck together (two restraints within a second and a half): all
    // within reach of their targets, or none strikes (the targets without a sync group of the tactic).
    syncReady_ = true;
    for (int s = 0; s < n; ++s) {
        const Bot& b = bots_[static_cast<size_t>(s)];
        if (!((seats >> s) & 1u) || !b.sync || b.syncGroup > 0 || !playing(w, s)) continue;
        const Enemy* e = w.findEnemy(b.target);
        if (!e || !e->alive()) continue;
        const Vec2 d = e->center() - w.playerAt(s).center();
        if (std::fabs(d.x) > e->box.w * 0.5f + 22.0f || std::fabs(d.y) > e->box.h * 0.5f + 18.0f) syncReady_ = false;
    }
    for (int s = 0; s < n; ++s)
        if ((seats >> s) & 1u) {
            frames[s] = drive(run, s, bots_[static_cast<size_t>(s)]);
            // 2.84 (setSeed): its first key a little later than the others' (0 to 0,6 s, by seed and seat), as players.
            if (seed_ != 0 && tick_ < static_cast<uint32_t>(noise(s + 8 * static_cast<int>(seed_), 999) * 0.6f * static_cast<float>(kSimHz))) {
                frames[s] = InputFrame{};
                bots_[static_cast<size_t>(s)].prevHeld = 0;
                bots_[static_cast<size_t>(s)].jumpHold = 0;
            }
        }
    // An order to revive or a place marked ends once done (the soul up, the place reached by someone).
    if (order_.kind == bots::OrderKind::Revive) {
        bool any = false;
        for (int d = 0; d < w.teamSize(); ++d)
            any = any || (w.member(d).presence == GameWorld::Presence::Down && (w.member(d).soul - order_.at).lengthSq() < 160.0f * 160.0f);
        if (!any) order_ = {};
    } else if (order_.kind == bots::OrderKind::Mark && tick_ - order_.tick > 20u * kSimHz) {
        order_ = {};
    }
    ++tick_;
}

// ----------------------------------------------------------------------- the roles of the group (2.79)

void TestBots::assignRoles(const RaidRun& run, const std::vector<int>& drivers, bool fight) {
    const GameWorld& w = run.world();
    const int n = static_cast<int>(drivers.size());
    for (int s = 0; s < static_cast<int>(bots_.size()); ++s)
        if (std::find(drivers.begin(), drivers.end(), s) == drivers.end()) bots_[static_cast<size_t>(s)].role = Role::None;
    if (n == 0) return;
    int souls = 0;
    Vec2 soulAt;
    for (int d = 0; d < w.teamSize(); ++d)
        if (w.member(d).presence == GameWorld::Presence::Down) {
            if (souls == 0) soulAt = w.member(d).soul;
            ++souls;
        }
    const Enemy* boss = fight ? enemyOf(w, bossUid_) : nullptr;
    int stationNeed = 0;
    for (const bots::Station& s : view_.stations) stationNeed += s.now ? s.need : 0;
    const int wantReviver = n >= 3 ? (n >= 6 && souls >= 2 ? 2 : 1) : 0;
    const int wantLure = fight && boss && n >= 4 && !view_.plan.noLure ? 1 : 0;  // 2.84: a plan may want none
    const int wantRunner = fight ? std::clamp(stationNeed, n >= 5 ? 1 : 0, std::max(0, n - wantReviver - wantLure - (n >= 3 ? 1 : 0)))
                                 : (n >= 4 ? 1 : 0);
    std::vector<int> free = drivers;
    std::vector<Role> next(bots_.size(), Role::None);
    auto take = [&](Role r, int count, const std::function<float(int)>& cost) {
        for (int k = 0; k < count && !free.empty(); ++k) {
            // Whoever had it keeps it (no swapping each quarter of a second), else the cheapest.
            int pick = -1;
            for (int s : free)
                if (bots_[static_cast<size_t>(s)].role == r) {
                    pick = s;
                    break;
                }
            if (pick < 0) {
                float best = 1e30f;
                for (int s : free) {
                    const float c = cost(s);
                    if (c < best) {
                        best = c;
                        pick = s;
                    }
                }
            }
            if (pick < 0) break;
            next[static_cast<size_t>(pick)] = r;
            free.erase(std::find(free.begin(), free.end(), pick));
        }
    };
    const auto feetOf = [&](int s) { return w.playerAt(s).feet(); };
    // The reviver: who carries the decoy, else the nearest to the souls (to the group's middle).
    take(Role::Reviver, wantReviver, [&](int s) {
        float c = powerOf(w.playerAt(s), "decoy") ? 0.0f : 5000.0f;
        if (souls > 0) c += (feetOf(s) - soulAt).length();
        return c + static_cast<float>(s);
    });
    // The lure: the nearest to the boss (its blows follow the nearest player).
    take(Role::Lure, wantLure, [&](int s) { return boss ? (feetOf(s) - boss->center()).length() + static_cast<float>(s) : static_cast<float>(s); });
    // The runners: the nearest to the places to hold.
    take(Role::Runner, wantRunner, [&](int s) {
        float best = 1e30f;
        for (const bots::Station& st : view_.stations) best = std::min(best, (feetOf(s) - st.plate.bottomCenter()).length());
        return (best < 1e29f ? best : 0.0f) + static_cast<float>(s);
    });
    for (int s : free) next[static_cast<size_t>(s)] = Role::Striker;
    for (int s : drivers) bots_[static_cast<size_t>(s)].role = next[static_cast<size_t>(s)];
}

// ----------------------------------------------------------------------- the tasks of the group

void TestBots::plan(RaidRun& run, uint32_t seats, bool adminActive) {
    GameWorld& w = run.world();
    const NavGraph* nav = navi_.graph();
    const int n = std::min(kMaxInstancePlayers, w.teamSize());
    std::vector<int> drivers;
    std::vector<Rect> wasPlate(static_cast<size_t>(kMaxInstancePlayers));  // the place each one was sent to hold
    for (int s = 0; s < n; ++s)
        if ((seats >> s) & 1u) {
            Bot& b = bots_[static_cast<size_t>(s)];
            const Task was = b.task;
            if (was == Task::Mechanism) wasPlate[static_cast<size_t>(s)] = b.mechanism;
            const uint32_t wasTarget = b.target;
            b.task = Task::Follow;
            b.target = 0;
            b.act = None;
            b.thing.clear();
            b.sync = false;
            b.syncGroup = 0;
            b.holdAt = 0.0f;
            b.ranged = false;
            b.mechanism = {};
            b.mechanismClosed = false;
            b.revivee = 0;
            b.preAim = {};
            b.thought.order = false;
            if (wasTarget == 0) b.spot = -1;
            if (was == Task::Flee) b.task = Task::Flee;  // it finishes sheltering (drive)
            if (playing(w, s)) drivers.push_back(s);
        }
    if (!fight_ || view_.stations.empty())
        for (Bot& b : bots_) b.fixedStation = {};  // 2.84: no place to hold any more (its phase over)
    assignRoles(run, drivers, fight_);
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
        if (b.target != target) b.spot = -1;
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
    // Someone not played by the bots (the administrator) standing on a rectangle.
    auto heldByOthers = [&](const Rect& r) {
        int k = 0;
        for (int s = 0; s < n; ++s)
            if (!((seats >> s) & 1u) && playing(w, s) && r.intersects(w.playerAt(s).box)) ++k;
        return k;
    };
    const Enemy* boss = fight_ ? enemyOf(w, bossUid_) : nullptr;
    const std::string bossName = tactic_ && fight_ ? std::string(tactic_->name()) : std::string("le Gigaboss");
    // ------------------------------------------------------------------ the administrator's orders
    const bots::OrderKind ord = order_.kind;
    if (ord == bots::OrderKind::Follow || ord == bots::OrderKind::Wait) {
        const bool follow = ord == bots::OrderKind::Follow;
        const Vec2 at = follow && playing(w, 0) ? w.playerAt(0).feet() : order_.at;
        int k = 0;
        bool anyDown = false;
        for (int d = 0; d < w.teamSize(); ++d) anyDown = anyDown || w.member(d).presence == GameWorld::Presence::Down;
        for (int s : drivers) {
            if (anyDown && bots_[static_cast<size_t>(s)].role == Role::Reviver) continue;  // the reviver still relieves the fallen
            const float side = (k % 2) != 0 ? -1.0f : 1.0f;
            const Vec2 p = at + Vec2{side * (22.0f + 14.0f * static_cast<float>(k / 2)), 0.0f};
            give(s, follow ? Task::Follow : Task::Wait, p, Stand);
            Bot& b = bots_[static_cast<size_t>(s)];
            b.goalNode = nav ? nav->locate(p, 96.0f) : -1;
            b.thought.order = true;
            think(b, follow ? "Ordre : suit " + nameOf(0) : std::string("Ordre : attend ici"));
            ++k;
        }
        said.push_back(follow ? "Ordre : suivez-moi" : "Ordre : attendez ici");
    }
    // ------------------------------------------------------------------ the players down
    if (w.canRevive()) {
        std::vector<int> down;
        for (int d = 0; d < w.teamSize(); ++d)
            if (w.member(d).presence == GameWorld::Presence::Down) down.push_back(d);
        // 2.84 (fight plan): the players of the places to hold first (a mirror, a hook without its player stays empty).
        if (view_.plan.fixedHolders)
            std::stable_sort(down.begin(), down.end(), [&](int a, int c) {
                return (bots_[static_cast<size_t>(a)].fixedStation.w > 0.0f) > (bots_[static_cast<size_t>(c)].fixedStation.w > 0.0f);
            });
        // « Réanimez-le »: the soul nearest to the place given first, the two nearest players on it.
        if (ord == bots::OrderKind::Revive)
            std::stable_sort(down.begin(), down.end(), [&](int a, int c) {
                return (w.member(a).soul - order_.at).lengthSq() < (w.member(c).soul - order_.at).lengthSq();
            });
        bool first = true;
        for (int d : down) {
            const Vec2 soul = w.member(d).soul;
            // 2.84: a soul under a liquid that rises or is up (Voidworm's acid) waits for it to fall again: its reviver
            // would kneel in it.
            bool drowned = false;
            for (const bots::Danger& dg : dangers_)
                drowned = drowned || (dg.kind == bots::Danger::Kind::Flood && fight_ && soul.x > dg.area.x - 10.0f && soul.x < dg.area.right() + 10.0f &&
                                      soul.y > dg.area.y - 12.0f);
            if (drowned && ord != bots::OrderKind::Revive) continue;
            // 2.89: a soul where nobody can go (Voidworm's stomach): its reviver stood over it for the rest of the fight.
            bool lost = false;
            for (const Rect& r : view_.noRevive) lost = lost || (fight_ && r.expanded(4.0f).contains(soul));
            if (lost) continue;
            // 2.84 (fight plan): a liquid that comes back on a cycle (Voidworm's acid): a soul under its height only in the
            // first seconds after it has fallen (the time to kneel 2 s and climb back before the next one).
            if (fight_ && view_.plan.reviveWindow > 0.0f && view_.plan.elevate > 0.0f && soul.y > view_.plan.floorY - view_.plan.elevate &&
                static_cast<float>(tick_ - lastFlood_) > view_.plan.reviveWindow * static_cast<float>(kSimHz) && ord != bots::OrderKind::Revive)
                continue;
            // The reviver first; else (a small group, or more souls than revivers) the nearest free player.
            int s = nearestFree(soul, [&](int seat) { return bots_[static_cast<size_t>(seat)].role == Role::Reviver; });
            if (s < 0 && (!fight_ || drivers.size() <= 2 || ord == bots::OrderKind::Revive || freeCount() > 2)) s = nearestFree(soul);
            if (s < 0) break;
            give(s, Task::Revive, soul, Stand);
            Bot& b = bots_[static_cast<size_t>(s)];
            b.goalNode = nav ? nav->locate(soul, 96.0f) : -1;
            b.revivee = static_cast<uint32_t>(d + 1);
            b.thought.order = ord == bots::OrderKind::Revive && first;
            think(b, (b.thought.order ? "Ordre : réanime " : "réanime ") + nameOf(d));
            if (ord == bots::OrderKind::Revive && first) {
                // A second player on the same soul (the order asks for it now).
                if (const int s2 = nearestFree(soul); s2 >= 0) {
                    give(s2, Task::Revive, soul, Stand);
                    Bot& b2 = bots_[static_cast<size_t>(s2)];
                    b2.goalNode = b.goalNode;
                    b2.revivee = b.revivee;
                    b2.thought.order = true;
                    think(b2, "Ordre : réanime " + nameOf(d));
                }
            }
            first = false;
            said.push_back("Réanimer");
        }
    }
    // ------------------------------------------------------------------ hurt: a health orb near, out of what is coming
    // 2.98 (Plan::orbsInReach): the place of the graph under an orb whence a single jump takes it (in a boss fight its double
    // jump is never pressed for an orb: one higher was waited under for good), -1 none.
    const auto orbNode = [&](const LevelEntity& e, const Player& p) {
        if (!nav) return -1;
        const float lift = p.config().jumpHeight - 6.0f;
        return nav->nearest(e.box.bottomCenter(), 160.0f, [&](const NavGraph::Node& nd) {
            return nd.mover < 0 && !nd.ladder && std::fabs(nd.feet.x - e.box.center().x) < 10.0f && e.box.bottom() <= nd.feet.y + 2.0f &&
                   e.box.bottom() > nd.feet.y - 22.0f - lift;
        });
    };
    for (int s : drivers) {
        if (busy[static_cast<size_t>(s)] || ord == bots::OrderKind::Follow || ord == bots::OrderKind::Wait) continue;
        const Player& p = w.playerAt(s);
        if (p.health * 10 > p.maxHealth() * (fight_ ? 6 : 4)) continue;  // 2.84: in a fight, an orb (4 hearts) as soon as it lacks them
        if (bots_[static_cast<size_t>(s)].role == Role::Runner && !view_.stations.empty()) continue;  // its plate first
        if (fight_ && view_.exposed) continue;  // 2.84: its few seconds bare are for striking
        const LevelEntity* orb = nullptr;
        float bd = 700.0f * 700.0f;
        for (const auto& e : w.entities())
            if (!e->removed && e->active && e->visible && e->type() == "health_orb" && (e->box.center() - p.center()).lengthSq() < bd &&
                safeSpot(w, s, e->box.bottomCenter(), 60) && (!fight_ || !view_.plan.orbsInReach || orbNode(*e, p) >= 0) &&
                !(view_.plan.elevate > 0.0f && e->box.bottom() > view_.plan.floorY - view_.plan.elevate)) {  // 2.84: not down under its waves, its acid
                bd = (e->box.center() - p.center()).lengthSq();
                orb = e.get();
            }
        if (!orb) continue;
        give(s, Task::Objective, orb->box.bottomCenter(), Enter, 0, orb->id());
        bots_[static_cast<size_t>(s)].goalNode = standNode(orb->box.expanded(4.0f));
        if (fight_ && view_.plan.orbsInReach)
            bots_[static_cast<size_t>(s)].goalNode = orbNode(*orb, p);  // 2.98: the place a single jump takes it from
        think(bots_[static_cast<size_t>(s)], std::string("va prendre un orbe de vie"));
    }
    // ------------------------------------------------------------------ a place marked
    if (ord == bots::OrderKind::Mark && freeCount() > 0) {
        const Vec2 at = order_.at;
        // What is there: an enemy to strike, a plate or a vent to hold, a lever or a generator, else the place itself.
        uint32_t foe = 0;
        float fd = 90.0f * 90.0f;
        for (const auto& e : w.enemies())
            if (e->alive() && !e->dormant() && (e->center() - at).lengthSq() < fd && !(boss && e->uid() == boss->uid() && !view_.bodyTakesDamage)) {
                fd = (e->center() - at).lengthSq();
                foe = e->uid();
            }
        Rect plate;
        for (const bots::Station& st : view_.stations)
            if (st.plate.expanded(40.0f).contains(at)) plate = st.plate;
        for (const CoopPlate& c : run.rules().coops)
            if (c.rect.expanded(40.0f).contains(at)) plate = c.rect;
        LevelEntity* thing = nullptr;
        for (const auto& e : w.entities())
            if (!e->removed && e->box.expanded(40.0f).contains(at) && (dynamic_cast<ReceptorEntity*>(e.get()) || e->type() == "lever" || e->type() == "switch"))
                thing = e.get();
        for (int k = 0; k < 2; ++k) {
            const int s = nearestFree(at);
            if (s < 0) break;
            Bot& b = bots_[static_cast<size_t>(s)];
            if (foe) {
                const Enemy* e = w.findEnemy(foe);
                give(s, Task::Fight, e->box.bottomCenter(), Strike, foe);
                b.ranged = true;
                think(b, "Signal : frappe " + e->def().name);
            } else if (plate.w > 0.0f) {
                give(s, Task::Mechanism, {plate.center().x, plate.bottom()}, Stand);
                b.mechanism = plate;
                b.goalNode = standNode(plate);
                think(b, std::string("Signal : tient la plaque"));
            } else if (thing) {
                const bool shoot = dynamic_cast<ReceptorEntity*>(thing) != nullptr;
                give(s, Task::Objective, thing->box.bottomCenter(), shoot ? Shoot : Interact, 0, thing->id());
                b.goalNode = standNode(thing->box.expanded(shoot ? 24.0f : 4.0f));
                think(b, std::string(shoot ? "Signal : active le générateur" : "Signal : actionne le levier"));
            } else {
                give(s, Task::Wait, at, Stand);
                b.goalNode = nav ? nav->locate(at, 120.0f) : -1;
                think(b, std::string("Signal : va voir"));
            }
            b.thought.order = true;
        }
        said.push_back("Signal");
    }
    // ------------------------------------------------------------------ a gigaboss's fight (its tactic)
    const bool allIn = ord == bots::OrderKind::Objective;
    if (fight_ && boss) {
        // Its places to hold: the runners first (all hands on an order « Tous sur l'objectif »), then the nearest.
        std::vector<const bots::Station*> stations;
        for (const bots::Station& st : view_.stations)
            if (st.now) stations.push_back(&st);
        std::stable_sort(stations.begin(), stations.end(), [](const auto* a, const auto* c) { return a->priority > c->priority; });
        int held = 0;
        // 2.84: who stands on a place keeps it, whatever comes first (a player on a closed vent was sent to an open one
        // across the arena, and the closed one to a player who could not reach it: none was ever held again).
        const auto standsOn = [&](int seat) -> const bots::Station* {
            for (const bots::Station* st : stations)
                if (st->plate.expanded(12.0f).intersects(w.playerAt(seat).box)) return st;
            return nullptr;
        };
        const auto other = [&](int seat, const bots::Station* st) {
            const bots::Station* on = standsOn(seat);
            return on && on != st && !on->plate.intersects(st->plate);
        };
        for (const bots::Station* st : stations) {
            int need = std::max(0, st->need - heldByOthers(st->plate));
            while (need-- > 0) {
                int s = nearestFree(st->plate.bottomCenter(), [&](int seat) { return standsOn(seat) == st; });
                // 2.84 (fight plan): its own player for the whole phase, wherever it is (never handed to another while it lives).
                if (s < 0 && view_.plan.fixedHolders)
                    s = nearestFree(st->plate.bottomCenter(), [&](int seat) { return bots_[static_cast<size_t>(seat)].fixedStation.intersects(st->plate); });
                // The one already on its way there keeps it (a place given again to another each plan is never reached).
                if (s < 0)
                    s = nearestFree(st->plate.bottomCenter(), [&](int seat) {
                        return wasPlate[static_cast<size_t>(seat)].intersects(st->plate) && bots_[static_cast<size_t>(seat)].role != Role::Reviver;
                    });
                if (s < 0)
                    s = nearestFree(st->plate.bottomCenter(), [&](int seat) { return bots_[static_cast<size_t>(seat)].role == Role::Runner && !other(seat, st); });
                if (s < 0)
                    s = nearestFree(st->plate.bottomCenter(), [&](int seat) { return bots_[static_cast<size_t>(seat)].role != Role::Reviver && !other(seat, st); });
                if (s < 0) s = nearestFree(st->plate.bottomCenter(), [&](int seat) { return !other(seat, st); });
                if (s < 0) break;
                give(s, Task::Mechanism, {st->plate.center().x, st->plate.bottom()}, Stand);
                Bot& b = bots_[static_cast<size_t>(s)];
                b.mechanism = st->plate;
                if (view_.plan.fixedHolders) {
                    // Its place from now on (a player who owned another gives it up only for this one).
                    bool taken = false;
                    for (int q = 0; q < w.teamSize(); ++q)
                        taken = taken || (q != s && w.member(q).presence != GameWorld::Presence::Gone && bots_[static_cast<size_t>(q)].fixedStation.intersects(st->plate));
                    if (!taken) b.fixedStation = st->plate;
                }
                b.mechanismClosed = st->closed && view_.plan.freeOnClosed;
                b.goalNode = standNode(st->plate);
                think(b, "tient " + std::string(st->label.empty() ? "la plaque" : st->label));
                ++held;
            }
        }
        if (held > 0) said.push_back(std::to_string(held) + " sur les plaques");
        // Its targets, the highest priority first: a rescue (a claw, a stomach), the targets of its mechanism
        // (finished together), its weak points bare; the strikers first, the lure last (it keeps the boss).
        std::vector<const bots::Target*> targets;
        for (const bots::Target& t : view_.targets) targets.push_back(&t);
        std::stable_sort(targets.begin(), targets.end(), [](const auto* a, const auto* c) { return a->priority > c->priority; });
        for (const bots::Target* t : targets) {
            if (t->priority < 10) continue;  // its body and its creatures: below
            const Enemy* e = enemyOf(w, t->uid);
            if (!e) continue;
            for (int k = 0; k < t->want; ++k) {
                // 2.87: never a weak point it found no place to fire at a moment ago (below).
                const auto reaches = [&](int seat) {
                    const Bot& o = bots_[static_cast<size_t>(seat)];
                    return !t->weak || o.noSpotFor != t->uid || static_cast<int>(tick_) >= o.noSpotUntil;
                };
                int s = nearestFree(e->center(), [&](int seat) { return bots_[static_cast<size_t>(seat)].role == Role::Striker && reaches(seat); });
                if (s < 0 && (t->priority >= 20 || allIn))
                    s = nearestFree(e->center(), [&](int seat) { return (bots_[static_cast<size_t>(seat)].role != Role::Reviver || allIn) && reaches(seat); });
                if (s < 0) break;
                give(s, Task::Fight, e->box.bottomCenter(), Strike, t->uid);
                Bot& b = bots_[static_cast<size_t>(s)];
                b.ranged = !t->melee || shooter(w.playerAt(s)) != nullptr;
                b.syncGroup = t->sync;
                b.holdAt = t->holdAt;
                b.sync = t->sync > 0;
                const std::string what = t->weak ? "le point faible (× 2)" : t->label;
                think(b, (t->sync > 0 ? "frappe " + what + " (ensemble)" : t->priority >= 30 ? "libère : frappe " + what : "frappe " + what));
            }
        }
        // Its body (or its creatures): the lure in front (its attention), then everyone left.
        const bots::Target* body = nullptr;
        for (const bots::Target* t : targets)
            if (t->uid == boss->uid()) body = t;
        for (int pass = 0; pass < 2; ++pass)
            for (int s : drivers) {
                if (busy[static_cast<size_t>(s)]) continue;
                Bot& b = bots_[static_cast<size_t>(s)];
                if (pass == 0 && b.role != Role::Lure) continue;
                if (b.role == Role::Reviver && pass == 1 && !allIn) {
                    // The reviver stays near the group, out of the blows, ready (it strikes from there).
                    give(s, Task::Fight, boss->box.bottomCenter(), Strike, boss->uid());
                    b.ranged = true;
                    think(b, "se tient prêt à réanimer");
                    continue;
                }
                uint32_t target = body ? boss->uid() : 0;
                std::string what = bossName;
                // Its creatures while its body is closed to blows, the nearest first.
                if (!body || b.role == Role::Striker) {
                    float bd = 1e30f;
                    for (const bots::Target* t : targets) {
                        if (t->priority >= 10 || t->uid == boss->uid()) continue;
                        const Enemy* e = enemyOf(w, t->uid);
                        if (!e) continue;
                        float d2 = (e->center() - w.playerAt(s).center()).lengthSq();
                        // 2.84: its places held (its carapace), the creatures closest to those holding them first, wherever
                        // they are (its parasites bite the players on its hooks: nobody else was near them).
                        bool guard = false;
                        if (!view_.bodyTakesDamage)
                            for (const bots::Station& st : view_.stations)
                                if ((e->center() - st.plate.center()).lengthSq() < 320.0f * 320.0f) {
                                    guard = true;
                                    d2 = std::min(d2, (e->center() - st.plate.center()).lengthSq() * 0.25f);
                                }
                        if (d2 < bd && (d2 < 360.0f * 360.0f || !body || guard)) {
                            bd = d2;
                            target = t->uid;
                            what = e->def().name;
                        }
                    }
                }
                if (target == 0) target = boss->uid();  // nothing to strike: it keeps near, out of its way
                give(s, Task::Fight, boss->box.bottomCenter(), Strike, target);
                b.ranged = true;
                if (b.role == Role::Lure) think(b, view_.bodyTakesDamage ? "attire " + bossName : "attire " + bossName + " loin des plaques");
                else if (!view_.bodyTakesDamage && target == boss->uid()) {
                    // 2.84: already where its weak points will be in reach once it is bare (the spots of its rules,
                    // as its sheet shows them): its few seconds bare are spent striking, not walking.
                    const GigabossRules* gr = run.gigaboss().rules();
                    if (gr && !gr->weakSpots.empty() && b.role != Role::Lure) {
                        const size_t k = static_cast<size_t>(s) % gr->weakSpots.size();
                        const Vec2 at = spotFeet(boss->box, boss->facing, gr->weakSpots[k]);
                        Vec2 size{40.0f, 40.0f};
                        if (const EnemyDefinition* wd = w.content().enemy(gr->weakEnemyAt(k))) size = wd->size * wd->scale;
                        b.preAim = Rect::fromBottomCenter(at, size);
                    }
                    think(b, std::string("garde son énergie pour les points faibles"));
                }
                else think(b, (view_.exposed && target == boss->uid() ? "frappe " + what + " à nu (× 2)" : "frappe " + what));
            }
        said.push_back(view_.phase.empty() ? "Combat de " + bossName : view_.phase);
    }
    // ------------------------------------------------------------------ the armory's tablets
    if (Branch::isArmory(run.zone()))  // 2.78: the lair's armory too
        for (int s : drivers) {
            if (busy[static_cast<size_t>(s)]) continue;
            const std::string want = wishOf(s);
            if (want.empty() || w.playerAt(s).hasPower(want)) continue;
            for (const auto& e : w.entities())
                if (auto* t = dynamic_cast<const PowerUnlockEntity*>(e.get()); t && !e->removed && t->power() == want) {
                    give(s, Task::Tablet, e->box.bottomCenter(), Enter, 0, e->id());
                    bots_[static_cast<size_t>(s)].goalNode = standNode(e->box);
                    think(bots_[static_cast<size_t>(s)], "prend son pouvoir : " + want);
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
            think(bots_[static_cast<size_t>(s)], "embuscade : frappe " + best->def().name);
        }
        said.push_back("Embuscade");
    }
    // ------------------------------------------------------------------ the plates held together
    bool platesLeft = false;
    if (!fight_)
        for (size_t k = 0; k < run.rules().coops.size(); ++k) {
            if (run.coop(k).done) continue;
            platesLeft = true;
            const CoopPlate& c = run.rules().coops[k];
            std::vector<size_t> group{k};
            if (!c.group.empty())
                for (size_t j = 0; j < run.rules().coops.size(); ++j)
                    if (j != k && run.rules().coops[j].group == c.group) group.push_back(j);
            // §40: the guards of the plates (an enemy awake near them, out of the ambushes: the Poignet-verrou's drone over its
            // upper twin plate, its stalker below): a spare player, never one the plates need, beats the nearest first. Their
            // blows made the one sent up wait at their edge for ever, and the twin plates never held together.
            if (!run.coop(k).ambush) {
                int needAll = 0;
                Rect around = run.rules().coops[k].rect;
                for (size_t g : group) {
                    needAll += std::max(0, run.coop(g).need - heldByOthers(run.rules().coops[g].rect));
                    const Rect r = run.rules().coops[g].rect;
                    const float x0 = std::min(around.x, r.x), y0 = std::min(around.y, r.y);
                    around = {x0, y0, std::max(around.right(), r.right()) - x0, std::max(around.bottom(), r.bottom()) - y0};
                }
                around = around.expanded(320.0f);
                const Enemy* guard = nullptr;
                float gd = 1e30f;
                for (const auto& e : w.enemies()) {
                    if (!e->alive() || e->dormant() || !around.intersects(e->box)) continue;
                    const float d = (e->center() - run.rules().coops[k].rect.center()).lengthSq();
                    if (d < gd) {
                        gd = d;
                        guard = e.get();
                    }
                }
                if (guard && freeCount() > needAll) {
                    const int s = nearestFree(guard->box.bottomCenter());
                    if (s >= 0) {
                        give(s, Task::Fight, guard->box.bottomCenter(), Strike, guard->uid());
                        think(bots_[static_cast<size_t>(s)], "dégage " + c.label + " : frappe " + guard->def().name);
                        said.push_back("Dégager " + shortLabel(c.label));
                    }
                }
            }
            int sent = 0;
            for (size_t g : group) {
                const RaidRun::CoopView v = run.coop(g);
                const Rect r = run.rules().coops[g].rect;
                int need = std::max(0, v.need - heldByOthers(r));
                // §40: those who can get there (the graph, with their powers) first: the nearest one sent to the upper twin
                // plate had no way up with its powers and stood under it for ever, while another who had one waited aside.
                const int plateNode = nav ? nav->nearest(r.bottomCenter(), std::max(40.0f, r.w), [&](const NavGraph::Node& nd) {
                    return r.contains({nd.feet.x, nd.feet.y - 4.0f}) && (nd.mover < 0 || (nav->moverFeet(w, nd.mover) - nd.feet).lengthSq() < 24.0f * 24.0f);
                })
                                          : -1;
                std::vector<int8_t> reach(static_cast<size_t>(kMaxInstancePlayers), -1);
                auto canReach = [&](int seat) {
                    int8_t& known = reach[static_cast<size_t>(seat)];
                    if (known < 0) known = plateNode < 0 || r.intersects(w.playerAt(seat).box) || navi_.reaches(w, seat, plateNode) ? 1 : 0;
                    return known == 1;
                };
                for (int slot = 0; need > 0; ++slot) {
                    // §40: who was going to this plate keeps it (each plan gave the plate to the nearest: two players
                    // between the twin plates swapped them four times a second and neither ever got up to the upper one).
                    int s = nearestFree(r.bottomCenter(), [&](int seat) { return wasPlate[static_cast<size_t>(seat)] == r && canReach(seat); });
                    if (s < 0) s = nearestFree(r.bottomCenter(), [&](int seat) { return bots_[static_cast<size_t>(seat)].role == Role::Runner && canReach(seat); });
                    if (s < 0) s = nearestFree(r.bottomCenter(), canReach);
                    if (s < 0) s = nearestFree(r.bottomCenter());  // nobody can: the nearest, as before
                    if (s < 0) break;
                    // Spread over the plate (never all on one pixel), feet on its floor.
                    const float span = std::max(0.0f, r.w - 16.0f);
                    const float off = span <= 0.0f ? 0.0f : std::fmod(static_cast<float>(slot) * 13.0f + 6.0f, span) - span * 0.5f;
                    give(s, Task::Mechanism, {r.center().x + off, r.bottom()}, Stand);
                    Bot& b = bots_[static_cast<size_t>(s)];
                    b.mechanism = r;
                    // 2.78: the plate of a passage of the group (« Prêts »): held, it takes them all away together.
                    for (const Passage& pa : run.rules().passages)
                        if (pa.plate == run.rules().coops[g].id) b.thing = kGroupPlate;
                    b.goalNode = nav ? nav->nearest({r.center().x + off, r.bottom()}, std::max(40.0f, r.w),
                                                    [&](const NavGraph::Node& nd) { return nd.mover < 0 && r.contains({nd.feet.x, nd.feet.y - 4.0f}); })
                                     : -1;
                    // §40: a plate on a goods lift at rest (« Monte-charge »): its only places are on the lift itself (a
                    // moving platform's), never the floor beside it (Poignet-verrou) nor the pit under it (Cage du cœur), where
                    // the nearest fixed place sent them: they stood there for ever and the lift never started.
                    if (b.goalNode < 0 && nav)
                        b.goalNode = nav->nearest({r.center().x + off, r.bottom()}, std::max(40.0f, r.w), [&](const NavGraph::Node& nd) {
                            return nd.mover >= 0 && r.contains({nd.feet.x, nd.feet.y - 4.0f}) && (nav->moverFeet(w, nd.mover) - nd.feet).lengthSq() < 24.0f * 24.0f;
                        });
                    if (b.goalNode < 0) b.goalNode = standNode(r);
                    think(b, "tient " + c.label);
                    --need;
                    ++sent;
                }
            }
            // The group gathers there (the passage needs several players: the others wait beside it).
            for (int s : drivers) {
                if (busy[static_cast<size_t>(s)] || freeCount() <= 0) continue;
                const Rect r = c.rect;
                const float side = (s % 2) != 0 ? -1.0f : 1.0f;
                const Vec2 at{r.center().x + side * (r.w * 0.5f + 20.0f + 10.0f * static_cast<float>(s / 2)), r.bottom()};
                if ((w.playerAt(s).feet() - at).lengthSq() > 900.0f * 900.0f) continue;  // too far: its own task
                give(s, Task::Wait, at, Stand);
                bots_[static_cast<size_t>(s)].goalNode = nav ? nav->locate(at, 96.0f) : -1;
                think(bots_[static_cast<size_t>(s)], "se regroupe à " + c.label);
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
        const std::string label = info.label;  // whole (its translation); shortened for the plan's line
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
            // A generator needs a shot (the wave or the boomerang): only those who carry one; the runner first.
            auto canDo = [&](int seat) { return j.act != Shoot || shooter(w.playerAt(seat)) != nullptr; };
            int s = nearestFree(j.at, [&](int seat) { return canDo(seat) && bots_[static_cast<size_t>(seat)].role == Role::Runner; });
            if (s < 0) s = nearestFree(j.at, canDo);
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
                think(b, "active " + j.label);
            } else {
                b.goalNode = standNode(j.area.expanded(4.0f));
                think(b, (j.act == Interact ? "actionne " : "va chercher ") + j.label);
            }
            ++sent;
        }
        if (sent > 0) said.push_back(shortLabel(j.label) + " : " + std::to_string(sent));
    }
    for (size_t k = 0; !heavy.empty() && freeCount() > 0; ++k) {
        const Job& j = heavy[k % heavy.size()];
        const int s = nearestFree(j.at);
        if (s < 0) break;
        give(s, j.act == Strike ? Task::Fight : Task::Objective, j.at, j.act, j.target, j.thing);
        Bot& b = bots_[static_cast<size_t>(s)];
        b.goalNode = nav ? (j.act == Strike ? nav->locate(j.at, 120.0f) : standNode(j.area.expanded(-16.0f))) : -1;
        think(b, j.act == Strike ? "combat : " + j.label : "nettoie : " + j.label);
        if (k < heavy.size()) said.push_back(shortLabel(j.label));
    }
    // ------------------------------------------------------------------ the way out
    // 2.78 (each player changes zone alone): only once every one of them here is free, and together — they
    // gather by the passage, then all go in (never one left alone in a zone).
    if (!fight_ && !platesLeft && !jobs && freeCount() > 0 && freeCount() == static_cast<int>(drivers.size())) {
        int to = 0;
        const int exit = chooseExit(run, &to);
        if (exit >= 0) {
            const Passage& p = run.rules().passages[static_cast<size_t>(exit)];
            bool gathered = true;
            for (int s : drivers) {
                const Vec2 f = w.playerAt(s).feet();
                if (std::fabs(f.x - p.rect.center().x) > 240.0f || std::fabs(f.y - p.rect.bottom()) > 160.0f) gathered = false;
            }
            // 2.85: one of them unable to come (stuck in a pit, behind a wall): after 30 s the others go on (it follows).
            const uint64_t waitKey = (static_cast<uint64_t>(run.zoneSerial()) << 16) | static_cast<uint64_t>(exit + 1);
            if (waitKey != exitWaitKey_) {
                exitWaitKey_ = waitKey;
                exitWaitSince_ = tick_;
            }
            if (!gathered && tick_ - exitWaitSince_ > 30u * static_cast<uint32_t>(kSimHz)) gathered = true;
            // The administrator there and playing: they lead the way out (the group waits near it);
            // away or AFK: the simulated players go on alone (all of them at once).
            const bool wait = (adminActive && !((seats >> 0) & 1u) && playing(w, 0)) || !gathered;
            const ZoneInfo* z = run.branch().zone(to);
            const std::string where = z ? z->name : "?";
            said.push_back((wait ? "Attend à la sortie vers " : "Sortie vers ") + where);
            for (int s : drivers) {
                if (busy[static_cast<size_t>(s)]) continue;
                give(s, Task::Exit, p.rect.bottomCenter(), wait ? Stand : Enter);
                Bot& b = bots_[static_cast<size_t>(s)];
                think(b, (wait ? "attend à la sortie vers " : "va vers ") + where);
                if (!nav) continue;
                if (wait)
                    b.goalNode = nav->nearest(p.rect.center(), 220.0f, [&](const NavGraph::Node& nd) {
                        const float d = std::fabs(nd.feet.x - p.rect.center().x);
                        return nd.mover < 0 && d >= 40.0f + 8.0f * static_cast<float>(s) && !p.rect.expanded(8.0f).contains({nd.feet.x, nd.feet.y - 8.0f});
                    });
                else b.goalNode = standNode(p.rect);
            }
        } else exitWaitKey_ = 0;
    } else exitWaitKey_ = 0;
    // ------------------------------------------------------------------ the others: help, or keep together
    for (int s : drivers) {
        if (busy[static_cast<size_t>(s)]) continue;
        Bot& b = bots_[static_cast<size_t>(s)];
        if (!heavy.empty()) {
            give(s, heavy.front().act == Strike ? Task::Fight : Task::Objective, heavy.front().at, heavy.front().act, heavy.front().target);
            b.goalNode = nav ? nav->locate(heavy.front().at, 160.0f) : -1;
            think(b, "aide : " + heavy.front().label);
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
        think(b, mate >= 0 ? "reste avec " + nameOf(mate) : std::string("attend"));
    }
    std::sort(said.begin(), said.end());
    said.erase(std::unique(said.begin(), said.end()), said.end());
    plan_.clear();
    for (const std::string& t : said) plan_ += (plan_.empty() ? "" : " · ") + t;
}

// ----------------------------------------------------------------------- one seat's frame

void TestBots::steer(const GameWorld& w, int seat, Bot& b, InputFrame& f, uint32_t& held, int& dir, float& stop) {
    // 2.79: the navigation (game/raid/nav/Pilot): its path on the graph, each move through the air chosen by
    // simulating the player's own physics ahead (the moving platforms where they will be, the dangers on
    // the way), played with the inputs of a player only.
    nav::SeatPlan plan;
    plan.goal = b.goal;
    plan.goalNode = b.goalNode;
    plan.stop = stop;
    plan.flee = b.task == Task::Flee;
    plan.hold = b.task == Task::Revive || b.task == Task::Mechanism;
    plan.mayLeave = (b.task == Task::Exit && b.act == Enter) || (b.task == Task::Mechanism && b.thing == kGroupPlate);
    // 2.84: in a fight its energy is for its shots and its shield (a dash costs its own and stops its regeneration for
    // 0,7 s): moves without one when it can (more than a tenth of its energy went into dashes on the way).
    plan.spare = fight_ && w.playerAt(seat).energy < 120.0f && b.task == Task::Fight;  // a place to reach or a soul: every move
    // 2.90 (docs/RAIDS.md §36, Plan::escape): its blows left by the navigation alone, with their time; a dash weighed as a way
    // out (5 of energy), more when it would leave too little for the shield ready; under its shield (or the guard after a
    // dash) a blow is nothing to leave.
    if (fight_ && view_.plan.escape) {
        const Player& me = w.playerAt(seat);
        const Power* sp = powerOf(me, "shield");
        plan.escape = true;
        plan.escapeDash = 30.0f + (sp && sp->ready(me) && me.energy < 30.0f ? 80.0f : 0.0f);
        // What is left of its shield (its cooldown runs from when it was raised) and of the guard after its dash.
        if (sp && me.shieldActive())
            plan.guardFor = std::max(0.0f, sp->def().duration - (sp->def().cooldown - sp->cooldownRemaining()) - 0.03f);
        if (const Power* dp = powerOf(me, "dash"); dp && me.blowGuarded())
            plan.guardFor = std::max(plan.guardFor, dp->def().param("duration", 0.16f) + kDashBlowGuardSeconds - (dp->def().cooldown - dp->cooldownRemaining()) - 0.02f);
        // Only on its place or kneeling by its soul does it stay under what comes (its shield): on its way there, it gets out.
        plan.hold = (b.task == Task::Mechanism && b.mechanism.w > 0.0f && b.mechanism.intersects(me.box) && !b.mechanismClosed) ||
                    (b.task == Task::Revive && std::fabs(b.goal.x - me.feet().x) <= GameWorld::kReviveReachX - 6.0f &&
                     std::fabs(b.goal.y - me.feet().y) <= GameWorld::kReviveReachY - 6.0f);
    }
    plan.fleeBody = b.fleeBody;
    b.fleeBody = false;
    plan.path = &b.path;
    plan.at = &b.at;
    plan.planned = &b.planned;
    plan.best = &b.best;
    plan.noProgress = &b.noProgress;
    const nav::Pad pad = navi_.steer(w, seat, plan, b.prevHeld);
    held |= pad.held & ~((1u << kLeft) | (1u << kRight));
    dir = pad.mx > 0 ? 1 : pad.mx < 0 ? -1 : 0;
    if (pad.my != 0) f.moveY = pad.my;
    // A move through the air under way is the navigation's own (no jump of the tactic's over it).
    // 2.84: except its jump over a wave running at it (a move along the ground gives way to it).
    // 2.87: and once it is off the ground, held to its top (released by the navigation the step after, the jump was cut
    // short — a hop under the wave).
    // 2.90: nor during a dodge of the escape (its track was simulated without that jump), but for a wave on its floor.
    if ((navi_.pilot(seat).committed() || (plan.escape && navi_.pilot(seat).dodging())) && b.waveJumpCool <= 0 && b.beamJump <= 0) b.jumpHold = 0;
}

InputFrame TestBots::drive(RaidRun& run, int seat, Bot& b) {
    InputFrame f;
    GameWorld& w = run.world();
    if (!playing(w, seat)) {
        b.prevHeld = 0;
        b.jumpHold = 0;
        b.beamJump = 0;
        b.path.clear();
        b.spot = -1;
        if (seat < w.teamSize() && w.member(seat).presence == GameWorld::Presence::Down) {
            b.thought.role = b.role;
            b.thought.intent = "à terre : attend d'être réanimé";
            b.thought.move.clear();
        }
        return f;  // down (its soul waits), or not in play
    }
    const Player& me = w.playerAt(seat);
    const Vec2 feet = me.feet();
    const Level lv = levelOf(skill_);
    const NavGraph* nav = navi_.graph();
    float stop = b.task == Task::Follow ? 14.0f : 6.0f;
    uint32_t held = 0;
    std::string react;  // what it does about what is coming this step (its thought)
    if (b.shieldCool > 0) --b.shieldCool;
    if (b.waveJumpCool > 0) --b.waveJumpCool;
    if (b.beamJump > 0) --b.beamJump;
    if (b.hop && me.grounded() && b.jumpHold <= 0) b.hop = 0;
    // 2.84: held back at the edge of what is coming (counted while it waits there; a step away from it forgets it).
    if (b.moveText != "attend que ça passe" && b.moveText != "s'écarte") b.blocked = 0;
    const Power* shield = powerOf(me, "shield");
    const bool shieldReady = shield && keyBit(shield) >= 0 && shield->ready(me) && !(b.prevHeld & (1u << keyBit(shield))) && !me.shieldActive();
    bool raiseShield = false;
    // 2.90 (docs/RAIDS.md §36, Plan::escape): the blows on an area are left by the navigation alone (nav::Pilot::escape: a place
    // clear of them when they land, reached in time — a few steps, a drop, a jump, a dash whose guard covers the blow); the
    // tactic keeps its shield for when no way out is clear, and never turns it the other way meanwhile.
    const bool escapeMode = fight_ && view_.plan.escape;
    const Danger* escHit = nullptr;  // the blow on it the navigation gets it out of (its shield if it cannot)
    // 2.84: a place closed for now (its vent shut) is kept, not held: its player dodges and jumps around it.
    const bool holding = b.task == Task::Mechanism && b.mechanism.w > 0.0f && b.mechanism.intersects(me.box) && !b.mechanismClosed;
    // 2.86: when to jump over a beam about to fire (steps before it fires; -1: no jump rises over it): high enough over the line
    // through its middle for its slope across its body (its own jump's rise), from then on as long as the beam fires.
    const auto beamJumpAt = [&](const Danger& d) {
        const Vec2 ab = d.to - d.from;
        const float slope = std::fabs(ab.y) / std::max(1.0f, std::fabs(ab.x));
        const float h = me.box.h * 0.5f + d.width * 0.5f + (me.box.w * 0.5f + d.width * 0.5f) * slope + 6.0f;
        const float v = me.config().jumpVelocity(), g = me.config().gravity();
        const float disc = v * v - 2.0f * g * h;
        if (disc < 0.0f || g <= 0.0f) return -1;
        return static_cast<int>(std::ceil((v - std::sqrt(disc)) / g * static_cast<float>(kSimHz)));
    };
    // Sent to a place to hold: what strikes that place (its own jet, a mark on it) is taken under the shield, never a
    // reason to leave it or to wait beside it — a vent or a hook left while its jet comes is never closed.
    const Rect station = b.task == Task::Mechanism && b.mechanism.w > 0.0f && !b.mechanismClosed ? b.mechanism.expanded(10.0f) : Rect{};
    b.onStation = holding;
    // ------------------------------------------------------------ rising liquids: above their top
    bool fleeing = false;
    Rect flood;
    for (const Danger& d : dangers_)
        if (d.kind == Danger::Kind::Flood && (flood.w <= 0.0f || d.area.w > flood.w) && d.area.x <= feet.x + 6.0f && d.area.right() >= feet.x - 6.0f) flood = d.area;
    // 2.93 (the escape): a wide blow along the floor is no liquid to climb above (Titanus's stomp, its band read as a flood: they
    // climbed onto the ledges across its leg, every other blow ignored on the way) — the navigation leaves it with its time.
    if (flood.w <= 0.0f && !escapeMode)
        for (const Danger& d : dangers_)
            if (d.kind == Danger::Kind::Area && d.area.w > 260.0f && d.in < kSimHz && d.area.expanded(6.0f).intersects(me.box) &&
                (!fight_ || d.area.h < 120.0f) && (flood.w <= 0.0f || d.area.w > flood.w) && !d.ceiling)  // 2.87: not Graviton's vault
                flood = d.area;
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
            react = "monte au-dessus du liquide";
        }
    } else if (b.task == Task::Flee) {
        b.task = Task::Follow;
        b.fleeNode = -1;
    }
    // ------------------------------------------------------------ 2.84: a storey about to be swept: onto another one
    if (!fleeing && !holding && nav && fight_) {  // a reviver too: it comes back once the blade has passed
        bool onSwept = false;
        int in = 1 << 20;
        for (const Danger& d : dangers_)
            if (d.kind == Danger::Kind::Storey && feet.y <= d.floorY + 6.0f && feet.y >= d.floorY - 30.0f && feet.x >= d.area.x && feet.x <= d.area.right()) {
                onSwept = true;
                in = std::min(in, d.in);
            }
        if (onSwept && in >= kSimHz / 3) {
            const auto swept = [&](Vec2 f) {
                for (const Danger& d : dangers_)
                    if (d.kind == Danger::Kind::Storey && f.y <= d.floorY + 6.0f && f.y >= d.floorY - 30.0f && f.x >= d.area.x && f.x <= d.area.right()) return true;
                return false;
            };
            if (b.storeyNode < 0 || swept(nav->node(b.storeyNode).feet)) {
                const int here = nav->locate(feet, 96.0f);
                b.storeyNode = here >= 0 ? nav->search(
                                               here,
                                               [&](int i) {
                                                   const NavGraph::Node& nd = nav->node(i);
                                                   return nd.mover < 0 && !nd.ladder && !swept(nd.feet) && safeSpot(w, seat, nd.feet, 60);
                                               },
                                               static_cast<float>(in) * 2.5f)
                                         : -1;
            }
            if (b.storeyNode >= 0) {
                fleeing = true;
                b.task = Task::Flee;
                b.goal = nav->node(b.storeyNode).feet;
                b.goalNode = b.storeyNode;
                stop = 4.0f;
                react = "change d'étage avant la lame";
            }
        } else if (!onSwept) {
            b.storeyNode = -1;
        }
    }
    // ------------------------------------------------------------ a sector whose gravity turns over (Graviton)
    int forced = 0;  // a side it must go to this step (a reaction), 0 none
    bool stay = false;  // it holds where it is this step (sheltered)
    float sectorRun = 0.0f;  // 2.87: how far it still has to go out of a sector, against the time left (> 1: too far at a walk)
    if (!fleeing && !holding)
        for (const Danger& d : dangers_) {
            if (d.kind != Danger::Kind::Sector || d.in > 3 * kSimHz || !d.area.contains(me.center())) continue;
            if (d.in == 0) {
                // Turned over: under a slab, it stays there.
                for (const Rect& s : view_.shelters) stay = stay || (feet.x > s.x && feet.x < s.right() && s.y < feet.y);
                // 2.87: otherwise out by its nearest side as it rises (only who is in the sector is lifted; a player stayed
                // under the vault, in its field, for the whole of it — 4,5 to 6 s).
                if (!stay) {
                    forced = feet.x - d.area.x < d.area.right() - feet.x ? -1 : 1;
                    react = "sort du secteur inversé";
                    sectorRun = 2.0f;  // rising toward the vault: as fast as it can
                }
                break;
            }
            // Out of it by its nearest side, or under a slab of it, whichever is nearer.
            float bestX = d.area.x - 14.0f, bestD = std::fabs(feet.x - bestX);
            if (std::fabs(d.area.right() + 14.0f - feet.x) < bestD) {
                bestX = d.area.right() + 14.0f;
                bestD = std::fabs(bestX - feet.x);
            }
            for (const Rect& s : view_.shelters) {
                if (s.w <= 0.0f || s.right() < d.area.x || s.x > d.area.right() || s.y >= feet.y - 22.0f) continue;
                const float x = std::clamp(feet.x, s.x + 2.0f, s.right() - 2.0f);
                if (std::fabs(x - feet.x) < bestD) {
                    bestD = std::fabs(x - feet.x);
                    bestX = x;
                }
            }
            if (bestD > 4.0f) {
                forced = bestX > feet.x ? 1 : -1;
                react = "quitte le secteur qui s'inverse";
                sectorRun = bestD / (me.config().maxRunSpeed * std::max(0.05f, static_cast<float>(d.in) / static_cast<float>(kSimHz)));
            } else {
                react = "s'abrite sous une dalle";
                stay = true;
            }
            break;
        }
    // ------------------------------------------------------------ a flash coming (Skullmaw): hide behind a column
    bool hiding = false;
    // 2.86: a player sent to a mirror that it reaches before the flash goes on to it (a mirror held is safe too): it went to
    // hide behind a column a few steps short of it, and the mirror was missing at the flash.
    const bool toPlate = b.task == Task::Mechanism && b.mechanism.w > 0.0f && !b.mechanismClosed &&
                         (std::fabs(b.mechanism.center().x - feet.x) + 2.0f * std::fabs(b.mechanism.bottom() - feet.y)) / 120.0f <
                             static_cast<float>(view_.flashIn) / static_cast<float>(kSimHz) - 0.4f;
    if (!fleeing && !forced && view_.flash && !holding && !toPlate && lv.hides && nav) {
        const auto hidden = [&](Vec2 c) { return w.physics().raycast(view_.eye, c).has_value(); };
        if (!hidden(me.center())) {
            if (b.hideNode < 0 || !hidden({nav->node(b.hideNode).feet.x, nav->node(b.hideNode).feet.y - 11.0f})) {
                const int here = nav->locate(feet);
                b.hideNode = here >= 0 ? nav->search(
                                             here,
                                             [&](int i) {
                                                 const NavGraph::Node& nd = nav->node(i);
                                                 return nd.mover < 0 && hidden({nd.feet.x, nd.feet.y - 11.0f}) && safeSpot(w, seat, nd.feet, 60);
                                             },
                                             900.0f)
                                       : -1;
            }
            if (b.hideNode >= 0) {
                b.goalNode = b.hideNode;
                b.goal = nav->node(b.hideNode).feet;
                react = "se cache derrière une colonne";
                hiding = true;
            }
            if (view_.flashIn <= 24) raiseShield = true;
        } else {
            react = "caché du regard";
            // 2.84: hidden as it charges, its shield anyway as it flashes (its skull drifts: a column that hid it may not any
            // more at the very instant).
            if (view_.flashIn <= 18) raiseShield = true;
            b.goal = feet;
            b.goalNode = -1;
            b.path.clear();
            hiding = true;
        }
    } else {
        b.hideNode = -1;
        if (toPlate && view_.flash && !holding && view_.flashIn <= 20) raiseShield = true;  // not on it yet as it flashes
    }
    // ------------------------------------------------------------ its target: where to strike it from
    const Enemy* foe = enemyOf(w, b.target);
    const Enemy* boss = fight_ ? enemyOf(w, bossUid_) : nullptr;
    const Rect fb = foe && b.preAim.w > 0.0f && fight_ && !view_.bodyTakesDamage ? b.preAim : foe ? strikeBox(*foe, w) : Rect{};
    const Vec2 fc = fb.center();
    const Power* sh = shooter(me);
    const bool bossFight = fight_ && boss;
    // 2.85: out of a boss fight (an ambush), a creature in the air or up over its head (a drone, a spectre on its
    // perch): the wave from below or from a place that sees it, never a blade swung under it for ever.
    // 2.85: out of a boss fight, a creature it cannot get to (4 s without a step toward it: what its way crosses never
    // passes, the creature keeps where its way ends) is struck from a place a wave reaches it from, 8 s.
    if (b.shootFor > 0) --b.shootFor;
    if (foe && !bossFight && b.task == Task::Fight && (fb.center() - me.center()).lengthSq() > 60.0f * 60.0f && (feet - b.idleAt).lengthSq() < 4.0f) {
        if (++b.idle > 4 * kSimHz) {
            b.shootFor = 8 * kSimHz;
            b.idle = 0;
        }
    } else {
        b.idle = 0;
        b.idleAt = feet;
    }
    const bool aloft = foe && !bossFight && (foe->def().flying || fb.bottom() < me.box.y - 4.0f || b.shootFor > 0);
    const bool spotted = foe && sh && (b.ranged || foe->def().isBoss || fb.w >= 56.0f || fb.h >= 72.0f || bossFight);  // from a place that sees it
    const bool ranged = spotted || (foe && sh && aloft);
    // Its body takes nothing now (armoured): keeps its energy, never strikes it (the lure keeps it busy).
    const bool worthStriking = foe && !(boss && foe == boss && !view_.bodyTakesDamage);
    // A target of a group finished together: brought low, then held there until every one of them is.
    // 2.93: counting the waves already flying at it.
    const bool holdFire = foe && b.syncGroup > 0 && b.syncGroup < 4 && !syncLow_[static_cast<size_t>(b.syncGroup)] &&
                          foe->hp() - incomingTo(foe->uid()) <= (b.holdAt + 0.001f) * foe->maxHp();
    if (!fleeing && !hiding && foe && !forced) {
        if (spotted) {
            // A place of the graph that sees it, out of its body and of what is coming (again every half second,
            // or when that place no longer sees it).
            // 2.84: its target moved away from where its place was chosen for (an arm that sweeps across the arena): again.
            const bool moved = b.spot >= 0 && (fc - b.spotFor).lengthSq() > 70.0f * 70.0f;
            const bool stale = b.spot < 0 || static_cast<int>(tick_) - b.spotAt > 60 || moved ||
                               (nav && b.spot >= 0 && !safeSpot(w, seat, nav->node(b.spot).feet, 60));
            // 2.84 (fight plan): its body closed to blows and nothing else to strike, a good way off it until it opens.
            const bool hide = view_.plan.hideArmoured && bossFight && !view_.bodyTakesDamage && foe == boss;
            if (hide) b.spot = -1;
            else if (stale && static_cast<int>(tick_) - b.spotAt > 12) chooseSpot(w, seat, b, fb, boss);
            // 2.87: a weak point bare no place lets it fire at for most of a second: another of its weak points for 4 s.
            if (bossFight && view_.exposed && foe != boss && b.syncGroup == 0) {
                const int now = static_cast<int>(tick_);
                if (b.spot >= 0 || b.noSpotWatch != b.target) {
                    b.noSpotWatch = b.target;
                    b.noSpotSince = now;
                } else if (now - b.noSpotSince > 90) {
                    b.noSpotFor = b.target;
                    b.noSpotUntil = now + 4 * kSimHz;
                }
            }
            if (b.spot < 0) {
                // No place sees it: near it, out of its body, each one its own place (never all on one pixel).
                const float side = fc.x < me.center().x ? 1.0f : -1.0f;
                b.goal = {fc.x + side * (fb.w * 0.5f + 120.0f + 34.0f * static_cast<float>(seat % 4)), std::max(fb.bottom(), feet.y)};
                if (nav && (b.goalNode < 0 || (nav->node(b.goalNode).feet - b.goal).lengthSq() > 64.0f * 64.0f)) b.goalNode = nav->locate(b.goal, 200.0f);
                // 2.84 (fight plan): a boss fight — no place to fire from now: it waits a good way off its body, somewhere
                // safe (never walking up to it for want of a better place), until one comes.
                if (bossFight && nav && b.role != Role::Lure) {
                    const int here = nav->locate(feet, 96.0f);
                    const Rect off = bossBody_.expanded(hide ? 320.0f : 170.0f);
                    const Rect near = bossBody_.expanded(hide ? 700.0f : 340.0f);  // and not out of the fight either
                    if (here >= 0 && (b.standOff < 0 || off.contains(nav->node(b.standOff).feet) || !near.contains(nav->node(b.standOff).feet) ||
                                      !safeSpot(w, seat, nav->node(b.standOff).feet, 36)))
                        b.standOff = nav->search(
                            here, [&](int i) {
                                const NavGraph::Node& nd = nav->node(i);
                                return nd.mover < 0 && !nd.ladder && !off.contains(nd.feet) && near.contains(nd.feet) && safeSpot(w, seat, nd.feet, 36) &&
                                       !(view_.plan.elevate > 0.0f && nd.feet.y > view_.plan.floorY - view_.plan.elevate);
                            },
                            4000.0f);
                    if (b.standOff >= 0) {
                        b.goalNode = b.standOff;
                        b.goal = nav->node(b.standOff).feet;
                    }
                }
            }
        } else if (aloft) {
            // 2.85: a creature in the air: the nearest place it can reach whence a wave reaches it (standing, or from the top
            // of a jump or of its double jump), again when it flew off; without a wave, or no such place, under it a little
            // aside (each one its own side) on the nearest ground — the blade from a jump.
            const int now = static_cast<int>(tick_);
            const bool redo = b.goalNode < 0 || (fc - b.spotFor).lengthSq() > 48.0f * 48.0f || now - b.spotAt > 240;
            if (nav && redo && now - b.spotAt > 12) {
                b.spotAt = now;
                b.spotFor = fc;
                int pick = -1;
                const int here = nav->locate(feet, 96.0f);
                if (here >= 0 && sh) {
                    const float h1 = me.config().jumpHeight - 6.0f, h2 = me.config().jumpHeight + me.config().doubleJumpHeight - 8.0f;
                    // A place whence it fires standing first (a jump from under a grate lands on it), then from a jump.
                    for (int pass = 0; pass < 2 && pick < 0; ++pass)
                        pick = nav->search(
                        here, [&](int i) {
                            const NavGraph::Node& nd = nav->node(i);
                            if (nd.mover >= 0 || nd.ladder) return false;
                            // Never against it (its body hurts, and a place on it is never reached).
                            if (Rect::fromBottomCenter(nd.feet, {me.box.w, me.box.h}).intersects(fb.expanded(40.0f))) return false;
                            // Each one its own place (one on another's head no longer stands where its shot was measured).
                            for (int o = 0; o < static_cast<int>(bots_.size()); ++o)
                                if (o != seat && ((seats_ >> o) & 1u) && bots_[static_cast<size_t>(o)].target == b.target &&
                                    std::fabs(bots_[static_cast<size_t>(o)].goal.x - nd.feet.x) < 20.0f && std::fabs(bots_[static_cast<size_t>(o)].goal.y - nd.feet.y) < 20.0f)
                                    return false;
                            const Vec2 c{nd.feet.x, nd.feet.y - 11.0f};
                            return shotFrom(w, c, fb, true) >= 0 ||
                                   (pass == 1 && (shotFrom(w, c - Vec2{0.0f, h1}, fb, false) >= 0 || shotFrom(w, c - Vec2{0.0f, h2}, fb, false) >= 0));
                        },
                        3000.0f);
                }
                if (pick >= 0) {
                    b.goalNode = pick;
                    b.goal = nav->node(pick).feet;
                } else {
                    const float side = (seat % 2) != 0 ? -1.0f : 1.0f;
                    b.goal = {fc.x + side * (fb.w * 0.5f + 30.0f + 10.0f * static_cast<float>(seat / 2)), fb.bottom()};
                    b.goalNode = nav->locate(b.goal, 280.0f);
                }
            }
            if (nav && b.goalNode >= 0) b.goal = nav->node(b.goalNode).feet;  // the plan gave it the creature's own place
        } else {
            const float side = fc.x < me.center().x ? 1.0f : -1.0f;
            b.goal = {fc.x + side * (fb.w * 0.5f + 12.0f), fb.bottom()};
            if (nav && (b.goalNode < 0 || (nav->node(b.goalNode).feet - b.goal).lengthSq() > 64.0f * 64.0f)) b.goalNode = nav->locate(b.goal, 160.0f);
        }
        stop = 4.0f;
    } else if (!fleeing && b.act == Strike && !foe) {
        b.act = None;  // beaten: the next plan gives it another task
        b.spot = -1;
    }
    // ------------------------------------------------------------ a blow coming on it: out of it (its level's reaction)
    bool dodging = forced != 0;
    // 2.84 (fight plan): a soul in the boss's body is approached only with its shield ready (it kneels 2 s under it): without it, it
    // waits off the body (the bites and the spit of Voidworm took its revivers one after the other).
    if (fight_ && bossUid_ && view_.plan.reviveShielded && b.task == Task::Revive && !me.shieldActive() && !shieldReady &&
        Rect{b.goal.x - 6.0f, b.goal.y - 22.0f, 12.0f, 22.0f}.intersects(bossBody_.expanded(20.0f)) && !me.box.intersects(bossBody_.expanded(60.0f))) {
        b.goal = feet;
        b.goalNode = -1;
        b.path.clear();
        react = "attend son bouclier pour réanimer";
    }
    const bool staysPut = b.task == Task::Revive && std::fabs(b.goal.x - feet.x) <= GameWorld::kReviveReachX - 6.0f &&
                          std::fabs(b.goal.y - feet.y) <= GameWorld::kReviveReachY - 6.0f;
    // A sweeping beam on it (announced or lit): no step gets out of it — the shield as soon as it is seen, whatever
    // else it dodges (its 2,2 s outlast the sweep).
    for (const Danger& d : dangers_)
        if (d.kind == Danger::Kind::Line && d.gapHalf > 0.0f && sweepIn(w, d, me.box) <= (d.turn != 0.0f ? 24 : kSimHz / 2) && hitsLine(d, me.box)) {
            if (shieldReady || me.shieldActive()) {
                raiseShield = raiseShield || shieldReady;
                react = "bouclier contre le rayon";
            }
            break;
        }
    // ------------------------------------------------------------ 2.84: through the boss's body, under its shield
    // Its way (a place to hold, a soul, a place to strike from) lies on the other side of the boss, whose body fills
    // the floor between (Pyroclast's arm over the arena): a player does not wait for it to leave — the shield up (it
    // takes the contact, 2,2 s), dashes through (invulnerable while it dashes), out the other side.
    if (fight_ && bossUid_ && b.cross <= 0 && !fleeing && !holding && (b.task == Task::Mechanism || (b.task == Task::Fight && b.syncGroup > 0 && b.spot >= 0))) {
        const Rect& bb = bossBody_;
        const bool across = (me.center().x < bb.x && b.goal.x > bb.right() + 8.0f) || (me.center().x > bb.right() && b.goal.x < bb.x - 8.0f);
        const float edge = me.center().x < bb.x ? bb.x - me.box.right() : me.box.x - bb.right();
        const bool level = me.box.bottom() > bb.y && me.box.y < bb.bottom();
        // At its edge, or held back short of it by its blows (waiting there for a second): the shield, and through.
        if (across && level && edge > -4.0f && (edge < 60.0f || (edge < 420.0f && b.blocked > kSimHz)) && me.grounded() && shieldReady && me.energy >= 45.0f) {
            b.cross = static_cast<int>(5.0f * static_cast<float>(kSimHz));
            b.crossDir = b.goal.x > me.center().x ? 1 : -1;
            if (!me.shieldActive()) raiseShield = true;
        }
    }
    // 2.84: on its way to a place to hold or a soul, held back by blows that keep coming (a second at their edge): the
    // shield up, it goes on through them on its path for as long as it lasts (it takes them all), as a player does.
    if (fight_ && b.push <= 0 && b.cross <= 0 && b.task == Task::Mechanism && !holding && b.blocked > kSimHz && shieldReady &&
        me.energy >= 30.0f) {
        b.push = static_cast<int>(2.1f * static_cast<float>(kSimHz));
        raiseShield = true;
    }
    if (b.push > 0) {
        --b.push;
        if (!me.shieldActive() && !raiseShield) b.push = 0;
        else react = "passe sous son bouclier";
    }
    const bool pushing = b.push > 0;
    const bool crossing = b.cross > 0;
    if (crossing) {
        --b.cross;
        const bool inside = me.box.expanded(4.0f).intersects(bossBody_);
        // Out the other side (or its shield fell before it was halfway): done.
        if (!inside && ((b.crossDir > 0 && me.box.x > bossBody_.right()) || (b.crossDir < 0 && me.box.right() < bossBody_.x))) b.cross = 0;
        react = "traverse sous son bouclier";
    }
    // 2.89: what no shield stops (Voidworm's maw: it swallows) is left even on the way up above a liquid (it took two or three
    // players climbing out of its acid at once), and never stepped back into to get out of something else.
    const auto gulpAt = [&](const Rect& r) {
        for (const Danger& d : dangers_)
            if (d.unblockable && d.kind == Danger::Kind::Area && (d.seat < 0 || d.seat == seat) && d.in <= 3 * kSimHz && d.area.intersects(r)) return true;
        return false;
    };
    const bool gulpOnMe = fleeing && gulpAt(me.box.expanded(5.0f));
    bool dropThrough = false;  // 2.89: down through a one-way ledge out of what swallows
    bool dashOut = false;      // 2.89 (Plan::dashOut): out of a blow too wide for a walk, dashing
    if ((!fleeing || gulpOnMe) && !forced) {
        // The most pressing blow on it: the one whose time left is the shortest against the time it needs to get
        // out (its way out at a run, its level's reaction), the blows still far off left for later.
        const Danger* hit = nullptr;
        float urgency = 1e30f;
        const Rect mine = me.box.expanded(5.0f);
        for (const Danger& d : dangers_) {
            if ((d.seat >= 0 && d.seat != seat) || d.in > 3 * kSimHz) continue;
            if (gulpOnMe && !d.unblockable) continue;
            if (d.kind == Danger::Kind::Line && d.gapHalf > 0.0f) continue;  // a sweep: its shield (above)
            if (station.w > 0.0f && d.kind == Danger::Kind::Area && !d.body && d.area.intersects(station) && !d.shot && !d.unblockable) {
                if (d.in <= 14 && d.area.intersects(mine)) {
                    raiseShield = true;
                    react = "tient sous son bouclier";
                }
                continue;
            }
            if (me.shieldActive() && !d.body && d.kind != Danger::Kind::Pull && !d.unblockable) continue;  // under its shield: nothing gets through
            if ((crossing || pushing) && (d.body || (me.shieldActive() && d.kind != Danger::Kind::Pull && !d.unblockable))) continue;  // 2.84: it goes through (above)
            float need = 0.0f;
            if (d.kind == Danger::Kind::Area || d.kind == Danger::Kind::Pull) {
                if (!d.area.intersects(mine)) continue;
                const float out = std::min(me.center().x - d.area.x, d.area.right() - me.center().x) + 8.0f;
                // A mark is left as soon as it is seen (half a second to spare: a crowd, a turn, a jump under way).
                need = d.kind == Danger::Kind::Pull ? 1e9f : out / 140.0f * static_cast<float>(kSimHz) + (d.shot ? 10.0f : 70.0f);
            } else if (d.kind == Danger::Kind::Line) {
                if (!hitsLine(d, mine)) continue;
                need = 36.0f;
            } else {
                continue;
            }
            const float u = static_cast<float>(d.in) - need - static_cast<float>(lv.reaction);
            if (u > 0.0f && !d.shot) continue;  // time enough: later
            if (d.shot && d.in > lv.reaction + 24) continue;
            if (u < urgency) {
                urgency = u;
                hit = &d;
            }
        }
        if (hit && hit->kind == Danger::Kind::Pull) {
            forced = feet.x >= hit->from.x ? 1 : -1;
            dodging = true;
            react = "s'éloigne du puits";
        } else if (hit) {
            if (b.react <= 0) b.react = lv.reaction + 1;
            const bool seen = --b.react <= 1;
            if (seen) b.react = 1;  // seen: it stays seen while it lasts
            const bool shot = hit->shot;
            // Holding a plate or reviving: it stays (its shield takes the blow); otherwise out by the nearest safe side.
            // 2.85 (fight plan): a blow marked « shield first » (Heartbreaker's rib traps under every player: a step aside
            // lands in another's or off its platform) is taken under the shield when it is ready.
            // 2.86 (fight plan): a beam to jump (Skullmaw's gazes) on a player kneeling by a soul without its shield: over it, the
            // revival begun again after (it took the beam kneeling, one gaze after the other).
            const bool kneelBare = staysPut && view_.plan.jumpBeams && hit->kind == Danger::Kind::Line && !shieldReady && !me.shieldActive();
            const bool piloted = escapeMode && hit->kind == Danger::Kind::Area && !hit->body && !holding && !staysPut && !hit->shieldFirst && !hit->unblockable;
            // 2.90 (the escape): against the boss's body, the navigation looks for its way out first (off its platform, a jump,
            // a dash through); its own way along the floor (below) only when it finds none.
            if (escapeMode && hit->body && !holding && !staysPut) b.fleeBody = true;
            if (piloted) {
                escHit = hit;  // the navigation gets it out (its way, its timing); its shield decided once it has chosen (below)
            } else if ((holding || (staysPut && !kneelBare) || ((shot || hit->shieldFirst) && (shieldReady || me.shieldActive()))) && !hit->unblockable) {
                if (hit->in <= 14) raiseShield = true;
                // 2.84: a beam on a player holding its place (Skullmaw's gazes on the mirrors): a step aside on the plate itself,
                // to its edge away from the beam — still on it, out of the line.
                if (holding && hit->kind == Danger::Kind::Line && b.mechanism.w > 24.0f && me.grounded()) {
                    const Vec2 ab = hit->to - hit->from;
                    if (std::fabs(ab.y) > std::fabs(ab.x) * 0.5f) {
                        const float len2 = std::max(1.0f, ab.lengthSq());
                        const float t = std::clamp(((me.center() - hit->from).x * ab.x + (me.center() - hit->from).y * ab.y) / len2, 0.0f, 1.0f);
                        const float away = me.center().x - (hit->from.x + ab.x * t);
                        int side = std::fabs(away) > 0.5f ? (away > 0.0f ? 1 : -1) : (me.center().x < b.mechanism.center().x ? -1 : 1);
                        // 2.86: the edge out of every beam about to fire (another player's gaze may cross its plate), if one is.
                        const auto litAt = [&](float x) {
                            const Rect body = bodyAt({x, feet.y}).expanded(3.0f);
                            for (const Danger& o : dangers_)
                                if (o.kind == Danger::Kind::Line && o.gapHalf <= 0.0f && (o.seat < 0 || o.seat == seat) && o.in <= hit->in + kSimHz / 2 &&
                                    hitsLine(o, body))
                                    return true;
                            return false;
                        };
                        const auto edgeOf = [&](int sd) { return sd > 0 ? b.mechanism.right() - 4.0f : b.mechanism.x + 4.0f; };
                        if (litAt(edgeOf(side)) && !litAt(edgeOf(-side))) side = -side;
                        const float edge = edgeOf(side);
                        if ((edge - me.center().x) * static_cast<float>(side) > 2.0f) {
                            forced = side;
                            dodging = true;
                            react = "s'écarte sur sa plaque";
                        }
                    }
                    // 2.86: a beam lying low across its plate (no step aside gets out of it) and its shield not ready: a jump
                    // as it fires (it rises over the line for half a second, back on its plate), never as the gaze flashes.
                    const int at = beamJumpAt(*hit);
                    if (std::fabs(ab.y) <= std::fabs(ab.x) * (view_.plan.jumpBeams ? 1.0f : 0.5f) && !shieldReady && !me.shieldActive() && at >= 0 &&
                        hit->in <= at && hit->in >= at - 3 && !(view_.flash && view_.flashIn < kSimHz) && b.jumpHold <= 0 &&
                        !(b.prevHeld & (1u << kJump))) {
                        b.jumpHold = 46;
                        b.beamJump = 46;
                        react = "saute par-dessus le rayon";
                    }
                }
            } else if (seen && noise(seat + 8 * static_cast<int>(seed_), tick_ / 30) < std::max(0.55f, lv.precision)) {
                int side = 0;
                if (hit->kind == Danger::Kind::Line) {
                    const Vec2 a = hit->from, c = hit->to;
                    const Vec2 ab = c - a;
                    const float len2 = std::max(1.0f, ab.lengthSq());
                    const float t = std::clamp(((me.center() - a).x * ab.x + (me.center() - a).y * ab.y) / len2, 0.0f, 1.0f);
                    const Vec2 near = a + ab * t;
                    const Vec2 away = me.center() - near;
                    if (std::fabs(ab.x) > std::fabs(ab.y) * (view_.plan.jumpBeams ? 1.0f : 2.0f)) {
                        // A beam lying along its floor: over it (jump) or under it (crouch).
                        // 2.86: the jump timed on its firing (over the line from about a tenth to two thirds of a second after it
                        // leaves the ground, as high as the beam's slope asks): jumped as soon as seen, it came down into the beam
                        // still firing.
                        const int at = view_.plan.jumpBeams ? beamJumpAt(*hit) : 1 << 20;
                        if (away.y <= 0.0f && me.grounded() && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump)) && at >= 0 && hit->in <= at) {
                            b.jumpHold = view_.plan.jumpBeams ? 46 : 26;
                            if (view_.plan.jumpBeams) b.beamJump = 46;  // held to its top (the navigation cut it short)
                        }
                        else if (away.y > 0.0f) held |= 1u << kDown;
                    } else {
                        side = std::fabs(away.x) > 1.0f ? (away.x > 0.0f ? 1 : -1) : (me.center().x >= a.x ? 1 : -1);
                        // 2.86: out of its own beam, but not into another's (Skullmaw's gazes fan out from the same eye, one per
                        // player: a step aside landed in a neighbour's) — the side whose nearest place along its floor is out of
                        // every beam about to fire, with ground under it and no wall on the way.
                        float dist[2] = {1e30f, 1e30f};
                        for (int sgn : {-1, 1})
                            for (float dx = 6.0f; dx <= 84.0f; dx += 6.0f) {
                                const Vec2 at{feet.x + static_cast<float>(sgn) * dx, feet.y};
                                if (w.physics().raycast(me.center(), {at.x, me.center().y}).has_value()) break;  // a wall
                                if (me.grounded() && !w.physics().raycast({at.x, at.y - 4.0f}, {at.x, at.y + 24.0f}, true).has_value()) break;  // the edge
                                const Rect body = bodyAt(at).expanded(5.0f);
                                bool lit = false;
                                for (const Danger& o : dangers_)
                                    lit = lit || (o.kind == Danger::Kind::Line && o.gapHalf <= 0.0f && (o.seat < 0 || o.seat == seat) &&
                                                  o.in <= hit->in + kSimHz / 2 && hitsLine(o, body));
                                if (lit) continue;
                                dist[sgn > 0 ? 1 : 0] = dx;
                                break;
                            }
                        const int other = -side;
                        if (dist[other > 0 ? 1 : 0] + 12.0f < dist[side > 0 ? 1 : 0]) side = other;
                    }
                    react = "sort de la ligne du rayon";
                } else if (hit->shot && std::fabs(hit->to.x) > std::fabs(hit->to.y) * 1.5f && me.grounded()) {
                    // A shot flying along the floor at it: over it.
                    if (b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump))) b.jumpHold = 26;
                    react = "saute par-dessus le tir";
                } else {
                    // The nearest place along its floor out of everything that strikes before it could leave again (the
                    // other players' marks too: a crowd under a volley), with ground under it and no wall on the way.
                    const float left = me.center().x - hit->area.x, right = hit->area.right() - me.center().x;
                    side = left < right ? -1 : 1;
                    float dist[2] = {1e30f, 1e30f};  // to the nearest safe place on the left, on the right
                    for (int sgn : {-1, 1})
                        for (float dx = 8.0f; dx <= 240.0f; dx += 8.0f) {
                            const Vec2 at{feet.x + static_cast<float>(sgn) * dx, feet.y};
                            if (w.physics().raycast(me.center(), {at.x, me.center().y}).has_value()) break;  // a wall
                            if (!w.physics().raycast({at.x, at.y - 4.0f}, {at.x, at.y + 24.0f}, true).has_value()) break;  // the edge
                            if (!safeSpot(w, seat, at, hit->in + 40) || gulpAt(bodyAt(at).expanded(4.0f))) continue;
                            if (nav && nav->locate(at, 12.0f) < 0) continue;  // a lasting danger there (lava, spikes): no place
                            dist[sgn > 0 ? 1 : 0] = dx + 12.0f;  // a little beyond its edge
                            break;
                        }
                    if (dist[0] < 1e29f || dist[1] < 1e29f) side = dist[0] <= dist[1] ? -1 : 1;
                    // 2.89: in what swallows (Voidworm's maw: its ring 150 px high over the place it locked on) on a ledge one
                    // drops through, with no way out along it in time (two rings side by side, the ledge's edges): down through
                    // the ledge (Down + Jump), under the ring in a fifth of a second — never into a liquid below.
                    if (hit->unblockable && me.grounded() && !(b.prevHeld & (1u << kJump)) &&
                        std::min(dist[0], dist[1]) > me.config().maxRunSpeed * static_cast<float>(hit->in) / static_cast<float>(kSimHz) - 12.0f)
                        if (const Collider* g = w.physics().groundBelow(me.box); g && g->kind == ColliderKind::OneWay) {
                            bool wet = false;
                            for (const Danger& d : dangers_)
                                wet = wet || (d.kind == Danger::Kind::Flood && d.area.contains({feet.x, feet.y + 40.0f}));
                            if (!wet) {
                                dropThrough = true;
                                side = 0;
                            }
                        }
                    // The way it already took, while it still leads out not much further than the other (no dithering);
                    // never a way that leads nowhere (a wall, an edge, more blows).
                    if (!dropThrough && b.escapeSteps > 0 && b.escapeDir != 0 && dist[b.escapeDir > 0 ? 1 : 0] <= std::min(dist[0], dist[1]) * 1.5f + 16.0f)
                        side = b.escapeDir;
                    react = hit->body && tactic_ ? "s'écarte du corps de " + std::string(tactic_->name()) : std::string("esquive");
                }
                if (side != 0) {
                    if (side != b.escapeDir || b.escapeSteps <= 0) b.escapeSteps = 24;
                    b.escapeDir = side;
                    forced = side;
                }
                dodging = true;
                // Too close to get out in time: the shield.
                const float out = side < 0 ? me.center().x - hit->area.x : hit->area.right() - me.center().x;
                if (hit->kind == Danger::Kind::Area && out / 150.0f * static_cast<float>(kSimHz) > static_cast<float>(hit->in) + 4.0f && hit->in <= 18)
                    raiseShield = true;
                // 2.89 (fight plan): too far out of it for a walk in time (Pyroclast's meteor fist: 440 px, its shadow locked 1 s
                // before it lands): out dashing, as a player does.
                if (view_.plan.dashOut && side != 0 && hit->kind == Danger::Kind::Area && !hit->body &&
                    (out + 6.0f) / me.config().maxRunSpeed * static_cast<float>(kSimHz) > static_cast<float>(hit->in) - 4.0f)
                    dashOut = true;
            } else if (hit->in <= 10) {
                raiseShield = true;
            }
            // About to land on it and not out yet: the shield (it takes everything for 2,2 s), whatever its way out.
            if (hit->in <= 10 && !hit->body && hit->kind != Danger::Kind::Pull && !escHit) raiseShield = true;
        } else {
            b.react = 0;
        }
    }
    if (b.escapeSteps > 0) --b.escapeSteps;
    // 2.87: a blade to take under the shield (Dreadfang's sweeps, 1 500 px/s) on a player in the air over its storey (its
    // jump toward a place, a jump down): the shield too as it comes (only a player on its floor saw it; 18 of 97 sweeps
    // that struck found the shield ready, none raised).
    if (!fleeing && !me.grounded() && !me.shieldActive() && shieldReady && me.energy >= 20.0f)
        for (const Danger& d : dangers_) {
            if (d.kind != Danger::Kind::FloorWave || !d.shieldFirst || (d.seat >= 0 && d.seat != seat) || d.speed == 0.0f) continue;
            if (me.box.bottom() < d.floorY - d.height - 24.0f || me.box.y > d.floorY + 4.0f) continue;  // over it, or under its storey
            const float gap = d.speed > 0.0f ? me.box.x - d.area.right() : d.area.x - me.box.right();
            if (gap < -(d.area.w + me.box.w)) continue;  // already past it
            const float t = std::max(0.0f, gap) / std::fabs(d.speed) + static_cast<float>(d.in) / static_cast<float>(kSimHz);
            if (t < 0.2f) {
                raiseShield = true;
                react = "bouclier contre la lame";
                break;
            }
        }
    // ------------------------------------------------------------ a wave running along its floor: jump it at the right moment
    if (!fleeing && me.grounded() && b.waveJumpCool <= 0)
        for (const Danger& d : dangers_) {
            // On its floor (or on an ally standing there: a wave's height takes both).
            if (d.kind != Danger::Kind::FloorWave || (d.seat >= 0 && d.seat != seat) || feet.y > d.floorY + 6.0f || feet.y < d.floorY - 30.0f ||
                d.speed == 0.0f)
                continue;
            float gap;
            if (d.speed > 0.0f) gap = me.box.x - d.area.right();
            else gap = d.area.x - me.box.right();
            if (gap < -(d.area.w + me.box.w)) continue;  // already past it
            const float t = std::max(0.0f, gap) / std::fabs(d.speed) + static_cast<float>(d.in) / static_cast<float>(kSimHz);
            // 2.84 (fight plan): a wave too fast for a sure jump — its shield as it comes, kept for it; a jump without it.
            if (d.shieldFirst && (me.shieldActive() || (shieldReady && me.energy >= 20.0f))) {
                if (!me.shieldActive() && t < 0.2f) {
                    raiseShield = true;
                    react = "bouclier contre la lame";
                }
                if (t < 0.6f) break;
            }
            // Its jump: above the wave's height from `up` to `down` seconds after it leaves the ground (its own jump's
            // physics: the rise, the top, the faster fall); the wave passes under it in `pass` seconds. A wave taller
            // than its jump: the double jump at the top (from `up` of that one).
            const PlayerConfig& pc = me.config();
            const float g = pc.gravity(), v0 = pc.jumpVelocity(), top = pc.jumpHeight, apex = v0 / g;
            const float h = d.height + 4.0f;
            float up, down;
            if (h < top - 2.0f) {
                up = (v0 - std::sqrt(std::max(0.0f, v0 * v0 - 2.0f * g * h))) / g;
                down = apex + std::sqrt(2.0f * (top - h) / (g * pc.fallGravityMultiplier));
            } else {
                const float v1 = pc.doubleJumpVelocity(), top2 = top + pc.doubleJumpHeight;
                if (h >= top2 - 2.0f) continue;  // out of its jumps: its shield, or out of the way
                up = apex + (v1 - std::sqrt(std::max(0.0f, v1 * v1 - 2.0f * g * (h - top)))) / g;
                down = apex + v1 / g + std::sqrt(2.0f * (top2 - h) / (g * pc.fallGravityMultiplier));
            }
            const float pass = (d.area.w + me.box.w) / std::fabs(d.speed);
            const float err = lv.timing * (noise(seat + 8 * static_cast<int>(seed_), tick_ / 20 + 101) - 0.5f) * 2.0f;
            const float lo = up + 0.03f, hi = std::max(lo + 0.02f, down - pass - 0.03f);
            if (t + err < lo && t < 0.18f && shieldReady) {
                raiseShield = true;  // too late for a jump: the shield
                react = "bouclier contre l'onde";
                break;
            }
            if (t + err < lo || t + err > std::min(hi, lo + 0.25f)) continue;
            if ((holding || staysPut) && shieldReady) {
                raiseShield = true;  // on its plate: the shield, not a jump off it
                react = "bouclier contre l'onde";
            } else if (!(b.prevHeld & (1u << kJump))) {
                b.jumpHold = 48;  // 2.84: held to the top of its jump (released before, the jump is cut short)
                b.airJump = d.height + 4.0f < me.config().jumpHeight - 2.0f;  // a tall wave: its air jump at the top
                b.waveJumpCool = 30;
                react = "saute l'onde";
            }
            break;
        }
    // ------------------------------------------------------------ a ring growing toward it: its shield as it passes
    for (const Danger& d : dangers_) {
        if (d.kind != Danger::Kind::Ring || d.in > 0) continue;
        const Vec2 c = me.center() - d.from;
        const float dist = c.length();
        if (dist > d.reach + 10.0f || dist < d.radius - 8.0f) continue;
        const float ang = std::atan2(c.y, c.x);
        float diff = std::fabs(ang - d.gap);
        while (diff > 3.14159265f) diff = std::fabs(diff - 6.2831853f);
        if (diff <= d.gapHalf * 0.8f) continue;  // in its opening
        if ((dist - d.radius) / d.speed < 0.14f) {
            raiseShield = true;
            react = "bouclier contre l'anneau";
        }
    }
    // A tall wave under it: the air jump at the top of its jump.
    if (!me.grounded() && !b.airJump && me.vel.y > -30.0f && b.waveJumpCool > 0 && !(b.prevHeld & (1u << kJump))) {
        b.airJump = true;
        held |= 1u << kJump;
        b.jumpHold = 14;
    }
    // ------------------------------------------------------------ moving
    // The navigation moves it (game/raid/nav: its path, its moves through the air, what it sees coming on its
    // way); the tactic chooses where (its goal), and overrides the direction for a reaction of its own — never
    // during a move through the air under way (it is played as it was simulated).
    int dir = 0;
    steer(w, seat, b, f, held, dir, stop);
    const int pilotDir = dir;
    // 2.90 (the escape): a dodge of the navigation under way — a shot only along its way (the stick's direction moves it that
    // step), never as it dashes (the dash goes where the stick points), no jump of the tactic's.
    const bool pilotDodging = escapeMode && navi_.pilot(seat).dodging();
    const Power* dashPower = powerOf(me, "dash");
    const bool dashNow = dashPower && keyBit(dashPower) >= 0 && (held & (1u << keyBit(dashPower)));
    const auto alongDodge = [&](int aim) { return !pilotDodging || (!dashNow && kDirX[aim] == pilotDir); };
    // 2.90 (the escape): its shield up first (one power a step: the dash pressed with it took its place, and it went through
    // bare); not up within a tenth of a second, the crossing is given up short of the body.
    const bool crossBare = escapeMode && crossing && !me.shieldActive();
    if (crossBare && b.cross < static_cast<int>(5.0f * static_cast<float>(kSimHz)) - 12) b.cross = 0;
    if (crossing && !navi_.pilot(seat).committed() && forced == 0 && (!escapeMode || b.cross > 0)) {
        // 2.84: straight through, dashing (invulnerable while it dashes) as soon as it can.
        dir = crossBare && me.box.expanded(24.0f).intersects(bossBody_) ? 0 : b.crossDir;
        b.moveText = "traverse en ruée";
        if (const Power* dp = powerOf(me, "dash"); dp && keyBit(dp) >= 0 && dp->ready(me) && me.energy >= 25.0f && !(b.prevHeld & (1u << keyBit(dp))) &&
                                                   me.box.expanded(30.0f).intersects(bossBody_) && !crossBare)
            held |= 1u << keyBit(dp);
    } else if (forced != 0 && (!navi_.pilot(seat).committed() || sectorRun > 0.0f)) {
        dir = forced;
        b.moveText = "s'écarte";
        // 2.87: out of a sector about to turn over (or turned over) too far for a walk in time: dashing (its 800 px sectors).
        if (sectorRun > 0.7f || dashOut)
            if (const Power* dp = powerOf(me, "dash"); dp && keyBit(dp) >= 0 && dp->ready(me) && me.energy >= 10.0f && !(b.prevHeld & (1u << keyBit(dp))))
                held |= 1u << keyBit(dp);
    } else {
        if (stay && !navi_.pilot(seat).committed()) dir = 0;
        // Never into what is coming (a mark, a cloud, a column, the boss's body): it waits at its edge, the way
        // it was going, until it has passed — out of a danger it may cross (it is getting out).
        // 2.85: out of a boss fight, what never passes (a creature standing at the edge, a hazard that stays) is gone through
        // after 4 s of waiting: 2 s on its way, its shield up against it if it can.
        if (b.brave > 0) --b.brave;
        if (!fight_ && b.blocked > 4 * kSimHz) {
            b.brave = 2 * kSimHz;
            b.blocked = 0;
        }
        if (b.brave > 0 && dir != 0 && shieldReady && threatened(w, seat, me.box.expanded(12.0f), 30)) raiseShield = true;
        if (dir != 0 && b.brave <= 0 && me.grounded() && !holding && !crossing && !pushing && !fleeing &&
            !(station.w > 0.0f && me.box.expanded(48.0f).intersects(station))) {
            const Rect next = me.box.translated({static_cast<float>(dir) * 10.0f, 0.0f}).expanded(3.0f);
            // 2.90 (the escape): the blows on an area are the navigation's (it reads their time: it passes before one lands);
            // only the boss's body, its beams and its wells are waited for here.
            const auto edge = [&](const Rect& box) {
                if (!escapeMode) return threatened(w, seat, box, 100);
                for (const Danger& d : dangers_) {
                    if ((d.seat >= 0 && d.seat != seat) || d.in > 100 || (d.kind == Danger::Kind::Area && !d.body)) continue;
                    if ((d.kind == Danger::Kind::Line && hitsLine(d, box)) || ((d.kind == Danger::Kind::Area || d.kind == Danger::Kind::Pull) && d.area.intersects(box)))
                        return true;
                }
                return false;
            };
            if (edge(next) && !edge(me.box.expanded(3.0f))) {
                dir = 0;
                b.moveText = "attend que ça passe";
                b.lost = 0;
                ++b.blocked;
            }
        }
    }
    // ------------------------------------------------------------ 2.84: nailed (Dreadfang's claw): it breaks its own claw
    // Held on its mark, the claw at its feet: its blade on it (turned toward it), its shield against the claw's bites.
    const bool nailed = fight_ && std::find(view_.held.begin(), view_.held.end(), seat) != view_.held.end();
    if (nailed) {
        const Enemy* claw = nullptr;
        float cd = 90.0f * 90.0f;
        for (const bots::Target& t : view_.targets)
            if (t.priority >= 30 && t.melee)
                if (const Enemy* e = enemyOf(w, t.uid); e && (e->center() - me.center()).lengthSq() < cd) {
                    cd = (e->center() - me.center()).lengthSq();
                    claw = e;
                }
        if (claw) {
            const float dx = claw->center().x - me.center().x;
            dir = 0;
            forced = 0;
            if (std::fabs(dx) > 2.0f && (dx >= 0.0f) != (me.facing >= 0.0f)) dir = dx > 0.0f ? 1 : -1;
            else if (b.attackCool <= 0) {
                held |= 1u << kAttack;
                b.attackCool = 22;
            }
            raiseShield = true;
            react = "brise la griffe qui le cloue";
        }
    }
    // ------------------------------------------------------------ what it does once there
    bool fired = false;
    // 2.87: a wave goes through what it meets: aimed at one target of a group finished together, it must not cross another
    // of that group already brought low and waiting for the others (Titanus's two restraints, side by side on its leg: the
    // one held low broke alone under the shots at the other, and grew back).
    const auto spoils = [&](int aim) {
        if (aim < 0) return false;
        const Vec2 m = me.center() + dirOf(aim) * 10.0f + Vec2{0.0f, -2.0f};
        const Vec2 end = m + dirOf(aim) * kWaveRange;
        for (const bots::Target& t : view_.targets) {
            if (t.sync <= 0 || t.sync >= 4 || t.uid == b.target || syncLow_[static_cast<size_t>(t.sync)]) continue;
            const Enemy* e = enemyOf(w, t.uid);
            if (!e || e->hp() - incomingTo(t.uid) > t.holdAt * e->maxHp() + 4.0f) continue;
            if (Collision::segmentIntersectsRect(m, end, e->box.expanded(6.0f))) return true;
        }
        return false;
    };
    if (!fleeing && foe && !dodging && !nailed) {
        const Vec2 d = fc - me.center();
        const bool melee = !ranged || !sh || me.energy < 10.0f;
        // Never into the boss's body for a blade (it hurts on contact).
        const bool bodyNear = boss && Rect{me.center().x + (d.x >= 0.0f ? 0.0f : -36.0f), me.box.y, 36.0f, me.box.h}.intersects(bossBody_.expanded(4.0f));
        const bool inReach = std::fabs(d.x) <= fb.w * 0.5f + 22.0f && std::fabs(d.y) <= fb.h * 0.5f + 18.0f;
        if (melee && inReach && foe != boss && !bodyNear && worthStriking && !holdFire) {
            dir = 0;
            b.goalBest = 0.0f;
            b.lost = 0;
            const bool facing = (d.x >= 0.0f) == (me.facing >= 0.0f) || std::fabs(d.x) < 3.0f;
            if (!facing) dir = d.x > 0.0f ? 1 : -1;
            else if (b.attackCool <= 0 && (!b.sync || b.syncGroup > 0 || syncReady_) && noise(seat + 8 * static_cast<int>(seed_), tick_) < lv.precision + 0.1f) {
                held |= 1u << kAttack;
                if (d.y < -30.0f) {
                    held |= 1u << kUp;  // an upward strike
                    f.moveY = -127;
                }
                b.attackCool = 22;
            }
            if (d.y < -30.0f && me.grounded() && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump))) b.jumpHold = 12;
        } else if (melee && !inReach && aloft && foe != boss && worthStriking && !holdFire && me.grounded() && std::fabs(d.x) <= fb.w * 0.5f + 22.0f &&
                   -d.y <= me.config().jumpHeight + me.config().doubleJumpHeight + fb.h * 0.5f && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump))) {
            // 2.85: no wave (or no energy for one) and the creature over its head: a jump (a double jump if it must), the
            // blade swung up once in reach.
            dir = 0;
            b.jumpHold = 40;
            b.airJump = -d.y <= me.config().jumpHeight + fb.h * 0.5f - 6.0f;
            b.hop = b.airJump ? 0 : 2;
        } else if (sh && keyBit(sh) >= 0 && worthStriking && !holdFire) {
            // The wave, aimed: from its place, or on the way when a direction reaches it now.
            const bool atSpot = b.spot >= 0 && nav && std::fabs(nav->node(b.spot).feet.x - feet.x) < 10.0f && std::fabs(nav->node(b.spot).feet.y - feet.y) < 12.0f;
            int aim = shotFrom(w, me.center(), fb, me.grounded());
            const bool ready = sh->ready(me) && me.energy >= (shield ? 32.0f : 10.0f);
            if (atSpot && me.grounded()) {
                dir = 0;
                b.goalBest = 0.0f;
                b.lost = 0;
                if (b.jumpShot && aim < 0) {
                    if (ready && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump))) {
                        b.jumpHold = 40;  // up: the double jump at the top, the shot at the top of that one
                        b.airJump = false;
                    }
                } else if (aim < 0) {
                    b.spotAt = -100000;  // it moved: another place
                }
            }
            if (b.jumpShot && !me.grounded() && !b.airJump && me.vel.y > -40.0f && !(b.prevHeld & (1u << kJump)) && b.spot >= 0 &&
                std::fabs(nav->node(b.spot).feet.x - feet.x) < 24.0f) {
                b.airJump = true;
                held |= 1u << kJump;
                b.jumpHold = 14;
            }
            // The energy kept: its shield (20), and outside the weak points a reserve for them.
            const bool burst = view_.exposed || b.syncGroup > 0 || !fight_;
            const float reserve = (shield ? 22.0f : 0.0f) + (burst ? 0.0f : 10.0f);
            // 2.84: nothing reaches it from where it stands, but from the top of a jump (or of its double jump) a wave
            // does: it jumps and fires up there, wherever it is (its place or on its way), as a player does — rather
            // than waiting, its energy full, to be at its place.
            if ((fight_ || aloft) && aim < 0 && me.grounded() && ready && me.energy - 10.0f >= reserve && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump)) &&
                !holding && b.task == Task::Fight && !navi_.pilot(seat).committed()) {
                const float h1 = me.config().jumpHeight - 6.0f, h2 = me.config().jumpHeight + me.config().doubleJumpHeight - 8.0f;
                if (shotFrom(w, me.center() - Vec2{0.0f, h1}, fb, false) >= 0) {
                    b.jumpHold = 40;
                    b.airJump = true;
                    b.hop = 1;
                } else if (shotFrom(w, me.center() - Vec2{0.0f, h2}, fb, false) >= 0) {
                    b.jumpHold = 40;
                    b.airJump = false;
                    b.hop = 2;
                }
            }
            if (b.hop == 2 && !me.grounded() && !b.airJump && me.vel.y > -40.0f && !(b.prevHeld & (1u << kJump))) {
                b.airJump = true;
                held |= 1u << kJump;
                b.jumpHold = 14;
            }
            // Its charged shot (the armory's), on a weak point bare: held still at its place, then let go aimed (4 damage,
            // through three targets) — the wave meanwhile waits.
            const Power* charge = powerOf(me, "charge_shot");
            const bool weakBare = view_.exposed && foe != boss;
            if (charge && weakBare && atSpot && me.grounded() && aim >= 0 && me.energy - 20.0f >= reserve && !pilotDodging) {
                dir = 0;
                if (b.chargeHold < static_cast<int>(0.9f * static_cast<float>(kSimHz))) {
                    held |= 1u << kAttack;  // charging (Attack held)
                    ++b.chargeHold;
                    react = "charge son tir sur le point faible";
                } else {
                    // Let go, aimed: the shot leaves with the stick's direction of this step.
                    f.moveX = static_cast<int8_t>(kDirX[aim] * 127);
                    f.moveY = static_cast<int8_t>(kDirY[aim] * 127);
                    b.chargeHold = 0;
                    fired = true;
                    react = "tir chargé sur le point faible (× 2)";
                }
            } else {
                b.chargeHold = 0;
            }
            if (!fired && b.chargeHold == 0 && aim >= 0 && !spoils(aim) && alongDodge(aim) && sh->ready(me) && me.energy - 10.0f >= reserve && !(b.prevHeld & (1u << keyBit(sh))) &&
                noise(seat + 8 * static_cast<int>(seed_), tick_ + 7) < lv.precision + 0.1f) {
                // A Beginner's aim slips a notch now and then (its precision).
                if (noise(seat + 8 * static_cast<int>(seed_), tick_ + 53) > lv.precision + 0.25f) aim = (aim + 1) % 8;
                held |= 1u << keyBit(sh);
                f.moveX = static_cast<int8_t>(kDirX[aim] * 127);
                f.moveY = static_cast<int8_t>(kDirY[aim] * 127);
                if (kDirX[aim] == 0) dir = 0;
                fired = true;
            }
        }
    }
    // 2.84 (fight plan): getting out of a blow, it still fires when a wave reaches its target from where it is (a step of
    // its run turned into a shot, as a player does) — more of its time spent striking.
    if (fight_ && dodging && !fired && !fleeing && !nailed && foe && worthStriking && !holdFire && sh && keyBit(sh) >= 0 && view_.plan.fireOnTheRun) {
        const int aim = shotFrom(w, me.center(), fb, me.grounded());
        const float reserve = (shield ? 22.0f : 0.0f) + (view_.exposed ? 0.0f : 10.0f);
        if (aim >= 0 && kDirX[aim] != 0 && !spoils(aim) && alongDodge(aim) && sh->ready(me) && me.energy - 10.0f >= reserve && !(b.prevHeld & (1u << keyBit(sh)))) {
            held |= 1u << keyBit(sh);
            f.moveX = static_cast<int8_t>(kDirX[aim] * 127);
            f.moveY = static_cast<int8_t>(kDirY[aim] * 127);
            fired = true;
        }
    }
    if (b.act == Shoot && !fleeing && !dodging) {
        // A generator: from its place, facing it, the wave (or the boomerang).
        LevelEntity* e = w.entity(b.thing);
        if (e && sh && keyBit(sh) >= 0 && nav && b.goalNode >= 0 && b.path.size() >= 1 && b.at + 1 >= b.path.size() && me.grounded()) {
            const float dx = e->box.center().x - me.center().x;
            dir = 0;
            if ((dx >= 0.0f) != (me.facing >= 0.0f)) dir = dx > 0.0f ? 1 : -1;
            else if (sh->ready(me) && !(b.prevHeld & (1u << keyBit(sh))) && noise(seat + 8 * static_cast<int>(seed_), tick_ + 13) < lv.precision + 0.1f) held |= 1u << keyBit(sh);
        }
    }
    // 2.84: an orb hanging in the air (over a floor, a platform): under it, a jump (or a double jump) to take it.
    // 2.85: a relic too (the objective of a secret zone, « emporter la relique »: up on its pedestal, a jump above the floor).
    if (b.task == Task::Objective && b.act == Enter && !fleeing && me.grounded())
        if (const LevelEntity* e = w.entity(b.thing); e && (e->type() == "health_orb" || e->type() == "relic") && std::fabs(e->box.center().x - me.center().x) < 12.0f &&
                                                     e->box.bottom() < me.box.y && e->box.bottom() > me.box.y - 110.0f && b.jumpHold <= 0 &&
                                                     !(b.prevHeld & (1u << kJump))) {
            b.jumpHold = 46;
            b.airJump = e->box.bottom() > me.box.y - me.config().jumpHeight + 4.0f;  // a jump is enough: no double jump
            b.hop = b.airJump ? 0 : 2;
        }
    // 2.85: out of a boss fight, its double jump at the top of a jump that needs it (a creature over its head, an orb, a relic).
    if (!fight_ && !fleeing && b.hop == 2 && !me.grounded() && !b.airJump && me.vel.y > -40.0f && !(b.prevHeld & (1u << kJump))) {
        b.airJump = true;
        held |= 1u << kJump;
        b.jumpHold = 14;
    }
    if (b.act == Interact && !fleeing) {
        if (LevelEntity* e = w.entity(b.thing); e && e->box.expanded(8.0f).intersects(me.box)) {
            dir = 0;
            if (!(b.prevHeld & (1u << kInteract))) held |= 1u << kInteract;
        }
    }
    if (b.task == Task::Revive && !fleeing && std::fabs(b.goal.x - feet.x) <= GameWorld::kReviveReachX - 6.0f &&
        std::fabs(b.goal.y - feet.y) <= GameWorld::kReviveReachY - 6.0f) {
        // 2.77: a revival is held (« Interagir » kept down for 2 s near the soul). 2.79: when it is safe — nothing
        // coming on it or the soul for that long — else it waits beside it under its shield; its decoy draws the
        // boss's blows away while it kneels.
        const Rect around = me.box.expanded(10.0f);
        bool unsafe = threatened(w, seat, around, 40) && !(me.shieldActive() || shieldReady);
        // 2.84: near the boss or with blows on their way, it kneels under its shield (2,2 s: the whole revival), never
        // bare; its shield not ready, it waits beside the soul for what is close to pass.
        if (fight_ && lv.shieldSense >= 1.0f && !me.shieldActive() &&
            (threatened(w, seat, around, 2 * kSimHz) || (bossUid_ && around.expanded(160.0f).intersects(bossBody_)))) {
            if (shieldReady && me.energy >= 20.0f) raiseShield = true;
            else unsafe = threatened(w, seat, around, 60);
        }
        if (!unsafe) {
            dir = 0;
            held |= 1u << kInteract;
            const int down = static_cast<int>(b.revivee) - 1;
            if (const Power* dc = powerOf(me, "decoy"); dc && fight_ && keyBit(dc) >= 0 && dc->ready(me) && !(b.prevHeld & (1u << keyBit(dc))) &&
                                                       down >= 0 && down < w.teamSize() && w.member(down).revive < 0.1f)
                held |= 1u << keyBit(dc);
            react = "réanime " + nameOf(static_cast<int>(b.revivee) - 1);
        } else {
            react = "attend que ce soit sûr pour réanimer";
            raiseShield = raiseShield || threatened(w, seat, around, 12);
        }
    }
    if (b.act == Stand && !fleeing) {
        const Rect target = b.task == Task::Mechanism ? b.mechanism : Rect{b.goal.x - 8.0f, b.goal.y - 24.0f, 16.0f, 26.0f};
        if (target.w > 0.0f && target.intersects(me.box) && me.grounded() && std::fabs(b.goal.x - feet.x) < 10.0f && !dodging) dir = 0;
    }
    // ------------------------------------------------------------ 2.84: the shield before any blow about to land on it
    // What a good player does in a fight: whatever is about to touch it in the next sixth of a second (a mark, a shot, a
    // beam, the boss's body or its blow), the shield goes up — not only against a blow it cannot leave any more. Never
    // while its last hit still protects it (wasted), nor for what it does not stop (a well's pull, a sector, a flood).
    // 2.90 (the escape): never during the guard after a dash either (the blows miss it: wasted — the other Gigaboss keep their
    // rule of before, unmeasured with it); a blow on an area it is getting out of in time (the navigation's dodge, clear of
    // every blow) is no reason for its shield.
    const bool safeDodge = escapeMode && navi_.pilot(seat).safeDodge();
    if (escHit && escHit->in <= 10 && !safeDodge && !me.blowGuarded()) {
        raiseShield = true;  // no way out clear of it: the shield as it lands
        if (react.empty()) react = "lève son bouclier";
    }
    if (fight_ && shieldReady && !raiseShield && !me.invulnerable() && !(escapeMode && me.blowGuarded()) && lv.shieldSense >= 1.0f &&
        !(b.task == Task::Mechanism && !holding && me.health > 3)) {
        const Rect near = me.box.expanded(8.0f);
        // 2.84 (fight plan): its shield kept for one blow — except against the boss's own body and blows (its touch).
        const bool reserve = view_.plan.reserveShield && me.health > 4;
        // Its touch first, unless a sweep is on its way (announced) or it holds its place (no storey to change to).
        bool sweepDue = holding;
        for (const Danger& d : dangers_) sweepDue = sweepDue || d.kind == Danger::Kind::Storey || (d.shieldFirst && d.in > 0);
        for (const Danger& d : dangers_) {
            if (reserve && (sweepDue || (!d.body && !d.attack))) continue;
            if (safeDodge && d.kind == Danger::Kind::Area && !d.body) continue;  // 2.90: the dodge under way gets it out in time
            if ((d.seat >= 0 && d.seat != seat) || d.in > (d.attack ? kSimHz / 4 : 20)) continue;  // its own blows: from their wind-up
            bool hit = false;
            switch (d.kind) {
                case Danger::Kind::Area: hit = d.area.intersects(near) && !d.unblockable; break;
                case Danger::Kind::Line: hit = hitsLine(d, near) && (d.turn == 0.0f || sweepIn(w, d, near) <= 20); break;
                case Danger::Kind::Pull: hit = d.area.intersects(near); break;  // 2.84: dragged to its core: its touch under the shield
                case Danger::Kind::Flood: hit = d.area.intersects(me.box) && fleeing; break;  // 2.84: caught by the liquid on its way up
                case Danger::Kind::FloorWave: {
                    if (!me.grounded() || feet.y > d.floorY + 6.0f || feet.y < d.floorY - 30.0f || d.speed == 0.0f) break;
                    const float gap = d.speed > 0.0f ? me.box.x - d.area.right() : d.area.x - me.box.right();
                    hit = gap > -(d.area.w + me.box.w) && std::max(0.0f, gap) / std::fabs(d.speed) < 0.1f && b.jumpHold <= 0;
                    break;
                }
                default: break;
            }
            if (hit) {
                raiseShield = true;
                if (react.empty()) react = "lève son bouclier";
                break;
            }
        }
    }
    // ------------------------------------------------------------ the shield (its level's choice), when a blow cannot be avoided
    // 2.90 (the escape): a player uses one power a step (Player::handlePowers), none while it dashes: the shield is pressed alone
    // (a shot pressed with it took its place, and the shield waited 30 steps more), never during a dash (its guard covers it).
    const bool shieldAlone = escapeMode && (dashNow || me.dashing());
    if (raiseShield && shieldReady && !shieldAlone && (b.shieldCool <= 0 || react == "bouclier contre la lame") && me.energy >= 20.0f &&
        noise(seat + 8 * static_cast<int>(seed_), tick_ / 12 + 77) < lv.shieldSense) {
        if (escapeMode)
            for (const auto& pw : me.powers())
                if (const int k = keyBit(pw.get()); k >= 0 && pw.get() != shield) held &= ~(1u << k);
        held |= 1u << keyBit(shield);
        b.shieldCool = 30;
        if (react.empty() || react == "esquive") react = "lève son bouclier";
    }
    // ------------------------------------------------------------ 2.90: the dash as the blow lands (its guard covers it)
    // The escape: a blow about to land on it (in three steps or less) that it is still under, its shield not raised now — the
    // dash, its guard (its duration + 0,15 s: 0,31 s) covering the blow (a fist strikes a quarter of a second, a crack bursts
    // three tenths, a wave passes in a tenth), the way that does not end in another blow nor against the boss's body. The
    // shield (5 s to come back) and the dash (0,45 s, 5 of energy) take turns: never both spent at once.
    bool dashGuard = false;
    if (escapeMode && dashPower && keyBit(dashPower) >= 0 && !(held & (1u << keyBit(shield ? shield : dashPower))) && !me.shieldActive() &&
        !me.blowGuarded() && !me.invulnerable() && !me.dashing() && dashPower->ready(me) && !(b.prevHeld & (1u << keyBit(dashPower))) &&
        me.energy >= 5.0f && navi_.pilot(seat).dashIn() < 0 && !nailed && !holding) {
        const Rect mine = me.box.expanded(3.0f);
        const Danger* soon = nullptr;
        for (const Danger& d : dangers_) {
            if ((d.seat >= 0 && d.seat != seat) || d.body || d.unblockable || d.in > 3) continue;
            if (d.kind == Danger::Kind::Area && d.last >= 0 && d.last <= 40 && d.area.intersects(mine)) {
                soon = &d;
                break;
            }
            if (d.kind == Danger::Kind::FloorWave && me.grounded() && d.speed != 0.0f && feet.y <= d.floorY + 6.0f && feet.y >= d.floorY - 30.0f &&
                b.jumpHold <= 0) {
                const float gap = d.speed > 0.0f ? me.box.x - d.area.right() : d.area.x - me.box.right();
                if (gap > -(d.area.w + me.box.w) && std::max(0.0f, gap) / std::fabs(d.speed) + static_cast<float>(d.in) / static_cast<float>(kSimHz) < 0.03f) {
                    soon = &d;
                    break;
                }
            }
        }
        if (soon) {
            // Its way: where it lands 70 px on is clear of every blow due soon and of the boss's body (else the other way).
            const auto clearAt = [&](float x) {
                const Rect at = bodyAt({x, feet.y}).expanded(4.0f);
                if (w.physics().raycast(me.center(), {x, me.center().y}).has_value()) return false;  // a wall
                for (const Danger& d : dangers_) {
                    if ((d.seat >= 0 && d.seat != seat) || d.in > 50) continue;
                    if ((d.kind == Danger::Kind::Area || d.kind == Danger::Kind::Pull) && d.area.intersects(at) && (d.body || d.last < 0 || d.in + d.last > 30)) return false;
                }
                return true;
            };
            const float away = soon->kind == Danger::Kind::FloorWave ? -soon->speed : me.center().x - soon->area.center().x;
            int side = away >= 0.0f ? 1 : -1;
            if (!clearAt(feet.x + 70.0f * static_cast<float>(side)) && clearAt(feet.x - 70.0f * static_cast<float>(side))) side = -side;
            for (const auto& pw : me.powers())
                if (const int k = keyBit(pw.get()); k >= 0) held &= ~(1u << k);
            held |= 1u << keyBit(dashPower);
            dir = side;
            fired = false;
            f.moveY = 0;
            dashGuard = true;
            ++b.dashGuards;
            react = "ruée sous le coup";
        }
    }
    // 2.79: a move through the air under way is played as it was simulated (nothing changes its direction).
    // 2.90: and a dodge of the escape (one authority: the navigation's way out, kept to its end).
    if (!dashGuard && (navi_.pilot(seat).committed() || (escapeMode && navi_.pilot(seat).safeDodge() && !nailed))) dir = pilotDir;
    // 2.79: never into a passage (it takes the whole group away) unless it is leaving the zone: it stops in
    // time (braking against its run).
    if (!(b.task == Task::Exit && b.act == Enter)) {
        const float stopDist = me.vel.x * std::fabs(me.vel.x) / (2.0f * me.config().turnAcceleration);
        const float reach = static_cast<float>(dir) * 3.0f + stopDist + (me.vel.x > 1.0f ? 3.0f : me.vel.x < -1.0f ? -3.0f : 0.0f);
        for (const Passage& p : run.rules().passages)
            if (!p.rect.intersects(me.box) && p.rect.intersects(me.box.translated({reach, 0.0f}))) {
                dir = me.vel.x > 1.0f ? -1 : me.vel.x < -1.0f ? 1 : 0;
                break;
            }
    }
    // Lost on the way (no nearer to its goal): counted for the rescue of a test (TestSession).
    const float gd = (b.goal - feet).length();
    if (b.goalBest <= 0.0f && gd >= 24.0f) b.goalBest = gd;  // left its fight: measured again from here
    if (gd < b.goalBest - 8.0f || gd < 24.0f || b.task == Task::Follow || b.task == Task::Wait || dodging || pilotDodging) {
        b.goalBest = gd;
        b.lost = 0;
    } else {
        ++b.lost;
    }
    if (dropThrough) {
        held |= (1u << kDown) | (1u << kJump);
        f.moveY = 127;
        dir = 0;
    }
    if (dir > 0) held |= 1u << kRight;
    if (dir < 0) held |= 1u << kLeft;
    if (dir != 0 && !fired) f.moveX = static_cast<int8_t>(dir * 127);
    // Stuck against something while wanting to move.
    if (dir != 0 && std::fabs(me.box.x - b.lastX) < 0.25f) ++b.still;
    else b.still = 0;
    b.lastX = me.box.x;
    if (pilotDodging && b.waveJumpCool <= 0 && b.beamJump <= 0) b.jumpHold = 0;  // 2.90: its track was simulated without it
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
    // Its thought: what it reacts to now, else its task; how it moves.
    b.thought.role = b.role;
    b.thought.intent = react.empty() ? b.intent : react;
    b.thought.move = dir == 0 && b.jumpHold <= 0 ? std::string() : b.moveText;
    return f;
}

}  // namespace cosmic::raid
