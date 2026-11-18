// 2.72 (phase 6): LE CŒUR D'ASTERION, the final fight of the clan raid (docs/RAIDS.md §15). Its generated
// sheets and film, its catalog apart from the seven branches (an aorta and a gigantic arena), its enemy of
// four phases, its health for 2 to 8 seats, the shared health given with the branch's state (applied at
// the same step on every computer, through the relay too: the same digest), the parts of the titan fallen
// acting on it (precision, sweeps, magma rain, cage, spawn, gravity traps, slower charges), its reports
// (its part of the shared health, its phases as the server reads them), the shared victory at a step, its
// supernova, and an administrator's test that never reports anything.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/camera/Camera.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/BossStories.h"
#include "game/content/ContentDatabase.h"
#include "game/coop/CoopProtocol.h"
#include "game/coop/CoopRelay.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/AsterionHeartRoutine.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "tests/TestFramework.h"

#ifdef _WIN32
#define COSMIC_HEART_NULL "NUL"
#else
#define COSMIC_HEART_NULL "/dev/null"
#endif

using namespace cosmic;
using namespace cosmic::coopnet;

namespace {

constexpr int R = static_cast<int>(Action::MoveRight);
constexpr int L = static_cast<int>(Action::MoveLeft);
constexpr int J = static_cast<int>(Action::Jump);
constexpr int A = static_cast<int>(Action::Attack);
constexpr int kArena = 2;

InputFrame scripted(int seat, uint32_t t) {
    InputFrame f;
    const uint32_t phase = (t / (60 + 7 * static_cast<uint32_t>(seat))) % 4;
    const int dir = phase == 0 || phase == 1 ? 1 : (phase == 2 ? -1 : 0);
    if (dir > 0) f.held |= 1u << R;
    if (dir < 0) f.held |= 1u << L;
    f.moveX = static_cast<int8_t>(dir * 127);
    if ((t + static_cast<uint32_t>(seat) * 13) % 47 < 8) f.held |= 1u << J;
    if ((t + static_cast<uint32_t>(seat) * 5) % 71 < 3) f.held |= 1u << A;
    return f;
}

struct Edges {
    std::vector<InputFrame> prev;
    explicit Edges(int n) : prev(static_cast<size_t>(n)) {}
    InputFrame operator()(int seat, InputFrame f) {
        InputFrame& p = prev[static_cast<size_t>(seat)];
        f.pressed = f.held & ~p.held;
        f.released = p.held & ~f.held;
        p = f;
        return f;
    }
};

struct Services {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Services() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

const raid::Branch* heart() { return raid::Branch::builtin(raid::kHeartPart); }

std::vector<InputFrame> still(int players) {
    std::vector<InputFrame> f(raid::kMaxInstancePlayers);
    for (int k = players; k < raid::kMaxInstancePlayers; ++k) f[static_cast<size_t>(k)].flags = InputFrame::Vacant;
    return f;
}

Enemy* bossOf(raid::RaidRun& run) {
    for (auto& e : run.world().enemies())
        if (e->objectId() == run.rules().gigaboss.boss) return e.get();
    return nullptr;
}

/// The players in play put on the arena's floor near the heart (spread out): it wakes up.
void intoHeart(raid::RaidRun& run, int players) {
    const raid::ArenaInfo& a = run.rules().arena;
    const float mid = a.rect.x + a.rect.w * 0.5f;
    for (int i = 0; i < players; ++i) run.world().playerAt(i).teleportTo({mid - 520.0f + 140.0f * static_cast<float>(i), a.floor});
}

bool engage(raid::RaidRun& run, int players, uint32_t mask = 0, raid::HeartShared h = {}, int limit = 900) {
    const auto f = still(players);
    for (int t = 0; t < limit && !run.gigaboss().engaged(); ++t) {
        if (t % 30 == 0) intoHeart(run, players);
        run.step(f.data(), static_cast<int>(f.size()), mask, h);
    }
    return run.gigaboss().engaged();
}

raid::RaidRun::Test testOf(int players, int phase) {
    raid::RaidRun::Test t;
    t.on = true;
    t.invincible = true;
    t.players = players;
    t.startPhase = phase;
    return t;
}

/// The branch's state with the « modifier » links of the parts in `parts` (bit p = part p) on.
uint32_t partsMask(const raid::Branch& b, uint32_t parts) {
    uint32_t m = 0;
    for (const raid::EffectInfo& e : b.effects)
        for (int p = 0; p < raid::AsterionHeartRoutine::PartCount; ++p)
            if ((parts >> p) & 1u && e.id == std::string("coeur_") + raid::AsterionHeartRoutine::partId(p)) m |= 1u << e.bit;
    return m;
}

/// A relay of the local network in raid mode, its messages kept by connection.
struct Wire {
    std::map<int, std::vector<std::vector<uint8_t>>> got;
    CoopRelay relay{[this](int c, const std::vector<uint8_t>& b) { got[c].push_back(b); }, [](int) {}};
    void send(int conn, const std::vector<uint8_t>& m, uint32_t now = 0) { relay.received(conn, m.data(), m.size(), now); }
    std::vector<std::vector<uint8_t>> of(int conn, Msg t) {
        std::vector<std::vector<uint8_t>> out;
        for (const auto& m : got[conn])
            if (!m.empty() && m[0] == static_cast<uint8_t>(t)) out.push_back(m);
        return out;
    }
    std::vector<std::vector<InputFrame>> frames(int conn, int players) {
        std::vector<std::vector<InputFrame>> out;
        for (const auto& m : of(conn, Msg::Frames)) {
            Reader r{m.data() + 1, m.data() + m.size()};
            const uint32_t first = r.u32();
            const uint16_t n = r.u16();
            std::vector<std::vector<InputFrame>> seats(static_cast<size_t>(players), std::vector<InputFrame>(n));
            for (int s = 0; s < players; ++s) CHECK(readFrames(r.p, r.end, seats[static_cast<size_t>(s)].data(), n));
            if (out.size() < first + n) out.resize(first + n);
            for (uint16_t i = 0; i < n; ++i)
                for (int s = 0; s < players; ++s) out[first + i].push_back(seats[static_cast<size_t>(s)][i]);
        }
        return out;
    }
    /// The branch's state and the heart's at step t (as NetCoopLink reads RAID_STATE).
    std::pair<uint32_t, uint32_t> stateAt(int conn, uint32_t t) {
        uint32_t mask = 0, h = 0;
        for (const auto& m : of(conn, Msg::RaidState)) {
            Reader r{m.data() + 1, m.data() + m.size()};
            const uint32_t at = r.u32();
            const uint32_t v = r.u32();
            const uint32_t hv = r.ok && r.p != r.end ? r.u32() : 0u;
            if (at <= t) mask = v, h = hv;
        }
        return {mask, h};
    }
};

}  // namespace

TEST_CASE("raid heart: its sheets and its victory film are their generators' output") {
    if (std::system("python3 -c \"import numpy, PIL\" > " COSMIC_HEART_NULL " 2>&1") != 0) {
        std::printf("  (python3 + numpy + Pillow absents : vérification des générateurs du Cœur sautée)\n");
        return;
    }
    CHECK(std::system("python3 tools/assetgen/characters/boss_n_coeur.py --check > " COSMIC_HEART_NULL " 2>&1") == 0);
    CHECK(std::system("python3 tools/assetgen/environment/story_asterion_victoire.py --check > " COSMIC_HEART_NULL " 2>&1") == 0);
}

TEST_CASE("raid heart: its catalog apart from the seven branches, its gigantic arena, its links from the seven parts") {
    const raid::Titan* t = raid::Titan::builtin();
    CHECK(t != nullptr);
    if (!t) return;
    CHECK(t->branches.size() == 7);
    CHECK(t->heart.part == "coeur");
    const raid::Branch* b = heart();
    CHECK(b != nullptr);
    if (!b) return;
    CHECK(raid::Branch::all().back() == b);  // the administrators' tests list it after the seven
    CHECK(b->zones.size() == 2);
    CHECK(b->zones[1].boss && !b->zones[0].boss);
    CHECK(b->zoneRules(1)->spawns.at("start").size() == 3);
    CHECK(!b->reachable(0)[2]);  // its aorta's guard opens the arena
    CHECK(b->reachable(1u << b->objectiveIndex("coeur_aorte"))[2]);
    const raid::ZoneRules* z = b->zoneRules(kArena);
    CHECK(z && z->gigaboss.present && z->gigaboss.routine == "coeur");
    if (!z) return;
    CHECK(z->arena.present);
    CHECK(z->arena.rect.w >= 8.0f * Camera::kViewWidth);  // gigantic: more than eight screens wide
    CHECK(z->arena.rect.h >= 3.5f * Camera::kViewHeight);
    CHECK(z->gigaboss.vents.size() == 4);
    CHECK(z->gigaboss.weakSpots.size() == 3);  // at most three a player can break in an exposure (the server's bound)
    CHECK(z->gigaboss.fistPeriods.empty());    // its blows on every player are its own (lances: their precision a modifier)
    CHECK(FileUtil::exists(Paths::assetsDir() / z->gigaboss.fx));
    CHECK(b->objective("coeur_asterion") && b->objective("coeur_asterion")->role == "gigaboss");
    // A « modifier » link from each of the seven parts' gigaboss, its bit read by the routine.
    int mods = 0;
    for (const raid::EffectInfo& e : b->effects) {
        mods += e.kind == "modifier" ? 1 : 0;
        CHECK(e.bit >= raid::kLinkBit0 && e.bit < 32);
        CHECK(z->gigaboss.titanLinks.count(e.id) == 1);
    }
    CHECK(mods == 7);
    // Moving platforms in its arena (the shuttles and ferries around the heart).
    int moving = 0;
    for (const auto& o : b->level(kArena)->objects) moving += o.type == "moving_platform" ? 1 : 0;
    CHECK(moving >= 6);
}

TEST_CASE("raid heart: an enormous heart of four phases, its health for 2 to 8 seats") {
    Services s;
    const EnemyDefinition* def = s.content.enemy("asterion_coeur");
    CHECK(def != nullptr);
    CHECK(s.content.enemy("asterion_ventricule") != nullptr);
    const raid::Branch* b = heart();
    if (!def || !b) return;
    CHECK(def->isBoss);
    CHECK(def->phases.size() == 4);
    CHECK(def->size.x >= 400.0f && def->size.y >= 400.0f);
    CHECK(def->scale >= 3.0f);
    CHECK(def->raid == "asterion");
    CHECK(!def->finalAttacks.empty());
    for (int players : {2, 5, 8}) {
        raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
        CHECK(run.begin(0, kArena));
        CHECK(engage(run, players));
        const Enemy* boss = bossOf(run);
        CHECK(boss != nullptr);
        if (!boss) continue;
        CHECK(std::abs(boss->maxHp() - def->hp * raid::Gigaboss::healthFactor(run.rules().gigaboss, players)) < 0.5f);
        CHECK(run.gigaboss().scaledFor() == players);
        CHECK(run.heartRoutine() != nullptr);
    }
}

TEST_CASE("raid heart: the others' part of its shared health is taken at the step it is given, never counted as this group's") {
    Services s;
    const raid::Branch* b = heart();
    if (!b) return;
    raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
    CHECK(run.begin(0, kArena));
    CHECK(engage(run, 4));
    const raid::AsterionHeartRoutine* hr = run.heartRoutine();
    CHECK(hr != nullptr);
    if (!hr) return;
    const auto f = still(4);
    raid::HeartShared h;
    h.others = 3000;
    run.step(f.data(), static_cast<int>(f.size()), 0, h);
    const Enemy* boss = bossOf(run);
    CHECK(boss && boss->hpRatio() <= 0.7001f);
    CHECK(hr->others() == 3000);
    CHECK(hr->own() < 100);  // what the others took is never this group's part
    CHECK(hr->remaining() <= 7000);
    for (int k = 0; k < 4; ++k) run.step(f.data(), static_cast<int>(f.size()), 0, h);
    CHECK(run.gigaboss().phase() >= 2);  // below 75 %: its 2nd phase
    // The relics (each one weakens it) count at its waking: a new run with 4 relics validated.
    raid::RaidRun r2(s.ws(), *b, raid::kMaxInstancePlayers);
    CHECK(r2.begin(0, kArena));
    raid::HeartShared rel;
    rel.relics = 4;
    CHECK(engage(r2, 4, 0, rel));
    const Enemy* b2 = bossOf(r2);
    const EnemyDefinition* def = s.content.enemy("asterion_coeur");
    if (b2 && def) CHECK(b2->maxHp() < def->hp * raid::Gigaboss::healthFactor(r2.rules().gigaboss, 4) * 0.9f);
    // Its whole health gone for the world: it falls here too (its last beat), its fall reported as the 4th phase.
    h.others = 10000;
    bool fell = false;
    std::vector<raid::Report> reports;
    for (int t = 0; t < 1800 && !fell; ++t) {
        run.step(f.data(), static_cast<int>(f.size()), 0, h);
        for (auto& r : run.takeReports()) reports.push_back(r);
        fell = run.gigaboss().stage() == raid::Gigaboss::Stage::Fallen;
    }
    CHECK(fell);
    int boss4 = 0;
    for (const auto& r : reports) boss4 += r.kind == raid::Report::Kind::Boss && r.phase == 4 ? 1 : 0;
    CHECK(boss4 == 1);
    for (const auto& r : reports) CHECK(r.kind != raid::Report::Kind::Boss || r.phase <= 4);
    // The victory is the server's: its objective in the branch's state at a step, seen at that step.
    CHECK(!run.heartWon());
    const uint32_t won = 1u << b->objectiveIndex("coeur_asterion");
    run.step(f.data(), static_cast<int>(f.size()), won, h);
    CHECK(run.heartWon() && run.heartWonNow());
    run.step(f.data(), static_cast<int>(f.size()), won, h);
    CHECK(run.heartWon() && !run.heartWonNow());
}

TEST_CASE("raid heart: this group's part is reported while it changes, its phases as the server reads them (1 to 3, then its fall)") {
    Services s;
    const raid::Branch* b = heart();
    if (!b) return;
    raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
    CHECK(run.begin(0, kArena));
    CHECK(engage(run, 2));
    run.takeReports();
    Enemy* boss = bossOf(run);
    CHECK(boss != nullptr);
    if (!boss) return;
    boss->wound(run.world(), boss->maxHp() * 0.8f);  // down to its 4th phase in one blow
    const auto f = still(2);
    std::vector<raid::Report> reports;
    for (int t = 0; t < 4 * 120; ++t) {
        run.step(f.data(), static_cast<int>(f.size()), 0);
        for (auto& r : run.takeReports()) reports.push_back(r);
    }
    CHECK(run.gigaboss().phase() == 4);
    int heartReports = 0, maxPhase = 0;
    for (const auto& r : reports) {
        if (r.kind == raid::Report::Kind::Heart) {
            ++heartReports;
            CHECK(r.damage >= 7900 && r.damage <= 10000);
        }
        if (r.kind == raid::Report::Kind::Boss) maxPhase = std::max(maxPhase, r.phase);
    }
    CHECK(heartReports >= 1);
    CHECK(maxPhase == 3);  // its 4th phase is not its fall
    // An administrator's test never reports anything.
    raid::RaidRun t(s.ws(), *b, raid::kMaxInstancePlayers);
    t.setTest(testOf(2, 4));
    CHECK(t.begin(0, kArena));
    CHECK(engage(t, 2));
    for (int k = 0; k < 600; ++k) t.step(f.data(), static_cast<int>(f.size()), 0);
    CHECK(t.takeReports().empty());
}

TEST_CASE("raid heart: the parts of the titan fallen act on its fight (lances, sweeps, magma, spawn, gravity, charges)") {
    Services s;
    const raid::Branch* b = heart();
    if (!b) return;
    struct Seen {
        int drops = 0, sweeps = 0, wells = 0, spawned = 0, lances = 0, tracking = 0, missed = 0;
    };
    auto watch = [&](uint32_t parts) {
        Seen seen;
        raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
        run.setTest(testOf(8, 2));
        CHECK(run.begin(0, kArena));
        const uint32_t mask = partsMask(*b, parts);
        CHECK(engage(run, 8, mask));
        const auto f = still(8);
        for (int t = 0; t < 40 * 120; ++t) {
            if (t % 240 == 0) intoHeart(run, 8);
            run.step(f.data(), static_cast<int>(f.size()), mask);
            const raid::AsterionHeartRoutine* hr = run.heartRoutine();
            if (!hr) break;
            seen.drops = std::max(seen.drops, hr->drops());
            seen.sweeps = std::max(seen.sweeps, hr->sweeps());
            seen.wells = std::max(seen.wells, hr->wells());
            seen.spawned = std::max(seen.spawned, hr->spawned());
            for (const auto& l : hr->lanceViews()) {
                ++seen.lances;
                seen.tracking += l.tracking ? 1 : 0;
                if (l.seat >= 0 && std::abs(l.at.x - run.world().playerAt(l.seat).feet().x) >= 40.0f) ++seen.missed;
            }
        }
        return seen;
    };
    const Seen standing = watch(0), fallen = watch(0x7Fu);
    CHECK(standing.drops > 0 && fallen.drops == 0);      // the Right arm: its magma rain
    CHECK(standing.sweeps > 0 && fallen.sweeps == 0);    // the Left arm: its sweeps
    CHECK(standing.wells > 0 && fallen.wells == 0);      // the Left leg: its gravity traps
    CHECK(standing.spawned > 0 && fallen.spawned == 0);  // the Abdomen: its spawn
    CHECK(standing.lances > 0 && fallen.lances > 0);     // its lances on every player either way…
    CHECK(standing.tracking > 0 && fallen.tracking == 0);  // …following them unless the Skull fell,
    CHECK(fallen.missed > 0);                              // then landing beside them
}

TEST_CASE("raid heart: 2 and 8 players in lockstep, the shared state given midway: the same digest on two computers and through the relay") {
    Services s;
    const raid::Branch* b = heart();
    if (!b) return;
    for (int players : {2, 8}) {
        for (int phase : {1, 4}) {
            raid::RaidRun x(s.ws(), *b, raid::kMaxInstancePlayers), y(s.ws(), *b, raid::kMaxInstancePlayers);
            if (phase > 1) {
                x.setTest(testOf(players, phase));
                y.setTest(testOf(players, phase));
            }
            CHECK(x.begin(0, kArena));
            CHECK(y.begin(0, kArena));
            Edges edges(raid::kMaxInstancePlayers);
            std::vector<InputFrame> frame(raid::kMaxInstancePlayers);
            bool same = true;
            for (uint32_t t = 0; t < 2400; ++t) {
                for (int i = 0; i < raid::kMaxInstancePlayers; ++i) {
                    InputFrame f;
                    if (i >= players) f.flags = InputFrame::Vacant;
                    else f = edges(i, scripted(i, t));
                    frame[static_cast<size_t>(i)] = f;
                }
                if (t == 30) {
                    intoHeart(x, players);
                    intoHeart(y, players);
                }
                raid::HeartShared h;
                if (t >= 900) h.others = 1500;  // the other instances' part arrives on the way
                if (t >= 1600) h.others = 2600, h.phase = 2;
                const uint32_t mask = t >= 1200 ? partsMask(*b, 0x55u) : 0u;  // parts fall on the way
                x.step(frame.data(), static_cast<int>(frame.size()), mask, h);
                y.step(frame.data(), static_cast<int>(frame.size()), mask, h);
                if (t % 30 == 29 && x.hash() != y.hash()) same = false;
            }
            CHECK(same);
            CHECK(x.gigaboss().engaged());
            CHECK(x.contributions().canonical() == y.contributions().canonical());
        }
        // Through the relay in raid mode: the heart's state given midway reaches every game at one step.
        Wire w;
        w.relay.setRaid(true);
        w.relay.setLevel(b->levelId(), b->levelHash(), "Asterion · " + b->name, "raid");
        for (int c = 1; c <= players; ++c) {
            w.relay.connected(c, 0);
            w.send(c, hello(255, "Joueur " + std::to_string(c)));
        }
        w.send(players, start(1));
        CHECK(w.relay.started());
        const uint32_t delay = w.relay.nextTick();
        Edges edges(players);
        std::vector<InputFrame> one(1);
        const uint32_t ticks = 1500;
        for (uint32_t t = delay; t < ticks; ++t) {
            if (t == ticks / 2) {
                raid::HeartShared h;
                h.others = 2000;
                h.phase = 1;
                h.relics = 2;
                w.relay.setRaidHeart(h.pack());
            }
            for (int c = 1; c <= players; ++c) {
                one[0] = edges(c - 1, scripted(c - 1, t));
                w.send(c, input(t, one.data(), 1));
            }
        }
        auto play = [&](int conn) {
            raid::RaidRun r(s.ws(), *b, raid::kMaxInstancePlayers);
            CHECK(r.begin(w.stateAt(conn, 0).first, kArena));
            const auto fr = w.frames(conn, raid::kMaxInstancePlayers);
            std::vector<uint64_t> digests;
            for (uint32_t t = 0; t < fr.size(); ++t) {
                if (t == 30) intoHeart(r, players);
                const auto [mask, hv] = w.stateAt(conn, t);
                r.step(fr[t].data(), static_cast<int>(fr[t].size()), mask, raid::HeartShared::unpack(hv));
                if (t % 60 == 59) digests.push_back(r.hash());
            }
            if (const raid::AsterionHeartRoutine* hr = r.heartRoutine()) CHECK(hr->others() == 2000);
            return digests;
        };
        const auto d1 = play(1), d2 = play(players);
        CHECK(!d1.empty());
        CHECK(d1 == d2);
    }
}

TEST_CASE("raid heart: RAID_STATE carries its shared state, an older form still read") {
    raid::HeartShared h;
    h.others = 4321;
    h.phase = 3;
    h.relics = 6;
    CHECK(raid::HeartShared::unpack(h.pack()) == h);
    CHECK(raid::HeartShared::unpack(0xFFFFFFFFu).others == 10000);
    const auto longer = raidState(5, 7, h.pack());
    const auto shorter = raidState(5, 7);
    CHECK(longer.size() == shorter.size() + 4);
}

TEST_CASE("raid heart: its supernova (4th phase) is contained by enough players on its anchors") {
    Services s;
    const raid::Branch* b = heart();
    if (!b) return;
    raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
    run.setTest(testOf(8, 4));
    CHECK(run.begin(0, kArena));
    CHECK(engage(run, 8));
    const auto f = still(8);
    const auto& plates = run.rules().gigaboss.vents;
    bool contained = false;
    for (int t = 0; t < 60 * 120 && !contained; ++t) {
        // Two players on each anchor (eight in play: four anchors needed).
        for (int i = 0; i < 8 && plates.size() == 4; ++i)
            run.world().playerAt(i).teleportTo({plates[static_cast<size_t>(i / 2)].center().x, plates[static_cast<size_t>(i / 2)].bottom()});
        run.step(f.data(), static_cast<int>(f.size()), partsMask(*b, 0x7Fu));
        if (const raid::AsterionHeartRoutine* hr = run.heartRoutine()) {
            CHECK(hr->anchorsNeed() <= 4);
            contained = hr->supernovasContained() > 0;
        }
    }
    CHECK(contained);
    CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Exposed);  // its ventricles bare
    CHECK(run.takeReports().empty());                                  // a test: nothing reported
}

TEST_CASE("raid heart: the film of Asterion's fall reads (its seven shots, its scenes)") {
    std::string err;
    const auto f = story::loadAsterionVictory(&err);
    if (!f) std::printf("  film de la victoire : %s\n", err.c_str());
    CHECK(f.has_value());
    if (!f) return;
    CHECK(f->isFilm());
    CHECK(f->shots.size() == 7);
    for (const auto& shot : f->shots) CHECK(shot.stage.has_value());
    CHECK(story::duration(*f) >= 60.0f);
}
