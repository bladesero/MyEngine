#include "GeneralHeapAllocator.h"

#include "PlatformAlignedAlloc.h"
#include "Core/Logger.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace {

constexpr uint64_t kMemMagic = 0x4D454D524B44524FULL; // "MEMRKDR"

struct MemHeader {
    uint64_t   magic = 0;
    size_t     userSize = 0;
    size_t     userAlign = 0;
    AllocTag   tag = AllocTag::Unknown;
#if defined(MYENGINE_MEM_TRACKING)
    const char* file = nullptr;
    int         line = 0;
#endif
};

#if defined(MYENGINE_MEM_GUARD)
constexpr uint32_t kGuardValue = 0xDEADBEEFu;
constexpr unsigned char kAllocFill = 0xCD;
#endif

MemHeader* HeaderFromUserPointer(void* user) {
    if (!user) {
        return nullptr;
    }
    auto** slot = reinterpret_cast<MemHeader**>(static_cast<char*>(user) - sizeof(MemHeader*));
    MemHeader* h = *slot;
    if (!h || h->magic != kMemMagic) {
        return nullptr;
    }
    return h;
}

} // namespace

GeneralHeapAllocator::GeneralHeapAllocator() = default;

GeneralHeapAllocator::~GeneralHeapAllocator() = default;

void* GeneralHeapAllocator::Allocate(size_t size,
                                     size_t alignment,
                                     AllocTag tag,
                                     const char* file,
                                     int line) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    const size_t baseAlign = std::max(alignment, alignof(MemHeader));
    // Offset from block start to user pointer (room for header + back-pointer slot before user).
    const size_t prefix =
        AlignUp(sizeof(MemHeader) + sizeof(MemHeader*), alignment);

#if defined(MYENGINE_MEM_GUARD)
    const size_t tail = sizeof(uint32_t);
#else
    const size_t tail = 0;
#endif

    const size_t total = prefix + size + tail;
    const size_t blockAlign = static_cast<size_t>(baseAlign);
    const size_t roundedTotal = AlignUp(total, blockAlign);

    void* block = PlatformAlignedAlloc(roundedTotal, blockAlign);
    if (!block) {
        Logger::Error("[Memory] GeneralHeapAllocator: out of memory (size=", size,
                      ", align=", alignment, ")");
        return nullptr;
    }

    std::memset(block, 0, roundedTotal);

    auto* h = reinterpret_cast<MemHeader*>(block);
    h->magic = kMemMagic;
    h->userSize = size;
    h->userAlign = alignment;
    h->tag = tag;
#if defined(MYENGINE_MEM_TRACKING)
    h->file = file;
    h->line = line;
#endif

    void* user = static_cast<char*>(block) + prefix;
    *reinterpret_cast<MemHeader**>(static_cast<char*>(user) - sizeof(MemHeader*)) = h;

#if defined(MYENGINE_MEM_GUARD)
    std::memset(user, kAllocFill, size);
    std::uint32_t* guard = reinterpret_cast<std::uint32_t*>(static_cast<char*>(user) + size);
    *guard = kGuardValue;
#endif

    AllocRecord rec;
    rec.size = size;
    rec.alignment = alignment;
    rec.tag = tag;
#if defined(MYENGINE_MEM_TRACKING)
    rec.file = file;
    rec.line = line;
#else
    (void)file;
    (void)line;
#endif

#if defined(MYENGINE_MEM_STATS) || defined(MYENGINE_MEM_TRACKING)
    m_Tracker.OnAllocate(user, rec);
#else
    (void)rec;
#endif

    return user;
}

void GeneralHeapAllocator::Free(void* ptr, const char* file, int line) {
    (void)file;
    (void)line;

    if (!ptr) {
        return;
    }

    MemHeader* h = HeaderFromUserPointer(ptr);
    if (!h) {
        Logger::Error("[Memory] GeneralHeapAllocator::Free: invalid user pointer (bad magic / use-after-free?) ",
                      ptr);
        return;
    }

#if defined(MYENGINE_MEM_GUARD)
    const std::uint32_t* guard =
        reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(ptr) + h->userSize);
    if (*guard != kGuardValue) {
        Logger::Error("[Memory] Heap corruption detected (tail canary mismatch) ptr=", ptr);
    }
#endif

#if defined(MYENGINE_MEM_STATS) || defined(MYENGINE_MEM_TRACKING)
    m_Tracker.OnFree(ptr, h->tag, h->userSize);
#endif

    void* block = h; // header at block start
    h->magic = 0; // poison

    // Clear back-pointer so a stale user address is less likely to look "valid" after free.
    auto** slot = reinterpret_cast<MemHeader**>(static_cast<char*>(ptr) - sizeof(MemHeader*));
    *slot = nullptr;

    PlatformAlignedFree(block);
}
