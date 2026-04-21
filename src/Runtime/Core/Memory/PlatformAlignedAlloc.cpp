#include "PlatformAlignedAlloc.h"

#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <malloc.h>
#endif

size_t AlignUp(size_t value, size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

void* PlatformAlignedAlloc(size_t size, size_t alignment) {
    if (size == 0) {
        size = alignment; // avoid zero-sized aligned_alloc issues
    }
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    // Round size up to a multiple of alignment (required by std::aligned_alloc).
    const size_t rounded = AlignUp(size, alignment);

#if defined(_WIN32)
    return _aligned_malloc(rounded, alignment);
#else
    void* p = std::aligned_alloc(alignment, rounded);
    return p;
#endif
}

void PlatformAlignedFree(void* ptr) {
    if (!ptr) {
        return;
    }
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}
