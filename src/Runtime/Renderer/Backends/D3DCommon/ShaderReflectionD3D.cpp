#include "Renderer/RHI/ShaderReflection.h"

#ifdef MYENGINE_PLATFORM_WINDOWS
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

static ShaderBindingType ToBindingType(D3D_SHADER_INPUT_TYPE type) {
    switch (type) {
    case D3D_SIT_CBUFFER:
        return ShaderBindingType::ConstantBuffer;
    case D3D_SIT_SAMPLER:
        return ShaderBindingType::Sampler;
    case D3D_SIT_UAV_RWTYPED:
        return ShaderBindingType::StorageTexture;
    case D3D_SIT_UAV_RWSTRUCTURED:
    case D3D_SIT_UAV_RWBYTEADDRESS:
        return ShaderBindingType::StorageBuffer;
    case D3D_SIT_STRUCTURED:
    case D3D_SIT_BYTEADDRESS:
        return ShaderBindingType::StructuredBuffer;
    default:
        return ShaderBindingType::Texture;
    }
}

bool ReflectDxbcStage(const void* bytecode, size_t byteSize, uint8_t stage, ShaderReflection& reflection,
                      std::string* error) {
    ComPtr<ID3D11ShaderReflection> shader;
    HRESULT hr = D3DReflect(bytecode, byteSize, __uuidof(ID3D11ShaderReflection),
                            reinterpret_cast<void**>(shader.GetAddressOf()));
    if (FAILED(hr)) {
        if (error)
            *error = "D3DReflect failed";
        return false;
    }
    D3D11_SHADER_DESC desc{};
    if (FAILED(shader->GetDesc(&desc))) {
        if (error)
            *error = "shader reflection description failed";
        return false;
    }
    for (uint32_t i = 0; i < desc.BoundResources; ++i) {
        D3D11_SHADER_INPUT_BIND_DESC native{};
        if (FAILED(shader->GetResourceBindingDesc(i, &native)))
            continue;
        auto existing = reflection.Find(native.Name ? native.Name : "");
        if (existing) {
            const_cast<ShaderBindingDesc*>(existing)->stages |= stage;
            continue;
        }
        ShaderBindingDesc binding;
        binding.name = native.Name ? native.Name : "";
        binding.type = ToBindingType(native.Type);
        binding.bindPoint = native.BindPoint;
        binding.bindSpace = 0;
        binding.bindCount = native.BindCount;
        binding.stages = stage;
        if (native.Type == D3D_SIT_CBUFFER) {
            auto* cb = shader->GetConstantBufferByName(native.Name);
            D3D11_SHADER_BUFFER_DESC cbDesc{};
            if (cb && SUCCEEDED(cb->GetDesc(&cbDesc)))
                binding.byteSize = cbDesc.Size;
        }
        reflection.bindings.push_back(std::move(binding));
    }
    if (stage == ShaderStageVertex) {
        for (uint32_t i = 0; i < desc.InputParameters; ++i) {
            D3D11_SIGNATURE_PARAMETER_DESC input{};
            if (SUCCEEDED(shader->GetInputParameterDesc(i, &input)))
                reflection.inputs.push_back({input.SemanticName ? input.SemanticName : "", input.SemanticIndex});
        }
    }
    return true;
}

bool ReflectDxbcProgram(const void* vs, size_t vsSize, const void* ps, size_t psSize, ShaderReflection& reflection,
                        std::string* error) {
    reflection = {};
    return ReflectDxbcStage(vs, vsSize, ShaderStageVertex, reflection, error) &&
           ReflectDxbcStage(ps, psSize, ShaderStagePixel, reflection, error);
}
#else
bool ReflectDxbcStage(const void*, size_t, uint8_t, ShaderReflection&, std::string* error) {
    if (error)
        *error = "DXBC reflection is only available on Windows";
    return false;
}
bool ReflectDxbcProgram(const void*, size_t, const void*, size_t, ShaderReflection&, std::string* error) {
    if (error)
        *error = "DXBC reflection is only available on Windows";
    return false;
}
#endif
