// 2.69 (phase 4B): TITANUS, Asterion's right leg, a colossus of its forge (docs/RAIDS.md §13).
//
// The leg walks its arena (the gigaboss itself: data/enemies/titanus.json, the golem's brain: its
// stomps, its charges, its leaps, its quakes); around the rules of raid::Gigaboss (its health and
// pressure for the seats, its heel falling on a mark under every player in play), its routine:
//
// * its stomps: announced (its heel raised), then two shockwaves run along the floor from its feet,
//   to the walls: whoever is on the floor must jump over them (the platforms are safe); a second
//   pair follows in its 3rd phase;
// * from its 2nd phase, boulders fall from the vault on marked spots all over the arena (and on the
//   players' own spots);
// * its armour (phases [carapace)): nothing hurts it while its two ankle restraints hold. Broken
//   within `window` seconds of each other (a restraint broken alone forges again), they bring it to
//   its knee: its knee and ankle seams (weak points) bare, its pace slowed. Fewer players, a longer
//   window; longer still once the leg's two reactors are cut (objectives of the branch).

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
const Color kForge{255, 176, 74, 255};
const Color kGold{255, 210, 122, 255};
const std::string kWave = "Onde de choc";
}  // namespace

class TitanusRoutine final : public GigabossRoutine {
public:
    void begin(GameWorld& w, const GigabossRules& r) override {
        (void)w;
        *this = TitanusRoutine{};
        rules_ = &r;
        const Json& x = r.extra;
        const Json wv = x.value("waves", Json::object());
        wavePeriods_ = floatsOf(wv.value("periods", Json::array()));
        waveWarn_ = jget<float>(wv, "warn", waveWarn_);
        waveSpeed_ = jget<float>(wv, "speed", waveSpeed_);
        waveH_ = jget<float>(wv, "height", waveH_);
        waveW_ = jget<float>(wv, "width", waveW_);
        waveDamage_ = jget<float>(wv, "damage", waveDamage_);
        waveDouble_ = jget<int>(wv, "double", waveDouble_);
        floor_ = jget<float>(wv, "floor", 0.0f);
        left_ = jget<float>(wv, "left", 0.0f);
        right_ = jget<float>(wv, "right", 0.0f);
        waveClock_ = steps(jget<float>(wv, "first", 5.0f));
        const Json bo = x.value("boulders", Json::object());
        rockPeriods_ = floatsOf(bo.value("periods", Json::array()));
        rockCounts_ = floatsOf(bo.value("count", Json::array()));
        rockWarn_ = jget<float>(bo, "warn", rockWarn_);
        rockDamage_ = jget<float>(bo, "damage", rockDamage_);
        top_ = jget<float>(bo, "top", 0.0f);
        rockClock_ = steps(4.0f);
        const Json re = x.value("restraints", Json::object());
        restraintId_ = jstr(re, "enemy", "titanus_entrave");
        for (const auto& s : re.value("spots", Json::array()))
            if (spots_.size() < 4) spots_.push_back(vecOf(s));
        window_ = jget<float>(re, "window", window_);
        reactors_ = jget<float>(re, "reactors", reactors_);
        restraint_.assign(spots_.size(), 0);
        brokeAt_.assign(spots_.size(), -1);
        breaker_.assign(spots_.size(), -1);
    }

    void step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) override {
        scaledFor_ = scaledFor(g);
        stepWaves(w, g, b);
        stepBoulders(w, g, b);
        const GigabossRules& r = *rules_;
        const int ph = phase(g);
        const bool armour = !b.lastStand() && ph >= r.carapaceFrom && ph < r.carapaceUntil && !spots_.empty();
        if (linkDone(g, "west") && linkDone(g, "east") && !linkTold_) {
            linkTold_ = true;
            alert(g, "TITAN · Les réacteurs de la jambe sont coupés : ses entraves se reforgent plus lentement.", Branch);
        }
        if (exposed(g)) {
            if (exposedStep(g, w, c, b)) return;
            if (armour) {
                setArmour(g, true);
                forge(w, c, b, true);
                alert(g, "GIGABOSS · Titanus se relève : ses entraves se reforgent !", Danger);
            } else {
                setFight(g);
            }
            return;
        }
        if (!armour) {
            if (armoured(g)) alert(g, "GIGABOSS · Son blindage se fend pour de bon : frappez le colosse !", Branch);
            clearRestraints(w, c);
            setFight(g);
            return;
        }
        if (!armoured(g)) {
            setArmour(g, true);
            forge(w, c, b, true);
            alert(g, "GIGABOSS · Titanus verrouille ses plaques : brisez ses deux entraves EN MÊME TEMPS !", Danger);
        }
        stepRestraints(w, c, g, b);
    }

    void fell(GameWorld& w, Gigaboss& g) override {
        (void)g;
        waves_.clear();
        stompWarn_ = 0;
        for (uint32_t& u : restraint_) {
            if (Enemy* e = u ? w.findEnemy(u) : nullptr; e && e->alive()) e->despawn();
            u = 0;
        }
    }

    uint64_t hash() const override {
        uint64_t h = 1469598103934665603ULL;
        for (const Wave& q : waves_) h = mix(h, (static_cast<uint64_t>(std::lround(q.x * 4.0f)) << 8) ^ static_cast<uint64_t>(q.dir > 0.0f ? 1u : 2u));
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(waveClock_)) << 32) | static_cast<uint32_t>(stompWarn_));
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(echo_)) << 32) | static_cast<uint32_t>(std::lround(stompX_ * 4.0f)));
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(rockClock_)) << 32) | static_cast<uint32_t>(volleys_));
        for (size_t k = 0; k < restraint_.size(); ++k)
            h = mix(h, (static_cast<uint64_t>(restraint_[k]) << 32) ^ (static_cast<uint64_t>(static_cast<uint32_t>(brokeAt_[k])) << 8) ^
                           static_cast<uint64_t>(breaker_[k] + 1));
        h = mix(h, (linkTold_ ? 1u : 0u) | (doubleNext_ ? 2u : 0u));
        return h;
    }

    void marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const override {
        (void)g;
        for (const Wave& q : waves_) {
            BossMark m;
            m.kind = BossMark::Kind::Wave;
            m.area = {q.x - 24.0f, floor_ - 72.0f, 48.0f, 72.0f};
            m.color = kForge;
            m.clip = "wave";
            m.strong = true;
            out.push_back(m);
        }
        if (stompWarn_ > 0) {
            BossMark m;
            m.kind = BossMark::Kind::Zone;
            m.area = {stompX_ - 140.0f, floor_ - 26.0f, 280.0f, 26.0f};
            m.color = kForge;
            m.progress = 1.0f - static_cast<float>(stompWarn_) / static_cast<float>(std::max(1, steps(waveWarn_)));
            m.label = "ONDE DE CHOC";
            out.push_back(m);
        }
        const float win = static_cast<float>(std::max(1, steps(window(g))));
        for (size_t k = 0; k < restraint_.size(); ++k) {
            const Enemy* e = restraint_[k] ? w.findEnemy(restraint_[k]) : nullptr;
            if (e && e->alive()) {
                BossMark m;
                m.kind = BossMark::Kind::Target;
                m.area = e->box.expanded(8.0f);
                m.color = kGold;
                m.progress = e->hpRatio();
                m.label = "ENTRAVE";
                m.strong = otherBroken(k);
                out.push_back(m);
            } else if (brokeAt_[k] >= 0) {
                const int left = std::max(0, steps(window(g)) - static_cast<int>(ticks(g) - static_cast<uint32_t>(brokeAt_[k])));
                BossMark m;
                m.kind = BossMark::Kind::Target;
                m.area = {anchor_[k].x - 30.0f, anchor_[k].y - 30.0f, 60.0f, 60.0f};
                m.color = Color{124, 240, 168, 255};
                m.progress = static_cast<float>(left) / win;
                m.label = "BRISÉE";
                m.strong = true;
                out.push_back(m);
            }
        }
    }

    void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const override {
        const Color orange{255, 176, 74, 255}, green{124, 240, 168, 255};
        if (exposed(g)) {
            out.push_back({std::to_string(static_cast<int>(std::ceil(g.exposedLeft()))) + " s",
                           "Titanus est à genou : brisez sa rotule et ses coutures (" + std::to_string(g.weakTotal() - g.weakLeft()) + " / " +
                               std::to_string(g.weakTotal()) + ")",
                           green});
        } else if (armoured(g)) {
            int broken = 0;
            for (size_t k = 0; k < restraint_.size(); ++k) {
                const Enemy* e = restraint_[k] ? w.findEnemy(restraint_[k]) : nullptr;
                broken += !e || !e->alive() ? 1 : 0;
            }
            out.push_back({std::to_string(broken) + " / " + std::to_string(restraint_.size()),
                           "Briser ses deux entraves EN MÊME TEMPS (moins de " + seconds(window(g)) + " d'écart) : il tombera à genou.", kGold});
        } else {
            out.push_back({"→", "Vaincre Titanus · phase " + std::to_string(std::max(1, g.phase())) + " / 3 : sautez ses ondes, quittez votre marque.",
                           orange});
        }
        out.push_back({"~", "Ses ondes de choc courent au sol : sautez par-dessus (les plateformes protègent).", kForge});
        if (!rockPeriods_.empty() && at(rockPeriods_, g.phase()) > 0.0f)
            out.push_back({"◆", "Des rochers tombent de la voûte : quittez les ombres au sol.", orange});
        out.push_back(linkDone(g, "west") && linkDone(g, "east")
                          ? BossLine{"OK", "Réacteurs de la jambe coupés : ses entraves se reforgent plus lentement.", green}
                          : BossLine{"◆", "Couper les 2 réacteurs de la jambe (Fémur, Mollet) ralentirait ses entraves.", Color{196, 140, 255, 255}});
    }

    void threats(const Gigaboss& g, std::vector<Vec2>& out) const override {
        (void)g;
        for (const Wave& q : waves_) out.push_back({q.x, floor_});
        if (stompWarn_ > 0) out.push_back({stompX_, floor_});
    }

private:
    struct Wave {
        float x = 0.0f, dir = 1.0f;
    };

    static std::string seconds(float s) {
        const int tenths = static_cast<int>(std::lround(s * 10.0f));
        return tenths % 10 == 0 ? std::to_string(tenths / 10) + " s" : std::to_string(tenths / 10) + "," + std::to_string(tenths % 10) + " s";
    }

    float window(const Gigaboss& g) const {
        return window_ * slack(inPlay(g)) * (linkDone(g, "west") && linkDone(g, "east") ? reactors_ : 1.0f);
    }

    bool otherBroken(size_t k) const {
        for (size_t j = 0; j < brokeAt_.size(); ++j)
            if (j != k && brokeAt_[j] >= 0) return true;
        return false;
    }

    Vec2 spotOf(const Enemy& b, size_t k) const { return {b.box.center().x + spots_[k].x * b.facing, b.box.bottom() + spots_[k].y}; }

    /// Its restraints forged again on its ankles (all of them, or those missing).
    void forge(GameWorld& w, Contributions& c, Enemy& b, bool all) {
        const float factor = 1.0f + 0.25f * static_cast<float>(std::max(0, scaledFor_ - 1));
        for (size_t k = 0; k < spots_.size(); ++k) {
            const Enemy* e = restraint_[k] ? w.findEnemy(restraint_[k]) : nullptr;
            if (e && e->alive()) continue;
            if (!all && brokeAt_[k] < 0) continue;
            brokeAt_[k] = -1;
            breaker_[k] = -1;
            restraint_[k] = 0;
            if (Enemy* n = w.spawnEnemy(restraintId_, spotOf(b, k), b.facing < 0.0f)) {
                n->summonedBy = b.uid();
                n->countsForStats = false;
                n->scaleHealth(factor);
                restraint_[k] = n->uid();
                w.particles().emit("fire_burst", n->box.center(), 1.0f);
            }
        }
        c.setWatched(restraint_);
    }

    void clearRestraints(GameWorld& w, Contributions& c) {
        for (size_t k = 0; k < restraint_.size(); ++k) {
            if (Enemy* e = restraint_[k] ? w.findEnemy(restraint_[k]) : nullptr; e && e->alive()) e->despawn();
            restraint_[k] = 0;
            brokeAt_[k] = -1;
            breaker_[k] = -1;
        }
        c.setWatched({});
    }

    void stepRestraints(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
        // Riveted on its ankles wherever it goes.
        for (size_t k = 0; k < restraint_.size(); ++k) {
            anchor_[k] = spotOf(b, k);
            Enemy* e = restraint_[k] ? w.findEnemy(restraint_[k]) : nullptr;
            if (e && e->alive()) {
                e->box = Rect::fromBottomCenter(anchor_[k], e->box.size());
                e->prevBox = e->box;
                e->vel = {0.0f, 0.0f};
                continue;
            }
            if (restraint_[k] != 0) {
                // Broken this step: who broke it, and when.
                brokeAt_[k] = static_cast<int>(ticks(g));
                breaker_[k] = c.brokeWeak(restraint_[k]);
                restraint_[k] = 0;
                w.particles().emit("rock_shatter", anchor_[k], 1.6f);
                w.playSound("sounds/golem_slam.wav", anchor_[k], 0.8f, 1.2f);
                if (!otherBroken(k)) alert(g, "GROUPE · Une entrave cède : brisez l'autre, vite !", Group);
            }
        }
        bool all = !brokeAt_.empty();
        for (int t : brokeAt_) all = all && t >= 0;
        if (all) {
            int lo = brokeAt_[0], hi = brokeAt_[0];
            for (int t : brokeAt_) {
                lo = std::min(lo, t);
                hi = std::max(hi, t);
            }
            if (hi - lo <= steps(window(g))) {
                for (int s : breaker_)
                    if (s >= 0) c.vent(s);
                for (size_t k = 0; k < brokeAt_.size(); ++k) {
                    brokeAt_[k] = -1;
                    breaker_[k] = -1;
                }
                c.setWatched({});
                b.freeze(rules_->exposed);  // on its knee: slowed while its weak points are bare
                w.shake(1.0f);
                expose(g, w, c, b,
                       "GIGABOSS · Les deux entraves cèdent : Titanus tombe à genou, sa rotule est à nu (" +
                           std::to_string(static_cast<int>(std::lround(rules_->exposed))) + " s) !");
                return;
            }
        }
        // A restraint broken alone forges again once the window is past.
        for (size_t k = 0; k < brokeAt_.size(); ++k) {
            if (brokeAt_[k] < 0 || static_cast<int>(ticks(g)) - brokeAt_[k] <= steps(window(g))) continue;
            forge(w, c, b, false);
            alert(g, "GIGABOSS · L'entrave se reforge : brisez les deux ensemble (moins de " + seconds(window(g)) + " d'écart) !", Danger);
            break;
        }
    }

    void stepWaves(GameWorld& w, Gigaboss& g, Enemy& b) {
        const float dx = waveSpeed_ / static_cast<float>(kSimHz);
        for (Wave& q : waves_) {
            q.x += q.dir * dx;
            strike(w, b, {q.x - waveW_ * 0.5f, floor_ - waveH_, waveW_, waveH_}, waveDamage_, 260.0f, kWave);
        }
        std::erase_if(waves_, [&](const Wave& q) { return q.x < left_ || q.x > right_; });
        if (wavePeriods_.empty() || right_ <= left_) return;
        if (stompWarn_ > 0) {
            if (--stompWarn_ == 0) {
                stomp(w, b);
                if (doubleNext_) echo_ = steps(0.75f);
            }
            return;
        }
        if (echo_ > 0 && --echo_ == 0) stomp(w, b);  // the second pair of its 3rd phase
        if (--waveClock_ > 0) return;
        waveClock_ = std::max(steps(3.0f), steps(at(wavePeriods_, phase(g)) / std::max(1.0f, b.pressure())));
        stompWarn_ = std::max(1, steps(waveWarn_));
        stompX_ = b.box.bottomCenter().x;
        doubleNext_ = phase(g) >= waveDouble_ || b.lastStand();
        alert(g, "DANGER · Titanus lève le talon : sautez par-dessus l'onde de choc !", Danger);
    }

    void stomp(GameWorld& w, Enemy& b) {
        stompX_ = b.box.bottomCenter().x;
        waves_.push_back({stompX_ - 30.0f, -1.0f});
        waves_.push_back({stompX_ + 30.0f, 1.0f});
        w.shake(0.6f);
        w.particles().emit("shockwave_dust", {stompX_, floor_}, 2.0f);
        w.playSound("sounds/golem_slam.wav", {stompX_, floor_}, 1.0f, 0.7f);
    }

    void stepBoulders(GameWorld& w, Gigaboss& g, Enemy& b) {
        const float period = at(rockPeriods_, b.lastStand() ? 3 : phase(g));
        if (period <= 0.0f || top_ <= 0.0f || right_ <= left_) return;
        if (--rockClock_ > 0) return;
        rockClock_ = std::max(steps(3.0f), steps(period / std::max(1.0f, b.pressure())));
        const int count = std::max(1, static_cast<int>(at(rockCounts_, phase(g))));
        // On the players' own spots first (where they stand), then spread over the whole arena.
        std::vector<Vec2> spots;
        for (int seat : playing(w)) {
            const Player& p = w.playerAt(seat);
            if (static_cast<int>(spots.size()) < count / 2) spots.push_back({p.feet().x, p.grounded() ? p.feet().y : floor_});
        }
        const float span = (right_ - left_) / static_cast<float>(count);
        for (int k = 0; static_cast<int>(spots.size()) < count; ++k)
            spots.push_back({left_ + span * (static_cast<float>(k) + 0.2f + 0.6f * noise(static_cast<uint32_t>(volleys_ * 37 + k))), floor_});
        for (const Vec2& s : spots) {
            BossHazards::Crack r;
            r.owner = b.uid();
            r.rock = true;
            r.at = s;
            r.fromY = top_;
            r.half = 26.0f;
            r.height = 44.0f;
            r.delay = rockWarn_;
            r.burst = 0.3f;
            r.damage = rockDamage_;
            r.knockback = 260.0f;
            r.color = kForge;
            w.hazards().addCrack(r);
        }
        ++volleys_;
        alert(g, "DANGER · La voûte s'effrite : des rochers tombent, quittez les ombres !", Danger);
    }

    // Its rules.
    std::vector<float> wavePeriods_;
    float waveWarn_ = 0.9f, waveSpeed_ = 430.0f, waveH_ = 40.0f, waveW_ = 26.0f, waveDamage_ = 1.0f;
    int waveDouble_ = 3;
    float floor_ = 0.0f, left_ = 0.0f, right_ = 0.0f, top_ = 0.0f;
    std::vector<float> rockPeriods_, rockCounts_;
    float rockWarn_ = 1.3f, rockDamage_ = 1.0f;
    std::string restraintId_ = "titanus_entrave";
    std::vector<Vec2> spots_;
    float window_ = 1.5f, reactors_ = 1.5f;
    // Its state.
    std::vector<Wave> waves_;
    int waveClock_ = 0, stompWarn_ = 0, echo_ = 0;
    bool doubleNext_ = false;
    float stompX_ = 0.0f;
    int rockClock_ = 0, volleys_ = 0;
    std::vector<uint32_t> restraint_;
    std::vector<int> brokeAt_, breaker_;
    Vec2 anchor_[4];
    int scaledFor_ = 1;
    bool linkTold_ = false;
};

std::unique_ptr<GigabossRoutine> makeTitanusRoutine() { return std::make_unique<TitanusRoutine>(); }

}  // namespace cosmic::raid
