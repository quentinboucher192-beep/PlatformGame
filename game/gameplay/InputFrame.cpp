#include "game/gameplay/InputFrame.h"

#include <cstring>

#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

void stepWorld(GameWorld& world, const InputFrame& frame, float dt) {
    if (world.powerOffer()) {
        if (frame.choice == 0) return;
        world.resolvePowerOffer(frame.choice);
    }
    if (frame.flags & InputFrame::SkipCinematic) {
        if (world.cinematic().active() && world.cinematic().skippable()) world.cinematic().skip(world);
        world.fixedUpdate(PlayerInput{}, dt);
        return;
    }
    world.fixedUpdate(frame.toPlayerInput(), dt);
}

namespace {
constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

struct Fnv {
    uint64_t h = kFnvOffset;
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= kFnvPrime;
        }
    }
    void u64(uint64_t v) { bytes(&v, sizeof v); }
    void i32(int32_t v) { bytes(&v, sizeof v); }
    void f32(float f) {
        uint32_t v;
        std::memcpy(&v, &f, sizeof v);
        bytes(&v, sizeof v);
    }
    void rect(const Rect& r) {
        f32(r.x);
        f32(r.y);
        f32(r.w);
        f32(r.h);
    }
};
}  // namespace

uint64_t levelSeed(const std::string& levelId) {
    Fnv f;
    f.bytes("cosmic-seed:", 12);
    f.bytes(levelId.data(), levelId.size());
    return f.h;
}

uint64_t simulationStateHash(const GameWorld& w) {
    Fnv f;
    f.i32(static_cast<int32_t>(w.state()));
    f.f32(w.time());
    f.f32(w.timer().elapsed());
    f.u64(w.gameplayRngState());
    const Player& p = w.player();
    f.rect(p.box);
    f.f32(p.vel.x);
    f.f32(p.vel.y);
    f.i32(p.health);
    f.i32(static_cast<int32_t>(p.state()));
    f.i32(w.stats().deaths);
    f.i32(static_cast<int32_t>(p.powers().size()));
    for (const auto& pw : p.powers()) f.bytes(pw->id().data(), pw->id().size());
    f.i32(w.powerOffer() ? 1 : 0);
    f.i32(static_cast<int32_t>(w.enemies().size()));
    for (const auto& e : w.enemies()) {
        f.rect(e->box);
        f.f32(e->vel.x);
        f.f32(e->vel.y);
        f.f32(e->hp());
        f.i32(static_cast<int32_t>(e->state()));
    }
    f.i32(static_cast<int32_t>(w.projectiles().size()));
    for (const auto& pr : w.projectiles()) {
        f.f32(pr.pos.x);
        f.f32(pr.pos.y);
        f.f32(pr.vel.x);
        f.f32(pr.vel.y);
    }
    f.i32(static_cast<int32_t>(w.entities().size()));
    for (const auto& e : w.entities()) {
        f.rect(e->box);
        uint8_t bits = static_cast<uint8_t>((e->active ? 1 : 0) | (e->removed ? 2 : 0) | (e->playerInside ? 4 : 0));
        f.bytes(&bits, 1);
    }
    return f.h;
}

}  // namespace cosmic
