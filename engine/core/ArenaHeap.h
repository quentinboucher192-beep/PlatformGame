#pragma once

// Arena heaps: the savestates of the machine's search (docs/TAS.md, « États sauvegardés »).
//
// A solo simulation (its GameWorld, and the ResourceManager and AudioSystem it uses) is built
// inside one arena: a single block of memory at a fixed address, whose allocator keeps all of
// its bookkeeping inside the block too. While an arena is the thread's current one (ArenaScope),
// every C++ allocation of that thread (operator new: the world's vectors, strings, maps, its
// entities, enemies, powers, physics, events...) is made in it. A savestate is then a plain copy
// of the bytes of the block in use, and restoring one copies them back: every object, every
// pointer between objects and the allocator itself are exactly as they were — nothing of the
// world can be forgotten, whatever its members are.
//
// The redirection only exists in a program that installs the hooks of tools/tas/HeapHooks.cpp
// (the search tool and the tests); the game itself never does (hooksInstalled() is false there,
// and every scope below costs a thread-local write).
//
// What must stay out of an arena: memory that belongs to something else than the world and that
// outlives the call that allocates it (a global cache, the log, the static tables built on their
// first use). Those places open an ArenaOutside scope, so their allocations always go to the
// normal heap; tests/test_tas_savestate.cpp checks the savestates on every campaign level.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cosmic::heap {

class Arena;

/// The arena of this thread (nullptr: the normal heap).
extern thread_local Arena* t_current;
/// ArenaScopes open on this thread (whatever ArenaOutside says inside them).
extern thread_local int t_scopes;

/// What this thread took from and gave back to the normal heap while an ArenaScope was open (an
/// ArenaOutside inside it, or memory of the normal heap freed by the world). Counted by the hooks:
/// a step of a world whose state is all in its arena leaves allocs == frees once its global caches
/// are filled (tests/test_tas_savestate.cpp).
struct OutsideCount {
    int64_t allocs = 0;
    int64_t frees = 0;
};
OutsideCount outsideCount();
/// The hooks: an allocation (true) or a free (false) on the normal heap.
void noteOutside(bool alloc) noexcept;

/// True in a program that redirects operator new to the arenas (tools/tas/HeapHooks.cpp).
bool hooksInstalled();
/// Called once by the hooks' translation unit.
void markHooksInstalled();

/// The live arena that owns `p` (nullptr for the normal heap).
Arena* owner(const void* p) noexcept;

/// Bytes of an arena in use, copied by a savestate.
struct Snapshot {
    std::unique_ptr<uint8_t[]> bytes;
    size_t size = 0;
    size_t capacity = 0;
    bool empty() const { return size == 0; }
};

class Arena {
public:
    /// Reserves `capacity` bytes of address space (committed as they are used).
    explicit Arena(size_t capacity = size_t(1) << 30);
    ~Arena();
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    /// Throws std::bad_alloc when the arena is full.
    void* allocate(size_t n, size_t align);
    void deallocate(void* p) noexcept;
    bool contains(const void* p) const noexcept { return p >= base_ && p < base_ + capacity_; }

    /// Bytes from the start of the block to its highest allocation: what a savestate copies.
    size_t used() const noexcept;
    /// Live allocations (statistics, tests).
    size_t liveBlocks() const noexcept;
    /// A digest of the arena's blocks (place, size class, used or free), never of their contents:
    /// the same allocations and frees in the same order give the same layout. Bytes nobody wrote
    /// (the padding of a structure, the unused end of a short string copied whole by MSVC) are
    /// left out, unlike a comparison of the bytes.
    uint64_t layoutDigest() const noexcept;

    /// Copies the bytes in use into `s` (its buffer is reused when large enough). Never call it
    /// with this arena current: the buffer is on the normal heap.
    void save(Snapshot& s) const;
    /// Puts the bytes of `s` back: every object of the arena is again as it was at save().
    /// Memory allocated since is free again. `s` must come from this arena.
    void restore(const Snapshot& s);
    /// Tests: after a restore, what lies above the bytes in use (memory allocated since the
    /// savestate, free again) is filled with `byte` (-1: left as it is). 0xDB: a pointer kept by
    /// something outside the arena to that memory cannot keep working by chance; 0: the memory
    /// is as it was before it was ever used, so that the same steps leave the same bytes.
    void setScrub(int byte) { scrub_ = byte; }

private:
    struct Header;
    Header* header() const noexcept { return reinterpret_cast<Header*>(base_); }
    void commit(size_t upTo);
    uint8_t* base_ = nullptr;
    size_t capacity_ = 0;
    size_t committed_ = 0;
    int scrub_ = -1;
};

/// The thread allocates in `a` while the scope lives (nested scopes restore the previous one).
class ArenaScope {
public:
    explicit ArenaScope(Arena* a) noexcept : prev_(t_current) {
        t_current = a;
        ++t_scopes;
    }
    ~ArenaScope() {
        t_current = prev_;
        --t_scopes;
    }
    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;

private:
    Arena* prev_;
};

/// The normal heap while the scope lives: for global caches, the log, static tables.
class ArenaOutside {
public:
    ArenaOutside() noexcept : prev_(t_current) { t_current = nullptr; }
    ~ArenaOutside() { t_current = prev_; }
    ArenaOutside(const ArenaOutside&) = delete;
    ArenaOutside& operator=(const ArenaOutside&) = delete;

private:
    Arena* prev_;
};

}  // namespace cosmic::heap
