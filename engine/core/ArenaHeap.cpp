#include "engine/core/ArenaHeap.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <new>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace cosmic::heap {

thread_local Arena* t_current = nullptr;
thread_local int t_scopes = 0;

namespace {

std::atomic<bool> g_hooks{false};

// The live arenas (a handful: one per thread of the search), for operator delete.
constexpr int kMaxArenas = 64;
std::array<std::atomic<Arena*>, kMaxArenas> g_arenas{};
std::atomic<uintptr_t> g_low{UINTPTR_MAX}, g_high{0};

// ------------------------------------------------------------------ blocks
// Every block starts with a 16-byte prefix (the payload stays 16-aligned); a free block keeps the
// next free block of its class in its first payload bytes.
struct Prefix {
    uint32_t cls;
    uint32_t tag;
    uint64_t size;  // payload bytes (an aligned allocation's prefix: the offset back to its block)
};
static_assert(sizeof(Prefix) == 16);
constexpr uint32_t kUsed = 0xA110C8EDu, kFree = 0xF4EEB10Cu, kAligned = 0xA119ED00u;
constexpr size_t kGrain = 16;

// Size classes: 16 to 1024 by 16, then four per doubling (rounded to 64) up to 512 MB.
struct Classes {
    std::array<uint64_t, 160> size{};
    int count = 0;
    Classes() {
        for (uint64_t s = 16; s <= 1024; s += 16) size[static_cast<size_t>(count++)] = s;
        double s = 1024.0;
        while (count < static_cast<int>(size.size())) {
            s *= 1.189207115;  // 2^(1/4)
            uint64_t v = (static_cast<uint64_t>(s) + 63) & ~uint64_t(63);
            if (v <= size[static_cast<size_t>(count - 1)]) v = size[static_cast<size_t>(count - 1)] + 64;
            size[static_cast<size_t>(count++)] = v;
            if (v >= (uint64_t(1) << 29)) break;
        }
    }
    int of(size_t n) const {
        if (n <= 1024) return n == 0 ? 0 : static_cast<int>((n + 15) / 16) - 1;
        int lo = 64, hi = count - 1;
        if (n > size[static_cast<size_t>(hi)]) return -1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (size[static_cast<size_t>(mid)] >= n) hi = mid;
            else lo = mid + 1;
        }
        return lo;
    }
};
const Classes& classes() {
    static const Classes c;  // built before any arena exists (Arena's constructor asks for it)
    return c;
}

size_t roundUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

}  // namespace

namespace {
thread_local OutsideCount t_outside;
}  // namespace

OutsideCount outsideCount() { return t_outside; }

void noteOutside(bool alloc) noexcept {
    if (t_scopes <= 0) return;
    if (alloc) ++t_outside.allocs;
    else ++t_outside.frees;
}

bool hooksInstalled() { return g_hooks.load(std::memory_order_relaxed); }
void markHooksInstalled() { g_hooks.store(true, std::memory_order_relaxed); }

Arena* owner(const void* p) noexcept {
    const auto a = reinterpret_cast<uintptr_t>(p);
    if (a < g_low.load(std::memory_order_relaxed) || a >= g_high.load(std::memory_order_relaxed)) return nullptr;
    for (auto& slot : g_arenas)
        if (Arena* r = slot.load(std::memory_order_acquire); r && r->contains(p)) return r;
    return nullptr;
}

// The bookkeeping, at the start of the block (copied with it by a savestate).
struct Arena::Header {
    uint64_t magic = 0;
    size_t top = 0;   // offset of the first byte never allocated
    size_t live = 0;  // allocations not freed
    std::array<Prefix*, 160> free{};
};

Arena::Arena(size_t capacity) {
    (void)classes();
    capacity_ = roundUp(capacity, size_t(1) << 16);
#ifdef _WIN32
    base_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, capacity_, MEM_RESERVE, PAGE_NOACCESS));
#else
    void* m = mmap(nullptr, capacity_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    base_ = m == MAP_FAILED ? nullptr : static_cast<uint8_t*>(m);
#endif
    if (!base_) throw std::bad_alloc();
    commit(size_t(1) << 20);
    Header* h = new (base_) Header();
    h->magic = 0xC05A1CA5E4A0ull;
    h->top = roundUp(sizeof(Header), 64);
    // Registered last: from now on operator delete finds it.
    bool placed = false;
    for (auto& slot : g_arenas) {
        Arena* expected = nullptr;
        if (slot.compare_exchange_strong(expected, this)) {
            placed = true;
            break;
        }
    }
    if (!placed) throw std::bad_alloc();
    const auto lo = reinterpret_cast<uintptr_t>(base_), hi = lo + capacity_;
    for (uintptr_t cur = g_low.load(); lo < cur && !g_low.compare_exchange_weak(cur, lo);) {
    }
    for (uintptr_t cur = g_high.load(); hi > cur && !g_high.compare_exchange_weak(cur, hi);) {
    }
}

Arena::~Arena() {
    for (auto& slot : g_arenas) {
        Arena* expected = this;
        if (slot.compare_exchange_strong(expected, nullptr)) break;
    }
#ifdef _WIN32
    VirtualFree(base_, 0, MEM_RELEASE);
#else
    munmap(base_, capacity_);
#endif
}

void Arena::commit(size_t upTo) {
    if (upTo <= committed_) return;
    if (upTo > capacity_) throw std::bad_alloc();
    size_t want = std::min(capacity_, roundUp(std::max(upTo, committed_ + (size_t(4) << 20)), size_t(1) << 16));
#ifdef _WIN32
    if (!VirtualAlloc(base_ + committed_, want - committed_, MEM_COMMIT, PAGE_READWRITE)) throw std::bad_alloc();
#endif
    committed_ = want;
}

void* Arena::allocate(size_t n, size_t align) {
    Header* h = header();
    if (align > kGrain) {
        // Over-aligned: a block large enough, and a prefix just before the aligned address that
        // says where the block starts.
        uint8_t* raw = static_cast<uint8_t*>(allocate(n + align + sizeof(Prefix), kGrain));
        uint8_t* aligned = reinterpret_cast<uint8_t*>(roundUp(reinterpret_cast<uintptr_t>(raw) + sizeof(Prefix), align));
        auto* pre = reinterpret_cast<Prefix*>(aligned - sizeof(Prefix));
        pre->cls = 0;
        pre->tag = kAligned;
        pre->size = static_cast<uint64_t>(aligned - raw);
        return aligned;
    }
    const int cls = classes().of(n);
    if (cls < 0) throw std::bad_alloc();
    Prefix* b = h->free[static_cast<size_t>(cls)];
    if (b) {
        Prefix* next;
        std::memcpy(&next, b + 1, sizeof next);
        h->free[static_cast<size_t>(cls)] = next;
    } else {
        const size_t payload = classes().size[static_cast<size_t>(cls)];
        const size_t at = h->top, end = at + sizeof(Prefix) + payload;
        if (end > committed_) commit(end);
        b = reinterpret_cast<Prefix*>(base_ + at);
        b->cls = static_cast<uint32_t>(cls);
        b->size = payload;
        h->top = end;
    }
    b->tag = kUsed;
    ++h->live;
    return b + 1;
}

void Arena::deallocate(void* p) noexcept {
    if (!p) return;
    auto* pre = static_cast<Prefix*>(p) - 1;
    if (pre->tag == kAligned) {
        p = static_cast<uint8_t*>(p) - pre->size;
        pre = static_cast<Prefix*>(p) - 1;
    }
    if (pre->tag != kUsed) return;  // a double free or a foreign pointer: never corrupt the lists
    Header* h = header();
    pre->tag = kFree;
    Prefix* head = h->free[pre->cls];
    std::memcpy(pre + 1, &head, sizeof head);
    h->free[pre->cls] = pre;
    --h->live;
}

size_t Arena::used() const noexcept { return header()->top; }
size_t Arena::liveBlocks() const noexcept { return header()->live; }

uint64_t Arena::layoutDigest() const noexcept {
    // The blocks lie one after the other from the end of the header to the top.
    uint64_t h = 1469598103934665603ull;
    auto mixIn = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xFFu;
            h *= 1099511628211ull;
        }
    };
    const size_t top = header()->top;
    for (size_t at = roundUp(sizeof(Header), 64); at + sizeof(Prefix) <= top;) {
        const auto* b = reinterpret_cast<const Prefix*>(base_ + at);
        mixIn(at);
        mixIn(b->cls);
        mixIn(b->tag == kUsed ? 1u : 0u);
        at += sizeof(Prefix) + b->size;
    }
    mixIn(top);
    mixIn(header()->live);
    return h;
}

void Arena::save(Snapshot& s) const {
    ArenaOutside normal;
    const size_t n = used();
    if (s.capacity < n) {
        s.bytes.reset(new uint8_t[n + (n >> 3)]);
        s.capacity = n + (n >> 3);
    }
    std::memcpy(s.bytes.get(), base_, n);
    s.size = n;
}

void Arena::restore(const Snapshot& s) {
    if (s.size == 0) return;
    const size_t before = used();
    std::memcpy(base_, s.bytes.get(), s.size);
    if (scrub_ >= 0 && before > s.size) std::memset(base_ + s.size, scrub_, before - s.size);
}

}  // namespace cosmic::heap
