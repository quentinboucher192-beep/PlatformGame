// 2.69 (phase 4B): VOIDWORM, the worm of the void coiled in Asterion's abdomen (docs/RAIDS.md §13).
//
// Its head rears out of the pit in the middle of its lair (the gigaboss itself: data/enemies/
// voidworm.json); around the rules of raid::Gigaboss (its health and pressure for the seats, its
// « Fouissement »: a maw bursting out under every player in play, announced on a mark), its routine:
//
// * its maw aims at players (from its 2nd phase: one, two from six players in play): a green ring
//   follows each one for a second, locks, then the maw snaps: whoever is still in it is swallowed,
//   held in its stomach (a closed pocket under the arena: hurt every few seconds) until the others
//   burst the stomach growing on its neck (a rescue for who bursts it) or it spits them out half
//   digested. Never the last players outside;
// * its parasites: waves out of its rings, one more every two players; once the Abdomen's nest is
//   destroyed (objective abd_nid of the branch), half as many;
// * the acid (3rd phase and its last stand): a tide rises over the whole arena, announced; only the
//   high roots stay dry (lower once the two valves of the Intestine are closed);
// * its chitin (phases [carapace)): its three hooks (Gigaboss's vents) must be held together, 3 s
//   on each, to pin it: its maw opens, its glowing rings (weak points) can be broken.

#include <algorithm>
#include <cmath>

#include "engine/core/Json.h"
#include "game/enemies/BossHazards.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Contributions.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/GigabossRoutine.h"

namespace cosmic::raid {

namespace {
Rect rectOf(const Json& j) {
    if (!j.is_array() || j.size() != 4) return {};
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}
Vec2 vecOf(const Json& j) { return j.is_array() && j.size() == 2 ? Vec2{j[0].get<float>(), j[1].get<float>()} : Vec2{}; }
std::vector<float> floatsOf(const Json& j) {
    std::vector<float> out;
    for (const auto& v : j)
        if (v.is_number()) out.push_back(v.get<float>());
    return out;
}
float at(const std::vector<float>& v, int phase) {
    return v.empty() ? 0.0f : v[static_cast<size_t>(std::clamp(phase - 1, 0, static_cast<int>(v.size()) - 1))];
}
const Color kAcid{157, 255, 58, 255};
const Color kViolet{196, 140, 255, 255};
const std::string kBite = "Morsure du ver";
const std::string kJuice = "Sucs du ventre";
}  // namespace

class VoidwormRoutine final : public GigabossRoutine {
public:
    void begin(GameWorld& w, const GigabossRules& r) override {
        (void)w;
        *this = VoidwormRoutine{};
        rules_ = &r;
        const Json& x = r.extra;
        const Json sw = x.value("swallow", Json::object());
        gulpPeriods_ = floatsOf(sw.value("periods", Json::array()));
        gulpTrack_ = jget<float>(sw, "track", gulpTrack_);
        gulpLock_ = jget<float>(sw, "lock", gulpLock_);
        gulpW_ = jget<float>(sw, "width", gulpW_);
        gulpH_ = jget<float>(sw, "height", gulpH_);
        digest_ = jget<float>(sw, "digest", digest_);
        digestTick_ = std::max(0.5f, jget<float>(sw, "tick", digestTick_));
        stomach_ = rectOf(sw.value("stomach", Json()));
        for (const auto& e : sw.value("exits", Json::array())) exits_.push_back(vecOf(e));
        pocheId_ = jstr(sw, "poche", "voidworm_poche");
        pocheSpot_ = vecOf(sw.value("pocheSpot", Json()));
        const Json pa = x.value("parasites", Json::object());
        parasitePeriods_ = floatsOf(pa.value("periods", Json::array()));
        for (const auto& k : pa.value("kinds", Json::array()))
            if (k.is_string()) parasiteKinds_.push_back(k.get<std::string>());
        for (const auto& s : pa.value("spawns", Json::array())) parasiteSpawns_.push_back(vecOf(s));
        parasiteClock_ = steps(jget<float>(pa, "first", 12.0f));
        const Json ac = x.value("acid", Json::object());
        acidFrom_ = jget<int>(ac, "from", acidFrom_);
        acidPeriod_ = std::max(6.0f, jget<float>(ac, "period", acidPeriod_));
        acidFirst_ = jget<float>(ac, "first", acidFirst_);
        acidWarn_ = jget<float>(ac, "warn", acidWarn_);
        acidRise_ = jget<float>(ac, "rise", acidRise_);
        acidHold_ = jget<float>(ac, "hold", acidHold_);
        acidFall_ = jget<float>(ac, "fall", acidFall_);
        acidHeight_ = jget<float>(ac, "height", acidHeight_);
        acidDamage_ = jget<float>(ac, "damage", acidDamage_);
        acidValves_ = jget<float>(ac, "valves", acidValves_);
        acidLeft_ = jget<float>(ac, "left", 0.0f);
        acidRight_ = jget<float>(ac, "right", 0.0f);
        acidFloor_ = jget<float>(ac, "floor", 0.0f);
        if (parasiteKinds_.empty()) parasiteKinds_.push_back("gloop_small");
        if (exits_.empty()) exits_.push_back({stomach_.center().x, stomach_.y - 16.0f});
        gulpClock_ = steps(6.0f);
        acidClock_ = steps(acidFirst_);
    }

    void step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) override {
        stepParasites(w, g, b);
        stepSwallow(w, c, g, b);
        stepAcid(w, g, b);
        // Its chitin: the three hooks held together pin it; its maw opens.
        const GigabossRules& r = *rules_;
        const int ph = phase(g);
        const bool chitin = !b.lastStand() && ph >= r.carapaceFrom && ph < r.carapaceUntil && !r.vents.empty();
        if (exposed(g)) {
            if (exposedStep(g, w, c, b)) return;
            resetPlates(g);
            if (chitin) {
                setArmour(g, true);
                alert(g, "GIGABOSS · Voidworm s'arrache à ses crochets : épinglez de nouveau ses anneaux !", Danger);
            } else {
                setFight(g);
            }
            return;
        }
        if (!chitin) {
            if (armoured(g)) alert(g, "GIGABOSS · Sa chitine se fend pour de bon : frappez le ver !", Branch);
            setFight(g);
            return;
        }
        if (!armoured(g)) {
            setArmour(g, true);
            resetPlates(g);
            alert(g, "GIGABOSS · Voidworm se love dans sa chitine : tenez ses " + std::to_string(r.vents.size()) + " crochets ensemble (" +
                         std::to_string(static_cast<int>(std::lround(r.hold))) + " s chacun) pour l'épingler !",
                  Danger);
        }
        if (holdPlates(g, w, c, b, {"GROUPE · Anneau épinglé", "GIGABOSS · Un anneau s'arrache à son crochet : épinglez-le de nouveau !"}))
            expose(g, w, c, b,
                   "GIGABOSS · Voidworm est épinglé : sa gueule s'ouvre, brisez ses anneaux lumineux (" +
                       std::to_string(static_cast<int>(std::lround(r.exposed))) + " s) !");
    }

    void fell(GameWorld& w, Gigaboss& g) override {
        (void)g;
        for (const Held& h : held_) release(w, h.seat);
        held_.clear();
        gulps_.clear();
        if (Enemy* p = poche_ ? w.findEnemy(poche_) : nullptr; p && p->alive()) p->despawn();
        poche_ = 0;
    }

    uint64_t hash() const override {
        uint64_t h = 1469598103934665603ULL;
        for (const Gulp& q : gulps_)
            h = mix(h, (static_cast<uint64_t>(q.seat) << 48) ^ (static_cast<uint64_t>(q.track) << 32) ^ (static_cast<uint64_t>(q.lock) << 16) ^
                           static_cast<uint64_t>(std::lround(q.at.x * 4.0f)) ^ (static_cast<uint64_t>(std::lround(q.at.y * 4.0f)) << 24));
        for (const Held& q : held_) h = mix(h, (static_cast<uint64_t>(q.seat) << 32) | static_cast<uint32_t>(q.left));
        h = mix(h, poche_);
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(gulpClock_)) << 32) | static_cast<uint32_t>(rot_));
        for (uint32_t u : parasites_) h = mix(h, u);
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(parasiteClock_)) << 32) | static_cast<uint32_t>(parasiteSpot_ * 1000 + waves_));
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(acidClock_)) << 32) | static_cast<uint32_t>(acidT_));
        h = mix(h, nestTold_ ? 1u : 0u);
        return h;
    }

    void marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const override {
        (void)g;
        const float total = static_cast<float>(std::max(1, steps(gulpTrack_) + steps(gulpLock_)));
        for (const Gulp& q : gulps_) {
            BossMark m;
            m.kind = BossMark::Kind::Target;
            m.area = {q.at.x - gulpW_ * 0.5f, q.at.y - 12.0f, gulpW_, 16.0f};
            m.progress = 1.0f - static_cast<float>(q.track + q.lock) / total;
            m.color = kAcid;
            m.strong = q.track <= 0;
            m.clip = m.strong ? "gulp" : "";
            m.label = m.strong ? "GUEULE" : "";
            out.push_back(m);
        }
        if (!held_.empty()) {
            int left = 0;
            for (const Held& h : held_) left = std::max(left, h.left);
            BossMark m;
            m.kind = BossMark::Kind::Zone;
            m.area = stomach_;
            m.color = kViolet;
            m.strong = true;
            m.progress = 1.0f - static_cast<float>(left) / static_cast<float>(std::max(1, steps(digest_)));
            m.label = "AVALÉ · " + std::to_string(static_cast<int>(std::ceil(static_cast<float>(left) / static_cast<float>(kSimHz)))) + " s";
            out.push_back(m);
            if (const Enemy* p = poche_ ? w.findEnemy(poche_) : nullptr; p && p->alive()) {
                BossMark t;
                t.kind = BossMark::Kind::Target;
                t.area = p->box.expanded(10.0f);
                t.color = kAcid;
                t.progress = p->hpRatio();
                t.label = "CREVEZ LA POCHE";
                t.strong = true;
                out.push_back(t);
            }
        }
        const int warn = steps(acidWarn_);
        if (acidArmed_ && acidT_ < 0 && acidClock_ <= warn && acidRight_ > acidLeft_) {
            BossMark m;
            m.kind = BossMark::Kind::Zone;
            m.area = {acidLeft_, acidFloor_ - acidLevel_, acidRight_ - acidLeft_, acidLevel_};
            m.color = kAcid;
            m.progress = 1.0f - static_cast<float>(acidClock_) / static_cast<float>(std::max(1, warn));
            m.label = "L'ACIDE MONTE";
            out.push_back(m);
        }
    }

    void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const override {
        (void)w;
        const GigabossRules& r = *rules_;
        const Color orange{255, 176, 74, 255}, green{124, 240, 168, 255}, red{255, 138, 138, 255};
        if (exposed(g)) {
            out.push_back({std::to_string(static_cast<int>(std::ceil(g.exposedLeft()))) + " s",
                           "Sa gueule est ouverte : brisez ses anneaux lumineux (" + std::to_string(g.weakTotal() - g.weakLeft()) + " / " +
                               std::to_string(g.weakTotal()) + ")",
                           green});
        } else if (armoured(g)) {
            out.push_back({std::to_string(g.ventsClosed()) + " / " + std::to_string(g.vents().size()),
                           "Épingler ses anneaux : un joueur sur chaque crochet, tenus ensemble (" + std::to_string(static_cast<int>(std::lround(r.hold))) +
                               " s chacun, ils se libèrent au bout de " + std::to_string(static_cast<int>(std::lround(r.reopen))) + " s)",
                           Color{255, 210, 122, 255}});
        } else {
            out.push_back({"→", "Vaincre Voidworm · phase " + std::to_string(std::max(1, g.phase())) + " / 3 : quittez votre marque, il fouit sous chacun.",
                           orange});
        }
        for (const Held& h : held_)
            out.push_back({"+", "Crever la poche de Voidworm : le joueur " + std::to_string(h.seat + 1) + " est avalé (" +
                                    std::to_string(static_cast<int>(std::ceil(static_cast<float>(h.left) / static_cast<float>(kSimHz)))) + " s)",
                           red});
        if (g.phase() >= 2 && held_.empty()) out.push_back({"◆", "Sa gueule vise un joueur : sortez de l'anneau vert avant qu'il se ferme.", orange});
        if (acidArmed_)
            out.push_back({"≈", "Reflux d'acide toutes les " + std::to_string(static_cast<int>(std::lround(acidPeriod_))) + " s : restez sur les racines hautes.",
                           kAcid});
        if (!parasitePeriods_.empty())
            out.push_back(linkDone(g, "nest") ? BossLine{"OK", "Nid des parasites détruit : Voidworm n'a plus que de maigres renforts.", green}
                                              : BossLine{"◆", "Ses parasites en renfort : détruire le nid (zone 3) les réduirait de moitié.",
                                                         Color{196, 140, 255, 255}});
    }

    void threats(const Gigaboss& g, std::vector<Vec2>& out) const override {
        (void)g;
        for (const Gulp& q : gulps_) out.push_back(q.at);
    }

    void targets(std::vector<uint32_t>& out) const override {
        if (poche_) out.push_back(poche_);
    }

    /// 2.69: a player in its stomach gets no mark of its « Fouissement » (they cannot leave it: it
    /// would hit them inside); its other blows never aim at them either.
    bool spared(int seat) const override {
        return std::any_of(held_.begin(), held_.end(), [&](const Held& h) { return h.seat == seat; });
    }

private:
    struct Gulp {
        int seat = -1;
        Vec2 at;
        int track = 0, lock = 0;
    };
    struct Held {
        int seat = -1;
        int left = 0;
    };

    Vec2 inside(int seat) const {
        const float x = stomach_.x + 20.0f + std::fmod(static_cast<float>(seat) * 23.0f, std::max(1.0f, stomach_.w - 40.0f));
        return {x, stomach_.bottom()};
    }

    void release(GameWorld& w, int seat) {
        if (seat < 0 || seat >= w.teamSize()) return;
        Player& p = w.playerAt(seat);
        if (!p.alive()) return;
        p.teleportTo(exits_[static_cast<size_t>(seat) % exits_.size()]);
        p.setInvulnerable(1.2f);
        w.particles().emit("splash_acid", p.feet(), 1.5f);
    }

    void gluePoche(GameWorld& w, const Enemy& b) {
        Enemy* e = poche_ ? w.findEnemy(poche_) : nullptr;
        if (!e || !e->alive()) return;
        const Vec2 spot{b.box.center().x + pocheSpot_.x * b.facing, b.box.bottom() + pocheSpot_.y};
        e->box = Rect::fromBottomCenter(spot, e->box.size());
        e->prevBox = e->box;
        e->vel = {0.0f, 0.0f};
    }

    void swallow(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b, int seat) {
        Player& p = w.playerAt(seat);
        p.teleportTo(inside(seat));
        held_.push_back({seat, steps(digest_)});
        w.particles().emit("acid_burst", p.center(), 2.0f);
        w.playSound("sounds/burrow.wav", b.box.center(), 1.0f, 0.7f);
        w.shake(0.35f);
        if (poche_ == 0 || !w.findEnemy(poche_)) {
            const Vec2 spot{b.box.center().x + pocheSpot_.x * b.facing, b.box.bottom() + pocheSpot_.y};
            if (Enemy* e = w.spawnEnemy(pocheId_, spot, b.facing < 0.0f)) {
                e->summonedBy = b.uid();
                e->countsForStats = false;
                e->scaleHealth(1.0f + 0.25f * static_cast<float>(std::max(0, scaledFor(g) - 1)));
                poche_ = e->uid();
                c.setWatched({poche_});
            }
        }
        alert(g, "GIGABOSS · Voidworm avale le joueur " + std::to_string(seat + 1) + " : crevez sa poche pour le libérer (" +
                     std::to_string(static_cast<int>(std::lround(digest_))) + " s) !",
              Danger);
    }

    void stepSwallow(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
        using P = GameWorld::Presence;
        gluePoche(w, b);
        // Its stomach burst: everyone it held is free (a rescue for who burst it).
        const Enemy* poche = poche_ ? w.findEnemy(poche_) : nullptr;
        if (poche_ != 0 && (!poche || !poche->alive())) {
            if (!held_.empty()) {
                const int seat = c.brokeWeak(poche_);
                if (seat >= 0) c.rescue(seat);
                const Vec2 burst = poche ? poche->box.center() : b.box.center();
                for (const Held& h : held_) release(w, h.seat);
                held_.clear();
                w.particles().emit("splash_acid", burst, 2.5f);
                alert(g, "GROUPE · La poche crève : les avalés sont libérés !", Group);
            }
            poche_ = 0;
            c.setWatched({});
        }
        // Those it holds: kept in, digested, spat out at the end.
        for (size_t k = 0; k < held_.size();) {
            Held& h = held_[k];
            const bool there = h.seat < w.teamSize() && w.member(h.seat).presence == P::Playing && w.playerAt(h.seat).alive();
            if (!there) {
                held_.erase(held_.begin() + static_cast<std::ptrdiff_t>(k));
                continue;
            }
            Player& p = w.playerAt(h.seat);
            if (!stomach_.contains(p.center())) p.teleportTo(inside(h.seat));
            if (--h.left <= 0) {
                const int seat = h.seat;
                held_.erase(held_.begin() + static_cast<std::ptrdiff_t>(k));
                release(w, seat);
                strike(w, b, w.playerAt(seat).box, 1.0f, 200.0f, kJuice);
                alert(g, "GIGABOSS · Voidworm recrache le joueur " + std::to_string(seat + 1) + ", à moitié digéré !", Danger);
                continue;
            }
            ++k;
        }
        if (!held_.empty() && ticks(g) % static_cast<uint32_t>(std::max(1, steps(digestTick_))) == 0) strike(w, b, stomach_, 1.0f, 0.0f, kJuice);
        if (held_.empty() && poche_ != 0) {
            if (Enemy* e = w.findEnemy(poche_); e && e->alive()) e->despawn();
            poche_ = 0;
            c.setWatched({});
        }
        // Its maw aimed at players: it follows each one, locks, snaps.
        const std::vector<int> play = playing(w);
        auto isHeld = [&](int seat) { return std::any_of(held_.begin(), held_.end(), [&](const Held& h) { return h.seat == seat; }); };
        for (size_t k = 0; k < gulps_.size();) {
            Gulp& q = gulps_[k];
            if (q.track > 0) {
                --q.track;
                if (std::find(play.begin(), play.end(), q.seat) != play.end() && !isHeld(q.seat)) q.at = w.playerAt(q.seat).feet();
                ++k;
                continue;
            }
            if (--q.lock > 0) {
                ++k;
                continue;
            }
            const Rect snap{q.at.x - gulpW_ * 0.5f, q.at.y - gulpH_, gulpW_, gulpH_ + 8.0f};
            w.particles().emit("acid_burst", {q.at.x, q.at.y - 20.0f}, 1.6f);
            w.shake(0.3f);
            const size_t most = play.size() >= 6 ? 3 : 2;
            for (int seat : playing(w)) {
                if (isHeld(seat) || !snap.intersects(w.playerAt(seat).box) || held_.size() >= most) continue;
                // Never the last players outside: at least one stays to burst its stomach.
                int outside = 0;
                for (int s : playing(w)) outside += isHeld(s) ? 0 : 1;
                if (outside <= 1) break;
                swallow(w, c, g, b, seat);
            }
            strike(w, b, snap, 1.0f, 240.0f, kBite);
            gulps_.erase(gulps_.begin() + static_cast<std::ptrdiff_t>(k));
        }
        // The next bite (from its 2nd phase, never while its maw is pinned open).
        const float period = at(gulpPeriods_, phase(g));
        if (period <= 0.0f || exposed(g) || b.lastStand()) return;
        if (--gulpClock_ > 0) return;
        gulpClock_ = std::max(steps(4.0f), steps(period / std::max(1.0f, b.pressure())));
        std::vector<int> free;
        for (int s : play)
            if (!isHeld(s)) free.push_back(s);
        if (free.size() < 2) return;  // never the last one outside
        const int n = std::min<int>(play.size() >= 6 ? 2 : 1, static_cast<int>(free.size()) - 1);
        for (int k = 0; k < n; ++k) {
            const int seat = free[static_cast<size_t>(rot_ + k) % free.size()];
            gulps_.push_back({seat, w.playerAt(seat).feet(), steps(gulpTrack_), steps(gulpLock_)});
        }
        rot_ += n;
        alert(g, "DANGER · La gueule de Voidworm vise " + std::string(n > 1 ? "deux joueurs" : "un joueur") + " : sortez de l'anneau vert !", Danger);
    }

    void stepParasites(GameWorld& w, Gigaboss& g, Enemy& b) {
        std::erase_if(parasites_, [&](uint32_t u) {
            const Enemy* e = w.findEnemy(u);
            return !e || !e->alive();
        });
        if (parasitePeriods_.empty() || parasiteSpawns_.empty() || b.lastStand()) return;
        const bool nest = linkDone(g, "nest");
        if (nest && !nestTold_) {
            nestTold_ = true;
            alert(g, "TITAN · Le nid des parasites est détruit : Voidworm n'a plus que de maigres renforts.", Branch);
        }
        if (--parasiteClock_ > 0) return;
        parasiteClock_ = std::max(steps(6.0f), steps(at(parasitePeriods_, phase(g)) / std::max(1.0f, b.pressure())));
        const int n = std::max(1, inPlay(g));
        int count = 1 + n / 2;
        if (nest) count = std::max(1, count / 2);
        const int cap = nest ? n / 2 + 1 : n + 2;
        count = std::min(count, cap - static_cast<int>(parasites_.size()));
        if (count <= 0) return;
        for (int k = 0; k < count; ++k) {
            const Vec2 spot = parasiteSpawns_[static_cast<size_t>(parasiteSpot_++) % parasiteSpawns_.size()];
            const std::string& kind = parasiteKinds_[static_cast<size_t>(waves_ + k) % parasiteKinds_.size()];
            if (Enemy* e = w.spawnEnemy(kind, spot, spot.x > b.box.center().x)) {
                e->summonedBy = b.uid();
                e->countsForStats = false;
                parasites_.push_back(e->uid());
                w.particles().emit("burrow_dirt", spot, 1.2f);
            }
        }
        ++waves_;
        alert(g, nest ? "DANGER · Quelques parasites sortent du ventre (le nid est détruit)" : "DANGER · Une vague de parasites sort des anneaux de Voidworm !",
              Danger);
    }

    void stepAcid(GameWorld& w, Gigaboss& g, Enemy& b) {
        acidArmed_ = (phase(g) >= acidFrom_ || b.lastStand()) && acidRight_ > acidLeft_;
        acidLevel_ = acidHeight_ * (linkDone(g, "valve_high") && linkDone(g, "valve_low") ? acidValves_ : 1.0f);
        if (acidT_ >= 0 && ++acidT_ > steps(acidRise_ + acidHold_ + acidFall_)) acidT_ = -1;
        if (!acidArmed_) return;
        if (--acidClock_ == steps(acidWarn_))
            alert(g, "DANGER · L'acide monte dans " + std::to_string(static_cast<int>(std::lround(acidWarn_))) + " s : grimpez sur les racines hautes !",
                  Danger);
        if (acidClock_ > 0) return;
        acidClock_ = steps(acidPeriod_ / std::max(1.0f, b.pressure()));
        BossHazards::Flood f;
        f.owner = b.uid();
        f.left = acidLeft_;
        f.right = acidRight_;
        f.floorY = acidFloor_;
        f.height = acidLevel_;
        f.rise = acidRise_;
        f.hold = acidHold_;
        f.fall = acidFall_;
        f.damage = acidDamage_;
        f.look = "tide";
        f.color = kAcid;
        w.hazards().addFlood(f);
        acidT_ = 0;
        w.shake(0.4f);
        w.playSound("sounds/wyrm_roar.wav", b.box.center(), 0.8f, 0.8f);
        alert(g, "DANGER · Le reflux d'acide submerge l'arène !", Danger);
    }

    // Its rules (metadata.raid.gigaboss of its lair).
    std::vector<float> gulpPeriods_;
    float gulpTrack_ = 1.0f, gulpLock_ = 0.7f, gulpW_ = 96.0f, gulpH_ = 150.0f;
    float digest_ = 12.0f, digestTick_ = 3.0f;
    Rect stomach_;
    std::vector<Vec2> exits_;
    std::string pocheId_ = "voidworm_poche";
    Vec2 pocheSpot_{0.0f, -170.0f};
    std::vector<float> parasitePeriods_;
    std::vector<std::string> parasiteKinds_;
    std::vector<Vec2> parasiteSpawns_;
    int acidFrom_ = 3;
    float acidPeriod_ = 22.0f, acidFirst_ = 6.0f, acidWarn_ = 3.0f, acidRise_ = 2.5f, acidHold_ = 6.0f, acidFall_ = 2.0f;
    float acidHeight_ = 150.0f, acidDamage_ = 2.0f, acidValves_ = 0.7f, acidLeft_ = 0.0f, acidRight_ = 0.0f, acidFloor_ = 0.0f;
    // Its state.
    std::vector<Gulp> gulps_;
    std::vector<Held> held_;
    uint32_t poche_ = 0;
    int gulpClock_ = 0, rot_ = 0;
    std::vector<uint32_t> parasites_;
    int parasiteClock_ = 0, parasiteSpot_ = 0, waves_ = 0;
    bool nestTold_ = false;
    int acidClock_ = 0, acidT_ = -1;
    bool acidArmed_ = false;
    float acidLevel_ = 0.0f;
};

std::unique_ptr<GigabossRoutine> makeVoidwormRoutine() { return std::make_unique<VoidwormRoutine>(); }

}  // namespace cosmic::raid
