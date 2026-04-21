#pragma once

#include "AllocTag.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

struct AllocRecord {
    size_t       size      = 0;
    size_t       alignment = 0;
    AllocTag     tag       = AllocTag::Unknown;
    const char*  file      = nullptr;
    int          line      = 0;
};

// Snapshot of ME_* heap counters (atomics read without holding m_Mutex).
struct EngineAllocStats {
    uint64_t liveAllocs = 0;
    uint64_t totalBytes = 0;
    uint64_t lifetimeAllocCalls = 0;
    uint64_t lifetimeFreeCalls = 0;
    std::array<uint64_t, static_cast<size_t>(AllocTag::Count_)> bytesByTag{};
    std::array<uint64_t, static_cast<size_t>(AllocTag::Count_)> liveCountByTag{};
    std::array<uint64_t, static_cast<size_t>(AllocTag::Count_)> allocCallsByTag{};
    std::array<uint64_t, static_cast<size_t>(AllocTag::Count_)> freeCallsByTag{};
};

// Optional detailed tracking + guard canaries (compile-time gated via xmake).
class AllocTracker {
public:
    void Reset();

    void OnAllocate(void* userPtr, const AllocRecord& rec);
    // When MYENGINE_MEM_TRACKING is enabled, tag/size for byte totals are taken from the live table.
    // When disabled, tag/size must reflect the allocation header.
    void OnFree(void* userPtr, AllocTag tag, size_t size);

    void DumpLeaks() const;
    // Same as DumpLeaks but sorts live blocks by size (largest first) for noisy scenes.
    void DumpLeaksTopN(size_t maxEntries) const;

    void DumpAggregateStats(const char* title = nullptr) const;

    EngineAllocStats CaptureStats() const;

    uint64_t LiveCount() const { return m_LiveAllocs.load(); }
    uint64_t BytesForTag(AllocTag tag) const;
    uint64_t TotalBytes() const { return m_TotalBytes.load(); }
    uint64_t LiveCountForTag(AllocTag tag) const;
    uint64_t LifetimeAllocCalls() const { return m_LifetimeAllocCalls.load(); }
    uint64_t LifetimeFreeCalls() const { return m_LifetimeFreeCalls.load(); }

private:
    mutable std::mutex m_Mutex;

#if defined(MYENGINE_MEM_TRACKING)
    std::unordered_map<void*, AllocRecord> m_Live;
#endif

    std::atomic<uint64_t> m_LiveAllocs{0};
    std::atomic<uint64_t> m_TotalBytes{0};
    std::atomic<uint64_t> m_LifetimeAllocCalls{0};
    std::atomic<uint64_t> m_LifetimeFreeCalls{0};
    std::array<std::atomic<uint64_t>, static_cast<size_t>(AllocTag::Count_)> m_BytesByTag{};
    std::array<std::atomic<uint64_t>, static_cast<size_t>(AllocTag::Count_)> m_LiveCountByTag{};
    std::array<std::atomic<uint64_t>, static_cast<size_t>(AllocTag::Count_)> m_AllocCallsByTag{};
    std::array<std::atomic<uint64_t>, static_cast<size_t>(AllocTag::Count_)> m_FreeCallsByTag{};
};
