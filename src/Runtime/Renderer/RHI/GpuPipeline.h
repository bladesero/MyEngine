#pragma once

#include "Renderer/RHI/GpuResource.h"
#include "Renderer/RHI/GpuShader.h"
#include "Renderer/RHI/RHITypes.h"

#include <memory>
#include <vector>

struct GraphicsPipelineDesc {
    std::shared_ptr<GpuShader> shader;
    std::vector<RHIFormat> colorFormats;
    RHIFormat depthFormat = RHIFormat::Unknown;
    bool depthTest = true;
    bool depthWrite = true;
    bool alphaBlend = false;
    bool twoSided = false;
    bool wireframe = false;
};

struct ComputePipelineDesc { std::shared_ptr<GpuShader> shader; };

struct GpuGraphicsPipeline : GpuResource {
    GraphicsPipelineDesc desc;
    virtual ~GpuGraphicsPipeline() = default;
};
struct GpuComputePipeline : GpuResource {
    ComputePipelineDesc desc;
    virtual ~GpuComputePipeline() = default;
};
