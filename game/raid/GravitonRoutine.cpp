// 2.69 (phase 4B): GRAVITON, Asterion's left leg wrapped in its gravity generators (docs/RAIDS.md §13).
//
// The leg floats over its arena (the gigaboss itself: data/enemies/graviton.json); around the rules of
// raid::Gigaboss (its health and pressure for the seats, debris falling on a mark under every player
// in play), its routine:
//
// * it bends space by sectors (the arena cut in four): a sector is announced (its border lit, arrows
//   up), then its gravity turns over for a few seconds: every player in it falls toward the vault,
//   whose field hurts, unless a slab of the sector shelters them (they stand under it, upside down).
//   One sector, then two at once, then half the arena in its 3rd phase;
// * from its 2nd phase, a gravity well opens under every player in play (announced): it pulls toward
//   its core, which hurts;
// * its field (phases [carapace)): nothing hurts it while its generators run. They stand in separate
//   sectors (one high, reached by its islands): one player on each, ALL AT THE SAME TIME, held
//   together for `hold` seconds, switch them off at once: the leg collapses to the floor, its gravity
//   cores (weak points) bare. Fewer players, fewer generators needed (one each); one less once the
//   leg's two gravity generators are cut (objectives of the branch).

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
std::vector<float> floatsOf(const Json& j) {
    std::vector<float> out;
    for (const auto& v : j)
        if (v.is_number()) out.push_back(v.get<float>());
    return out;
}
float at(const std::vector<float>& v, int phase) {
    return v.empty() ? 0.0f : v[static_cast<size_t>(std::clamp(phase - 1, 0, static_cast<int>(v.size()) - 1))];
}
const Color kCyan{127, 246, 255, 255};
const Color kGold{255, 210, 122, 255};
const std::string kVault = "Voûte de gravité";
}  // namespace

class GravitonRoutine final : public GigabossRoutine {
public:
    void begin(GameWorld& w, const GigabossRules& r) override {
        (void)w;
        *this = GravitonRoutine{};
        rules_ = &r;
        const Json& x = r.extra;
        for (const auto& gen : x.value("generators", Json::array())) gens_.push_back(rectOf(gen.value("rect", Json())));
        held_.assign(gens_.size(), 0);
        const Json se = x.value("sectors", Json::object());
        edges_ = floatsOf(se.value("edges", Json::array()));
        top_ = jget<float>(se, "top", 0.0f);
        floor_ = jget<float>(se, "floor", 0.0f);
        band_ = jget<float>(se, "band", band_);
        lift_ = jget<float>(se, "lift", lift_);
        flipPeriods_ = floatsOf(se.value("periods", Json::array()));
        flipTimes_ = floatsOf(se.value("time", Json::array()));
        flipWarnS_ = jget<float>(se, "warn", flipWarnS_);
        flipClock_ = steps(jget<float>(se, "first", 8.0f));
        const Json we = x.value("wells", Json::object());
        wellPeriods_ = floatsOf(we.value("periods", Json::array()));
        wellWarn_ = jget<float>(we, "warn", wellWarn_);
        wellTime_ = jget<float>(we, "time", wellTime_);
        wellSpeed_ = jget<float>(we, "speed", wellSpeed_);
        wellRange_ = jget<float>(we, "range", wellRange_);
        wellCore_ = jget<float>(we, "core", wellCore_);
        wellDamage_ = jget<float>(we, "damage", wellDamage_);
        wellClock_ = steps(5.0f);
        fallY_ = jget<float>(x, "fall", floor_);
    }

    void step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) override {
        stepFlips(w, g, b);
        stepWells(w, g, b);
        const GigabossRules& r = *rules_;
        const int ph = phase(g);
        const bool field = !b.lastStand() && ph >= r.carapaceFrom && ph < r.carapaceUntil && !gens_.empty();
        if (exposed(g)) {
            pinDown(b);
            if (exposedStep(g, w, c, b)) return;
            collapse_ = -1;
            if (field) {
                setArmour(g, true);
                sync_ = 0;
                alert(g, "GIGABOSS · Graviton se relève : ses générateurs se rallument !", Danger);
            } else {
                setFight(g);
            }
            return;
        }
        if (!field) {
            if (armoured(g)) alert(g, "GIGABOSS · Son champ de gravité se dissipe pour de bon !", Branch);
            setFight(g);
            return;
        }
        if (!armoured(g)) {
            setArmour(g, true);
            sync_ = 0;
            alert(g, "GIGABOSS · Graviton s'entoure de son champ : coupez ses " + std::to_string(need(g)) +
                         " générateurs EN MÊME TEMPS (un joueur sur chacun) !",
                  Danger);
        }
        if (linkDone(g, "west") && linkDone(g, "east") && !linkTold_) {
            linkTold_ = true;
            alert(g, "TITAN · Les générateurs de la jambe sont coupés : un générateur de moins à tenir.", Branch);
        }
        stepGenerators(w, c, g, b);
    }

    void fell(GameWorld& w, Gigaboss& g) override {
        (void)w;
        (void)g;
        flipMask_ = 0;
        flipLeft_ = flipWarn_ = 0;
        wells_.clear();
    }

    uint64_t hash() const override {
        uint64_t h = 1469598103934665603ULL;
        h = mix(h, (static_cast<uint64_t>(flipMask_) << 48) ^ (static_cast<uint64_t>(static_cast<uint32_t>(flipLeft_)) << 24) ^
                       static_cast<uint32_t>(flipWarn_));
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(flipClock_)) << 32) | static_cast<uint32_t>(flipRot_));
        for (const Well& q : wells_)
            h = mix(h, (static_cast<uint64_t>(q.left) << 40) ^ static_cast<uint64_t>(std::lround(q.at.x * 4.0f)) ^
                           (static_cast<uint64_t>(std::lround(q.at.y * 4.0f)) << 20));
        h = mix(h, static_cast<uint64_t>(static_cast<uint32_t>(wellClock_)));
        for (uint8_t m : held_) h = mix(h, m);
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(sync_)) << 32) | static_cast<uint32_t>(collapse_ + 1));
        h = mix(h, linkTold_ ? 1u : 0u);
        return h;
    }

    void marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const override {
        (void)w;
        // The sectors turning over (announced, then on) and the vault's field over them.
        for (int k = 0; k + 1 < static_cast<int>(edges_.size()); ++k) {
            if (!(flipMask_ & (1u << k))) continue;
            const Rect sector{edges_[static_cast<size_t>(k)], top_, edges_[static_cast<size_t>(k) + 1] - edges_[static_cast<size_t>(k)], floor_ - top_};
            BossMark m;
            m.kind = BossMark::Kind::Zone;
            m.area = sector;
            m.color = kCyan;
            m.strong = flipLeft_ > 0;
            m.progress = m.strong ? static_cast<float>(flipLeft_) / static_cast<float>(std::max(1, steps(flipTime(g))))
                                  : 1.0f - static_cast<float>(flipWarn_) / static_cast<float>(std::max(1, steps(flipWarnS_)));
            m.label = m.strong ? "GRAVITÉ INVERSÉE" : "INVERSION";
            out.push_back(m);
            BossMark a;
            a.kind = BossMark::Kind::Arrow;
            a.area = {sector.center().x - 24.0f, floor_ - 220.0f, 48.0f, 160.0f};
            a.color = kCyan;
            a.strong = m.strong;
            out.push_back(a);
            if (m.strong) {
                BossMark v;
                v.kind = BossMark::Kind::Zone;
                v.area = {sector.x, top_, sector.w, band_};
                v.color = Color{255, 90, 140, 255};
                v.strong = true;
                out.push_back(v);
            }
        }
        // Its generators: dark outside its field, lit while it runs, gold under a player (the hold).
        const bool field = armoured(g);
        const float hold = static_cast<float>(std::max(1, steps(rules_->hold)));
        for (size_t k = 0; k < gens_.size(); ++k) {
            BossMark m;
            m.kind = BossMark::Kind::Plate;
            m.area = gens_[k];
            m.color = held_[k] ? kGold : kCyan;
            m.clip = !field ? "gen_off" : held_[k] ? "gen_held" : "gen_on";
            m.progress = field && held_[k] ? static_cast<float>(sync_) / hold : 0.0f;
            m.strong = field;
            out.push_back(m);
        }
        const float warn = static_cast<float>(std::max(1, steps(wellWarn_)));
        for (const Well& q : wells_) {
            BossMark m;
            m.kind = BossMark::Kind::Target;
            m.area = {q.at.x - wellCore_ * 1.6f, q.at.y - 8.0f, wellCore_ * 3.2f, 16.0f};
            m.color = Color{190, 150, 255, 255};
            m.progress = 1.0f - static_cast<float>(q.left) / warn;
            m.clip = "well";
            out.push_back(m);
        }
    }

    void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const override {
        (void)w;
        const Color orange{255, 176, 74, 255}, green{124, 240, 168, 255};
        if (exposed(g)) {
            out.push_back({std::to_string(static_cast<int>(std::ceil(g.exposedLeft()))) + " s",
                           "Graviton s'est effondré : brisez ses cœurs de gravité (" + std::to_string(g.weakTotal() - g.weakLeft()) + " / " +
                               std::to_string(g.weakTotal()) + ")",
                           green});
        } else if (armoured(g)) {
            int on = 0;
            for (uint8_t m : held_) on += m ? 1 : 0;
            out.push_back({std::to_string(std::min(on, need(g))) + " / " + std::to_string(need(g)),
                           "Couper ses générateurs : un joueur sur chacun, tous en même temps (" + seconds(rules_->hold) + " ensemble)", kGold});
        } else {
            out.push_back({"→", "Vaincre Graviton · phase " + std::to_string(std::max(1, g.phase())) + " / 3 : quittez votre marque, des débris tombent.",
                           orange});
        }
        if (flipMask_ != 0)
            out.push_back({"↑", flipLeft_ > 0 ? "Gravité inversée : abritez-vous sous une dalle, la voûte blesse." : "Un secteur va s'inverser : quittez-le ou visez un abri.",
                           kCyan});
        if (!wellPeriods_.empty() && at(wellPeriods_, g.phase()) > 0.0f)
            out.push_back({"•", "Un puits s'ouvre sous chacun : éloignez-vous de son cœur.", Color{190, 150, 255, 255}});
        out.push_back(linkDone(g, "west") && linkDone(g, "east")
                          ? BossLine{"OK", "Générateurs de la jambe coupés : un générateur de moins à tenir ici.", green}
                          : BossLine{"◆", "Couper les 2 générateurs de la jambe (Cuisse, Tibia) en retirerait un ici.", Color{196, 140, 255, 255}});
    }

    void threats(const Gigaboss& g, std::vector<Vec2>& out) const override {
        (void)g;
        for (const Well& q : wells_) out.push_back(q.at);
    }

private:
    struct Well {
        Vec2 at;
        int left = 0;
    };

    static std::string seconds(float s) {
        const int tenths = static_cast<int>(std::lround(s * 10.0f));
        return tenths % 10 == 0 ? std::to_string(tenths / 10) + " s" : std::to_string(tenths / 10) + "," + std::to_string(tenths % 10) + " s";
    }

    /// Generators to hold together: one per player in play, all of them at most (one less once the
    /// leg's two generators are cut).
    int need(const Gigaboss& g) const {
        const int all = static_cast<int>(gens_.size()) - (linkDone(g, "west") && linkDone(g, "east") ? 1 : 0);
        return std::clamp(std::min(all, std::max(1, inPlay(g))), 1, std::max(1, all));
    }

    float flipTime(const Gigaboss& g) const { return std::max(1.0f, at(flipTimes_, phase(g))); }

    void pinDown(Enemy& b) {
        // Collapsed: it falls to the floor (half a second) and stays there while its cores are bare.
        if (collapse_ < 0) {
            collapse_ = 0;
            collapseFrom_ = b.box.y;
        }
        ++collapse_;
        const float t = std::min(1.0f, static_cast<float>(collapse_) / static_cast<float>(steps(0.5f)));
        const float to = fallY_ - b.box.h;
        b.box.y = collapseFrom_ + (to - collapseFrom_) * t * t;
        b.vel = {0.0f, 0.0f};
    }

    void stepGenerators(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
        int occupied = 0;
        for (size_t k = 0; k < gens_.size(); ++k) {
            held_[k] = 0;
            for (int seat : playing(w))
                if (gens_[k].intersects(w.playerAt(seat).box)) held_[k] |= static_cast<uint8_t>(1u << seat);
            occupied += held_[k] ? 1 : 0;
        }
        const int hold = std::max(1, steps(rules_->hold));
        if (occupied >= need(g)) {
            if (sync_ == 0) alert(g, "GROUPE · Tous les générateurs sont tenus : restez dessus !", Group);
            ++sync_;
        } else {
            sync_ = std::max(0, sync_ - 3);
        }
        if (sync_ < hold) return;
        sync_ = 0;
        for (uint8_t m : held_)
            for (int i = 0; i < 8; ++i)
                if (m & (1u << i)) c.vent(i);
        for (const Rect& gen : gens_) w.particles().emit("portal_burst", gen.center(), 1.4f);
        w.playSound("sounds/gravity_well.wav", b.box.center(), 1.0f, 0.6f);
        alert(g, "GROUPE · Les générateurs s'éteignent ensemble !", Group);
        collapse_ = -1;
        expose(g, w, c, b,
               "GIGABOSS · Graviton s'effondre : ses cœurs de gravité sont à nu (" + std::to_string(static_cast<int>(std::lround(rules_->exposed))) + " s) !");
    }

    void stepFlips(GameWorld& w, Gigaboss& g, Enemy& b) {
        const int sectors = static_cast<int>(edges_.size()) - 1;
        if (sectors < 2 || flipPeriods_.empty()) return;
        if (flipLeft_ > 0) {
            --flipLeft_;
            // Gravity turned over in the sectors: the players there fall toward the vault (a slab
            // over them stops them), whose field hurts.
            const float dy = lift_ / static_cast<float>(kSimHz);
            for (int seat : playing(w)) {
                Player& p = w.playerAt(seat);
                const int k = sectorOf(p.center().x);
                if (k < 0 || !(flipMask_ & (1u << k))) continue;
                w.physics().move(p.box, {0.0f, -dy}, p.moveOptions());
                if (p.vel.y > 0.0f) p.vel.y = 0.0f;
            }
            for (int k = 0; k < sectors; ++k)
                if (flipMask_ & (1u << k))
                    strike(w, b, {edges_[static_cast<size_t>(k)], top_, edges_[static_cast<size_t>(k) + 1] - edges_[static_cast<size_t>(k)], band_}, 1.0f,
                           140.0f, kVault);
            if (flipLeft_ == 0) {
                flipMask_ = 0;
                alert(g, "GROUPE · La gravité revient : attention à la chute !", Group);
            }
            return;
        }
        if (flipWarn_ > 0) {
            if (--flipWarn_ == 0) {
                flipLeft_ = steps(flipTime(g));
                w.shake(0.5f);
                w.playSound("sounds/gravity_well.wav", b.box.center(), 0.9f, 1.2f);
                alert(g, "DANGER · La gravité s'inverse : abritez-vous sous une dalle !", Danger);
            }
            return;
        }
        if (--flipClock_ > 0) return;
        flipClock_ = std::max(steps(4.0f), steps(at(flipPeriods_, phase(g)) / std::max(1.0f, b.pressure())));
        // One sector, then two apart, then half the arena (3rd phase and its last stand): in turn.
        const int ph = b.lastStand() ? 3 : phase(g);
        static const uint8_t kOne[] = {0b0001, 0b0100, 0b0010, 0b1000};
        static const uint8_t kTwo[] = {0b0101, 0b1010};
        static const uint8_t kHalf[] = {0b0011, 0b1100};
        const uint8_t all = static_cast<uint8_t>((1u << sectors) - 1u);
        flipMask_ = static_cast<uint8_t>((ph >= 3 ? kHalf[flipRot_ % 2] : ph == 2 ? kTwo[flipRot_ % 2] : kOne[flipRot_ % 4]) & all);
        if (flipMask_ == 0) flipMask_ = 1;
        ++flipRot_;
        flipWarn_ = steps(flipWarnS_);
        alert(g, ph >= 3 ? "DANGER · Graviton retourne la moitié de l'arène : le haut et le bas vont s'échanger !"
                         : "DANGER · Graviton plie l'espace : la gravité va s'inverser dans les secteurs marqués !",
              Danger);
    }

    void stepWells(GameWorld& w, Gigaboss& g, Enemy& b) {
        for (size_t k = 0; k < wells_.size();) {
            if (--wells_[k].left > 0) {
                ++k;
                continue;
            }
            BossHazards::Pull p;
            p.owner = b.uid();
            p.center = wells_[k].at;
            p.speed = wellSpeed_;
            p.range = wellRange_;
            p.core = wellCore_;
            p.damage = wellDamage_;
            p.time = wellTime_;
            w.hazards().addPull(p);
            w.particles().emit("portal_burst", wells_[k].at, 1.0f);
            wells_.erase(wells_.begin() + static_cast<std::ptrdiff_t>(k));
        }
        const float period = at(wellPeriods_, phase(g));
        if (period <= 0.0f || b.lastStand() || exposed(g)) return;
        if (--wellClock_ > 0) return;
        wellClock_ = std::max(steps(4.0f), steps(period / std::max(1.0f, b.pressure())));
        for (int seat : playing(w)) wells_.push_back({w.playerAt(seat).feet() - Vec2{0.0f, 18.0f}, steps(wellWarn_)});
        if (!wells_.empty()) alert(g, "DANGER · Un puits de gravité s'ouvre sous chacun : éloignez-vous !", Danger);
    }

    int sectorOf(float x) const {
        for (int k = 0; k + 1 < static_cast<int>(edges_.size()); ++k)
            if (x >= edges_[static_cast<size_t>(k)] && x < edges_[static_cast<size_t>(k) + 1]) return k;
        return -1;
    }

    // Its rules.
    std::vector<Rect> gens_;
    std::vector<float> edges_;
    float top_ = 0.0f, floor_ = 0.0f, band_ = 40.0f, lift_ = 300.0f;
    std::vector<float> flipPeriods_, flipTimes_;
    float flipWarnS_ = 2.0f;
    std::vector<float> wellPeriods_;
    float wellWarn_ = 1.2f, wellTime_ = 3.0f, wellSpeed_ = 90.0f, wellRange_ = 240.0f, wellCore_ = 30.0f, wellDamage_ = 1.0f;
    float fallY_ = 0.0f;
    // Its state.
    uint8_t flipMask_ = 0;
    int flipLeft_ = 0, flipWarn_ = 0, flipClock_ = 0, flipRot_ = 0;
    std::vector<Well> wells_;
    int wellClock_ = 0;
    std::vector<uint8_t> held_;
    int sync_ = 0;
    int collapse_ = -1;
    float collapseFrom_ = 0.0f;
    bool linkTold_ = false;
};

std::unique_ptr<GigabossRoutine> makeGravitonRoutine() { return std::make_unique<GravitonRoutine>(); }

}  // namespace cosmic::raid
