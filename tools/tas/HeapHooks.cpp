// The replacement of the global operator new / delete of the programs that use arena heaps
// (engine/core/ArenaHeap.h): the machine's search (cosmic_tas) and the tests. Linked as an
// object library into those programs only, never into the game: there, every allocation goes to
// the normal heap as before.
//
// While a thread has a current arena (heap::ArenaScope), its allocations are made in it; a delete
// returns memory to the arena that owns it, whichever thread or scope frees it.

#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

#include "engine/core/ArenaHeap.h"

using cosmic::heap::Arena;

namespace {

struct Mark {
    Mark() { cosmic::heap::markHooksInstalled(); }
} g_mark;

void* normalAlloc(std::size_t n) {
    if (void* p = std::malloc(n ? n : 1)) return p;
    return nullptr;
}

void* normalAlignedAlloc(std::size_t n, std::size_t align) {
#ifdef _WIN32
    return _aligned_malloc(n ? n : 1, align);
#else
    const std::size_t size = ((n ? n : 1) + align - 1) / align * align;
    return std::aligned_alloc(align, size);
#endif
}

void normalAlignedFree(void* p) {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}

void* allocate(std::size_t n) {
    if (Arena* a = cosmic::heap::t_current) return a->allocate(n, 16);
    if (void* p = normalAlloc(n)) {
        cosmic::heap::noteOutside(true);
        return p;
    }
    throw std::bad_alloc();
}

void* allocateNoThrow(std::size_t n) noexcept {
    try {
        return allocate(n);
    } catch (...) {
        return nullptr;
    }
}

void* allocateAligned(std::size_t n, std::align_val_t al) {
    const auto align = static_cast<std::size_t>(al);
    if (Arena* a = cosmic::heap::t_current) return a->allocate(n, align);
    if (void* p = normalAlignedAlloc(n, align)) {
        cosmic::heap::noteOutside(true);
        return p;
    }
    throw std::bad_alloc();
}

void* allocateAlignedNoThrow(std::size_t n, std::align_val_t al) noexcept {
    try {
        return allocateAligned(n, al);
    } catch (...) {
        return nullptr;
    }
}

void release(void* p) noexcept {
    if (!p) return;
    if (Arena* a = cosmic::heap::owner(p)) {
        a->deallocate(p);
        return;
    }
    cosmic::heap::noteOutside(false);
    std::free(p);
}

void releaseAligned(void* p) noexcept {
    if (!p) return;
    if (Arena* a = cosmic::heap::owner(p)) {
        a->deallocate(p);
        return;
    }
    cosmic::heap::noteOutside(false);
    normalAlignedFree(p);
}

}  // namespace

void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return allocateNoThrow(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return allocateNoThrow(n); }
void* operator new(std::size_t n, std::align_val_t a) { return allocateAligned(n, a); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocateAligned(n, a); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocateAlignedNoThrow(n, a); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocateAlignedNoThrow(n, a); }

void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete(void* p, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { releaseAligned(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned(p); }
