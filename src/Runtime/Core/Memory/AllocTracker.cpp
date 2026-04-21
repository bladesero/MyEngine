#include "AllocTracker.h"

#include "Core/Logger.h"

#include <algorithm>
#include <vector>

namespace {

constexpr size_t TagIndex(AllocTag tag) {
    const auto idx = static_cast<size_t>(tag);
    return idx < static_cast<size_t>(AllocTag::Count_) ? idx : 0;
}

} // namespace

void AllocTracker::Reset() {
#if defined(MYENGINE_MEM_TRACKING)
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Live.clear();
#endif
    m_LiveAllocs.store(0);
    m_TotalBytes.store(0);
    m_LifetimeAllocCalls.store(0);
    m_LifetimeFreeCalls.store(0);
    for (auto& b : m_BytesByTag) {
        b.store(0);
    }
    for (auto& c : m_LiveCountByTag) {
        c.store(0);
    }
    for (auto& a : m_AllocCallsByTag) {
        a.store(0);
    }
    for (auto& f : m_FreeCallsByTag) {
        f.store(0);
    }
}

void AllocTracker::OnAllocate(void* userPtr, const AllocRecord& rec) {
    if (!userPtr) {
        return;
    }

#if defined(MYENGINE_MEM_TRACKING)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Live.find(userPtr) != m_Live.end()) {
            Logger::Error("[Memory] Tracked live map already contains pointer ", userPtr,
                          " (allocator or tracker bug)");
            return;
        }
        m_Live[userPtr] = rec;
    }
#endif

    const size_t idx = TagIndex(rec.tag);
    if (rec.size > 0) {
        m_BytesByTag[idx].fetch_add(static_cast<uint64_t>(rec.size));
        m_TotalBytes.fetch_add(static_cast<uint64_t>(rec.size));
    }
    m_LiveAllocs.fetch_add(1);
    m_LiveCountByTag[idx].fetch_add(1);
    m_AllocCallsByTag[idx].fetch_add(1);
    m_LifetimeAllocCalls.fetch_add(1);
}

void AllocTracker::OnFree(void* userPtr, AllocTag tag, size_t size) {
    if (!userPtr) {
        return;
    }

    AllocTag effTag = tag;
    size_t effSize = size;

#if defined(MYENGINE_MEM_TRACKING)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Live.find(userPtr);
        if (it == m_Live.end()) {
            Logger::Error("[Memory] Double-free, unknown pointer, or corrupted header: ", userPtr);
            return;
        }
        effTag = it->second.tag;
        effSize = it->second.size;
        m_Live.erase(it);
    }
#endif

    const size_t idx = TagIndex(effTag);
    if (effSize > 0) {
        m_BytesByTag[idx].fetch_sub(static_cast<uint64_t>(effSize));
        m_TotalBytes.fetch_sub(static_cast<uint64_t>(effSize));
    }
    m_LiveAllocs.fetch_sub(1);
    m_LiveCountByTag[idx].fetch_sub(1);
    m_FreeCallsByTag[idx].fetch_add(1);
    m_LifetimeFreeCalls.fetch_add(1);
}

void AllocTracker::DumpLeaks() const {
    DumpLeaksTopN(32);
}

void AllocTracker::DumpLeaksTopN(size_t maxEntries) const {
#if defined(MYENGINE_MEM_TRACKING)
    std::vector<std::pair<void*, AllocRecord>> rows;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        rows.reserve(m_Live.size());
        for (const auto& kv : m_Live) {
            rows.push_back(kv);
        }
    }

    if (rows.empty()) {
        Logger::Info("[Memory] Leak check: no live ME_* heap allocations tracked.");
        return;
    }

    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.second.size > b.second.size; });

    const size_t n = std::min(maxEntries, rows.size());
    Logger::Warn("[Memory] Leak summary: ", rows.size(), " live allocation(s), showing top ", n, " by size");

    for (size_t i = 0; i < n; ++i) {
        const void* p = rows[i].first;
        const AllocRecord& r = rows[i].second;
        if (r.file) {
            Logger::Warn("  #", i, " ptr=", p, " size=", r.size, " align=", r.alignment,
                         " tag=", AllocTagToString(r.tag), " at ", r.file, ":", r.line);
        } else {
            Logger::Warn("  #", i, " ptr=", p, " size=", r.size, " align=", r.alignment,
                         " tag=", AllocTagToString(r.tag));
        }
    }
    if (rows.size() > n) {
        Logger::Warn("  ... ", rows.size() - n, " more not shown ...");
    }
#else
    (void)maxEntries;
    Logger::Info("[Memory] Leak detail skipped (MYENGINE_MEM_TRACKING disabled). Live count=",
                 m_LiveAllocs.load());
#endif
}

EngineAllocStats AllocTracker::CaptureStats() const {
    EngineAllocStats s;
    s.liveAllocs = m_LiveAllocs.load();
    s.totalBytes = m_TotalBytes.load();
    s.lifetimeAllocCalls = m_LifetimeAllocCalls.load();
    s.lifetimeFreeCalls = m_LifetimeFreeCalls.load();
    for (size_t i = 0; i < s.bytesByTag.size(); ++i) {
        s.bytesByTag[i] = m_BytesByTag[i].load();
        s.liveCountByTag[i] = m_LiveCountByTag[i].load();
        s.allocCallsByTag[i] = m_AllocCallsByTag[i].load();
        s.freeCallsByTag[i] = m_FreeCallsByTag[i].load();
    }
    return s;
}

void AllocTracker::DumpAggregateStats(const char* title) const {
    const EngineAllocStats s = CaptureStats();
    if (title) {
        Logger::Info("[Memory] ", title);
    } else {
        Logger::Info("[Memory] ME_* heap aggregate stats");
    }
    Logger::Info("[Memory]   liveAllocs=", s.liveAllocs, " totalBytes=", s.totalBytes,
                 " lifetimeAllocs=", s.lifetimeAllocCalls, " lifetimeFrees=", s.lifetimeFreeCalls);

    for (size_t i = 0; i < s.bytesByTag.size(); ++i) {
        const auto tag = static_cast<AllocTag>(i);
        if (s.bytesByTag[i] == 0 && s.liveCountByTag[i] == 0 && s.allocCallsByTag[i] == 0 &&
            s.freeCallsByTag[i] == 0) {
            continue;
        }
        Logger::Info("[Memory]   tag=", AllocTagToString(tag),
                     " bytes=", s.bytesByTag[i],
                     " liveCount=", s.liveCountByTag[i],
                     " allocCalls=", s.allocCallsByTag[i],
                     " freeCalls=", s.freeCallsByTag[i]);
    }
}

uint64_t AllocTracker::BytesForTag(AllocTag tag) const {
    const size_t idx = TagIndex(tag);
    return m_BytesByTag[idx].load();
}

uint64_t AllocTracker::LiveCountForTag(AllocTag tag) const {
    const size_t idx = TagIndex(tag);
    return m_LiveCountByTag[idx].load();
}
