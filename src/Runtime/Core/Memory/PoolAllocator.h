#pragma once

#include "Core/Logger.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include <vector>

// Fixed-capacity object pool: slab storage + freelist. Destruction runs ~T on in-use slots only.
template <typename T> class PoolAllocator {
public:
    explicit PoolAllocator(uint32_t maxObjects) : m_Capacity(maxObjects) {
        if (m_Capacity == 0) {
            return;
        }
        m_SlotStride = AlignUp(sizeof(T), alignof(T));
        const size_t bytes = static_cast<size_t>(m_Capacity) * m_SlotStride;
        m_Slab.reset(new std::byte[bytes]);
        std::memset(m_Slab.get(), 0, bytes);

        m_InUse.resize(m_Capacity, 0);
        m_LiveCount = 0;

        m_Free.reserve(m_Capacity);
        for (uint32_t i = m_Capacity; i-- > 0;) {
            m_Free.push_back(i);
        }
    }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    PoolAllocator(PoolAllocator&&) = default;
    PoolAllocator& operator=(PoolAllocator&&) = default;

    ~PoolAllocator() {
        if (!m_Slab || m_Capacity == 0) {
            return;
        }
        for (uint32_t i = 0; i < m_Capacity; ++i) {
            if (m_InUse[i]) {
                T* live = reinterpret_cast<T*>(SlotPtr(i));
                live->~T();
                m_InUse[i] = 0;
            }
        }
        m_LiveCount = 0;
    }

    uint32_t Capacity() const { return m_Capacity; }

    uint32_t LiveCount() const { return m_LiveCount; }

    bool IsFull() const { return m_Free.empty(); }

    template <typename... Args> T* Allocate(Args&&... args) {
        if (m_Free.empty() || !m_Slab) {
            return nullptr;
        }
        const uint32_t index = m_Free.back();
        m_Free.pop_back();
        void* slot = SlotPtr(index);
        m_InUse[index] = 1;
        ++m_LiveCount;
        return new (slot) T(std::forward<Args>(args)...);
    }

    void Free(T* obj) {
        if (!obj) {
            return;
        }
        uint32_t index = 0;
        if (!TryIndexFromPointer(obj, index)) {
            Logger::Error("[Memory] PoolAllocator::Free: invalid pointer ", static_cast<void*>(obj));
            return;
        }
        if (!m_InUse[index]) {
            Logger::Error("[Memory] PoolAllocator::Free: double-free or stale pointer slot=", index);
            return;
        }
        obj->~T();
        m_InUse[index] = 0;
        --m_LiveCount;
        m_Free.push_back(index);
    }

private:
    static size_t AlignUp(size_t value, size_t alignment) {
        const size_t mask = alignment - 1;
        return (value + mask) & ~mask;
    }

    void* SlotPtr(uint32_t index) { return m_Slab.get() + static_cast<size_t>(index) * m_SlotStride; }

    const void* SlotPtr(uint32_t index) const { return m_Slab.get() + static_cast<size_t>(index) * m_SlotStride; }

    bool TryIndexFromPointer(const T* p, uint32_t& outIndex) const {
        if (!m_Slab || !p) {
            return false;
        }
        const auto* base = reinterpret_cast<const std::byte*>(m_Slab.get());
        const auto* pp = reinterpret_cast<const std::byte*>(p);
        if (pp < base) {
            return false;
        }
        const size_t off = static_cast<size_t>(pp - base);
        if (m_SlotStride == 0 || (off % m_SlotStride) != 0) {
            return false;
        }
        const uint32_t idx = static_cast<uint32_t>(off / m_SlotStride);
        if (idx >= m_Capacity) {
            return false;
        }
        if (pp != reinterpret_cast<const std::byte*>(SlotPtr(idx))) {
            return false;
        }
        outIndex = idx;
        return true;
    }

    std::unique_ptr<std::byte[]> m_Slab;
    std::vector<uint32_t> m_Free;
    std::vector<uint8_t> m_InUse;
    uint32_t m_Capacity = 0;
    uint32_t m_LiveCount = 0;
    size_t m_SlotStride = 0;
};
