// 2.69 (phase 4B): the gigaboss of the abdomen and the legs of Asterion (docs/RAIDS.md §13):
// VOIDWORM, GRAVITON, TITANUS. Their generated sheets, their enormous size and arenas, their health
// for 2 to 8 seats, the same digest in lockstep at 2 and 8 players (also through the relay), their
// collective mechanisms (hooks held together, generators cut at the same time, restraints broken
// together), what they throw at every player at once, their fall and its reports (never from an
// administrator's test).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Paths.h"
#include "engine/resources/ResourceManager.h"
#include "engine/camera/Camera.h"
#include "engine/core/FileUtil.h"
#include "game/content/ContentDatabase.h"
#include "game/coop/CoopProtocol.h"
#include "game/coop/CoopRelay.h"
#include "game/enemies/BossHazards.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/GigabossRoutine.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "tests/TestFramework.h"

#ifdef _WIN32
#define COSMIC_GIGA_NULL "NUL"
#else
#define COSMIC_GIGA_NULL "/dev/null"
#endif

using namespace cosmic;
using namespace cosmic::coopnet;

namespace {

constexpr int R = static_cast<int>(Action::MoveRight);
constexpr int L = static_cast<int>(Action::MoveLeft);
constexpr int J = static_cast<int>(Action::Jump);
constexpr int A = static_cast<int>(Action::Attack);

struct Boss {
    const char* part;
    const char* id;
    const char* objective;
    const char* routine;
};
const Boss kBosses[] = {{"abdomen", "voidworm", "abd_voidworm", "voidworm"},
                        {"jambe_gauche", "graviton", "jg_graviton", "graviton"},
                        {"jambe_droite", "titanus", "jd_titanus", "titanus"}};
constexpr int kLair = 8;

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

const raid::Branch& branchOf(const char* part) {
    const raid::Branch* b = raid::Branch::builtin(part);
    CHECK(b != nullptr);
    if (!b) std::printf("  branche introuvable : %s\n", part);
    return *b;
}

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

/// The players in play put on the floor of the arena, a good way in: the gigaboss wakes up.
void intoArena(raid::RaidRun& run, int players) {
    const raid::ArenaInfo& a = run.rules().arena;
    for (int i = 0; i < players; ++i) run.world().playerAt(i).teleportTo({a.entry.x + 420.0f + 26.0f * static_cast<float>(i), a.floor});
}

bool engage(raid::RaidRun& run, int players, int limit = 900) {
    const auto f = still(players);
    for (int t = 0; t < limit && !run.gigaboss().engaged(); ++t) {
        if (t % 30 == 0) intoArena(run, players);
        run.step(f.data(), static_cast<int>(f.size()), 0);
    }
    return run.gigaboss().engaged();
}

raid::RaidRun::Test testOf(int players, int phase) {
    raid::RaidRun::Test t;  // invincible: only its mechanisms are counted here
    t.on = true;
    t.invincible = true;
    t.players = players;
    t.startPhase = phase;
    return t;
}

Rect rectOf(const Json& j) { return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()}; }

std::vector<raid::BossMark> marksOf(raid::RaidRun& run) {
    std::vector<raid::BossMark> out;
    if (const raid::GigabossRoutine* rt = run.gigaboss().routine()) rt->marks(run.world(), run.gigaboss(), out);
    return out;
}

int aliveOf(raid::RaidRun& run, const char* id) {
    int n = 0;
    for (auto& e : run.world().enemies()) n += e->alive() && e->def().id == id ? 1 : 0;
    return n;
}

uint32_t bitOf(const raid::Branch& b, const char* objective) { return 1u << b.objectiveIndex(objective); }

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
    uint32_t stateAt(int conn, uint32_t t) {
        uint32_t mask = 0;
        for (const auto& m : of(conn, Msg::RaidState)) {
            Reader r{m.data() + 1, m.data() + m.size()};
            const uint32_t at = r.u32();
            const uint32_t v = r.u32();
            if (at <= t) mask = v;
        }
        return mask;
    }
};

void playThroughRelay(Wire& w, const raid::Branch& b, int players, uint32_t ticks) {
    w.relay.setRaid(true);
    w.relay.setLevel(b.levelId(), b.levelHash(), "Asterion · " + b.name, "raid");
    for (int c = 1; c <= players; ++c) {
        w.relay.connected(c, 0);
        w.send(c, hello(255, "Joueur " + std::to_string(c)));
    }
    w.send(players, start(1));
    CHECK(w.relay.started());
    const uint32_t delay = w.relay.nextTick();
    Edges edges(players);
    std::vector<InputFrame> one(1);
    for (uint32_t t = delay; t < ticks; ++t) {
        if (t == ticks / 2) w.relay.setRaidState(1u);  // the branch's state changes midway
        for (int c = 1; c <= players; ++c) {
            one[0] = edges(c - 1, scripted(c - 1, t));
            w.send(c, input(t, one.data(), 1));
        }
    }
}

}  // namespace

TEST_CASE("raid gigaboss 4B: the sheets of Voidworm, Graviton and Titanus are their generators' output") {
    if (std::system("python3 -c \"import numpy, PIL\" > " COSMIC_GIGA_NULL " 2>&1") != 0) {
        std::printf("  (python3 + numpy + Pillow absents : vérification des générateurs de la phase 4B sautée)\n");
        return;
    }
    for (const char* script : {"boss_m_voidworm.py", "boss_m_graviton.py", "boss_m_titanus.py"})
        CHECK(std::system((std::string("python3 tools/assetgen/characters/") + script + " --check > " COSMIC_GIGA_NULL " 2>&1").c_str()) == 0);
}

TEST_CASE("raid gigaboss 4B: three enormous gigaboss, three phases each, their lairs unsealed with their routine") {
    Services s;
    for (const Boss& k : kBosses) {
        const EnemyDefinition* def = s.content.enemy(k.id);
        CHECK(def != nullptr);
        if (!def) continue;
        CHECK(def->isBoss);
        CHECK(def->phases.size() == 3);
        CHECK(def->size.y >= 330.0f);  // several times Nova's height
        CHECK(def->scale >= 2.5f);
        CHECK(def->raid == "asterion");
        const raid::Branch& b = branchOf(k.part);
        const raid::ZoneRules* z = b.zoneRules(kLair);
        CHECK(z != nullptr);
        if (!z) continue;
        const raid::GigabossRules& g = z->gigaboss;
        CHECK(g.present);
        CHECK(z->sealedLabel.empty());
        CHECK(g.routine == k.routine);
        CHECK(g.objective == k.objective);
        CHECK(b.objective(k.objective) && b.objective(k.objective)->role == "gigaboss");
        CHECK(raid::GigabossRoutine::make(g.routine) != nullptr);
        CHECK(s.content.enemy(g.weakEnemy) != nullptr);
        CHECK(g.weakSpots.size() == 3);  // at most three a player can break in an exposure (the server's bound)
        CHECK(g.fistPeriods.size() == 3);
        CHECK(!g.fallText.empty() && !g.summary.empty() && g.fx != "animations/pyroclast_vents.json");
        CHECK(FileUtil::exists(Paths::assetsDir() / g.fx));
        // Its arena: the format of the seven lairs, several screens wide.
        CHECK(z->arena.present);
        CHECK(z->arena.rect.w >= 6.0f * Camera::kViewWidth);
        CHECK(z->arena.rect.contains({z->arena.entry.x, z->arena.entry.y - 1.0f}));
        // The objectives of the branch it reads (phase 4A's nest, valves, generators, reactors).
        for (const auto& [name, bit] : g.links) CHECK(bit >= 0);
        CHECK(!g.links.empty());
    }
    CHECK(s.content.enemy("voidworm_poche") != nullptr);
    CHECK(s.content.enemy("titanus_entrave") != nullptr);
}

TEST_CASE("raid gigaboss 4B: their health follows the seats taken (2 to 8), the pressure the players in play") {
    Services s;
    for (const Boss& k : kBosses) {
        const EnemyDefinition* def = s.content.enemy(k.id);
        const raid::Branch& b = branchOf(k.part);
        if (!def || !raid::Branch::builtin(k.part)) continue;
        for (int players : {2, 5, 8}) {
            raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
            CHECK(run.begin(0, kLair));
            CHECK(run.gigaboss().present());
            CHECK(engage(run, players));
            const Enemy* boss = bossOf(run);
            CHECK(boss != nullptr);
            if (!boss) continue;
            const float want = def->hp * raid::Gigaboss::healthFactor(run.rules().gigaboss, players);
            CHECK(std::abs(boss->maxHp() - want) < 0.5f);
            CHECK(run.gigaboss().scaledFor() == players);
            CHECK(boss->pressure() >= 1.0f);
        }
    }
}

TEST_CASE("raid gigaboss 4B: 2 and 8 players in lockstep give the same digest on two computers and through the relay") {
    Services s;
    for (const Boss& k : kBosses) {
        const raid::Branch& b = branchOf(k.part);
        if (!raid::Branch::builtin(k.part)) continue;
        for (int players : {2, 8}) {
            // From the start of its fight, and from its 3rd phase (an administrator's start: its acid,
            // its sectors turned over by halves, its double shockwaves).
            for (int phase : {1, 3}) {
                raid::RaidRun x(s.ws(), b, raid::kMaxInstancePlayers), y(s.ws(), b, raid::kMaxInstancePlayers);
                if (phase > 1) {
                    x.setTest(testOf(players, phase));
                    y.setTest(testOf(players, phase));
                }
                CHECK(x.begin(0, kLair));
                CHECK(y.begin(0, kLair));
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
                        intoArena(x, players);
                        intoArena(y, players);
                    }
                    const uint32_t mask = t >= 1200 ? 0xFFu : 0u;  // the branch's state changes on the way (its links)
                    x.step(frame.data(), static_cast<int>(frame.size()), mask);
                    y.step(frame.data(), static_cast<int>(frame.size()), mask);
                    if (t % 30 == 29 && x.hash() != y.hash()) same = false;
                }
                CHECK(same);
                CHECK(x.gigaboss().engaged());
                CHECK(x.contributions().canonical() == y.contributions().canonical());
            }
            // Through the relay in raid mode: two games fed with what each received play the same.
            Wire w;
            playThroughRelay(w, b, players, 1500);
            auto play = [&](int conn) {
                raid::RaidRun r(s.ws(), b, raid::kMaxInstancePlayers);
                CHECK(r.begin(w.stateAt(conn, 0), kLair));
                const auto fr = w.frames(conn, raid::kMaxInstancePlayers);
                std::vector<uint64_t> digests;
                for (uint32_t t = 0; t < fr.size(); ++t) {
                    if (t == 30) intoArena(r, players);
                    r.step(fr[t].data(), static_cast<int>(fr[t].size()), w.stateAt(conn, t));
                    if (t % 60 == 59) digests.push_back(r.hash());
                }
                return digests;
            };
            const auto d1 = play(1), d2 = play(players);
            CHECK(!d1.empty());
            CHECK(d1 == d2);
        }
    }
}

TEST_CASE("raid gigaboss 4B: each strikes every player in play at once (one mark under each, the same step)") {
    Services s;
    for (const Boss& k : kBosses) {
        const raid::Branch& b = branchOf(k.part);
        if (!raid::Branch::builtin(k.part)) continue;
        for (int players : {2, 8}) {
            raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
            run.setTest(testOf(players, 1));
            CHECK(run.begin(0, kLair));
            CHECK(engage(run, players));
            const auto f = still(players);
            size_t most = 0;
            for (int t = 0; t < 1400; ++t) {
                run.step(f.data(), static_cast<int>(f.size()), 0);
                most = std::max(most, run.gigaboss().fists().size());
            }
            CHECK(most == static_cast<size_t>(players));
        }
    }
}

TEST_CASE("raid gigaboss 4B: Voidworm's hooks held together pin it; it swallows a player the others free; its acid; its parasites") {
    Services s;
    const raid::Branch& b = branchOf("abdomen");
    if (!raid::Branch::builtin("abdomen")) return;
    {
        raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
        run.setTest(testOf(3, 2));
        CHECK(run.begin(0, kLair));
        CHECK(engage(run, 3));
        const auto f = still(3);
        const auto& hooks = run.rules().gigaboss.vents;
        CHECK(hooks.size() == 3);
        auto onHook = [&](int player, int k) { run.world().playerAt(player).teleportTo({hooks[static_cast<size_t>(k)].center().x, hooks[static_cast<size_t>(k)].bottom()}); };
        for (int t = 0; t < 30; ++t) run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(run.gigaboss().phase() == 2);
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace);
        Enemy* boss = bossOf(run);
        CHECK(boss && boss->raidGuarded());
        // One hook alone: pinned, the chitin holds.
        for (int t = 0; t < 420; ++t) {
            onHook(0, 0);
            run.step(f.data(), static_cast<int>(f.size()), 0);
        }
        CHECK(run.gigaboss().ventsClosed() >= 1);
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace);
        // The three together: its maw opens, its glowing rings bare.
        for (int t = 0; t < 420 && run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace; ++t) {
            for (int i = 0; i < 3; ++i) onHook(i, i);
            run.step(f.data(), static_cast<int>(f.size()), 0);
        }
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Exposed);
        run.step(f.data(), static_cast<int>(f.size()), 0);
        CHECK(boss && !boss->raidGuarded());
        CHECK(run.gigaboss().weakTotal() == 3);
        CHECK(aliveOf(run, "voidworm_anneau") == 3);
        int pinned = 0;
        for (const raid::Contribution& c : run.contributions().records()) pinned += c.vents;
        CHECK(pinned >= 3);
    }
    {
        // Its maw aims at a player standing still: swallowed into its stomach, freed when it bursts.
        raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
        run.setTest(testOf(3, 2));
        CHECK(run.begin(0, kLair));
        CHECK(engage(run, 3));
        const auto f = still(3);
        const Rect stomach = rectOf(run.rules().gigaboss.extra["swallow"]["stomach"]);
        int held = -1;
        for (int t = 0; t < 2400 && held < 0; ++t) {
            run.step(f.data(), static_cast<int>(f.size()), 0);
            for (int i = 0; i < 3; ++i)
                if (stomach.contains(run.world().playerAt(i).center())) held = i;
        }
        CHECK(held >= 0);
        run.step(f.data(), static_cast<int>(f.size()), 0);  // its stomach joins the world with the next step
        bool shown = false;
        for (const raid::BossMark& m : marksOf(run)) shown = shown || m.label.rfind("AVALÉ", 0) == 0;
        CHECK(shown);
        CHECK(aliveOf(run, "voidworm_poche") == 1);
        int outside = 0;
        for (int i = 0; i < 3; ++i) outside += stomach.contains(run.world().playerAt(i).center()) ? 0 : 1;
        CHECK(outside >= 1);  // never the last players outside
        // 2.69: the one in its stomach gets no mark of its « Fouissement » (it would hit them inside).
        bool markedInside = false, volley = false;
        for (int t = 0; t < 1000 && stomach.contains(run.world().playerAt(held).center()); ++t) {
            run.step(f.data(), static_cast<int>(f.size()), 0);
            for (const auto& fist : run.gigaboss().fists()) {
                volley = true;
                markedInside = markedInside || fist.seat == held || stomach.contains({fist.at.x, fist.at.y - 2.0f});
            }
        }
        CHECK(volley);
        CHECK(!markedInside);
        for (auto& e : run.world().enemies())
            if (e->alive() && e->def().id == "voidworm_poche") e->kill(run.world());
        for (int t = 0; t < 3; ++t) run.step(f.data(), static_cast<int>(f.size()), 0);
        if (held >= 0) CHECK(!stomach.contains(run.world().playerAt(held).center()));
        CHECK(aliveOf(run, "voidworm_poche") == 0);
    }
    {
        // Its acid in its 3rd phase: a tide over the arena.
        raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
        run.setTest(testOf(2, 3));
        CHECK(run.begin(0, kLair));
        CHECK(engage(run, 2));
        const auto f = still(2);
        bool flood = false;
        for (int t = 0; t < 1800 && !flood; ++t) {
            run.step(f.data(), static_cast<int>(f.size()), 0);
            if (const Enemy* boss = bossOf(run)) flood = run.world().hazards().hasFlood(boss->uid());
        }
        CHECK(flood);
    }
    // Its parasites: fewer once the nest of the Abdomen is destroyed (phase 4A's objective).
    auto parasites = [&](uint32_t mask) {
        raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
        run.setTest(testOf(8, 1));
        CHECK(run.begin(mask, kLair));
        const auto f = still(8);
        for (int t = 0; t < 900 && !run.gigaboss().engaged(); ++t) {
            if (t % 30 == 0) intoArena(run, 8);
            run.step(f.data(), static_cast<int>(f.size()), mask);
        }
        int most = 0;
        for (int t = 0; t < 2400; ++t) {
            run.step(f.data(), static_cast<int>(f.size()), mask);
            int n = 0;
            if (const Enemy* boss = bossOf(run))
                for (auto& e : run.world().enemies()) n += e->alive() && e->summonedBy == boss->uid() && (e->def().id == "gloop_small" || e->def().id == "wisp");
            most = std::max(most, n);
        }
        return most;
    };
    const int full = parasites(0), fewer = parasites(bitOf(b, "abd_nid"));
    CHECK(full >= 4);
    CHECK(fewer >= 1);
    CHECK(fewer < full);
}

TEST_CASE("raid gigaboss 4B: Graviton's generators cut at the same time make it collapse; its sectors turn over") {
    Services s;
    const raid::Branch& b = branchOf("jambe_gauche");
    if (!raid::Branch::builtin("jambe_gauche")) return;
    auto gens = [&](raid::RaidRun& run) {
        std::vector<Rect> out;
        for (const auto& g : run.rules().gigaboss.extra["generators"]) out.push_back(rectOf(g["rect"]));
        return out;
    };
    for (uint32_t mask : {0u, bitOf(b, "jg_generateur_ouest") | bitOf(b, "jg_generateur_est")}) {
        raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
        run.setTest(testOf(4, 2));
        CHECK(run.begin(mask, kLair));
        const auto f = still(4);
        for (int t = 0; t < 900 && !run.gigaboss().engaged(); ++t) {
            if (t % 30 == 0) intoArena(run, 4);
            run.step(f.data(), static_cast<int>(f.size()), mask);
        }
        for (int t = 0; t < 30; ++t) run.step(f.data(), static_cast<int>(f.size()), mask);
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace);
        const auto g = gens(run);
        CHECK(g.size() == 4);
        auto on = [&](int player, int k) { run.world().playerAt(player).teleportTo({g[static_cast<size_t>(k)].center().x, g[static_cast<size_t>(k)].bottom()}); };
        // Two of four generators held, however long: its field holds (one generator less once the
        // leg's two generators are cut: three then).
        for (int t = 0; t < 600; ++t) {
            on(0, 0);
            on(1, 3);
            run.step(f.data(), static_cast<int>(f.size()), mask);
        }
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace);
        const int need = mask ? 3 : 4;
        for (int t = 0; t < 600 && run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace; ++t) {
            for (int i = 0; i < need; ++i) on(i, i);
            run.step(f.data(), static_cast<int>(f.size()), mask);
        }
        CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Exposed);
        for (int t = 0; t < 90; ++t) run.step(f.data(), static_cast<int>(f.size()), mask);
        const Enemy* boss = bossOf(run);
        CHECK(boss != nullptr);
        if (boss) CHECK(std::abs(boss->box.bottom() - run.rules().arena.floor) < 2.0f);  // collapsed onto the floor
        CHECK(aliveOf(run, "graviton_coeur") == 3);
        int cut = 0;
        for (const raid::Contribution& c : run.contributions().records()) cut += c.vents;
        CHECK(cut >= need);
    }
    // Its sectors turn over: a player standing in one falls toward the vault.
    raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
    run.setTest(testOf(2, 1));
    CHECK(run.begin(0, kLair));
    CHECK(engage(run, 2));
    const auto f = still(2);
    bool turned = false, lifted = false;
    for (int t = 0; t < 2400 && !lifted; ++t) {
        run.step(f.data(), static_cast<int>(f.size()), 0);
        for (const raid::BossMark& m : marksOf(run)) {
            if (m.kind != raid::BossMark::Kind::Zone || !m.strong) continue;
            turned = true;
            // Put a player in the middle of the sector turned over, on its floor; it rises.
            Player& p = run.world().playerAt(0);
            p.teleportTo({m.area.center().x, run.rules().arena.floor});
            const float y0 = p.feet().y;
            for (int k = 0; k < 40; ++k) run.step(f.data(), static_cast<int>(f.size()), 0);
            lifted = run.world().playerAt(0).feet().y < y0 - 40.0f;
            break;
        }
    }
    CHECK(turned);
    CHECK(lifted);
}

TEST_CASE("raid gigaboss 4B: Titanus's restraints broken together bring it to its knee; one alone forges again; its shockwaves") {
    Services s;
    const raid::Branch& b = branchOf("jambe_droite");
    if (!raid::Branch::builtin("jambe_droite")) return;
    raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
    run.setTest(testOf(3, 2));
    CHECK(run.begin(0, kLair));
    CHECK(engage(run, 3));
    const auto f = still(3);
    for (int t = 0; t < 30; ++t) run.step(f.data(), static_cast<int>(f.size()), 0);
    CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace);
    CHECK(aliveOf(run, "titanus_entrave") == 2);
    auto restraints = [&]() {
        std::vector<Enemy*> out;
        for (auto& e : run.world().enemies())
            if (e->alive() && e->def().id == "titanus_entrave") out.push_back(e.get());
        return out;
    };
    // One alone: it forges again after the window, the armour holds.
    restraints().front()->kill(run.world());
    run.step(f.data(), static_cast<int>(f.size()), 0);
    CHECK(aliveOf(run, "titanus_entrave") == 1);
    for (int t = 0; t < 600 && aliveOf(run, "titanus_entrave") < 2; ++t) run.step(f.data(), static_cast<int>(f.size()), 0);
    CHECK(aliveOf(run, "titanus_entrave") == 2);
    CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Carapace);
    // Both at once: on its knee, its knee bare.
    for (Enemy* e : restraints()) e->kill(run.world());
    for (int t = 0; t < 3; ++t) run.step(f.data(), static_cast<int>(f.size()), 0);
    CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Exposed);
    CHECK(aliveOf(run, "titanus_genou") == 3);
    const Enemy* boss = bossOf(run);
    CHECK(boss && !boss->raidGuarded());
    // Its shockwaves run along the floor (shown as crests over the arena).
    bool wave = false;
    for (int t = 0; t < 2400 && !wave; ++t) {
        run.step(f.data(), static_cast<int>(f.size()), 0);
        for (const raid::BossMark& m : marksOf(run)) wave = wave || m.kind == raid::BossMark::Kind::Wave;
    }
    CHECK(wave);
}

TEST_CASE("raid gigaboss 4B: each fall reports its phases, its objective and the contributions; an admin test never reports") {
    Services s;
    for (const Boss& k : kBosses) {
        const raid::Branch& b = branchOf(k.part);
        if (!raid::Branch::builtin(k.part)) continue;
        for (bool test : {false, true}) {
            raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
            if (test) run.setTest(testOf(8, 1));
            CHECK(run.begin(0, kLair));
            CHECK(engage(run, 2));
            std::vector<raid::Report> reports;
            for (auto& r : run.takeReports()) reports.push_back(r);
            Enemy* boss = bossOf(run);
            CHECK(boss != nullptr);
            if (!boss) continue;
            const auto f = still(2);
            boss->wound(run.world(), boss->maxHp() * 2.0f);  // its last stand, then its fall
            for (int t = 0; t < 5000 && run.gigaboss().stage() != raid::Gigaboss::Stage::Fallen; ++t) {
                if (t % 60 == 0)
                    for (int i = 0; i < 2; ++i) run.world().playerAt(i).heal(10);
                run.step(f.data(), static_cast<int>(f.size()), 0);
                for (auto& r : run.takeReports()) reports.push_back(r);
            }
            CHECK(run.gigaboss().stage() == raid::Gigaboss::Stage::Fallen);
            for (int t = 0; t < 10; ++t) {
                run.step(f.data(), static_cast<int>(f.size()), 0);
                for (auto& r : run.takeReports()) reports.push_back(r);
            }
            int present = 0;
            for (const raid::Contribution& c : run.contributions().records()) present += c.gigaboss ? 1 : 0;
            CHECK(present == 2);
            if (test) {
                CHECK(reports.empty());  // never a report: no consensus, no ranking, no reward
                continue;
            }
            bool fell = false, objective = false, result = false;
            for (const raid::Report& r : reports) {
                fell = fell || (r.kind == raid::Report::Kind::Boss && r.phase == 4 && r.zone == kLair);
                objective = objective || (r.kind == raid::Report::Kind::Objective && r.objective == k.objective);
                if (r.kind == raid::Report::Kind::Result)
                    for (const raid::Contribution& c : r.records) result = result || c.gigaboss;
            }
            CHECK(fell);
            CHECK(objective);
            CHECK(result);
        }
    }
}
