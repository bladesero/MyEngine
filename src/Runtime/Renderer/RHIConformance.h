#pragma once

#include "API/RuntimeApi.h"

#include <string>
#include <vector>

class IRHIContext;

struct MYENGINE_RUNTIME_API RHIConformanceReport {
    bool passed = false;
    std::vector<std::string> completedStages;
    std::string failure;

    std::string Summary() const;
};

// Backend-neutral runtime contract. It deliberately uses only public RHI
// interfaces so the same executable path exercises D3D11 and D3D12.
MYENGINE_RUNTIME_API RHIConformanceReport RunRHIConformance(IRHIContext& context);
