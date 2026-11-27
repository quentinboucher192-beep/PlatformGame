#pragma once

// A solo run of a level whose whole simulation lives in its own arena heap
// (engine/core/ArenaHeap.h): savestates of everything the run is made of — the GameWorld and all
// it owns (player, powers, entities, enemies, bosses and their parts, physics, events, timers,
// cinematics, the gameplay random generator...), the ResourceManager and AudioSystem it uses,
// and the count of steps — taken and put back by copying the arena's bytes.
//
// Only in a program that installs the arena hooks (tools/tas/HeapHooks.cpp: the search tool and
// the tests); the constructor throws elsewhere, so a savestate can never silently miss what was
// allocated on the normal heap. The world is set up as a run of the game and its replay viewer
// set it up (keys per power slot, the level's seed, the running simulation version), so that
// the inputs played here give the same run in the normal replay playback.

#include <cstdint>
#include <memory>

#include "engine/core/ArenaHeap.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"

namespace cosmic {

class ContentDatabase;
struct LevelData;

namespace tas {

class ArenaWorld {
public:
    struct Options {
        size_t arenaBytes = size_t(1) << 30;  // address space reserved (committed as used)
        bool quiet = true;                    // no sound, no particle: changes nothing of the run
        int scrub = -1;                       // tests: Arena::setScrub
        int simVersion = kSimulationVersion;
    };
    /// `content` must outlive the world and stay unchanged (it is shared, read only, between
    /// the threads of the search).
    ArenaWorld(const ContentDatabase& content, const LevelData& level);
    ArenaWorld(const ContentDatabase& content, const LevelData& level, const Options& opt);
    ~ArenaWorld();
    ArenaWorld(const ArenaWorld&) = delete;
    ArenaWorld& operator=(const ArenaWorld&) = delete;

    /// The world (read it freely; anything that changes it or allocates for it goes through
    /// step() or inside()).
    const GameWorld& world() const { return *world_; }
    /// One step of the run (stepWorld) in the arena.
    void step(const InputFrame& f);
    /// Steps simulated since the load (part of the state: a restore puts it back).
    uint32_t steps() const;
    /// simulationStateHash of the world.
    uint64_t digest() const;
    /// Runs `f(GameWorld&)` with the arena current (what allocates for the world).
    template <class F>
    decltype(auto) inside(F&& f) {
        heap::ArenaScope scope(&arena_);
        return f(*world_);
    }

    using State = heap::Snapshot;
    /// The whole run now. Call it outside any arena scope.
    void save(State& s) const { arena_.save(s); }
    /// The run exactly as it was at save().
    void restore(const State& s) { arena_.restore(s); }
    /// Tests: Arena::setScrub.
    void setScrub(int byte) { arena_.setScrub(byte); }
    /// Bytes a savestate copies.
    size_t stateBytes() const { return arena_.used(); }

private:
    struct Parts;
    heap::Arena arena_;
    Parts* parts_ = nullptr;   // in the arena
    GameWorld* world_ = nullptr;
};

}  // namespace tas
}  // namespace cosmic
