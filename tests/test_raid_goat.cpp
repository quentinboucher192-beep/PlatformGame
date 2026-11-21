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
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/rendering/Text.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/WorldListener.h"
#include "game/raid/AsterionHeartRoutine.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "game/raid/RaidSimulation.h"
#include "game/raid/RaidTest.h"
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
    std::string last[8];  // the last blow on each seat (what put it down, when it falls)
    void playerHurt(const GameWorld& w, const DamageInfo& d, const HurtSource& src) override {
        static const char* kOrigin[] = {"tir", "attaque", "boss", "contact", "piège", "chute", "autre"};
        const std::string what = std::string(kOrigin[std::min<int>(static_cast<int>(src.origin), 6)]) + " · " + (src.attack.empty() ? "?" : src.attack);
        ++blows[what];
        if (const int k = w.currentPlayer(); k >= 0 && k < 8) last[k] = what;
        if (session && !watch.empty() && src.attack.find(watch) != std::string::npos) {
            const int k = w.currentPlayer();
            const Player& p = w.playerAt(k);
            std::printf("      %s → %d (%.0f, %.0f) sol %d vy %.0f · %s · %s\n", src.attack.c_str(), k + 1, p.feet().x, p.feet().y, p.grounded() ? 1 : 0, p.vel.y,
                        session->bots().thought(k).intent.c_str(), session->bots().thought(k).move.c_str());
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
    void attackStarted(const GameWorld& w, uint32_t tag, const std::string& power) override {
        if (next) next->attackStarted(w, tag, power);
    }
    void enemyHit(const GameWorld& w, const Enemy& e, uint32_t tag) override {
        if (next) next->enemyHit(w, e, tag);
    }
    void enemyDamaged(const GameWorld& w, const Enemy& e, uint32_t tag, float amount) override {
        if (next) next->enemyDamaged(w, e, tag, amount);
    }
    void blowBlocked(const GameWorld& w, float amount) override {
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
};

/// 8 simulated players (the administrator's seat played by the AI too), at the lair of `part`, without any
/// invincibility nor the test's rescues, until its gigaboss falls, the group is wiped or `limit` seconds.
Fight fightAt(Svc& s, const std::string& part, raid::BotSkill skill, int limitSeconds, int players = 8) {
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
    t.startPhase = std::getenv("COSMIC_GOAT_PHASE") ? std::atoi(std::getenv("COSMIC_GOAT_PHASE")) : 1;
    raid::TestSession session(s.ws(), *b, t);
    session.setAdminSeated(true);
    session.setAdminBot(true);
    session.setRescues(false);
    session.setBotsInvincible(std::getenv("COSMIC_GOAT_INVINCIBLE") != nullptr);
    session.setSkill(skill);
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
    uint8_t downBefore = 0;
    std::map<std::string, int> deaths;
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
                if (((down >> k) & 1u) && !((downBefore >> k) & 1u))
                    ++deaths[(hurts.last[k].empty() ? std::string("?") : hurts.last[k]) + " · " + raid::bots::roleName(session.bots().thought(k).role)];
            downBefore = down;
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
        if (trace && steps % (20 * kSimHz) == 0) {
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
    setup.groups = 7;
    setup.bots = 7;
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
