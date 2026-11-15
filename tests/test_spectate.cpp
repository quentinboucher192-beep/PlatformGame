// Live spectating between friends (2.35, game side): the inputs as they are streamed, the
// stream a viewer rebuilds from the server's answers, the re-simulation of a friend's run
// (exactly the run played, step for step, like an exact replay, with or without challenge
// rules) and the broadcaster's order of requests through a cut network, a lost answer and
// a room that forgot the run. Nothing here touches the simulation's rules.

#include <chrono>
#include <map>
#include <mutex>
#include <thread>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/ChallengeRules.h"
#include "game/gameplay/GameWorld.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/OnlineService.h"
#include "game/online/Spectate.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// Plausible play (the determinism tests' kind): running, jumps, attacks, dashes, crouches.
std::vector<InputFrame> playInputs(uint64_t seed, int ticks) {
    Random r(seed);
    std::vector<InputFrame> out;
    InputFrame prev, cur;
    int hold = 0;
    for (int i = 0; i < ticks; ++i) {
        if (hold-- <= 0) {
            cur = InputFrame{};
            float k = r.unit();
            int dir = k < 0.75f ? 1 : (k < 0.88f ? -1 : 0);
            if (dir > 0) cur.held |= 1u << static_cast<int>(Action::MoveRight);
            if (dir < 0) cur.held |= 1u << static_cast<int>(Action::MoveLeft);
            cur.moveX = static_cast<int8_t>(dir * 127);
            if (r.chance(0.45f)) cur.held |= 1u << static_cast<int>(Action::Jump);
            if (r.chance(0.2f)) cur.held |= 1u << static_cast<int>(Action::Attack);
            if (r.chance(0.08f)) cur.held |= 1u << static_cast<int>(Action::PowerSlot1);
            if (r.chance(0.05f)) {
                cur.held |= 1u << static_cast<int>(Action::MoveDown);
                cur.moveY = 127;
            }
            hold = r.rangeInt(4, 40);
        }
        InputFrame f = cur;
        f.pressed = f.held & ~prev.held;
        f.released = prev.held & ~f.held;
        out.push_back(f);
        prev = f;
    }
    return out;
}

struct World {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;

    /// The level loaded as GameScene and the viewer do it (keys per slot, the level's seed).
    explicit World(const std::string& levelId) {
        content.reload();
        res.init(nullptr, &audio);
        auto level = content.loadLevel(levelId);
        CHECK(level.has_value());
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->setKeysPerSlot(true);
        w->load(*level);
        w->seedGameplay(levelSeed(levelId));
    }
};

/// An answer of GET /spectate/{id} with these chunks.
Json answer(const SpectateHeader& h, const std::vector<Json>& chunks, const std::string& ended = {}) {
    Json run = {{"run_id", h.runId}, {"stream", h.stream}, {"kind", h.kind}, {"level", {{"source", h.levelSource}, {"id", h.levelId}, {"version", h.levelVersion},
                {"hash", h.levelHash}, {"title", "Forêt"}}}, {"seed", spectate::hex64(h.seed)}, {"keys_per_slot", h.keysPerSlot}, {"sim_version", h.simVersion},
                {"game_version", "2.35.0"}, {"started_at", "2026-11-15T20:00:00.000Z"}, {"challenge", nullptr}};
    if (!h.challengeId.empty()) run["challenge"] = {{"id", h.challengeId}, {"kind", "daily"}, {"rules", h.challengeRules}};
    Json list = Json::array();
    for (const auto& c : chunks) {
        Json k = c;
        k.erase("action");
        k.erase("run_id");
        if (!k.contains("checks")) k["checks"] = Json::array();
        list.push_back(k);
    }
    return {{"live", true}, {"run", run}, {"chunks", list}, {"more", false}, {"last_seq", chunks.empty() ? 0 : chunks.back()["seq"].get<int>()},
            {"ended", ended.empty() ? Json(nullptr) : Json(ended)}, {"stale", false}};
}

SpectateHeader header(uint64_t seed, const std::string& stream = "s-1") {
    SpectateHeader h;
    h.runId = "0b6f3b0e-1d2c-4a5b-8c7d-9e0f1a2b3c4d";
    h.stream = stream;
    h.kind = "campaign";
    h.levelSource = "campaign";
    h.levelId = "sylvanox_01";
    h.levelVersion = 1;
    h.levelHash = std::string(64, 'a');
    h.seed = seed;
    h.keysPerSlot = true;
    h.simVersion = kSimulationVersion;
    return h;
}

/// Plays a run on `levelId` (with `rules`, a challenge run) as the game does while it streams
/// it: the recorder's inputs and digests cut into chunks every 60 steps. Returns the state
/// digest after every step (`live`), and the replay recorded at the same time.
struct LiveRun {
    std::vector<uint64_t> live;
    std::vector<Json> chunks;
    ReplayData replay;
    uint64_t seed = 0;
};
LiveRun playLive(const std::string& levelId, const std::vector<InputFrame>& inputs, const ChallengeRules* rules) {
    LiveRun out;
    World game(levelId);
    if (rules) game.w->setChallenge(*rules);
    out.seed = game.w->gameplaySeed();
    ReplayRecorder rec;
    rec.begin(levelId, std::string(64, 'a'), makeUuidV4(), out.seed);
    std::vector<InputFrame> pending;
    std::vector<ReplayCheck> checks;
    size_t checksGiven = 0;
    uint32_t sent = 0;
    int seq = 0;
    auto flush = [&] {
        out.chunks.push_back(spectate::chunkBody("run", ++seq, sent, pending.data(), pending.size(), checks));
        sent += static_cast<uint32_t>(pending.size());
        pending.clear();
        checks.clear();
    };
    for (const auto& in : inputs) {
        InputFrame f = in;
        if (game.w->cinematic().active() && game.w->cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
        rec.recordInput(f);
        pending.push_back(f);
        stepWorld(*game.w, f, kSimDt);
        rec.tick(*game.w);
        for (; checksGiven < rec.data().checks.size(); ++checksGiven) checks.push_back(rec.data().checks[checksGiven]);
        out.live.push_back(simulationStateHash(*game.w));
        if (pending.size() == 60) flush();
    }
    if (!pending.empty()) flush();
    out.replay = rec.take();
    return out;
}

}  // namespace

TEST_CASE("spectate: inputs survive the stream encoding unchanged") {
    // Every value a step can hold: actions on 21 bits, sticks at both ends, a skipped
    // cinematic, the power choices (a slot, leave it).
    std::vector<InputFrame> frames;
    InputFrame a;
    a.held = (1u << InputFrame::kActions) - 1u;
    a.pressed = 1u << static_cast<int>(Action::PowerSlot8);
    a.released = 1u;
    a.moveX = -127;
    a.moveY = 127;
    a.flags = InputFrame::SkipCinematic;
    frames.insert(frames.end(), 70000, a);  // more than one run (u16 repeat)
    InputFrame b;
    b.choice = InputFrame::kChoiceLeave;
    frames.push_back(b);
    b.choice = 8;
    frames.push_back(b);
    auto more = playInputs(5, 900);
    frames.insert(frames.end(), more.begin(), more.end());
    const std::string text = spectate::encodeInputs(frames.data(), frames.size());
    std::vector<InputFrame> back;
    CHECK(spectate::decodeInputs(text, static_cast<uint32_t>(frames.size()), back));
    CHECK(back == frames);
    // The exact count, nothing else.
    std::vector<InputFrame> none;
    std::string err;
    CHECK(!spectate::decodeInputs(text, static_cast<uint32_t>(frames.size()) - 1, none, &err));
    CHECK(!spectate::decodeInputs(text, static_cast<uint32_t>(frames.size()) + 1, none, &err));
    CHECK(none.empty());
    CHECK(!spectate::decodeInputs("pas du base64!", 60, none));
    CHECK(!spectate::decodeInputs(text.substr(0, 20), 60, none));
    CHECK(spectate::decodeInputs("", 0, none) && none.empty());
    // Values no game sends: refused (the server refuses them too).
    for (int bad = 0; bad < 4; ++bad) {
        InputFrame f;
        if (bad == 0) f.held = 1u << InputFrame::kActions;
        if (bad == 1) f.moveX = -128;
        if (bad == 2) f.flags = 2;
        if (bad == 3) f.choice = 9;
        CHECK(!spectate::decodeInputs(spectate::encodeInputs(&f, 1), 1, none));
    }
    // Base64 of every length, and the 64-bit values of the seed and the digests.
    for (size_t n = 0; n < 40; ++n) {
        std::vector<uint8_t> bytes(n);
        for (size_t i = 0; i < n; ++i) bytes[i] = static_cast<uint8_t>(i * 37 + n);
        auto dec = spectate::base64Decode(spectate::base64Encode(bytes.data(), bytes.size()));
        CHECK(dec.has_value() && *dec == bytes);
    }
    CHECK(!spectate::base64Decode("QQ=A").has_value());
    uint64_t v = 0;
    CHECK(spectate::parseHex64(spectate::hex64(0xfedcba9876543210ull), v) && v == 0xfedcba9876543210ull);
    CHECK(!spectate::parseHex64("FEDCBA9876543210", v));  // lower case only, as the server writes it
}

TEST_CASE("spectate: the viewer's stream follows the chunks in order and starts again when told") {
    auto inputs = playInputs(11, 300);
    SpectateHeader h = header(42);
    std::vector<Json> chunks;
    for (int i = 0; i < 5; ++i) chunks.push_back(spectate::chunkBody(h.runId, i + 1, i * 60, inputs.data() + i * 60, 60, {}));
    SpectateStream s;
    CHECK(s.absorb({{"live", false}}) == SpectateStream::Update::None);
    CHECK(!s.live() && s.query().empty());
    CHECK(s.absorb(answer(h, {chunks[0], chunks[1]})) == SpectateStream::Update::Started);
    CHECK(s.live() && s.frames() == 120 && s.lastSeq() == 2);
    CHECK(s.replay().seed == 42 && s.replay().keysPerSlot);
    CHECK(s.query() == "?stream=s-1&after=2");
    // Again the same chunks (an answer read twice): nothing new.
    CHECK(s.absorb(answer(h, {chunks[1]})) == SpectateStream::Update::None);
    CHECK(s.absorb(answer(h, {chunks[2]})) == SpectateStream::Update::Added);
    CHECK(s.frames() == 180);
    CHECK(std::equal(s.replay().inputs.begin(), s.replay().inputs.end(), inputs.begin()));
    // A gap: everything again from the start.
    std::string err;
    CHECK(s.absorb(answer(h, {chunks[4]}), &err) == SpectateStream::Update::Invalid);
    CHECK(!err.empty() && s.frames() == 0 && s.query().empty());
    CHECK(s.absorb(answer(h, chunks)) == SpectateStream::Update::Started);
    CHECK(s.frames() == 300);
    // The ended run.
    CHECK(s.absorb(answer(h, {}, "finished")) == SpectateStream::Update::None);
    CHECK(s.ended() == "finished" && s.frames() == 300);
    // Another stream (a restart, or the same run sent again): from its start.
    SpectateHeader h2 = header(7, "s-2");
    CHECK(s.absorb(answer(h2, {chunks[0]})) == SpectateStream::Update::Started);
    CHECK(s.frames() == 60 && s.replay().seed == 7 && s.ended().empty());
    // A header that does not hold together is refused.
    Json broken = answer(h2, {});
    broken["run"]["seed"] = "xyz";
    CHECK(s.absorb(broken) == SpectateStream::Update::Invalid);
}

TEST_CASE("spectate: a friend's run is re-simulated exactly, step for step") {
    const auto inputs = playInputs(99, 120 * 25);
    for (int variant = 0; variant < 2; ++variant) {
        // 0: a campaign run; 1: a challenge run (its rules from the header, as the server gives them).
        ChallengeRules rules;
        rules.challengeId = "daily-2026-11-15";
        rules.hearts = 2;
        rules.slots = 3;
        rules.powers = {"dash"};
        rules.checkpoints = false;
        const ChallengeRules* r = variant == 1 ? &rules : nullptr;
        LiveRun run = playLive("sylvanox_01", inputs, r);
        SpectateHeader h = header(run.seed);
        if (r) {
            h.kind = "challenge";
            h.challengeId = rules.challengeId;
            h.challengeRules = rules.canonical();
        }
        // The viewer receives the chunks a few at a time (every half second, late, in bursts)
        // and simulates what it has, never more.
        World view("sylvanox_01");
        SpectateStream s;
        std::unique_ptr<ReplaySimulator> sim;
        std::vector<Json> received;
        size_t next = 0, mismatches = 0;
        Random jitter(3);
        while (next < run.chunks.size() || (sim && sim->tick() < s.frames())) {
            const size_t burst = std::min(run.chunks.size() - next, static_cast<size_t>(jitter.rangeInt(0, 4)));
            for (size_t i = 0; i < burst; ++i) received.push_back(run.chunks[next++]);
            const auto u = s.absorb(answer(h, received));
            CHECK(u != SpectateStream::Update::Invalid);
            if (u == SpectateStream::Update::Started) sim = std::make_unique<ReplaySimulator>(s.replay(), *view.w);
            if (!sim) continue;
            const uint32_t upTo = std::min<uint32_t>(s.frames(), sim->tick() + static_cast<uint32_t>(jitter.rangeInt(10, 200)));
            while (sim->tick() < upTo) {
                CHECK(sim->step());
                view.w->update(1.0f / 50.0f);  // the viewer's own display rate
                if (simulationStateHash(*view.w) != run.live[sim->tick() - 1]) ++mismatches;
            }
        }
        CHECK(sim != nullptr);
        CHECK(mismatches == 0);
        CHECK(sim->tick() == inputs.size());
        CHECK(!sim->desynced());
        // Every digest of the run checked on the way, and the stream is the replay recorded.
        CHECK(sim->checksPassed() == s.replay().checks.size());
        CHECK(s.replay().checks.size() >= run.replay.checks.size() - 1);
        CHECK(s.replay().inputs == run.replay.inputs);
        CHECK(s.replay().challengeRules == run.replay.challengeRules);
    }
    // Another level content (or engine) is caught by the digests, like an exact replay.
    LiveRun run = playLive("sylvanox_01", inputs, nullptr);
    SpectateHeader h = header(run.seed ^ 0x5555);
    SpectateStream s;
    s.absorb(answer(h, run.chunks));
    World view("sylvanox_01");
    ReplaySimulator sim(s.replay(), *view.w);
    while (sim.step()) {
    }
    if (sim.desynced()) CHECK(sim.desyncTick() > 0);
    else CHECK(sim.finished());
}

namespace {

/// The server's side for the broadcaster's test: a player's room (start, chunk, end, read)
/// with its rules (resync when out of order, a chunk sent again accepted once), and the
/// network's mischiefs.
struct Room {
    std::mutex m;
    bool down = false;
    bool loseNextAnswer = false;  // the chunk is taken, its answer never arrives
    bool forgetNext = false;      // the room loses the run before the next chunk (evicted)
    bool hasRun = false;
    std::string runId;
    int seq = 0, starts = 0, ends = 0, posts = 0;
    std::string endReason;
    std::vector<InputFrame> inputs;
    std::vector<std::pair<uint32_t, std::string>> checks;
    bool spectateAllowed = true;

    static HttpResponse ok(const Json& data, int status = 200) {
        HttpResponse r;
        r.status = status;
        r.body = Json{{"ok", true}, {"data", data}, {"request_id", "t"}}.dump();
        r.headers["content-type"] = "application/json";
        return r;
    }
    static HttpResponse error(int status, const std::string& code) {
        HttpResponse r;
        r.status = status;
        r.body = Json{{"ok", false}, {"error", {{"code", code}, {"message", "x"}}}, {"request_id", "t"}}.dump();
        r.headers["content-type"] = "application/json";
        return r;
    }

    HttpResponse handle(const HttpRequest& rq) {
        std::lock_guard<std::mutex> lock(m);
        std::string path = rq.url.substr(rq.url.find("/api/"));
        if (down) {
            HttpResponse r;
            r.error = NetError::Connect;
            r.errorMessage = "refused";
            return r;
        }
        if (path == "/api/v1/auth/login")
            return ok({{"player", {{"id", "c0ffee00-0000-4000-8000-000000000001"}, {"username", "Nova"}, {"presence_visible", true}, {"spectate_allowed", spectateAllowed}}},
                       {"session", {{"access_token", "a-000000000000000000000000"}, {"access_expires_at", "2099-01-01T00:00:00.000Z"},
                                    {"refresh_token", "r-000000000000000000000000"}, {"refresh_expires_at", "2099-02-01T00:00:00.000Z"}}}});
        if (path != "/api/v1/spectate/broadcast") return error(404, "NOT_FOUND");
        ++posts;
        Json b = Json::parse(rq.body, nullptr, false);
        std::string action = jstr(b, "action");
        if (action == "start") {
            hasRun = true;
            runId = jstr(b, "run_id");
            seq = 0;
            inputs.clear();
            checks.clear();
            ++starts;
            return ok({{"accepted", true}, {"run_id", runId}});
        }
        if (action == "chunk") {
            if (forgetNext) {
                forgetNext = false;
                hasRun = false;
            }
            const int s = jget<int>(b, "seq", 0);
            const uint32_t from = jget<uint32_t>(b, "from_frame", 0), count = jget<uint32_t>(b, "count", 0);
            if (!hasRun || jstr(b, "run_id") != runId) return error(409, "SPECTATE_RESYNC");
            if (s <= seq) return ok({{"accepted", true}, {"seq", seq}, {"frames", inputs.size()}});  // sent again
            if (s != seq + 1 || from != inputs.size()) return error(409, "SPECTATE_RESYNC");
            if (!spectate::decodeInputs(jstr(b, "runs"), count, inputs)) return error(400, "VALIDATION_ERROR");
            for (const auto& c : b.value("checks", Json::array())) checks.push_back({c[0].get<uint32_t>(), c[1].get<std::string>()});
            seq = s;
            if (loseNextAnswer) {
                loseNextAnswer = false;
                HttpResponse r;
                r.error = NetError::Connect;
                r.errorMessage = "connexion coupée";
                return r;
            }
            return ok({{"accepted", true}, {"seq", seq}, {"frames", inputs.size()}});
        }
        if (action == "end") {
            ++ends;
            endReason = jstr(b, "reason");
            return ok({{"ended", true}});
        }
        return error(400, "VALIDATION_ERROR");
    }
};

class RoomTransport final : public IHttpTransport {
public:
    explicit RoomTransport(std::shared_ptr<Room> r) : r_(std::move(r)) {}
    HttpResponse perform(const HttpRequest& rq) override { return r_->handle(rq); }
    const char* name() const override { return "fake"; }

private:
    std::shared_ptr<Room> r_;
};

fs::path testDir(const std::string& name) {
    fs::path p = fs::temp_directory_path() / ("cosmic_spectate_" + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(p);
    return p;
}

template <typename P>
bool spin(OnlineService& svc, SpectateBroadcaster& b, P done, int ms = 4000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        svc.update(0.05f);
        b.update(0.05f);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

bool login(OnlineService& svc) {
    bool logged = false;
    svc.login("Nova", "pw", false, [&](const ApiResult& r) { logged = r.ok; });
    for (int i = 0; i < 3000 && !logged; ++i) {
        svc.update(0.05f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return logged;
}

}  // namespace

TEST_CASE("spectate: the broadcaster sends the run in order, through cuts, lost answers and a forgetful room") {
    auto room = std::make_shared<Room>();
    fs::path dir = testDir("broadcast");
    OnlineService svc;
    svc.init({}, "http://127.0.0.1:9", std::make_unique<RoomTransport>(room), dir);
    CHECK(login(svc));
    SpectateBroadcaster b(svc);
    const auto inputs = playInputs(21, 120 * 12);
    unsigned token = b.begin(header(1));
    CHECK(token != 0);
    size_t fed = 0;
    // The game plays: 120 steps per second of play, checks every 60, the requests on their own pace.
    auto play = [&](size_t upTo) {
        for (; fed < upTo; ++fed) {
            b.input(token, inputs[fed]);
            if ((fed + 1) % 60 == 0) b.check(token, {static_cast<uint32_t>(fed + 1), 0x1234 + fed});
            if (fed % 6 == 0) {
                svc.update(0.05f);
                b.update(0.05f);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    };
    play(240);
    CHECK(spin(svc, b, [&] { return b.framesAcked() == 240; }));
    CHECK(room->starts == 1);
    // The network is cut: the steps wait here, then go in order.
    room->down = true;
    play(600);
    room->down = false;
    CHECK(spin(svc, b, [&] { return b.framesAcked() == 600; }, 20000));
    // An answer lost: the same chunk again, taken once.
    room->loseNextAnswer = true;
    play(700);
    CHECK(spin(svc, b, [&] { return b.framesAcked() == 700; }, 20000));
    // The room lost the run (evicted): the whole run again from its start.
    room->forgetNext = true;
    play(900);
    CHECK(spin(svc, b, [&] { return b.framesAcked() == 900; }, 20000));
    CHECK(room->starts == 2);
    play(inputs.size());
    b.end(token + 1, "quit");  // another scene's token: ignored
    b.end(token, "finished");
    CHECK(spin(svc, b, [&] { return b.runEnded(); }, 20000));
    {
        std::lock_guard<std::mutex> lock(room->m);
        CHECK(room->inputs == inputs);
        CHECK(room->endReason == "finished" && room->ends == 1);
        CHECK(room->checks.size() == inputs.size() / 60);
        bool ordered = true;
        for (size_t i = 1; i < room->checks.size(); ++i) ordered = ordered && room->checks[i].first > room->checks[i - 1].first;
        CHECK(ordered);
        CHECK(room->checks.front().second == spectate::hex64(0x1234 + 59));
    }
    // About two posts per second, not one per step.
    CHECK(room->posts < 80);

    // A run while paused: nothing simulated, a keep-alive now and then.
    unsigned t2 = b.begin(header(2));
    CHECK(t2 != token);
    b.input(token, inputs[0]);  // the previous scene's token: ignored
    CHECK(spin(svc, b, [&] { return b.seqAcked() >= 1; }, 15000));
    CHECK(b.framesAcked() == 0);

    // The setting off in the profile: nothing streamed.
    svc.logout(false);
    for (int i = 0; i < 100; ++i) svc.update(0.05f);
    room->spectateAllowed = false;
    CHECK(login(svc));
    CHECK(!b.enabled());
    CHECK(b.begin(header(3)) == 0);
    CHECK(!b.active());
    svc.shutdown();
    std::error_code ec;
    fs::remove_all(dir, ec);
}
