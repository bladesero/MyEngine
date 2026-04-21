#include "MemoryService.h"

#include "Core/Logger.h"

#include <algorithm>

namespace {

constexpr size_t kDefaultFrameArenaBytes = 4u * 1024u * 1024u;

} // namespace

MemoryService& MemoryService::Get() {
    static MemoryService instance;
    return instance;
}

void MemoryService::Init() {
    if (m_Initialized) {
        return;
    }

    if (!m_FrameArena.Init(kDefaultFrameArenaBytes)) {
        Logger::Error("[Memory] MemoryService::Init: frame arena failed; continuing without linear arena");
    }

    m_GeneralHeap.Tracker().Reset();
    m_SceneLiveActors.store(0);

    m_Initialized = true;
    Logger::Info("[Memory] MemoryService initialized (linear arena capacity ",
                 m_FrameArena.Capacity(), " bytes)");
}

void MemoryService::Shutdown() {
    if (!m_Initialized) {
        return;
    }

#if defined(MYENGINE_MEM_STATS) || defined(MYENGINE_MEM_TRACKING)
    m_GeneralHeap.Tracker().DumpAggregateStats("MemoryService::Shutdown");
    m_GeneralHeap.Tracker().DumpLeaks();
#endif

    m_FrameArena.Shutdown();
    m_Initialized = false;
    Logger::Info("[Memory] MemoryService shutdown");
}

void* MemoryService::Allocate(AllocTag tag,
                             size_t size,
                             size_t alignment,
                             const char* file,
                             int line) {
    if (!m_Initialized) {
        Logger::Error("[Memory] MemoryService::Allocate called before Init()");
        return nullptr;
    }
    return m_GeneralHeap.Allocate(size, alignment, tag, file, line);
}

void MemoryService::Free(void* ptr, const char* file, int line) {
    if (!ptr) {
        return;
    }
    if (!m_Initialized) {
        Logger::Error("[Memory] MemoryService::Free called after Shutdown or before Init()");
        return;
    }
    m_GeneralHeap.Free(ptr, file, line);
}

void MemoryService::FrameBegin() {
    if (!m_Initialized) {
        return;
    }
    if (m_FrameArena.IsInitialized()) {
        m_FrameArena.Reset();
    }
}

uint64_t MemoryService::GetSceneLiveActorCount() const {
    const int64_t n = m_SceneLiveActors.load();
    return n > 0 ? static_cast<uint64_t>(n) : 0u;
}

void MemoryService::SetSceneActorBudget(uint64_t maxActors) {
    m_SceneActorBudget.store(maxActors);
}

void MemoryService::SceneNotifyActorCreated() {
    if (!m_Initialized) {
        return;
    }
    m_SceneLiveActors.fetch_add(1);
    SceneCheckActorBudget();
}

void MemoryService::SceneNotifyActorDestroyed() {
    if (!m_Initialized) {
        return;
    }
    const int64_t prev = m_SceneLiveActors.fetch_sub(1);
    if (prev <= 0) {
        m_SceneLiveActors.fetch_add(1);
        Logger::Warn("[Memory] SceneNotifyActorDestroyed while live actor count was 0 (desync?)");
    }
}

void MemoryService::SceneNotifyActorsDestroyed(uint64_t count) {
    if (!m_Initialized || count == 0) {
        return;
    }
    const int64_t cur = m_SceneLiveActors.load();
    const int64_t dec = static_cast<int64_t>(std::min<uint64_t>(count, static_cast<uint64_t>(std::max<int64_t>(0, cur))));
    if (dec > 0) {
        m_SceneLiveActors.fetch_sub(dec);
    }
    if (static_cast<uint64_t>(dec) != count) {
        Logger::Warn("[Memory] SceneNotifyActorsDestroyed: requested ", count, " but adjusted ", dec,
                     " (count desync?)");
    }
}

void MemoryService::SceneCheckActorBudget() {
    const uint64_t cap = m_SceneActorBudget.load();
    if (cap == 0) {
        return;
    }
    const uint64_t live = GetSceneLiveActorCount();
    if (live > cap) {
        Logger::Warn("[Memory] Scene actor budget soft-exceeded: live=", live, " budget=", cap);
    }
}
