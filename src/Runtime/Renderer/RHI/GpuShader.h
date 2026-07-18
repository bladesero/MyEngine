#pragma once

#include "Renderer/RHI/GpuResource.h"
#include "Renderer/RHI/VertexLayout.h"

#include <cstdint>
#include <string>
#include <vector>

enum class ShaderBindingType : uint8_t {
    ConstantBuffer,
    Texture,
    Sampler,
    StructuredBuffer,
    StorageBuffer,
    StorageTexture
};
enum ShaderStageMask : uint8_t { ShaderStageVertex = 1, ShaderStagePixel = 2, ShaderStageCompute = 4 };

struct ShaderBindingDesc {
    std::string name;
    ShaderBindingType type = ShaderBindingType::Texture;
    uint32_t bindPoint = 0;
    uint32_t bindCount = 1;
    uint32_t byteSize = 0;
    uint8_t stages = 0;
    uint32_t bindSpace = 0;
};

struct ShaderInputDesc {
    std::string semantic;
    uint32_t semanticIndex = 0;
};

struct ShaderReflection {
    std::vector<ShaderBindingDesc> bindings;
    std::vector<ShaderInputDesc> inputs;
    const ShaderBindingDesc* Find(const std::string& name) const {
        for (const auto& binding : bindings)
            if (binding.name == name)
                return &binding;
        return nullptr;
    }
};

struct GpuShader : GpuResource {
    ShaderReflection reflection;
    std::vector<uint8_t> vertexBytecode;
    std::vector<uint8_t> pixelBytecode;
    std::vector<uint8_t> computeBytecode;
    std::vector<VertexElement> vertexLayout;
    uint32_t threadGroupSize[3] = {1, 1, 1};
    uint32_t abiVersion = 0;
    virtual ~GpuShader() = default;
};
