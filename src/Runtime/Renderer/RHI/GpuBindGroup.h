#pragma once

#include "Renderer/RHI/GpuAccelerationStructure.h"
#include "Renderer/RHI/GpuBuffer.h"
#include "Renderer/RHI/GpuBufferView.h"
#include "Renderer/RHI/GpuSampler.h"
#include "Renderer/RHI/GpuShader.h"
#include "Renderer/RHI/GpuTextureView.h"

#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class GpuBindGroup : public GpuResource {
public:
    explicit GpuBindGroup(std::shared_ptr<GpuShader> shader) : m_Shader(std::move(shader)) {
        CommitAccounting(GpuResourceAccountingClass::Descriptor, 0, 1);
    }
    virtual ~GpuBindGroup() = default;

    bool SetConstants(const std::string& name, const void* data, uint32_t byteSize) {
        const auto* binding = Find(name, ShaderBindingType::ConstantBuffer);
        if ((!binding && HasReflection()) || !data)
            return false;
        if (binding && binding->byteSize != 0 && binding->byteSize != byteSize)
            return false;
        auto& bytes = m_Constants[name];
        bytes.resize(byteSize);
        std::memcpy(bytes.data(), data, byteSize);
        return true;
    }
    bool SetTexture(const std::string& name, std::shared_ptr<GpuTextureView> view) {
        if ((!Find(name, ShaderBindingType::Texture) && HasReflection()) || !view)
            return false;
        m_Textures[name] = std::move(view);
        return true;
    }
    bool SetSampler(const std::string& name, std::shared_ptr<GpuSampler> sampler) {
        if ((!Find(name, ShaderBindingType::Sampler) && HasReflection()) || !sampler)
            return false;
        m_Samplers[name] = std::move(sampler);
        return true;
    }
    bool SetStorageBuffer(const std::string& name, std::shared_ptr<GpuBufferView> view) {
        if ((!Find(name, ShaderBindingType::StorageBuffer) && HasReflection()) || !view)
            return false;
        m_StorageBuffers[name] = std::move(view);
        return true;
    }
    bool SetBuffer(const std::string& name, std::shared_ptr<GpuBufferView> view) {
        if ((!Find(name, ShaderBindingType::StructuredBuffer) && HasReflection()) || !view)
            return false;
        m_Buffers[name] = std::move(view);
        return true;
    }
    bool SetStorageTexture(const std::string& name, std::shared_ptr<GpuTextureView> view) {
        if ((!Find(name, ShaderBindingType::StorageTexture) && HasReflection()) || !view)
            return false;
        m_StorageTextures[name] = std::move(view);
        return true;
    }
    bool SetAccelerationStructure(const std::string& name, std::shared_ptr<GpuAccelerationStructure> value) {
        if ((!Find(name, ShaderBindingType::AccelerationStructure) && HasReflection()) || !value)
            return false;
        m_AccelerationStructures[name] = std::move(value);
        return true;
    }
    bool Validate(std::string* error = nullptr) const {
        if (!m_Shader) {
            if (error)
                *error = "bind group has no shader";
            return false;
        }
        for (const auto& binding : m_Shader->reflection.bindings) {
            // Unbounded descriptor arrays are populated by the device-level bindless table, not by a
            // per-draw bind group. Requiring a single view here would both reject valid bindless passes and
            // misrepresent the lifetime of the global descriptor heap.
            if (binding.bindCount == UINT32_MAX && binding.type == ShaderBindingType::Texture)
                continue;
            bool found = false;
            switch (binding.type) {
            case ShaderBindingType::ConstantBuffer:
                found = m_Constants.count(binding.name) != 0;
                break;
            case ShaderBindingType::Texture:
                found = m_Textures.count(binding.name) != 0;
                break;
            case ShaderBindingType::Sampler:
                found = m_Samplers.count(binding.name) != 0;
                break;
            case ShaderBindingType::StorageBuffer:
                found = m_StorageBuffers.count(binding.name) != 0;
                break;
            case ShaderBindingType::StructuredBuffer:
                found = m_Buffers.count(binding.name) != 0;
                break;
            case ShaderBindingType::StorageTexture:
                found = m_StorageTextures.count(binding.name) != 0;
                break;
            case ShaderBindingType::AccelerationStructure:
                found = m_AccelerationStructures.count(binding.name) != 0;
                break;
            }
            if (!found) {
                if (error)
                    *error = "missing shader binding '" + binding.name + "'";
                return false;
            }
        }
        return true;
    }
    const std::shared_ptr<GpuShader>& GetShader() const { return m_Shader; }
    const auto& GetConstants() const { return m_Constants; }
    const auto& GetTextures() const { return m_Textures; }
    const auto& GetSamplers() const { return m_Samplers; }
    const auto& GetStorageBuffers() const { return m_StorageBuffers; }
    const auto& GetBuffers() const { return m_Buffers; }
    const auto& GetStorageTextures() const { return m_StorageTextures; }
    const auto& GetAccelerationStructures() const { return m_AccelerationStructures; }

private:
    bool HasReflection() const { return m_Shader && !m_Shader->reflection.bindings.empty(); }
    const ShaderBindingDesc* Find(const std::string& name, ShaderBindingType type) const {
        if (!m_Shader)
            return nullptr;
        const auto* binding = m_Shader->reflection.Find(name);
        return binding && binding->type == type ? binding : nullptr;
    }
    std::shared_ptr<GpuShader> m_Shader;
    std::unordered_map<std::string, std::vector<uint8_t>> m_Constants;
    std::unordered_map<std::string, std::shared_ptr<GpuTextureView>> m_Textures;
    std::unordered_map<std::string, std::shared_ptr<GpuSampler>> m_Samplers;
    std::unordered_map<std::string, std::shared_ptr<GpuBufferView>> m_StorageBuffers;
    std::unordered_map<std::string, std::shared_ptr<GpuBufferView>> m_Buffers;
    std::unordered_map<std::string, std::shared_ptr<GpuTextureView>> m_StorageTextures;
    std::unordered_map<std::string, std::shared_ptr<GpuAccelerationStructure>> m_AccelerationStructures;
};
