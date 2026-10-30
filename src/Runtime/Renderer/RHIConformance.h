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

// Backend-neutral runtime contract using public RHI interfaces. Vulkan probe shaders are compiled to SPIR-V with
// Slang, while D3D and Metal preserve their native source paths. A Vulkan probe requires the Slang compiler.
MYENGINE_RUNTIME_API RHIConformanceReport RunRHIConformance(IRHIContext& context);
