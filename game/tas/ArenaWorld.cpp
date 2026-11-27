#include "game/tas/ArenaWorld.h"

#include <new>
#include <stdexcept>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/LevelData.h"

namespace cosmic::tas {

// Everything of the run, built in the arena (the steps too: a restore puts them back).
struct ArenaWorld::Parts {
    AudioSystem audio;  // never initialised: no device, nothing heard
    ResourceManager res;
    std::unique_ptr<GameWorld> world;
    uint32_t steps = 0;
};

ArenaWorld::ArenaWorld(const ContentDatabase& content, const LevelData& level) : ArenaWorld(content, level, Options{}) {}

ArenaWorld::ArenaWorld(const ContentDatabase& content, const LevelData& level, const Options& opt) : arena_(opt.arenaBytes) {
    if (!heap::hooksInstalled())
        throw std::logic_error("ArenaWorld: this program does not redirect its allocations to the arenas (tools/tas/HeapHooks.cpp)");
    arena_.setScrub(opt.scrub);
    heap::ArenaScope scope(&arena_);
    parts_ = new Parts();
    parts_->res.init(nullptr, &parts_->audio);
    WorldServices services{parts_->res, parts_->audio, content, nullptr, 0.0f, opt.quiet ? 0.0f : 1.0f};
    parts_->world = std::make_unique<GameWorld>(services, WorldMode::Play);
    world_ = parts_->world.get();
    // As a run of the game and its replay viewer set it up (GameScene, ReplayPlayback).
    world_->setKeysPerSlot(true);
    world_->load(level);
    world_->seedGameplay(levelSeed(level.id));
    world_->setSimulationVersion(opt.simVersion);
    if (opt.quiet) world_->setQuiet(true);
}

ArenaWorld::~ArenaWorld() {
    heap::ArenaScope scope(&arena_);
    delete parts_;
}

void ArenaWorld::step(const InputFrame& f) {
    heap::ArenaScope scope(&arena_);
    stepWorld(*world_, f, kSimDt);
    ++parts_->steps;
}

uint32_t ArenaWorld::steps() const { return parts_->steps; }

uint64_t ArenaWorld::digest() const {
    heap::ArenaScope scope(const_cast<heap::Arena*>(&arena_));
    return simulationStateHash(*world_);
}

}  // namespace cosmic::tas
