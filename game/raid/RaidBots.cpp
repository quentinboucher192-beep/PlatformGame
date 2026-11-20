#include "game/raid/RaidBots.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "engine/physics/Collision.h"
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
Rect bodyBox(const Enemy& e, const GameWorld& w) {
    Rect out = e.box;
    if (!fightsByParts(e, w)) return out;
    const AnimationSetPtr& set = e.animator().set();
    if (!set || set->frameWidth <= 0 || set->frameHeight <= 0) return out;
    const HitPartsSheet* sheet = HitPartsSheet::of(set->sourcePath);
    if (!sheet) return out;
    const Rect src = e.animator().sourceRect();
    const std::vector<HitPartPiece>* pieces = sheet->cell(static_cast<int>(src.y) / set->frameHeight, static_cast<int>(src.x) / set->frameWidth);
    if (!pieces) return out;
    for (const HitPartPiece& piece : *pieces) {
        const std::vector<Vec2> poly = placePiece(piece, *sheet, e.box.bottomCenter(), e.def().scale, e.facing < 0.0f);
        for (const Vec2& q : poly) {
            const float l = std::min(out.x, q.x), t = std::min(out.y, q.y);
            out = {l, t, std::max(out.right(), q.x) - l, std::max(out.bottom(), q.y) - t};
        }
    }
    return out;
}

const Enemy* enemyOf(const GameWorld& w, uint32_t uid) {
    if (uid == 0) return nullptr;
    const Enemy* e = w.findEnemy(uid);
    return e && e->alive() && !e->dormant() ? e : nullptr;
}

std::string shortLabel(const std::string& s) { return s.substr(0, s.find(" (")); }

Rect bodyAt(Vec2 feet) { return {feet.x - 5.0f, feet.y - 22.0f, 10.0f, 22.0f}; }

bool hitsLine(const Danger& d, const Rect& box) { return Collision::segmentIntersectsRect(d.from, d.to, box.expanded(d.width * 0.5f)); }

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
    return mix(h, static_cast<uint64_t>(skill_));
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
    if (here == 0) {
        // The armory: the way to the start zone.
        for (size_t k = 0; k < run.rules().passages.size(); ++k)
            if (run.passageOpen(run.rules().passages[k])) {
                if (toZone) *toZone = run.rules().passages[k].to;
                return static_cast<int>(k);
            }
        return -1;
    }
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

void TestBots::ensureNav(const RaidRun& run) {
    // 2.79: the graph of the zone (game/raid/nav): asked for when another world is loaded or a door opened
    // or closed, built in the background, used from a step fixed by the zone; the copy of the zone and the
    // blows announced this step, as its tactic reads them (the shots and the beams are read by the
    // navigation itself, with their paths; the waves along a floor are jumped by drive()).
    std::vector<Rect> blows;
    for (const bots::Danger& d : dangers_) {
        if (d.shot || d.body) continue;
        switch (d.kind) {
            case bots::Danger::Kind::Area:
            case bots::Danger::Kind::Pull:
            case bots::Danger::Kind::Flood:
            case bots::Danger::Kind::Sector: blows.push_back(d.area); break;
            default: break;
        }
    }
    navi_.update(run, tick_, blows);
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
        tactic_->read(run, view_);
        bossUid_ = boss->uid();
        // Its body hurts on contact (every part of it): a lasting area to keep out of.
        Danger body;
        body.kind = Danger::Kind::Area;
        bossBody_ = bodyBox(*boss, w);
        body.area = bossBody_.expanded(6.0f);
        body.body = true;
        view_.dangers.push_back(body);
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
    dangers_ = view_.dangers;
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
    for (int pass = 0; pass < 2; ++pass) {
        auto ok = [&](int i) {
            const NavGraph::Node& nd = nav->node(i);
            if (nd.mover >= 0 || nd.ladder) return false;
            if (std::fabs(nd.feet.x - tc.x) > kWaveRange + 20.0f || std::fabs(nd.feet.y - 11.0f - tc.y) > kWaveRange + 20.0f) return false;
            const Vec2 c{nd.feet.x, nd.feet.y - 11.0f};
            const float fromTarget = (c - tc).length();
            if (pass == 0) {
                if (b.role == Role::Lure ? fromTarget > 210.0f : fromTarget < 130.0f) return false;
                for (int o = 0; o < static_cast<int>(bots_.size()); ++o)
                    if (o != seat && bots_[static_cast<size_t>(o)].spot >= 0 && ((seats_ >> o) & 1u) &&
                        (nav->node(bots_[static_cast<size_t>(o)].spot).feet - nd.feet).lengthSq() < 52.0f * 52.0f)
                        return false;  // another's place
            }
            if (body && bodyAt(nd.feet).intersects(bossBox.expanded(clearance))) return false;
            // On its side of the boss's body (under its top): the way to the other side goes through it.
            if (body && nd.feet.y > bossBox.y && nd.feet.y - 22.0f < bossBox.bottom() &&
                (me.center().x < bossBox.center().x ? nd.feet.x > bossBox.x : nd.feet.x < bossBox.right()))
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
                    if (Collision::segmentIntersectsRect(m, m + v * kWaveRange, target.expanded(5.0f)) && shotReaches(w, from, k, target)) {
                        d = k;
                        high = lift != 0;
                        break;
                    }
                }
            }
            if (d < 0 || !safeSpot(w, seat, nd.feet, 90)) return false;
            dir = d;
            jump = high;
            return true;
        };
        std::vector<int> path;
        const int found = nav->search(here, ok, 5000.0f, &path);
        if (found < 0) continue;
        b.spot = found;
        b.spotDir = dir;
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
    if (tick_ % 15 == 0 || newZone || navi_.graphChanged()) plan(run, seats, adminActive);
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
        if ((seats >> s) & 1u) frames[s] = drive(run, s, bots_[static_cast<size_t>(s)]);
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
    const int wantLure = fight && boss && n >= 4 ? 1 : 0;
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
    for (int s = 0; s < n; ++s)
        if ((seats >> s) & 1u) {
            Bot& b = bots_[static_cast<size_t>(s)];
            const Task was = b.task;
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
            b.revivee = 0;
            b.thought.order = false;
            if (wasTarget == 0) b.spot = -1;
            if (was == Task::Flee) b.task = Task::Flee;  // it finishes sheltering (drive)
            if (playing(w, s)) drivers.push_back(s);
        }
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
        // « Réanimez-le »: the soul nearest to the place given first, the two nearest players on it.
        if (ord == bots::OrderKind::Revive)
            std::stable_sort(down.begin(), down.end(), [&](int a, int c) {
                return (w.member(a).soul - order_.at).lengthSq() < (w.member(c).soul - order_.at).lengthSq();
            });
        bool first = true;
        for (int d : down) {
            const Vec2 soul = w.member(d).soul;
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
    for (int s : drivers) {
        if (busy[static_cast<size_t>(s)] || ord == bots::OrderKind::Follow || ord == bots::OrderKind::Wait) continue;
        const Player& p = w.playerAt(s);
        if (p.health * 10 > p.maxHealth() * 4) continue;
        if (bots_[static_cast<size_t>(s)].role == Role::Runner && !view_.stations.empty()) continue;  // its plate first
        const LevelEntity* orb = nullptr;
        float bd = 700.0f * 700.0f;
        for (const auto& e : w.entities())
            if (!e->removed && e->active && e->visible && e->type() == "health_orb" && (e->box.center() - p.center()).lengthSq() < bd &&
                safeSpot(w, s, e->box.bottomCenter(), 60)) {
                bd = (e->box.center() - p.center()).lengthSq();
                orb = e.get();
            }
        if (!orb) continue;
        give(s, Task::Objective, orb->box.bottomCenter(), Enter, 0, orb->id());
        bots_[static_cast<size_t>(s)].goalNode = standNode(orb->box.expanded(4.0f));
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
        for (const bots::Station* st : stations) {
            int need = std::max(0, st->need - heldByOthers(st->plate));
            while (need-- > 0) {
                int s = nearestFree(st->plate.bottomCenter(), [&](int seat) { return bots_[static_cast<size_t>(seat)].role == Role::Runner; });
                if (s < 0) s = nearestFree(st->plate.bottomCenter(), [&](int seat) { return bots_[static_cast<size_t>(seat)].role != Role::Reviver; });
                if (s < 0) s = nearestFree(st->plate.bottomCenter());
                if (s < 0) break;
                give(s, Task::Mechanism, {st->plate.center().x, st->plate.bottom()}, Stand);
                Bot& b = bots_[static_cast<size_t>(s)];
                b.mechanism = st->plate;
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
                int s = nearestFree(e->center(), [&](int seat) { return bots_[static_cast<size_t>(seat)].role == Role::Striker; });
                if (s < 0 && (t->priority >= 20 || allIn))
                    s = nearestFree(e->center(), [&](int seat) { return bots_[static_cast<size_t>(seat)].role != Role::Reviver || allIn; });
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
                        const float d2 = (e->center() - w.playerAt(s).center()).lengthSq();
                        if (d2 < bd && (d2 < 360.0f * 360.0f || !body)) {
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
                else if (!view_.bodyTakesDamage && target == boss->uid()) think(b, std::string("garde son énergie pour les points faibles"));
                else think(b, (view_.exposed && target == boss->uid() ? "frappe " + what + " à nu (× 2)" : "frappe " + what));
            }
        said.push_back(view_.phase.empty() ? "Combat de " + bossName : view_.phase);
    }
    // ------------------------------------------------------------------ the armory's tablets
    if (run.zone() == 0)
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
            int sent = 0;
            for (size_t g : group) {
                const RaidRun::CoopView v = run.coop(g);
                const Rect r = run.rules().coops[g].rect;
                int need = std::max(0, v.need - heldByOthers(r));
                for (int slot = 0; need > 0; ++slot) {
                    int s = nearestFree(r.bottomCenter(), [&](int seat) { return bots_[static_cast<size_t>(seat)].role == Role::Runner; });
                    if (s < 0) s = nearestFree(r.bottomCenter());
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
    if (!fight_ && !platesLeft && !jobs && freeCount() > 0) {
        int to = 0;
        const int exit = chooseExit(run, &to);
        if (exit >= 0) {
            const Passage& p = run.rules().passages[static_cast<size_t>(exit)];
            // The administrator there and playing: they lead the way out (the group waits near it);
            // away or AFK: the simulated players go on alone.
            const bool wait = adminActive && !((seats >> 0) & 1u) && playing(w, 0);
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
        }
    }
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
    plan.mayLeave = b.task == Task::Exit && b.act == Enter;
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
    if (navi_.pilot(seat).committed()) b.jumpHold = 0;
}

InputFrame TestBots::drive(RaidRun& run, int seat, Bot& b) {
    InputFrame f;
    GameWorld& w = run.world();
    if (!playing(w, seat)) {
        b.prevHeld = 0;
        b.jumpHold = 0;
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
    const Power* shield = powerOf(me, "shield");
    const bool shieldReady = shield && keyBit(shield) >= 0 && shield->ready(me) && !(b.prevHeld & (1u << keyBit(shield))) && !me.shieldActive();
    bool raiseShield = false;
    const bool holding = b.task == Task::Mechanism && b.mechanism.w > 0.0f && b.mechanism.intersects(me.box);
    b.onStation = holding;
    // ------------------------------------------------------------ rising liquids: above their top
    bool fleeing = false;
    Rect flood;
    for (const Danger& d : dangers_)
        if (d.kind == Danger::Kind::Flood && (flood.w <= 0.0f || d.area.w > flood.w) && d.area.x <= feet.x + 6.0f && d.area.right() >= feet.x - 6.0f) flood = d.area;
    if (flood.w <= 0.0f)
        for (const Danger& d : dangers_)
            if (d.kind == Danger::Kind::Area && d.area.w > 260.0f && d.in < kSimHz && d.area.expanded(6.0f).intersects(me.box) &&
                (!fight_ || d.area.h < 120.0f) && (flood.w <= 0.0f || d.area.w > flood.w))
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
    // ------------------------------------------------------------ a sector whose gravity turns over (Graviton)
    int forced = 0;  // a side it must go to this step (a reaction), 0 none
    bool stay = false;  // it holds where it is this step (sheltered)
    if (!fleeing && !holding)
        for (const Danger& d : dangers_) {
            if (d.kind != Danger::Kind::Sector || d.in > 3 * kSimHz || !d.area.contains(me.center())) continue;
            if (d.in == 0) {
                // Turned over: too late to move (it rises); under a slab, it stays there.
                for (const Rect& s : view_.shelters) stay = stay || (feet.x > s.x && feet.x < s.right() && s.y < feet.y);
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
            } else {
                react = "s'abrite sous une dalle";
                stay = true;
            }
            break;
        }
    // ------------------------------------------------------------ a flash coming (Skullmaw): hide behind a column
    bool hiding = false;
    if (!fleeing && !forced && view_.flash && !holding && lv.hides && nav) {
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
            b.goal = feet;
            b.goalNode = -1;
            b.path.clear();
            hiding = true;
        }
    } else {
        b.hideNode = -1;
    }
    // ------------------------------------------------------------ its target: where to strike it from
    const Enemy* foe = enemyOf(w, b.target);
    const Enemy* boss = fight_ ? enemyOf(w, bossUid_) : nullptr;
    const Rect fb = foe ? strikeBox(*foe, w) : Rect{};
    const Vec2 fc = fb.center();
    const Power* sh = shooter(me);
    const bool bossFight = fight_ && boss;
    const bool ranged = foe && sh && (b.ranged || foe->def().isBoss || fb.w >= 56.0f || fb.h >= 72.0f || bossFight);
    // Its body takes nothing now (armoured): keeps its energy, never strikes it (the lure keeps it busy).
    const bool worthStriking = foe && !(boss && foe == boss && !view_.bodyTakesDamage);
    // A target of a group finished together: brought low, then held there until every one of them is.
    const bool holdFire = foe && b.syncGroup > 0 && b.syncGroup < 4 && !syncLow_[static_cast<size_t>(b.syncGroup)] &&
                          foe->hpRatio() <= b.holdAt + 0.001f;
    if (!fleeing && !hiding && foe && !forced) {
        if (ranged) {
            // A place of the graph that sees it, out of its body and of what is coming (again every half second,
            // or when that place no longer sees it).
            const bool stale = b.spot < 0 || static_cast<int>(tick_) - b.spotAt > 60 ||
                               (nav && b.spot >= 0 && !safeSpot(w, seat, nav->node(b.spot).feet, 60));
            if (stale && static_cast<int>(tick_) - b.spotAt > 12) chooseSpot(w, seat, b, fb, boss);
            if (b.spot < 0) {
                // No place sees it: near it, out of its body, each one its own place (never all on one pixel).
                const float side = fc.x < me.center().x ? 1.0f : -1.0f;
                b.goal = {fc.x + side * (fb.w * 0.5f + 120.0f + 34.0f * static_cast<float>(seat % 4)), std::max(fb.bottom(), feet.y)};
                if (nav && (b.goalNode < 0 || (nav->node(b.goalNode).feet - b.goal).lengthSq() > 64.0f * 64.0f)) b.goalNode = nav->locate(b.goal, 200.0f);
            }
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
    const bool staysPut = b.task == Task::Revive && std::fabs(b.goal.x - feet.x) <= GameWorld::kReviveReachX - 6.0f &&
                          std::fabs(b.goal.y - feet.y) <= GameWorld::kReviveReachY - 6.0f;
    if (!fleeing && !forced) {
        // The most pressing blow on it: the one whose time left is the shortest against the time it needs to get
        // out (its way out at a run, its level's reaction), the blows still far off left for later.
        const Danger* hit = nullptr;
        float urgency = 1e30f;
        const Rect mine = me.box.expanded(5.0f);
        for (const Danger& d : dangers_) {
            if ((d.seat >= 0 && d.seat != seat) || d.in > 3 * kSimHz) continue;
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
            if (holding || staysPut || (shot && shieldReady)) {
                if (hit->in <= 14) raiseShield = true;
            } else if (seen && noise(seat, tick_ / 30) < std::max(0.55f, lv.precision)) {
                int side = 0;
                if (hit->kind == Danger::Kind::Line) {
                    const Vec2 a = hit->from, c = hit->to;
                    const Vec2 ab = c - a;
                    const float len2 = std::max(1.0f, ab.lengthSq());
                    const float t = std::clamp(((me.center() - a).x * ab.x + (me.center() - a).y * ab.y) / len2, 0.0f, 1.0f);
                    const Vec2 near = a + ab * t;
                    const Vec2 away = me.center() - near;
                    if (std::fabs(ab.x) > std::fabs(ab.y) * 2.0f) {
                        // A beam lying along its floor: over it (jump) or under it (crouch).
                        if (away.y <= 0.0f && me.grounded() && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump))) b.jumpHold = 26;
                        else if (away.y > 0.0f) held |= 1u << kDown;
                    } else {
                        side = std::fabs(away.x) > 1.0f ? (away.x > 0.0f ? 1 : -1) : (me.center().x >= a.x ? 1 : -1);
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
                            if (!safeSpot(w, seat, at, hit->in + 40)) continue;
                            if (nav && nav->locate(at, 12.0f) < 0) continue;  // a lasting danger there (lava, spikes): no place
                            dist[sgn > 0 ? 1 : 0] = dx + 12.0f;  // a little beyond its edge
                            break;
                        }
                    if (dist[0] < 1e29f || dist[1] < 1e29f) side = dist[0] <= dist[1] ? -1 : 1;
                    // The way it already took, while it still leads out not much further than the other (no dithering);
                    // never a way that leads nowhere (a wall, an edge, more blows).
                    if (b.escapeSteps > 0 && b.escapeDir != 0 && dist[b.escapeDir > 0 ? 1 : 0] <= std::min(dist[0], dist[1]) * 1.5f + 16.0f)
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
            } else if (hit->in <= 10) {
                raiseShield = true;
            }
            // About to land on it and not out yet: the shield (it takes everything for 2,2 s), whatever its way out.
            if (hit->in <= 10 && !hit->body && hit->kind != Danger::Kind::Pull) raiseShield = true;
        } else {
            b.react = 0;
        }
    }
    if (b.escapeSteps > 0) --b.escapeSteps;
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
            const float err = lv.timing * (noise(seat, tick_ / 20 + 101) - 0.5f) * 2.0f;
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
                b.jumpHold = 34;
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
    if (forced != 0 && !navi_.pilot(seat).committed()) {
        dir = forced;
        b.moveText = "s'écarte";
    } else {
        if (stay && !navi_.pilot(seat).committed()) dir = 0;
        // Never into what is coming (a mark, a cloud, a column, the boss's body): it waits at its edge, the way
        // it was going, until it has passed — out of a danger it may cross (it is getting out).
        if (dir != 0 && me.grounded() && !holding) {
            const Rect next = me.box.translated({static_cast<float>(dir) * 10.0f, 0.0f}).expanded(3.0f);
            if (threatened(w, seat, next, 100) && !threatened(w, seat, me.box.expanded(3.0f), 100)) {
                dir = 0;
                b.moveText = "attend que ça passe";
                b.lost = 0;
            }
        }
    }
    // ------------------------------------------------------------ what it does once there
    bool fired = false;
    if (!fleeing && foe && !dodging) {
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
            else if (b.attackCool <= 0 && (!b.sync || b.syncGroup > 0 || syncReady_) && noise(seat, tick_) < lv.precision + 0.1f) {
                held |= 1u << kAttack;
                if (d.y < -30.0f) {
                    held |= 1u << kUp;  // an upward strike
                    f.moveY = -127;
                }
                b.attackCool = 22;
            }
            if (d.y < -30.0f && me.grounded() && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump))) b.jumpHold = 12;
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
            // Its charged shot (the armory's), on a weak point bare: held still at its place, then let go aimed (4 damage,
            // through three targets) — the wave meanwhile waits.
            const Power* charge = powerOf(me, "charge_shot");
            const bool weakBare = view_.exposed && foe != boss;
            if (charge && weakBare && atSpot && me.grounded() && aim >= 0 && me.energy - 20.0f >= reserve) {
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
            if (!fired && b.chargeHold == 0 && aim >= 0 && sh->ready(me) && me.energy - 10.0f >= reserve && !(b.prevHeld & (1u << keyBit(sh))) &&
                noise(seat, tick_ + 7) < lv.precision + 0.1f) {
                // A Beginner's aim slips a notch now and then (its precision).
                if (noise(seat, tick_ + 53) > lv.precision + 0.25f) aim = (aim + 1) % 8;
                held |= 1u << keyBit(sh);
                f.moveX = static_cast<int8_t>(kDirX[aim] * 127);
                f.moveY = static_cast<int8_t>(kDirY[aim] * 127);
                if (kDirX[aim] == 0) dir = 0;
                fired = true;
            }
        }
    }
    if (b.act == Shoot && !fleeing && !dodging) {
        // A generator: from its place, facing it, the wave (or the boomerang).
        LevelEntity* e = w.entity(b.thing);
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
        // 2.77: a revival is held (« Interagir » kept down for 2 s near the soul). 2.79: when it is safe — nothing
        // coming on it or the soul for that long — else it waits beside it under its shield; its decoy draws the
        // boss's blows away while it kneels.
        const Rect around = me.box.expanded(10.0f);
        const bool unsafe = threatened(w, seat, around, 40) && !(me.shieldActive() || shieldReady);
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
    // ------------------------------------------------------------ the shield (its level's choice), when a blow cannot be avoided
    if (raiseShield && shieldReady && b.shieldCool <= 0 && me.energy >= 20.0f && noise(seat, tick_ / 12 + 77) < lv.shieldSense) {
        held |= 1u << keyBit(shield);
        b.shieldCool = 30;
        if (react.empty() || react == "esquive") react = "lève son bouclier";
    }
    // 2.79: a move through the air under way is played as it was simulated (nothing changes its direction).
    if (navi_.pilot(seat).committed()) dir = pilotDir;
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
    if (gd < b.goalBest - 8.0f || gd < 24.0f || b.task == Task::Follow || b.task == Task::Wait || dodging) {
        b.goalBest = gd;
        b.lost = 0;
    } else {
        ++b.lost;
    }
    if (dir > 0) held |= 1u << kRight;
    if (dir < 0) held |= 1u << kLeft;
    if (dir != 0 && !fired) f.moveX = static_cast<int8_t>(dir * 127);
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
    // Its thought: what it reacts to now, else its task; how it moves.
    b.thought.role = b.role;
    b.thought.intent = react.empty() ? b.intent : react;
    b.thought.move = dir == 0 && b.jumpHold <= 0 ? std::string() : b.moveText;
    return f;
}

}  // namespace cosmic::raid
