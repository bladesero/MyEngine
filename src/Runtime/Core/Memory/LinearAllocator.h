#pragma once

#include <cstddef>
#include <cstdint>

// Bump-pointer arena: Reset() per frame (or phase). Individual frees are not supported.
class LinearAllocator {
public:
    LinearAllocator() = default;
    ~LinearAllocator();

    LinearAllocator(const LinearAllocator&)            = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    // Allocates backing storage (single contiguous block). Safe to call again after Shutdown().
    bool Init(size_t capacityBytes);
    void Shutdown();

    void Reset();

    // Returns nullptr if the arena cannot satisfy alignment + size.
    void* Allocate(size_t size, size_t alignment);

    size_t Capacity() const { return m_Capacity; }
    size_t Offset() const { return m_Offset; }
    size_t HighWatermark() const { return m_HighWatermark; }

    bool IsInitialized() const { return m_Buffer != nullptr; }

private:
    unsigned char* m_Buffer = nullptr;
    size_t         m_Capacity = 0;
    size_t         m_Offset = 0;
    size_t         m_HighWatermark = 0;
};
