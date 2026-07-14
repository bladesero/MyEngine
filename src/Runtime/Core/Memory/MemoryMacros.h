#pragma once

#include "MemoryService.h"

#if defined(MYENGINE_MEM_TRACKING)
#define ME_ALLOC(tag, size, align) MemoryService::Get().Allocate((tag), (size), (align), __FILE__, __LINE__)
#define ME_FREE(ptr) MemoryService::Get().Free((ptr), __FILE__, __LINE__)
#else
#define ME_ALLOC(tag, size, align) MemoryService::Get().Allocate((tag), (size), (align), nullptr, 0)
#define ME_FREE(ptr) MemoryService::Get().Free((ptr), nullptr, 0)
#endif
