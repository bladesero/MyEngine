#pragma once

#include "AllocTracker.h"
#include "IAllocator.h"

class GeneralHeapAllocator final : public IAllocator {
public:
    GeneralHeapAllocator();
    ~GeneralHeapAllocator() override;

    void* Allocate(size_t size,
                   size_t alignment,
                   AllocTag tag,
                   const char* file,
                   int line) override;

    void Free(void* ptr, const char* file, int line) override;

    const char* Name() const override { return "GeneralHeap"; }

    AllocTracker& Tracker() { return m_Tracker; }
    const AllocTracker& Tracker() const { return m_Tracker; }

private:
    AllocTracker m_Tracker;
};
