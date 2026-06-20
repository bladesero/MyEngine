#pragma once

#include "Renderer/RHI/GpuResource.h"
#include "Renderer/RHI/GpuShader.h"
#include "Renderer/RHI/RHITypes.h"

#include <memory>
#include <vector>

struct RHIStencilFaceState {
    RHIStencilOp failOp = RHIStencilOp::Keep;
    RHIStencilOp depthFailOp = RHIStencilOp::Keep;
    RHIStencilOp passOp = RHIStencilOp::Keep;
    RHICompareOp compareOp = RHICompareOp::Always;
};

struct RHIDepthStencilState {
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    RHICompareOp depthCompareOp = RHICompareOp::Less;
    bool stencilEnable = false;
    uint8_t stencilReadMask = 0xff;
    uint8_t stencilWriteMask = 0xff;
    uint8_t stencilReference = 0;
    RHIStencilFaceState frontFace;
    RHIStencilFaceState backFace;
};

struct RHIBlendAttachmentState {
    bool blendEnable = false;
    RHIBlendFactor srcColorFactor = RHIBlendFactor::SrcAlpha;
    RHIBlendFactor dstColorFactor = RHIBlendFactor::OneMinusSrcAlpha;
    RHIBlendOp colorOp = RHIBlendOp::Add;
    RHIBlendFactor srcAlphaFactor = RHIBlendFactor::One;
    RHIBlendFactor dstAlphaFactor = RHIBlendFactor::OneMinusSrcAlpha;
    RHIBlendOp alphaOp = RHIBlendOp::Add;
    uint8_t colorWriteMask = RHIColorWriteAll;
};

struct RHIBlendState {
    bool alphaToCoverageEnable = false;
    bool independentBlendEnable = false;
    std::vector<RHIBlendAttachmentState> attachments{1};
    float blendConstants[4] = {0, 0, 0, 0};
    uint32_t sampleMask = 0xffffffffu;
};

struct RHIRasterizerState {
    RHIFillMode fillMode = RHIFillMode::Solid;
    RHICullMode cullMode = RHICullMode::Back;
    RHIFrontFace frontFace = RHIFrontFace::Clockwise;
    int32_t depthBias = 0;
    float depthBiasClamp = 0.0f;
    float slopeScaledDepthBias = 0.0f;
    bool depthClipEnable = true;
    bool scissorEnable = false;
    bool multisampleEnable = false;
    bool antialiasedLineEnable = false;
};

struct RHIMultisampleState {
    uint32_t sampleCount = 1;
    uint32_t sampleQuality = 0;
};

struct GraphicsPipelineDesc {
    std::shared_ptr<GpuShader> shader;
    std::vector<RHIFormat> colorFormats;
    RHIFormat depthFormat = RHIFormat::Unknown;
    RHIPrimitiveTopology topology = RHIPrimitiveTopology::TriangleList;
    RHIDepthStencilState depthStencil;
    RHIBlendState blend;
    RHIRasterizerState rasterizer;
    RHIMultisampleState multisample;
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
