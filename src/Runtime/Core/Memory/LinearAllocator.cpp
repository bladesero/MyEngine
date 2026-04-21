#include "LinearAllocator.h"

#include "Core/Logger.h"

#include <cstdlib>

LinearAllocator::~LinearAllocator() {
    Shutdown();
}

bool LinearAllocator::Init(size_t capacityBytes) {
    Shutdown();
    if (capacityBytes == 0) {
        Logger::Error("[Memory] LinearAllocator::Init: capacity must be > 0");
        return false;
    }

    m_Buffer = static_cast<unsigned char*>(std::malloc(capacityBytes));
    if (!m_Buffer) {
        Logger::Error("[Memory] LinearAllocator::Init: malloc failed (", capacityBytes, " bytes)");
        return false;
    }

    m_Capacity = capacityBytes;
    m_Offset = 0;
    m_HighWatermark = 0;
    return true;
}

void LinearAllocator::Shutdown() {
    if (m_Buffer) {
        std::free(m_Buffer);
        m_Buffer = nullptr;
    }
    m_Capacity = 0;
    m_Offset = 0;
    m_HighWatermark = 0;
}

void LinearAllocator::Reset() {
    m_Offset = 0;
}

namespace {

uintptr_t AlignPointer(uintptr_t value, size_t alignment) {
    if (alignment < 1) {
        return value;
    }
    // Power-of-two alignment (same contract as std::aligned_alloc).
    const uintptr_t mask = static_cast<uintptr_t>(alignment - 1);
    return (value + mask) & ~mask;
}

} // namespace

void* LinearAllocator::Allocate(size_t size, size_t alignment) {
    if (!m_Buffer || size == 0) {
        return nullptr;
    }
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(m_Buffer);
    const uintptr_t bump = base + m_Offset;
    const uintptr_t aligned = AlignPointer(bump, alignment);
    const size_t padding = static_cast<size_t>(aligned - bump);
    const size_t newOffset = m_Offset + padding + size;

    if (newOffset > m_Capacity) {
        Logger::Warn("[Memory] LinearAllocator OOM (need ", newOffset, " have ", m_Capacity, ")");
        return nullptr;
    }

    m_Offset = newOffset;
    if (m_Offset > m_HighWatermark) {
        m_HighWatermark = m_Offset;
    }
    return reinterpret_cast<void*>(aligned);
}
