#pragma once

#include "AllocTag.h"
#include <cstddef>

// Pluggable allocator interface (general heap path).
class IAllocator {
public:
    virtual ~IAllocator() = default;

    virtual void* Allocate(size_t size, size_t alignment, AllocTag tag, const char* file, int line) = 0;

    virtual void Free(void* ptr, const char* file, int line) = 0;

    virtual const char* Name() const = 0;
};
