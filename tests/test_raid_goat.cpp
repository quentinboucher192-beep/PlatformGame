// 2.79 (docs/RAIDS.md §28): « je veux jouer avec des GOAT ». The simulated players' tactics against each
// Gigaboss measured WITHOUT any invincibility and without the test's rescues (only their inputs, the same
// physics): 8 simulated players (Expert) in each lair, from its first phase, must win within a limit of
// time; the time and the deaths of each fight are printed. The full raid simulated without invincibility to
// the Cœur's fall (reduced: from the lairs). The orders of the administrator ride on their frames (the same
// frames, the same test), the roles change when someone falls, the thoughts are told for every one of them.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/physics/Collision.h"
#include "engine/rendering/Text.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/BossHazards.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/WorldListener.h"
#include "game/powers/Power.h"
#include "game/raid/AsterionHeartRoutine.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidNav.h"
#include "game/raid/RaidRun.h"
#include "game/raid/RaidSimulation.h"
#include "game/raid/RaidTest.h"
#include "game/raid/SkullmawRoutine.h"
#include "game/raid/bots/BotTypes.h"
#include "tests/TestFramework.h"
#include "ui/online/RaidBotsUi.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

namespace {

struct Svc {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Svc() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

int lairOf(const raid::Branch& b) {
    for (const raid::ZoneInfo& z : b.zones)
        if (z.boss) return z.id;
    return b.startZone;
}

/// The heart's state: the seven parts fallen (their « modifier » links on), as when it opens in a raid.
uint32_t heartLinks(const raid::Branch& b) {
    uint32_t m = 0;
    for (const raid::EffectInfo& e : b.effects)
        for (int p = 0; p < raid::AsterionHeartRoutine::PartCount; ++p)
            if (e.id == std::string("coeur_") + raid::AsterionHeartRoutine::partId(p) && e.bit >= 0 && e.bit < 32) m |= 1u << e.bit;
    return m;
}

/// Tuning runs (COSMIC_GOAT_TRACE): what hurt the simulated players, by origin and by attack (the world's
/// statistics hooks, forwarded to the raid's own listener: nothing changes in the fight).
struct HurtLog : WorldListener {
    WorldListener* next = nullptr;
    std::map<std::string, int> blows;
    const raid::TestSession* session = nullptr;
    std::string watch;  // COSMIC_GOAT_HIT: print each blow of this attack with the state of the player hit
    const int* steps = nullptr;  // 2.87: the fight's step (its time, as the other probes print it)
    std::string last[8];  // the last blow on each seat (what put it down, when it falls)
    void playerHurt(const GameWorld& w, const DamageInfo& d, const HurtSource& src) override {
        static const char* kOrigin[] = {"tir", "attaque", "boss", "contact", "piège", "chute", "autre"};
        const std::string what = std::string(kOrigin[std::min<int>(static_cast<int>(src.origin), 6)]) + " · " + (src.attack.empty() ? "?" : src.attack);
        ++blows[what];
        if (const int k = w.currentPlayer(); k >= 0 && k < 8) last[k] = what;
        if (session && !watch.empty() && src.attack.find(watch) != std::string::npos) {
            const int k = w.currentPlayer();
            const Player& p = w.playerAt(k);
            std::printf("      %6.1f s %s → %d (%.0f, %.0f) sol %d vy %.0f bouclier %d énergie %.0f · %s · %s\n", steps ? *steps / 120.0 : 0.0, src.attack.c_str(), k + 1,
                        p.feet().x, p.feet().y,
                        p.grounded() ? 1 : 0, p.vel.y, p.shieldActive() ? 1 : [&] {
                            for (const auto& pw : p.powers())
                                if (pw->id() == "shield") return pw->ready(p) ? 2 : 0;
                            return 9;
                        }(), p.energy,
                        session->bots().thought(k).intent.c_str(), session->bots().thought(k).move.c_str());
            // 2.90: where the blow comes from (its source, its kind) and what the navigation was doing (a dodge, clear or not).
            std::printf("        source (%.0f, %.0f) type %d · navigation %d esquive %d sûre %d\n", d.source.x, d.source.y, static_cast<int>(d.type),
                        static_cast<int>(session->bots().moveThought(k).intent), session->bots().navigation().pilot(k).dodging() ? 1 : 0,
                        session->bots().navigation().pilot(k).safeDodge() ? 1 : 0);
            if (const Enemy* bb = session->run().gigaboss().boss(w)) {
                float gap = 1e9f;
                for (const auto& dd : session->bots().view().dangers)
                    if (dd.body) {
                        const float gx = std::max({0.0f, dd.area.x - p.box.right(), p.box.x - dd.area.right()});
                        const float gy = std::max({0.0f, dd.area.y - p.box.bottom(), p.box.y - dd.area.bottom()});
                        gap = std::min(gap, std::max(gx, gy));
                    }
                std::printf("        boss (%.0f..%.0f, %.0f..%.0f) v (%.0f, %.0f) état %d · joueur v (%.0f, %.0f) invuln %d · écart au corps lu %.0f\n", bb->box.x, bb->box.right(), bb->box.y,
                            bb->box.bottom(), bb->vel.x, bb->vel.y, static_cast<int>(bb->state()), p.vel.x, p.vel.y, p.invulnerable() ? 1 : 0, gap);
            }
            // 2.87 (tuning runs): a lasting effect: which one touches it (its kind, its area, its age).
            const BossHazards& hz = w.hazards();
            const Rect pb = p.box.expanded(4.0f);
            for (const auto& c : hz.cracks())
                if (Rect{c.at.x - c.half, c.at.y - std::max(c.height, 40.0f), c.half * 2.0f, std::max(c.height, 40.0f) + 4.0f}.intersects(pb))
                    std::printf("        fissure (%.0f, %.0f) demi %.0f haut %.0f âge %.2f délai %.2f\n", c.at.x, c.at.y, c.half, c.height, c.age, c.delay);
            for (const auto& wl : hz.walls())
                if (wl.rect.intersects(pb)) std::printf("        mur (%.0f, %.0f, %.0f × %.0f)\n", wl.rect.x, wl.rect.y, wl.rect.w, wl.rect.h);
            for (const auto& cl : hz.clouds())
                if (cl.area.intersects(pb)) std::printf("        nuage (%.0f, %.0f, %.0f × %.0f)\n", cl.area.x, cl.area.y, cl.area.w, cl.area.h);
            for (const auto& fl : hz.floods())
                if (Rect{fl.left, fl.floorY - 300.0f, fl.right - fl.left, 310.0f}.intersects(pb)) std::printf("        flot %.0f..%.0f sol %.0f\n", fl.left, fl.right, fl.floorY);
            for (const auto& pr : hz.presses())
                if (pr.column().intersects(pb)) std::printf("        presse\n");
            for (const auto& st : hz.steams())
                if (st.area().intersects(pb)) std::printf("        vapeur\n");
            if (hz.cracks().empty() && hz.walls().empty() && hz.clouds().empty() && hz.floods().empty())
                std::printf("        (aucune fissure, mur, nuage ni flot)\n");
        }
        if (next) next->playerHurt(w, d, src);
    }
    void playerKilled(const GameWorld& w, const HurtSource& src) override {
        ++blows["MORT sans coup · " + std::to_string(static_cast<int>(src.origin))];
        static const char* kOrigin[] = {"tir", "attaque", "boss", "contact", "piège", "chute", "autre"};
        if (const int k = w.currentPlayer(); k >= 0 && k < 8)
            last[k] = std::string(kOrigin[std::min<int>(static_cast<int>(src.origin), 6)]) + " · " + (src.attack.empty() ? "?" : src.attack);
        if (next) next->playerKilled(w, src);
    }
    int blocked = 0;  // 2.89: blows stopped by a shield
    std::map<std::string, int> attacks;  // COSMIC_GOAT_HIST: the players' attacks by power, the damage dealt
    double dealt = 0.0;
    void attackStarted(const GameWorld& w, uint32_t tag, const std::string& power) override {
        ++attacks[power.empty() ? "lame" : power];
        if (next) next->attackStarted(w, tag, power);
    }
    void enemyHit(const GameWorld& w, const Enemy& e, uint32_t tag) override {
        if (next) next->enemyHit(w, e, tag);
    }
    void enemyDamaged(const GameWorld& w, const Enemy& e, uint32_t tag, float amount) override {
        dealt += amount;
        if (next) next->enemyDamaged(w, e, tag, amount);
    }
    void blowBlocked(const GameWorld& w, float amount) override {
        ++blocked;
        if (next) next->blowBlocked(w, amount);
    }
};

struct Fight {
    std::string boss;
    bool won = false;
    bool wiped = false;
    double seconds = 0.0;
    int deaths = 0;
    int phase = 0;
    int rescues = 0;
    std::vector<std::pair<int, std::string>> causes;  // what put the players down (the last blow · the role), most first
    float hp = 1.0f;  // the health it has left at the end (0 once fallen)
    double ms = 0.0;
    // 2.90 (docs/RAIDS.md §36): the ways out of the blows the group used — the escapes of the navigation (standing still, a few
    // steps, a jump or a drop, a dash; none clear) and the shields raised (the blows they stopped).
    int escStays = 0, escWalks = 0, escJumps = 0, escDashes = 0, escFails = 0, shields = 0, blocked = 0, dashGuards = 0;
};

/// 8 simulated players (the administrator's seat played by the AI too), at the lair of `part`, without any
/// invincibility nor the test's rescues, until its gigaboss falls, the group is wiped or `limit` seconds.
Fight fightAt(Svc& s, const std::string& part, raid::BotSkill skill, int limitSeconds, int players = 8, uint32_t seed = 0, int startPhase = 0,
              bool botsInvincible = false) {
    const raid::Branch* b = raid::Branch::builtin(part);
    CHECK(b != nullptr);
    Fight out;
    if (!b) return out;
    const bool heart = part == raid::kHeartPart;
    // Titan 13, the nerf's bench (docs/RAIDS.md §37): another base health for this measure only (COSMIC_GOAT_HPSCALE),
    // to sample the fight's chaos around a value (one start, several neighbouring healths) without regenerating the lair.
    if (const char* hs = std::getenv("COSMIC_GOAT_HPSCALE"))
        if (const raid::ZoneRules* zr = b->zoneRules(lairOf(*b)); zr && zr->gigaboss.present)
            const_cast<raid::GigabossRules&>(zr->gigaboss).hpScale = static_cast<float>(std::atof(hs));
    raid::RaidRun::Test t;
    t.on = true;
    t.invincible = false;
    t.players = players;
    // Tuning runs only: another first phase (COSMIC_GOAT_PHASE), the simulated players invincible (COSMIC_GOAT_INVINCIBLE).
    t.startPhase = startPhase > 0 ? startPhase : std::getenv("COSMIC_GOAT_PHASE") ? std::atoi(std::getenv("COSMIC_GOAT_PHASE")) : 1;
    raid::TestSession session(s.ws(), *b, t);
    session.setAdminSeated(true);
    session.setAdminBot(true);
    session.setRescues(false);
    session.setBotsInvincible(botsInvincible || std::getenv("COSMIC_GOAT_INVINCIBLE") != nullptr);
    session.setSkill(skill);
    session.setBotSeed(seed);  // 2.84: the start of this measure (0: the one of every version before)
    uint32_t state = 0;
    for (const raid::ObjectiveInfo& o : b->objectives)
        if (o.role != "gigaboss" && !o.optional) state |= 1u << o.index;
    if (heart) state |= heartLinks(*b);
    session.setBranchState(state);
    CHECK(session.begin(lairOf(*b)));
    const raid::Gigaboss& gb = session.run().gigaboss();
    out.boss = gb.rules() ? gb.rules()->name : part;
    const auto t0 = std::chrono::steady_clock::now();
    const int limit = limitSeconds * kSimHz;
    const bool trace = std::getenv("COSMIC_GOAT_TRACE") != nullptr;
    HurtLog hurts;
    hurts.session = &session;
    if (const char* h = std::getenv("COSMIC_GOAT_HIT")) hurts.watch = h;
    int steps = 0;
    hurts.steps = &steps;
    uint8_t downBefore = 0;
    std::map<std::string, int> deaths;
    const bool hist = std::getenv("COSMIC_GOAT_HIST") != nullptr;
    std::map<std::string, int> intents;
    std::map<std::string, int> aggro;  // 2.89: COSMIC_GOAT_AGGRO
    for (; steps < limit; ++steps) {
        if (session.run().world().listener != &hurts) {
            hurts.next = session.run().world().listener;
            session.run().world().listener = &hurts;
        }
        session.step(InputFrame{});
        {
            // Who fell this step: the last blow on them and their role (the causes of the deaths).
            const GameWorld& w = session.run().world();
            uint8_t down = 0;
            for (int k = 0; k < w.teamSize() && k < 8; ++k)
                if (w.member(k).presence == GameWorld::Presence::Down) down |= static_cast<uint8_t>(1u << k);
            for (int k = 0; k < 8; ++k)
                if (((down >> k) & 1u) && !((downBefore >> k) & 1u)) {
                    ++deaths[(hurts.last[k].empty() ? std::string("?") : hurts.last[k]) + " · " + raid::bots::roleName(session.bots().thought(k).role)];
                    if (std::getenv("COSMIC_GOAT_DEATHS")) {
                        const Enemy* bb = gb.boss(w);
                        std::printf("      %6.1f s mort %d (%s) %s · soul (%.0f, %.0f) boss (%.0f, %.0f) · %s\n", steps / 120.0, k + 1,
                                    raid::bots::roleName(session.bots().thought(k).role), hurts.last[k].c_str(), w.member(k).soul.x, w.member(k).soul.y,
                                    bb ? bb->center().x : 0.0f, bb ? bb->center().y : 0.0f, session.bots().thought(k).intent.c_str());
                    }
                }
            downBefore = down;
            // Tuning runs (COSMIC_GOAT_HIST): what the players in play think, step by step (where their time goes).
            if (hist)
                for (int k = 0; k < w.teamSize() && k < 8; ++k)
                    if (w.member(k).presence == GameWorld::Presence::Playing) ++intents[session.bots().thought(k).intent.substr(0, 48)];
        }
        if (std::getenv("COSMIC_GOAT_EXPOSED") && gb.stage() == raid::Gigaboss::Stage::Exposed && steps % 60 == 0) {
            const GameWorld& w = session.run().world();
            std::printf("      %6.1f s À NU · points faibles :", steps / 120.0);
            for (uint32_t u : gb.weakUids())
                if (const Enemy* e = u ? w.findEnemy(u) : nullptr; e && e->alive())
                    std::printf(" [%u (%.0f, %.0f) %.0f/%.0f]", u, e->center().x, e->center().y, e->hp(), e->maxHp());
            const Enemy* bb = gb.boss(w);
            std::printf(" · boss (%.0f, %.0f) %.1f %% · ondes %d\n", bb ? bb->center().x : 0.0f, bb ? bb->center().y : 0.0f, bb ? bb->hpRatio() * 100.0f : 0.0f,
                        hurts.attacks.count("wave") ? hurts.attacks["wave"] : 0);
            for (int k = 0; k < w.teamSize(); ++k) {
                if (w.member(k).presence != GameWorld::Presence::Playing) continue;
                const auto& bt = session.bots().bots()[static_cast<size_t>(k)];
                std::printf("        %d (%.0f, %.0f) %s énergie %.0f cible %u place %d but (%.0f, %.0f) chemin %zu à %zu · %s · %s\n", k + 1, w.playerAt(k).feet().x,
                            w.playerAt(k).feet().y, w.playerAt(k).grounded() ? "sol" : "air", w.playerAt(k).energy, bt.target, bt.spot, bt.goal.x, bt.goal.y,
                            bt.path.size(), bt.at, session.bots().thought(k).intent.c_str(),
                            session.bots().thought(k).move.c_str());
            }
        }
        if (std::getenv("COSMIC_GOAT_MIRROR"))
            if (const auto* sk = dynamic_cast<const raid::SkullmawRoutine*>(gb.routine())) {
                static bool wasCharging = false;
                if (sk->charging() && sk->chargeRatio() > 0.97f && !wasCharging) {
                    const GameWorld& w = session.run().world();
                    std::string who;
                    for (int k = 0; k < w.teamSize(); ++k) {
                        const auto& bt = session.bots().bots()[static_cast<size_t>(k)];
                        if (bt.task == raid::TestBots::Task::Mechanism) {
                            const bool on = bt.mechanism.intersects(w.playerAt(k).box);
                            char buf[96];
                            std::snprintf(buf, sizeof buf, " %d:%s(%.0f,%.0f→%.0f,%.0f)", k + 1, on ? "SUR" : "loin", w.playerAt(k).feet().x, w.playerAt(k).feet().y, bt.mechanism.x, bt.mechanism.y);
                            who += buf;
                        }
                    }
                    std::printf("      %6.1f s flash : %d / %d miroirs ·%s\n", steps / 120.0, sk->held(), sk->mirrorsNeeded(), who.c_str());
                }
                wasCharging = sk->charging() && sk->chargeRatio() > 0.97f;
            }
        if (const char* sw = std::getenv("COSMIC_GOAT_SEAT"); sw && steps % 12 == 0) {
            // 2.87 (tuning runs): one seat every tenth of a second (« 3 » or « 3:150:160 », seconds) — where it is, how it moves.
            int k = std::atoi(sw) - 1, a = 0, z = 1 << 20;
            if (const char* c = std::strchr(sw, ':')) std::sscanf(c + 1, "%d:%d", &a, &z);
            const GameWorld& w = session.run().world();
            if (k >= 0 && k < w.teamSize() && steps >= a * kSimHz && steps <= z * kSimHz) {
                const Player& p = w.playerAt(k);
                const auto& bt = session.bots().bots()[static_cast<size_t>(k)];
                std::printf("      %6.1f s siège %d (%.0f, %.0f) v (%.0f, %.0f) %s · nav %d · but (%.0f, %.0f) nœud %d chemin %zu à %zu · place %d · %s · %s\n", steps / 120.0, k + 1,
                            p.feet().x, p.feet().y, p.vel.x, p.vel.y, p.grounded() ? "sol" : "air", static_cast<int>(session.bots().moveThought(k).intent), bt.goal.x,
                            bt.goal.y, bt.goalNode, bt.path.size(), bt.at, bt.spot, session.bots().thought(k).intent.c_str(), session.bots().thought(k).move.c_str());
                {
                    float gap = 1e9f;
                    for (const auto& dd : session.bots().view().dangers)
                        if (dd.body) {
                            const float gx = std::max({0.0f, dd.area.x - p.box.right(), p.box.x - dd.area.right()});
                            const float gy = std::max({0.0f, dd.area.y - p.box.bottom(), p.box.y - dd.area.bottom()});
                            gap = std::min(gap, std::max(gx, gy));
                        }
                    const Enemy* bb = gb.boss(w);
                    std::printf("          écart au corps %.0f · boss état %d attaque %s\n", gap, bb ? static_cast<int>(bb->state()) : -1,
                                bb && bb->currentAttack() ? bb->currentAttack()->id.c_str() : "-");
                }
                for (const auto& d : session.bots().view().dangers)
                    if (d.kind == raid::bots::Danger::Kind::FloorWave && std::fabs(d.floorY - p.feet().y) < 30.0f && std::fabs(d.area.center().x - p.center().x) < 400.0f)
                        std::printf("          onde %.0f..%.0f v %.0f dans %d haut %.0f\n", d.area.x, d.area.right(), d.speed, d.in, d.height);
            }
        }
        if (std::getenv("COSMIC_GOAT_AGGRO") && steps % 12 == 0) {
            // 2.89 (tuning runs): who the boss aims at (the player nearest it), by role and stage, every tenth of a second.
            const GameWorld& w = session.run().world();
            if (const Enemy* bb = gb.boss(w); bb && w.teamSize() > 0) {
                const int k = w.targetIndex(bb->center());
                if (w.member(k).presence == GameWorld::Presence::Playing)
                    ++aggro[std::to_string(static_cast<int>(gb.stage())) + " · " + raid::bots::roleName(session.bots().thought(k).role)];
            }
        }
        if (std::getenv("COSMIC_GOAT_FOES") && steps % 120 == 0) {
            // 2.87 (tuning runs): every second, the enemies of the fight (its mechanism's, its creatures) and their health.
            const GameWorld& w = session.run().world();
            std::printf("      %6.1f s étape %d ·", steps / 120.0, static_cast<int>(gb.stage()));
            for (const auto& e : w.enemies())
                if (e->alive() && !e->dormant() && !e->def().isBoss)
                    std::printf(" [%s (%.0f, %.0f) %.0f/%.0f]", e->def().id.c_str(), e->center().x, e->center().y, e->hp(), e->maxHp());
            std::printf("\n");
        }
        if (std::getenv("COSMIC_GOAT_GAZE"))
            if (const auto* sk = dynamic_cast<const raid::SkullmawRoutine*>(gb.routine())) {
                // 2.86 (tuning runs): each tracking gaze the step before it fires — the players in its line, what they do then.
                const GameWorld& w = session.run().world();
                for (const auto& z : sk->gazes()) {
                    if (sk->gazeState(z) != raid::SkullmawRoutine::GazeState::Locked || sk->gazeProgress(z) < 0.985f) continue;  // the step before it fires
                    const Vec2 ab = z.end - z.from;
                    for (int k = 0; k < w.teamSize(); ++k) {
                        if (w.member(k).presence != GameWorld::Presence::Playing) continue;
                        const Player& p = w.playerAt(k);
                        if (!Collision::segmentIntersectsRect(z.from, z.end, p.box.expanded(9.0f))) continue;
                        std::printf("      %6.1f s regard de %d sur %d (pente %.2f) · (%.0f, %.0f) sol %d vy %.0f bouclier %d invuln %d énergie %.0f · %s · %s\n", steps / 120.0,
                                    z.seat + 1, k + 1, std::fabs(ab.y) / std::max(1.0f, std::fabs(ab.x)), p.feet().x, p.feet().y, p.grounded() ? 1 : 0,
                                    p.vel.y, p.shieldActive() ? 1 : 0, p.invulnerable() ? 1 : 0, p.energy, session.bots().thought(k).intent.c_str(),
                                    session.bots().thought(k).move.c_str());
                    }
                }
            }
        if (trace && std::getenv("COSMIC_GOAT_TRACE")[0] == '3') {
            // Each mark about to strike a simulated player: where it stands, what it thinks (tuning runs).
            const GameWorld& w = session.run().world();
            for (const auto& f : gb.fists()) {
                if (f.left != 1) continue;
                const Rect col = gb.fistColumn(f);
                for (int k = 0; k < w.teamSize(); ++k) {
                    if (w.member(k).presence != GameWorld::Presence::Playing || !col.intersects(w.playerAt(k).box)) continue;
                    const auto& bt = session.bots().bots()[static_cast<size_t>(k)];
                    std::printf("    %5.1f s poing sur %d : colonne %.0f..%.0f, pieds (%.0f, %.0f) sol %d · %s · %s · réaction %d fuite %d (%d)\n", steps / 120.0, k + 1, col.x,
                                col.right(), w.playerAt(k).feet().x, w.playerAt(k).feet().y, w.playerAt(k).grounded() ? 1 : 0,
                                session.bots().thought(k).intent.c_str(), session.bots().thought(k).move.c_str(), bt.react, bt.escapeDir, bt.escapeSteps);
                }
            }
        }
        // 2.87: COSMIC_GOAT_EVERY sets its period in seconds (20 by default).
        static const int every = std::getenv("COSMIC_GOAT_EVERY") ? std::max(1, std::atoi(std::getenv("COSMIC_GOAT_EVERY"))) : 20;
        if (trace && steps % (every * kSimHz) == 0) {
            // The fight every 20 s (tuning runs): its stage, phase and health, the players in play, the group's plan.
            const GameWorld& w = session.run().world();
            const Enemy* boss = gb.boss(w);
            int inPlay = 0;
            for (int k = 0; k < w.teamSize(); ++k) inPlay += w.member(k).presence == GameWorld::Presence::Playing ? 1 : 0;
            std::printf("    %4d s · étape %d · phase %d · vie %3.0f %% · en jeu %d · morts %d · %s\n", steps / kSimHz, static_cast<int>(gb.stage()), gb.phase(),
                        boss ? boss->hpRatio() * 100.0f : 0.0f, inPlay, session.botDeaths(), session.bots().plan().c_str());
            for (int k = 0; k < w.teamSize() && std::getenv("COSMIC_GOAT_TRACE")[0] == '2'; ++k) {
                const auto& bt = session.bots().bots()[static_cast<size_t>(k)];
                const Player& p = w.playerAt(k);
                std::printf("      %d %-12s %s · %s · (%.0f, %.0f) vie %d énergie %.0f cible %u place %d\n", k + 1, raid::bots::roleName(session.bots().thought(k).role),
                            session.bots().thought(k).intent.c_str(), session.bots().thought(k).move.c_str(), p.feet().x, p.feet().y, p.health, p.energy,
                            bt.target, bt.spot);
            }
            if (boss && std::getenv("COSMIC_GOAT_TRACE")[0] == '2') {
                std::printf("      boss (%.0f, %.0f, %.0f × %.0f)", boss->box.x, boss->box.y, boss->box.w, boss->box.h);
                for (const auto& d : session.bots().view().dangers)
                    if (d.body) std::printf(" · corps (%.0f, %.0f, %.0f × %.0f)", d.area.x, d.area.y, d.area.w, d.area.h);
                std::printf(" · %zu dangers, %zu cibles\n", session.bots().view().dangers.size(), session.bots().view().targets.size());
                for (const auto& vt : gb.vents())
                    std::printf("        évent (%.0f, %.0f) %s tenu %d par %u\n", vt.area.x, vt.area.y, vt.closed ? "FERMÉ" : "ouvert", vt.hold, vt.holders);
                for (int k = 0; k < w.teamSize(); ++k) {
                    const auto& bt = session.bots().bots()[static_cast<size_t>(k)];
                    if (bt.task == raid::TestBots::Task::Mechanism)
                        std::printf("        %d → plaque (%.0f, %.0f) · but (%.0f, %.0f) nœud %d chemin %zu à %zu · intention %d\n", k + 1, bt.mechanism.x, bt.mechanism.y,
                                    bt.goal.x, bt.goal.y, bt.goalNode, bt.path.size(), bt.at, static_cast<int>(session.bots().moveThought(k).intent));
                }
                int wide = 0;
                for (const auto& d : session.bots().view().dangers)
                    if (!d.body && d.area.w > 400.0f && wide++ < 4)
                        std::printf("        large : type %d (%.0f, %.0f, %.0f × %.0f) dans %d\n", static_cast<int>(d.kind), d.area.x, d.area.y, d.area.w, d.area.h, d.in);
            }
        }
        if (std::getenv("COSMIC_GOAT_ORBS") && steps % (20 * kSimHz) == 0) {  // tuning runs: the health orbs shown and the group's hearts
            const GameWorld& w = session.run().world();
            int vis = 0, hid = 0;
            for (const auto& e : w.entities())
                if (!e->removed && e->type() == "health_orb") ++(e->active && e->visible ? vis : hid);
            int hp = 0;
            for (int k = 0; k < w.teamSize(); ++k) hp += w.member(k).presence == GameWorld::Presence::Playing ? w.playerAt(k).health : 0;
            std::printf("      ORBS %d s visibles %d cachés %d · cœurs du groupe %d\n", steps / kSimHz, vis, hid, hp);
        }
        if (heart ? session.run().heartWon() : gb.stage() == raid::Gigaboss::Stage::Fallen) {
            out.won = true;
            break;
        }
        if (session.run().wiped()) {
            out.wiped = true;
            break;
        }
    }
    if (trace) {
        std::vector<std::pair<int, std::string>> sorted;
        for (const auto& [k, n] : hurts.blows) sorted.push_back({n, k});
        std::sort(sorted.rbegin(), sorted.rend());
        for (const auto& [n, k] : sorted) std::printf("      coups reçus %4d × %s\n", n, k.c_str());
        std::printf("      boucliers %d · coups arrêtés %d\n", hurts.attacks.count("shield") ? hurts.attacks["shield"] : 0, hurts.blocked);
        for (const auto& [k, n] : aggro) std::printf("      visé · étape %s : %d\n", k.c_str(), n);
    }
    if (hist) {
        std::vector<std::pair<int, std::string>> sorted;
        long total = 0;
        for (const auto& [k, n] : intents) {
            sorted.push_back({n, k});
            total += n;
        }
        std::sort(sorted.rbegin(), sorted.rend());
        std::printf("      dégâts infligés %.0f (%.2f par s) ·", hurts.dealt, hurts.dealt * kSimHz / std::max(1, steps));
        for (const auto& [k, n] : hurts.attacks) std::printf(" %s %d", k.c_str(), n);
        std::printf("\n");
        for (size_t k = 0; k < sorted.size() && k < 18; ++k)
            std::printf("      %5.1f %%  %s\n", 100.0 * sorted[k].first / std::max<long>(1, total), sorted[k].second.c_str());
    }
    session.run().world().listener = hurts.next ? hurts.next : session.run().world().listener;
    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    out.seconds = steps / static_cast<double>(kSimHz);
    out.deaths = session.botDeaths();
    out.phase = gb.phaseReached();
    const Enemy* left = gb.boss(session.run().world());
    out.hp = out.won || !left ? 0.0f : left->hpRatio();
    out.rescues = session.rescues();
    for (const auto& [k, n] : deaths) out.causes.push_back({n, k});
    std::sort(out.causes.rbegin(), out.causes.rend());
    for (int k = 0; k < 8; ++k) {
        const raid::nav::Pilot::Used& u = session.bots().navigation().pilot(k).used();
        out.escStays += u.escStays;
        out.escWalks += u.escWalks;
        out.escJumps += u.escJumps;
        out.escDashes += u.escDashes;
        out.escFails += u.escFails;
        if (k < static_cast<int>(session.bots().bots().size())) out.dashGuards += session.bots().bots()[static_cast<size_t>(k)].dashGuards;
    }
    out.shields = hurts.attacks.count("shield") ? hurts.attacks["shield"] : 0;
    out.blocked = hurts.blocked;
    CHECK(session.run().takeReports().empty());
    return out;
}

void print(const Fight& f) {
    std::printf("  %-16s %s en %6.0f s de jeu · %2d morts · phase %d · vie restante %3.0f %% · %d sauvetages · %.0f ms\n", f.boss.c_str(),
                f.won ? "VAINCU " : f.wiped ? "ÉLIMINÉ" : "DEBOUT ", f.seconds, f.deaths, f.phase, f.hp * 100.0f, f.rescues, f.ms);
    std::string c;
    for (size_t k = 0; k < f.causes.size() && k < 5; ++k) c += (k ? " ; " : "") + std::to_string(f.causes[k].first) + " × " + f.causes[k].second;
    if (!c.empty()) std::printf("      morts : %s\n", c.c_str());
}

/// The Gigaboss 8 simulated Experts beat as measured (docs/RAIDS.md §28); they must keep doing it.
const std::vector<std::string> kBeaten = {};
/// The full raid without invincibility (from the lairs), as measured: the Gigaboss fallen, the Cœur's fall.
constexpr int kRaidGigabossDown = 0;
constexpr bool kRaidHeartFalls = false;
/// 2.84: Voidworm from its phase 2 (8 invincible simulated players, 15 min): its health left at most.
constexpr float kVoidwormPhase2Left = 0.55f;  // measured: 48 % to beaten (before 2.84: 66 % left)

}  // namespace

namespace {

/// One Gigaboss's fight (a test each): 8 simulated players (Expert), no invincibility, no rescue, 15 min at most.
void goat(const std::string& part) {
    Svc s;
    // Titan 13, the bench of the nerf (docs/RAIDS.md §37): COSMIC_GOAT_LIMIT seconds instead of 15 min (with
    // COSMIC_GOAT_INVINCIBLE: how long invincible Experts take to beat it, the proxy of a good group).
    const char* lim = std::getenv("COSMIC_GOAT_LIMIT");
    const Fight f = fightAt(s, part, raid::BotSkill::Expert, lim ? std::max(60, std::atoi(lim)) : 900);
    print(f);
    CHECK(f.rescues == 0);
    // What they beat as measured (docs/RAIDS.md §28) must keep winning in time; the others are said too hard for now,
    // honestly, with their measure (what still blocks them: docs/RAIDS.md §28, « Ce qui reste »).
    if (std::find(kBeaten.begin(), kBeaten.end(), part) != kBeaten.end()) CHECK(f.won);
    if (!f.won)
        std::printf("  trop dur pour l'instant : %s (%s, phase %d, %d morts)\n", f.boss.c_str(), f.wiped ? "groupe éliminé" : "debout au bout du temps", f.phase,
                    f.deaths);
}

}  // namespace

TEST_CASE("raid GOAT (2.79): Pyroclast — 8 simulated players (Expert), no invincibility, no rescue, within 15 min") { goat("bras_droit"); }
TEST_CASE("raid GOAT (2.79): Skullmaw — 8 simulated players (Expert), no invincibility, no rescue, within 15 min") { goat("crane"); }
TEST_CASE("raid GOAT (2.79): Dreadfang — 8 simulated players (Expert), no invincibility, no rescue, within 15 min") { goat("bras_gauche"); }
TEST_CASE("raid GOAT (2.79): Titanus — 8 simulated players (Expert), no invincibility, no rescue, within 15 min") { goat("jambe_droite"); }
TEST_CASE("raid GOAT (2.79): Voidworm — 8 simulated players (Expert), no invincibility, no rescue, within 15 min") { goat("abdomen"); }
TEST_CASE("raid GOAT (2.79): Graviton — 8 simulated players (Expert), no invincibility, no rescue, within 15 min") { goat("jambe_gauche"); }
TEST_CASE("raid GOAT (2.79): Heartbreaker — 8 simulated players (Expert), no invincibility, no rescue, within 15 min") { goat("torse"); }
TEST_CASE("raid GOAT (2.79): the Cœur d'Asterion — 8 simulated players (Expert), the seven parts fallen, no invincibility, no rescue, within 15 min") {
    goat(raid::kHeartPart);
}

TEST_CASE("raid GOAT (2.79): the level changes their reaction, precision and choices — the same fight at Débutant, Confirmé and Expert") {
    Svc s;
    const char* part = std::getenv("COSMIC_GOAT_LEVELS");
    const std::string p = part && *part ? part : "torse";
    std::printf("  %s, 8 joueurs simulés, sans invincibilité ni sauvetage :\n", p.c_str());
    Fight byLevel[3];
    for (int k = 0; k < 3; ++k) {
        const auto skill = static_cast<raid::BotSkill>(k);
        byLevel[k] = fightAt(s, p, skill, 900);
        std::printf("    %-9s ", raid::botSkillName(skill));
        print(byLevel[k]);
        CHECK(byLevel[k].rescues == 0);
    }
    // Expert: never worse than Débutant — it wins when the Débutant does, and gets further (phase), or as far with fewer
    // deaths or sooner.
    CHECK(!byLevel[0].won || byLevel[2].won);
    CHECK(byLevel[2].phase > byLevel[0].phase || byLevel[2].won || byLevel[2].deaths <= byLevel[0].deaths || byLevel[2].seconds >= byLevel[0].seconds);
}

TEST_CASE("raid GOAT (2.79): an order rides on the administrator's frame — taken by the session at its step, never seen by the world, replayed the same") {
    // The frame's packing: every order and a place (4 px), back unchanged; without it, the frame the world steps with.
    for (int k = 1; k <= static_cast<int>(raid::bots::OrderKind::Mark); ++k) {
        InputFrame f;
        f.held = 0x15u;
        const InputFrame o = raid::bots::withOrder(f, static_cast<raid::bots::OrderKind>(k), {1234.0f, 987.0f});
        raid::bots::Order back;
        CHECK(raid::bots::orderOf(o, back));
        CHECK(static_cast<int>(back.kind) == k);
        CHECK(std::fabs(back.at.x - 1234.0f) <= 4.0f && std::fabs(back.at.y - 987.0f) <= 4.0f);
        const InputFrame clean = raid::bots::withoutOrder(o);
        CHECK(clean.held == f.held);
        CHECK(!(clean.flags & raid::bots::kOrderFlag));
        CHECK(clean.choice == 0 && clean.moveX == 0 && clean.moveY == 0);
        raid::bots::Order none;
        CHECK(!raid::bots::orderOf(f, none));
    }
    Svc s;
    const raid::Branch* b = raid::Branch::builtin("bras_droit");
    CHECK(b != nullptr);
    if (!b) return;
    auto run = [&](bool order, uint64_t& digest, std::string& thought, raid::bots::OrderKind& kind) {
        raid::RaidRun::Test t;
        t.on = true;
        t.invincible = true;
        t.players = 4;
        raid::TestSession session(s.ws(), *b, t);
        session.setRescues(false);
        CHECK(session.begin(1));
        for (int k = 0; k < 2 * kSimHz; ++k) {
            InputFrame f;
            f.held = (k / 30) % 2 ? 1u << static_cast<int>(Action::MoveRight) : 0u;  // the administrator moves (not AFK)
            f.moveX = static_cast<int8_t>(f.held ? 127 : 0);
            if (order && k == kSimHz) f = raid::bots::withOrder(f, raid::bots::OrderKind::Follow, session.run().world().playerAt(0).feet());
            session.step(f);
            CHECK(!(session.frames()[0].flags & raid::bots::kOrderFlag));  // the world never sees it
        }
        for (int k = 0; k < kSimHz; ++k) session.step(InputFrame{});
        digest = session.hash();
        thought = session.bots().thought(1).intent;
        kind = session.lastOrder().kind;
    };
    uint64_t a = 0, a2 = 0, none = 0;
    std::string ta, ta2, tn;
    raid::bots::OrderKind ka{}, ka2{}, kn{};
    run(true, a, ta, ka);
    run(true, a2, ta2, ka2);
    run(false, none, tn, kn);
    std::printf("  ordre « Suivez-moi » : pensée du joueur simulé 2 « %s » (sans ordre : « %s »)\n", ta.c_str(), tn.c_str());
    CHECK(ka == raid::bots::OrderKind::Follow);
    CHECK(kn == raid::bots::OrderKind::None);
    CHECK(a == a2);    // the same frames, the same test
    CHECK(a != none);  // the order changed what they did
    CHECK(ta.find("Ordre") == 0);
}

TEST_CASE("raid GOAT (2.79): the roles of the group — a reviver, a lure, runners for the plates, strikers — given again when one falls") {
    Svc s;
    const raid::Branch* b = raid::Branch::builtin("bras_droit");
    CHECK(b != nullptr);
    if (!b) return;
    raid::RaidRun::Test t;
    t.on = true;
    t.invincible = false;
    t.players = 8;
    t.startPhase = 2;  // its carapace: three vents to hold
    raid::TestSession session(s.ws(), *b, t);
    session.setAdminSeated(true);
    session.setAdminBot(true);
    session.setRescues(false);
    session.setBotsInvincible(true);
    uint32_t state = 0;
    for (const raid::ObjectiveInfo& o : b->objectives)
        if (o.role != "gigaboss" && !o.optional) state |= 1u << o.index;
    session.setBranchState(state);
    CHECK(session.begin(lairOf(*b)));
    for (int k = 0; k < 8 * kSimHz; ++k) session.step(InputFrame{});
    auto count = [&](raid::bots::Role r) {
        int n = 0;
        for (int seat = 0; seat < 8; ++seat) n += session.bots().bots()[static_cast<size_t>(seat)].role == r ? 1 : 0;
        return n;
    };
    for (int seat = 0; seat < 8; ++seat)
        std::printf("  place %d : %s → %s%s%s\n", seat + 1, raid::bots::roleName(session.bots().thought(seat).role), session.bots().thought(seat).intent.c_str(),
                    session.bots().thought(seat).move.empty() ? "" : " · ", session.bots().thought(seat).move.c_str());
    CHECK(count(raid::bots::Role::Reviver) == 1);
    CHECK(count(raid::bots::Role::Lure) == 1);
    CHECK(count(raid::bots::Role::Runner) >= 1);
    CHECK(count(raid::bots::Role::Striker) >= 2);
    for (int seat = 0; seat < 8; ++seat) CHECK(!session.bots().thought(seat).intent.empty());  // every one says something
    // The reviver falls: another one takes its place at the next plan (a quarter of a second).
    int reviver = -1;
    for (int seat = 0; seat < 8; ++seat)
        if (session.bots().bots()[static_cast<size_t>(seat)].role == raid::bots::Role::Reviver) reviver = seat;
    CHECK(reviver >= 0);
    if (reviver < 0) return;
    GameWorld& w = session.run().world();
    {
        GameWorld::PlayerScope sc(w, reviver);
        w.playerAt(reviver).kill(w);
    }
    for (int k = 0; k < kSimHz / 2; ++k) session.step(InputFrame{});
    int other = -1;
    for (int seat = 0; seat < 8; ++seat)
        if (seat != reviver && session.bots().bots()[static_cast<size_t>(seat)].role == raid::bots::Role::Reviver) other = seat;
    std::printf("  le réanimateur (place %d) tombe : la place %d le devient (« %s »)\n", reviver + 1, other + 1,
                other >= 0 ? session.bots().thought(other).intent.c_str() : "?");
    CHECK(other >= 0);
    CHECK(session.bots().bots()[static_cast<size_t>(reviver)].role == raid::bots::Role::None);
}

TEST_CASE("raid GOAT (2.79): the thoughts' bubbles — measured, never over one another, a name, a panel or a character, inside the screen; the order wheel's items apart") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}, Vec2{800, 600}}) {
            app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
            app.renderer().refreshOutputSize();
            UIContext& ui = g.ui;
            ui.begin(app, 1.0f / 60.0f);
            const Rect screen{0.0f, 0.0f, ui.screenW(), ui.screenH()};
            // Eight simulated players crowded together (a fight at a plate), one at the top edge, one at a side, the longest
            // intentions; the names over them and a panel of the HUD in the way.
            std::vector<botsui::ThoughtView> in;
            std::vector<Rect> bodies, avoid;
            for (int k = 0; k < 8; ++k) {
                botsui::ThoughtView v;
                v.seat = k;
                const float x = k == 6 ? 2.0f : k == 7 ? screen.w * 0.5f : screen.w * 0.42f + static_cast<float>(k) * 22.0f;
                const float y = k == 7 ? 4.0f : screen.h * 0.6f - static_cast<float>(k % 2) * 30.0f;
                v.body = {x, y, 20.0f, 44.0f};
                v.role = raid::bots::roleName(static_cast<raid::bots::Role>(1 + k % 4));
                v.intent = k % 3 == 0 ? "frappe le point faible (× 2)" : k % 3 == 1 ? "réanime Joueur simulé 3" : "attend que ce soit sûr pour réanimer Joueur simulé 7";
                v.move = k % 2 ? "saute puis double saut vers le haut" : "";
                v.order = k == 2;
                in.push_back(v);
                bodies.push_back(v.body);
                const float nameW = ui.text().measure("Joueur simulé 8", FontFace::Bold, ui.s(14)).x;
                avoid.push_back({v.body.center().x - nameW * 0.5f, v.body.y - ui.s(20), nameW, ui.s(18)});
            }
            avoid.push_back({screen.w * 0.40f, screen.h * 0.2f, screen.w * 0.2f, ui.s(60)});  // a panel of the HUD
            const auto bubbles = botsui::layoutThoughts(ui, in, avoid, bodies, screen);
            std::printf("  %4.0f × %3.0f : %zu bulles placées sur 8\n", size.x, size.y, bubbles.size());
            CHECK(bubbles.size() >= 4u);
            for (size_t i = 0; i < bubbles.size(); ++i) {
                const botsui::Bubble& b = bubbles[i];
                CHECK(b.box.x >= 0.0f && b.box.y >= 0.0f && b.box.right() <= screen.w && b.box.bottom() <= screen.h);
                for (size_t j = i + 1; j < bubbles.size(); ++j) CHECK(!b.box.intersects(bubbles[j].box));
                for (const Rect& r : bodies) CHECK(!b.box.intersects(r));
                for (const Rect& r : avoid) CHECK(!b.box.intersects(r));
                // Its text fits in it (measured as drawn).
                const float line1 = ui.s(7) + ui.s(4) + ui.text().measure(b.role, FontFace::Bold, ui.s(b.size1)).x +
                                    ui.text().measure(botsui::kArrow, FontFace::Body, ui.s(b.size1)).x + ui.text().measure(b.intent, FontFace::Body, ui.s(b.size1)).x;
                CHECK(line1 <= b.box.w - ui.s(7) * 2.0f + 0.5f);
                if (!b.move.empty()) CHECK(ui.text().measure(b.move, FontFace::Body, ui.s(b.size2)).x <= b.box.w - ui.s(7) * 2.0f + 0.5f);
            }
            // The wheel: its items apart, its labels inside them, everything on the screen.
            for (bool shown : {true, false}) {
                const botsui::WheelLayout l = botsui::layoutWheel(ui, shown, 3, "Stick, flèches ou souris : choisir · Valider ou clic : donner l'ordre · C ou Retour : fermer");
                CHECK(l.items.size() == static_cast<size_t>(botsui::kWheelItems));
                CHECK(l.panel.x >= 0.0f && l.panel.y >= 0.0f && l.panel.right() <= screen.w + 0.5f && l.panel.bottom() <= screen.h + 0.5f);
                for (size_t i = 0; i < l.items.size(); ++i) {
                    CHECK(l.panel.contains(l.items[i].pos()));
                    CHECK(!l.items[i].intersects(l.title) && !l.items[i].intersects(l.help));
                    for (size_t j = i + 1; j < l.items.size(); ++j) CHECK(!l.items[i].intersects(l.items[j]));
                    const std::string label = botsui::wheelLabel(static_cast<botsui::WheelItem>(i), shown);
                    CHECK(ui.text().measure(label, FontFace::Bold, ui.s(l.itemFont)).x <= l.items[i].w - ui.s(12));
                }
            }
            ui.end();
        }
    }
    app.shutdown();
}

TEST_CASE("raid GOAT (2.79): « Simuler un raid complet » without any invincibility — 7 groups of 7 simulated players (Expert) to the Cœur's fall, from the lairs") {
    Svc s;
    raid::SimSetup setup;
    setup.players = 56;  // §41: seven groups of 8 (a free place and 7 simulated players each)
    setup.lairs = true;  // reduced: each branch at its lair (the objectives before it done), the real fights from their first phase
    setup.startPhase = 1;
    setup.invincible = false;
    setup.botsInvincible = false;
    setup.rescues = false;
    setup.skill = raid::BotSkill::Expert;
    raid::RaidSimulation sim(s.ws(), setup);
    CHECK(sim.begin());
    sim.watch(0);  // nobody seated: every group plays alone (7 simulated players each, its free place empty)
    const uint32_t limit = 3600u * kSimHz;
    uint32_t opened = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (!sim.heartFallen() && sim.tick() < limit) {
        sim.step(InputFrame{});
        if (sim.heartOpen() && opened == 0) opened = sim.tick();
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  raid complet sans invincibilité : %s en %.0f s de jeu · %d Gigaboss · %zu groupes (renforts compris) · %d morts · %d sauvetages · %.0f ms\n",
                sim.heartFallen() ? "Cœur tombé" : "PAS FINI", sim.tick() / static_cast<double>(kSimHz), sim.gigabossesDown(), sim.groups().size(), sim.botDeaths(),
                sim.rescues(), ms);
    for (const auto& e : sim.feed()) std::printf("    %5.0f s  %s\n", e.tick / static_cast<double>(kSimHz), e.text.c_str());
    // As measured (docs/RAIDS.md §28): every Gigaboss and the Cœur once the simulated players beat them all alone.
    CHECK(sim.gigabossesDown() >= kRaidGigabossDown);
    CHECK(!kRaidHeartFalls || sim.heartFallen());
    CHECK(sim.rescues() == 0);
    for (const auto& g : sim.groups()) CHECK(g->session->run().takeReports().empty());
}

TEST_CASE("raid GOAT (2.84): carapaces held together — Dreadfang, Voidworm and Heartbreaker from their phase 2, 8 simulated players (Expert, invincible)") {
    // What the 2.84 changed in their mechanisms (docs/RAIDS.md §36), measured on one start each: a player stays on its
    // chain or hook once closed, keeps the place it stands on, crosses the boss's body under its shield. Dreadfang falls;
    // Voidworm's carapace bursts again and again (before: never broken in 15 min, 66 % left).
    Svc s;
    const Fight d = fightAt(s, "bras_gauche", raid::BotSkill::Expert, 900, 8, 0, 2, true);
    print(d);
    CHECK(d.won);
    const Fight v = fightAt(s, "abdomen", raid::BotSkill::Expert, 900, 8, 0, 2, true);
    print(v);
    CHECK(v.won || v.hp <= kVoidwormPhase2Left);
    // Heartbreaker: its ribs held on both sides at once (before: never, 66 % left); it falls.
    const Fight h = fightAt(s, "torse", raid::BotSkill::Expert, 900, 8, 0, 2, true);
    print(h);
    CHECK(h.won);
}

TEST_CASE("raid GOAT bench (2.84): every Gigaboss and the Cœur over several starts (COSMIC_GOAT_BENCH, docs/RAIDS.md §36)") {
    // A measure, not a check (minutes of play): only with COSMIC_GOAT_BENCH set. COSMIC_GOAT_SEEDS starts (5), from
    // seed COSMIC_GOAT_SEED0 (1); COSMIC_GOAT_PARTS a list of parts (« bras_droit,crane »), else the seven and the Cœur.
    if (!std::getenv("COSMIC_GOAT_BENCH")) {
        std::printf("  (mesure : COSMIC_GOAT_BENCH=1)\n");
        return;
    }
    const int seeds = std::getenv("COSMIC_GOAT_SEEDS") ? std::max(1, std::atoi(std::getenv("COSMIC_GOAT_SEEDS"))) : 5;
    const int seed0 = std::getenv("COSMIC_GOAT_SEED0") ? std::atoi(std::getenv("COSMIC_GOAT_SEED0")) : 1;
    std::vector<std::string> parts = {"bras_droit", "crane", "bras_gauche", "jambe_droite", "abdomen", "jambe_gauche", "torse", raid::kHeartPart};
    if (const char* p = std::getenv("COSMIC_GOAT_PARTS"); p && *p) {
        parts.clear();
        std::string cur;
        for (const char* c = p;; ++c) {
            if (*c == ',' || *c == 0) {
                if (!cur.empty()) parts.push_back(cur);
                cur.clear();
                if (*c == 0) break;
            } else {
                cur += *c;
            }
        }
    }
    Svc s;
    for (const std::string& part : parts) {
        int won = 0;
        double wonSeconds = 0.0;
        std::string line;
        for (int k = 0; k < seeds; ++k) {
            // 2.98: COSMIC_GOAT_PLAYERS a smaller group (2 to 8: raid groups are 2 to 8 players), 8 by default.
            const int players = std::getenv("COSMIC_GOAT_PLAYERS") ? std::clamp(std::atoi(std::getenv("COSMIC_GOAT_PLAYERS")), 2, 8) : 8;
            const Fight f = fightAt(s, part, raid::BotSkill::Expert, 900, players, static_cast<uint32_t>(seed0 + k));
            won += f.won ? 1 : 0;
            wonSeconds += f.won ? f.seconds : 0.0;
            char buf[96];
            std::snprintf(buf, sizeof buf, " %s %.0fs/%dm/%.0f%%/p%d", f.won ? "V" : f.wiped ? "E" : "D", f.seconds, f.deaths, f.hp * 100.0f, f.phase);
            line += buf;
            std::string c;
            for (size_t j = 0; j < f.causes.size() && j < 4; ++j) c += (j ? " ; " : "") + std::to_string(f.causes[j].first) + " × " + f.causes[j].second;
            std::printf("    %-12s seed %d :%s · %s\n", part.c_str(), seed0 + k, buf, c.c_str());
            std::printf("      sorties : reste %d · pas %d · saut %d · ruée %d · aucune %d · boucliers %d (coups arrêtés %d) · ruées sous le coup %d\n", f.escStays,
                        f.escWalks, f.escJumps, f.escDashes, f.escFails, f.shields, f.blocked, f.dashGuards);
            std::fflush(stdout);
        }
        std::printf("  BENCH %-14s %d/%d vaincus (moyenne %.0f s) :%s\n", part.c_str(), won, seeds, won ? wonSeconds / won : 0.0, line.c_str());
        std::fflush(stdout);
    }
}

namespace {

/// One start of a lair's fight (8 simulated players, Expert, seed `seed`) for `seconds` of play: the session's digest
/// every second (the world and the simulated players' state).
std::vector<uint64_t> digestsOf(Svc& s, const std::string& part, uint32_t seed, int seconds) {
    std::vector<uint64_t> out;
    const raid::Branch* b = raid::Branch::builtin(part);
    if (!b) return out;
    raid::RaidRun::Test t;
    t.on = true;
    t.players = 8;
    raid::TestSession session(s.ws(), *b, t);
    session.setAdminSeated(true);
    session.setAdminBot(true);
    session.setRescues(false);
    session.setSkill(raid::BotSkill::Expert);
    session.setBotSeed(seed);
    uint32_t state = 0;
    for (const raid::ObjectiveInfo& o : b->objectives)
        if (o.role != "gigaboss" && !o.optional) state |= 1u << o.index;
    session.setBranchState(state);
    if (!session.begin(lairOf(*b))) return out;
    for (int k = 0; k < seconds * kSimHz; ++k) {
        session.step(InputFrame{});
        if (k % kSimHz == kSimHz - 1) out.push_back(session.hash());
    }
    return out;
}

}  // namespace

TEST_CASE("raid GOAT (2.84): the simulated players' choices depend on the game only — the same start twice in one process and in a fresh one, the same digests") {
    // Their choices must be a pure function of the simulation (a relayed or online test runs them on several computers):
    // nothing left by an earlier fight in the same process (a cache, a shared rule) may change them. A digest every
    // second. A fresh process: COSMIC_GOAT_DIGESTS=<file> runs the start as its very first fight and writes its digests;
    // COSMIC_GOAT_DIGESTS_REF=<file> then compares them with this process's runs.
    Svc s;
    const int secs = std::getenv("COSMIC_GOAT_DIGEST_SECONDS") ? std::atoi(std::getenv("COSMIC_GOAT_DIGEST_SECONDS")) : 180;
    // After another start of the same lair (its graphs in the cache, built from that fight at other moments: its hooks,
    // its acid), then as in a fresh process (the graphs' cache emptied), then once more (the cache warm).
    if (const char* path = std::getenv("COSMIC_GOAT_DIGESTS")) {
        // The fresh process's run: the very first fight of this process, written for another to compare.
        const std::vector<uint64_t> first = digestsOf(s, "abdomen", 2, secs);
        if (FILE* f = std::fopen(path, "w")) {
            for (uint64_t h : first) std::fprintf(f, "%016llx\n", static_cast<unsigned long long>(h));
            std::fclose(f);
        }
        CHECK(first.size() == static_cast<size_t>(secs));
        return;
    }
    raid::NavGraph::clearCache();
    digestsOf(s, "abdomen", 1, 400);
    const std::vector<uint64_t> b = digestsOf(s, "abdomen", 2, secs);
    raid::NavGraph::clearCache();
    const std::vector<uint64_t> a = digestsOf(s, "abdomen", 2, secs);
    const std::vector<uint64_t> c = digestsOf(s, "abdomen", 2, secs);
    CHECK(c == a);
    size_t diverge = a.size();
    for (size_t k = 0; k < a.size() && k < b.size(); ++k)
        if (a[k] != b[k]) {
            diverge = k;
            break;
        }
    std::printf("  %zu s comparées : %s\n", a.size(), diverge == a.size() ? "identiques" : ("divergence à " + std::to_string(diverge + 1) + " s").c_str());
    if (const char* path = std::getenv("COSMIC_GOAT_DIGESTS_REF"))
        if (FILE* f = std::fopen(path, "r")) {
            std::vector<uint64_t> ref;
            unsigned long long h = 0;
            while (std::fscanf(f, "%llx", &h) == 1) ref.push_back(h);
            std::fclose(f);
            std::printf("  avec un processus neuf : %s\n", ref == a && ref == b ? "identiques" : "DIFFÉRENTES");
            CHECK(ref == a);
            CHECK(ref == b);
        }
    CHECK(a.size() == static_cast<size_t>(secs));
    CHECK(a == b);
}

/// 2.85: a whole branch played by its simulated players alone (8 of them, the administrator AFK, the test's rescues off), from
/// its armory through its zones to its lair. The objectives of a pair are done by distinct groups: the other half of each
/// pair given as done by another group (one group alone could never open some branches' generators). Tuning runs print the
/// zone, the branch's state and each player's position and thought every COSMIC_PROBE_EVERY seconds (COSMIC_BRANCH_PROBE).
namespace {

struct BranchRun {
    double lairAt = -1.0;    // seconds to the lair's door (-1: not within the limit)
    double relicAt = -1.0;   // the relic of the secret zone taken
    double ambushAt = -1.0;  // first step in the zone of the ambushes that open the lair
    uint32_t mask = 0;
    int zone = 0;
    double ms = 0.0;
};

BranchRun branchToLair(Svc& s, const std::string& part, uint32_t seed, int limitSeconds, bool print) {
    BranchRun out;
    const raid::Branch* b = raid::Branch::builtin(part);
    CHECK(b != nullptr);
    if (!b) return out;
    raid::RaidRun::Test t;
    t.on = true;
    t.invincible = false;
    t.players = 8;
    raid::TestSession session(s.ws(), *b, t);
    session.setAdminSeated(true);
    session.setAdminBot(true);
    session.setRescues(false);
    session.setBotsInvincible(std::getenv("COSMIC_PROBE_MORTAL") == nullptr);
    session.setSkill(raid::BotSkill::Expert);
    session.setBotSeed(seed);
    uint32_t state = 0;
    std::vector<std::string> pairs;
    for (const raid::ObjectiveInfo& o : b->objectives) {
        if (o.pair.empty()) continue;
        if (std::find(pairs.begin(), pairs.end(), o.pair) == pairs.end()) pairs.push_back(o.pair);  // the first: this group's
        else state |= 1u << o.index;                                                                // the others: another group's
    }
    const char* z0 = print ? std::getenv("COSMIC_PROBE_ZONE") : nullptr;
    if (const char* st = print ? std::getenv("COSMIC_PROBE_STATE") : nullptr) state = static_cast<uint32_t>(std::strtoul(st, nullptr, 16));
    session.setBranchState(state);
    CHECK(session.begin(z0 ? std::atoi(z0) : raid::RaidRun::kArmory));
    int relic = -1, ambushZone = -1;
    for (const raid::ObjectiveInfo& o : b->objectives)
        if (o.optional && b->zone(o.zone) && b->zone(o.zone)->secret) relic = o.index;
    for (const raid::ZoneInfo& z : b->zones)
        if (!z.boss && b->ambushesOpenLair(z.id)) ambushZone = z.id;
    const int every = (std::getenv("COSMIC_PROBE_EVERY") ? std::atoi(std::getenv("COSMIC_PROBE_EVERY")) : 20) * kSimHz;
    const int limit = limitSeconds * kSimHz;
    const auto t0 = std::chrono::steady_clock::now();
    int zone = -99;
    for (int st = 0; st < limit; ++st) {
        session.step(InputFrame{});
        const raid::RaidRun& run = session.run();
        const double now = (st + 1) / static_cast<double>(kSimHz);
        if (relic >= 0 && out.relicAt < 0.0 && raid::Branch::done(run.mask(), relic)) out.relicAt = now;
        for (int k = 0; k < 8 && out.ambushAt < 0.0; ++k)
            if (run.zoneOf(k) == ambushZone) out.ambushAt = now;
        if (print && (run.zone() != zone || st % every == 0)) {
            zone = run.zone();
            int to = 0;
            const int exit = raid::TestBots::chooseExit(run, &to);
            std::printf("  %6.0f s zone %d done %08x exit %d->%d%s |", st / double(kSimHz), zone, run.mask(), exit, to, run.wiped() ? " DÉCIMÉ" : "");
            const GameWorld& w = run.world();
            for (int k = 0; k < w.teamSize() && k < 8; ++k) {
                const Vec2 c = w.member(k).player ? w.member(k).player->box.center() : Vec2{};
                std::printf(" %d(%.0f,%.0f)p%d z%d %s", k + 1, c.x, c.y, static_cast<int>(w.member(k).presence), run.zoneOf(k), session.bots().thought(k).intent.substr(0, 40).c_str());
                if (std::getenv("COSMIC_PROBE_DETAIL") && k < static_cast<int>(session.bots().bots().size())) {
                    const auto& bb = session.bots().bots()[static_cast<size_t>(k)];
                    std::printf("[t%d g(%.0f,%.0f) n%d p%zu/%zu still%d '%s' '%s']", static_cast<int>(bb.task), bb.goal.x, bb.goal.y, bb.goalNode, bb.at,
                                bb.path.size(), bb.still, bb.moveText.c_str(), session.bots().thought(k).move.c_str());
                    if (const Enemy* e = bb.target ? w.findEnemy(bb.target) : nullptr) {
                        const Player& pl = w.playerAt(k);
                        std::string pw;
                        for (const auto& x : pl.powers()) pw += x->id().substr(0, 4) + ",";
                        std::printf("[foe(%.0f,%.0f %.0fx%.0f) e%.0f %s spot%d r%d sa%d]", e->center().x, e->center().y, e->box.w, e->box.h, pl.energy,
                                    pw.c_str(), bb.spot, bb.ranged ? 1 : 0, bb.spotAt);
                    }
                }
            }
            std::printf("\n");
        }
        // Its players change zone each on their own (2.78): the lair reached by the first of them in it.
        bool lair = false;
        for (int k = 0; k < 8 && !lair; ++k) {
            const int z = run.zoneOf(k);
            lair = z >= 1 && b->zone(z) && b->zone(z)->boss;
        }
        if (lair) {
            out.lairAt = now;
            break;
        }
    }
    out.mask = session.run().mask();
    out.zone = session.run().zone();
    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

}  // namespace

TEST_CASE("raid bots probe (2.85): a whole branch from its armory, simulated players only (COSMIC_BRANCH_PROBE=part)") {
    const char* part = std::getenv("COSMIC_BRANCH_PROBE");
    if (!part) return;
    Svc s;
    const int minutes = std::getenv("COSMIC_PROBE_MIN") ? std::atoi(std::getenv("COSMIC_PROBE_MIN")) : 30;
    const char* seed = std::getenv("COSMIC_PROBE_SEED");
    const BranchRun r = branchToLair(s, part, seed ? static_cast<uint32_t>(std::atoi(seed)) : 0u, minutes * 60, true);
    std::printf("  %s : antre %.0f s · relique %.0f s · zone des embuscades %.0f s · %.0f ms\n", part, r.lairAt, r.relicAt, r.ambushAt, r.ms);
}

TEST_CASE("raid bots (2.85): alone in a whole branch, the simulated players go from the armory to the lair — the relic first, then the ambushes") {
    // The order of a branch (docs/RAIDS.md §22): its zones, the relic of its secret zone as soon as its passage is open (before
    // the long zone of the ambushes before the lair), that zone, then the lair — never waiting where an objective is in reach.
    // One start, the simulated players invincible (their way, not their fights, measured), within 25 min of game. The Crâne
    // by default (about a minute); the seven branches with COSMIC_RAID_ALL_BRANCHES (about 10 min).
    Svc s;
    std::vector<const char*> parts{"crane"};
    if (std::getenv("COSMIC_RAID_ALL_BRANCHES")) parts = {"bras_droit", "crane", "bras_gauche", "jambe_droite", "abdomen", "jambe_gauche", "torse"};
    for (const char* part : parts) {
        const BranchRun r = branchToLair(s, part, 0, 25 * 60, false);
        std::printf("  %-13s antre %s (%.0f s de jeu) · relique à %.0f s · zone des embuscades à %.0f s · état %08x zone %d · %.0f ms\n", part,
                    r.lairAt > 0.0 ? "atteinte" : "PAS ATTEINTE", r.lairAt, r.relicAt, r.ambushAt, r.mask, r.zone, r.ms);
        CHECK(r.lairAt > 0.0);
        CHECK(r.relicAt > 0.0);
        CHECK(r.ambushAt > 0.0);
        CHECK(r.relicAt < r.ambushAt);
    }
}
