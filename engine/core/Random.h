#pragma once

#include <cstdint>

namespace cosmic {

/// Small, fast PCG32 random generator (deterministic when seeded).
class Random {
public:
    explicit Random(uint64_t seed = 0x853c49e6748fea9bULL) { reseed(seed); }

    void reseed(uint64_t seed) {
        state_ = 0;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + 1442695040888963407ULL;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31));
    }

    /// Uniform float in [0, 1).
    float unit() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
    int rangeInt(int lo, int hiInclusive) {
        if (hiInclusive <= lo) return lo;
        return lo + static_cast<int>(next() % static_cast<uint32_t>(hiInclusive - lo + 1));
    }
    bool chance(float p) { return unit() < p; }
    /// Internal state (determinism checksums).
    uint64_t state() const { return state_; }
    float sign() { return (next() & 1u) ? 1.0f : -1.0f; }

    /// Process-wide generator for visual effects.
    static Random& global() {
        static Random r(0xC05A1CULL);
        return r;
    }

private:
    uint64_t state_ = 0;
};

/// Stable 32-bit hash for (x, y) integer coordinates (tile variants, decor jitter...).
inline uint32_t hash2(int x, int y, uint32_t seed = 0) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

}  // namespace cosmic
