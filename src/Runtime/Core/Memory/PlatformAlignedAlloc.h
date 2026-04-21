#pragma once

#include <cstddef>

// Cross-platform aligned allocation (C++17 / MSVC).
// - Windows: _aligned_malloc / _aligned_free
// - POSIX: std::aligned_alloc / std::free (size must be multiple of alignment)

void* PlatformAlignedAlloc(size_t size, size_t alignment);
void  PlatformAlignedFree(void* ptr);

size_t AlignUp(size_t value, size_t alignment);
