#include "game/ghost/ReplayPlayback.h"

#include <algorithm>

#include "game/gameplay/InputFrame.h"

namespace cosmic {

namespace {
/// The last steps before a jump's target are also shown to the display update (particles,
/// animations: never the simulation), so the picture arrived at looks like a playback.
constexpr uint32_t kSettleSteps = 45;
}  // namespace

ReplayPlayback::ReplayPlayback(WorldServices services, LevelData level, const ReplayData& replay)
    : services_(services), level_(std::move(level)), replay_(replay) {
    chrono_.assign(static_cast<size_t>(replay_.inputs.size()) + 1, -1.0f);
    chrono_[0] = 0.0f;
    main_ = makeRun();
    main_.world->silent = false;  // the world shown plays its sounds (the copies never do)
    ++generation_;
}

ReplayPlayback::~ReplayPlayback() {
    spares_.clear();
    main_.sim.reset();
    main_.world.reset();
}

ReplayPlayback::Run ReplayPlayback::makeRun() const {
    // The same start as a run: the level loaded, its seed, then the replay's (ReplaySimulator).
    Run r;
    r.world = std::make_unique<GameWorld>(services_, WorldMode::Play);
    r.world->load(level_);
    r.world->seedGameplay(levelSeed(level_.id));
    r.world->silent = true;
    r.sim = std::make_unique<ReplaySimulator>(replay_, *r.world);
    return r;
}

uint32_t ReplayPlayback::tick() const { return main_.sim->tick(); }
uint32_t ReplayPlayback::totalTicks() const { return main_.sim->totalTicks(); }
bool ReplayPlayback::finished() const { return main_.sim->finished(); }
bool ReplayPlayback::desynced() const { return main_.sim->desynced(); }
uint32_t ReplayPlayback::desyncTick() const { return main_.sim->desyncTick(); }
size_t ReplayPlayback::checksPassed() const { return main_.sim->checksPassed(); }
const InputFrame* ReplayPlayback::currentInput() const { return main_.sim->currentInput(); }
float ReplayPlayback::runTime() const { return main_.world->timer().elapsed(); }

float ReplayPlayback::chronoAt(uint32_t tick) const {
    return tick <= known_ && tick < chrono_.size() ? chrono_[tick] : -1.0f;
}

void ReplayPlayback::note() {
    uint32_t t = main_.sim->tick();
    if (t < chrono_.size()) chrono_[t] = main_.world->timer().elapsed();
    known_ = std::max(known_, t);
}

bool ReplayPlayback::step() {
    bool ok = main_.sim->step();
    if (ok) note();
    return ok;
}

void ReplayPlayback::seekTick(uint32_t tick) {
    tick = std::min(tick, totalTicks());
    if (tick < main_.sim->tick()) {
        // The past: from the nearest point de reprise at or before it, else from the start.
        int best = -1;
        for (size_t i = 0; i < spares_.size(); ++i)
            if (spares_[i].sim->tick() <= tick && !spares_[i].sim->desynced() &&
                (best < 0 || spares_[i].sim->tick() > spares_[static_cast<size_t>(best)].sim->tick()))
                best = static_cast<int>(i);
        if (best >= 0) {
            main_ = std::move(spares_[static_cast<size_t>(best)]);
            spares_.erase(spares_.begin() + best);
        } else {
            main_ = makeRun();
        }
        lastResume_ = main_.sim->tick();
        ++generation_;
    }
    mode_ = Mode::Tick;
    targetTick_ = tick;
    fromTick_ = main_.sim->tick();
    aged_ = false;
}

void ReplayPlayback::seekTime(double seconds) {
    seconds = std::max(0.0, seconds);
    // Already simulated once: the step is known, the jump is a plain step jump.
    if (known_ > 0 && static_cast<double>(chrono_[known_]) + 1e-4 >= seconds) {
        auto begin = chrono_.begin(), end = chrono_.begin() + static_cast<std::ptrdiff_t>(known_) + 1;
        auto it = std::lower_bound(begin, end, static_cast<float>(seconds - 1e-4));
        seekTick(static_cast<uint32_t>(it - begin));
        return;
    }
    // Beyond what was ever simulated: forward from here (every step known shows less).
    mode_ = Mode::Time;
    targetTime_ = seconds;
    fromTick_ = main_.sim->tick();
    aged_ = false;
}

bool ReplayPlayback::arrived() const {
    switch (mode_) {
        case Mode::None: return true;
        case Mode::Tick: return main_.sim->tick() >= targetTick_;
        case Mode::Time: return static_cast<double>(main_.world->timer().elapsed()) + 1e-4 >= targetTime_;
    }
    return true;
}

bool ReplayPlayback::nearTarget() const {
    if (mode_ == Mode::Tick) return main_.sim->tick() + kSettleSteps >= targetTick_;
    if (mode_ == Mode::Time) return static_cast<double>(main_.world->timer().elapsed()) + kSettleSteps * static_cast<double>(kSimDt) >= targetTime_;
    return true;
}

bool ReplayPlayback::advance(int budget) {
    if (mode_ == Mode::None) return true;
    bool done = false;
    while (!(done = arrived()) && budget-- > 0) {
        bool settle = nearTarget();
        if (settle && !aged_) {
            // What the fast-forward emitted was never shown: it goes.
            main_.world->particles().clear();
            aged_ = true;
        }
        main_.world->silent = true;
        bool ok = main_.sim->step();
        if (ok) note();
        if (ok && settle) main_.world->update(kSimDt);
        if (!ok) {
            done = true;
            break;
        }
    }
    if (done) {
        if (!aged_) main_.world->particles().clear();
        main_.world->silent = false;
        mode_ = Mode::None;
    }
    return done;
}

void ReplayPlayback::finishSeek() {
    while (!advance(1 << 20)) {
    }
}

float ReplayPlayback::seekProgress() const {
    if (mode_ == Mode::Tick) {
        uint32_t span = targetTick_ > fromTick_ ? targetTick_ - fromTick_ : 1u;
        uint32_t done = main_.sim->tick() > fromTick_ ? main_.sim->tick() - fromTick_ : 0u;
        return std::clamp(static_cast<float>(done) / static_cast<float>(span), 0.0f, 1.0f);
    }
    if (mode_ == Mode::Time) {
        float now = main_.world->timer().elapsed();
        float from = std::max(0.0f, chronoAt(fromTick_));
        double span = targetTime_ - from;
        return span <= 0.0 ? 1.0f : std::clamp(static_cast<float>((now - from) / span), 0.0f, 1.0f);
    }
    return 1.0f;
}

void ReplayPlayback::maintain(int budget) {
    if (mode_ != Mode::None || main_.sim->desynced()) return;
    const uint32_t now = main_.sim->tick();
    // Copies ahead of the step shown (after a jump back) are of no use any more.
    spares_.erase(std::remove_if(spares_.begin(), spares_.end(), [&](const Run& r) { return r.sim->tick() >= now || r.sim->desynced(); }),
                  spares_.end());
    if (now <= kLags[0]) return;  // near the start, the level loaded again is as quick
    if (spares_.size() < kLags.size()) spares_.push_back(makeRun());  // one load per call at most
    // The nearest one goes to ~0.75 s behind, the next ones further (3 s, 12 s).
    std::sort(spares_.begin(), spares_.end(), [](const Run& a, const Run& b) { return a.sim->tick() > b.sim->tick(); });
    for (size_t i = 0; i < spares_.size() && budget > 0; ++i) {
        uint32_t target = now > kLags[i] ? now - kLags[i] : 0u;
        ReplaySimulator& s = *spares_[i].sim;
        while (s.tick() < target && budget-- > 0)
            if (!s.step()) break;
    }
}

std::vector<uint32_t> ReplayPlayback::resumePoints() const {
    std::vector<uint32_t> out;
    for (const Run& r : spares_) out.push_back(r.sim->tick());
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace cosmic
