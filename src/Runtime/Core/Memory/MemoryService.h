#pragma once

#include "API/RuntimeApi.h"

#include "AllocTag.h"
#include "GeneralHeapAllocator.h"
#include "LinearAllocator.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

// Central memory entry: general heap (ME_*), per-frame linear arena, (optional) pools live in subsystems.
class MYENGINE_RUNTIME_API MemoryService {
public:
    static MemoryService& Get();

    MemoryService(const MemoryService&) = delete;
    MemoryService& operator=(const MemoryService&) = delete;

    void Init();
    void Shutdown();

    bool IsInitialized() const { return m_Initialized; }

    // General heap — tracked path (headers + optional AllocTracker).
    void* Allocate(AllocTag tag, size_t size, size_t alignment, const char* file, int line);
    void Free(void* ptr, const char* file, int line);

    GeneralHeapAllocator& GeneralHeap() { return m_GeneralHeap; }
    const GeneralHeapAllocator& GeneralHeap() const { return m_GeneralHeap; }

    LinearAllocator& FrameArena() { return m_FrameArena; }
    const LinearAllocator& FrameArena() const { return m_FrameArena; }

    // Call once per frame before layer updates / rendering scratch.
    void FrameBegin();

    // -----------------------------------------------------------------------
    // Scene path — actor count + optional soft budget (warn only).
    // -----------------------------------------------------------------------
    void SetSceneActorBudget(uint64_t maxActors);
    uint64_t GetSceneActorBudget() const { return m_SceneActorBudget.load(); }
    uint64_t GetSceneLiveActorCount() const;

    void SceneNotifyActorCreated();
    void SceneNotifyActorDestroyed();
    void SceneNotifyActorsDestroyed(uint64_t count);

private:
    MemoryService() = default;

    bool m_Initialized = false;
    GeneralHeapAllocator m_GeneralHeap;
    LinearAllocator m_FrameArena;

    std::atomic<int64_t> m_SceneLiveActors{0};
    std::atomic<uint64_t> m_SceneActorBudget{0}; // 0 = unlimited

    void SceneCheckActorBudget();
};
