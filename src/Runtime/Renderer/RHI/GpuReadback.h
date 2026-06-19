#pragma once

#include <cstdint>
#include <vector>

class GpuReadbackTicket {
public:
    virtual ~GpuReadbackTicket() = default;
    virtual bool IsReady() const = 0;
    virtual bool Read(std::vector<uint8_t>& data) = 0;
    virtual uint32_t GetSize() const = 0;
};

