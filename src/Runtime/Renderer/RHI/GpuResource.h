#pragma once

#include <cstdint>

enum class GpuResourceAccountingClass : uint8_t { None, Buffer, Texture, Descriptor };

// Base class for all GPU-side opaque resources.
struct GpuResource {
    GpuResource() = default;
    GpuResource(const GpuResource&) = delete;
    GpuResource& operator=(const GpuResource&) = delete;
    virtual ~GpuResource();
    void CommitAccounting(GpuResourceAccountingClass resourceClass, uint64_t bytes = 0, uint32_t descriptors = 0);

private:
    GpuResourceAccountingClass m_AccountingClass = GpuResourceAccountingClass::None;
    uint64_t m_AccountingBytes = 0;
    uint32_t m_AccountingDescriptors = 0;
};
