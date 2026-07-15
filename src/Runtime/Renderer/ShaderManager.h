#pragma once

#include "Assets/ShaderAsset.h"
#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/ShaderCacheService.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ShaderHandle {
    std::shared_ptr<GpuShader> shader;
    uint64_t version = 0;
};

class ShaderManager {
public:
    static ShaderManager& Get();
    void SetDevice(IRHIDevice* device) { m_Device = device; }
    void SetShaderCacheMode(ShaderCacheMode mode) { m_CacheMode = mode; }
    ShaderCacheMode GetShaderCacheMode() const { return m_CacheMode; }

    std::shared_ptr<ShaderHandle> GetOrCreate(const std::string& shaderAssetPath, const VertexElement* layout,
                                              uint32_t layoutCount);
    std::shared_ptr<ShaderHandle> GetOrCreatePass(const std::string& shaderAssetPath, ShaderPass pass,
                                                  const VertexElement* layout, uint32_t layoutCount);
    std::shared_ptr<ShaderHandle> GetOrCreateCompute(const std::string& shaderAssetPath);

    void Recompile(const std::string& shaderAssetPath = "");
    void RecompileAll() { Recompile(""); }
    ShaderBackend GetActiveShaderBackend() const;
    bool ApplyCompiledArtifact(const std::string& shaderAssetPath, const std::string& cookedArtifactPath,
                               std::string* error = nullptr);
    void Clear();

private:
    struct ShaderRecord {
        std::string key;
        std::string path;
        ShaderAssetHandle asset;
        std::vector<VertexElement> layout;
        bool compute = false;
        ShaderPass pass = ShaderPass::Default;
        std::shared_ptr<ShaderHandle> handle;
    };
    std::shared_ptr<GpuShader> CompileRecord(const ShaderRecord& rec);
    std::string MakeKey(const ShaderAsset& asset, ShaderPass pass, const VertexElement* layout, uint32_t layoutCount,
                        bool compute) const;
    std::shared_ptr<ShaderHandle> GetOrCreateInternal(const std::string&, ShaderPass, const VertexElement*, uint32_t,
                                                      bool);

    IRHIDevice* m_Device = nullptr;
    ShaderCacheMode m_CacheMode = ShaderCacheMode::EditorOnDemandCompile;
    std::vector<ShaderRecord> m_Records;
    std::unordered_map<std::string, size_t> m_KeyToIndex;
    std::unordered_map<std::string, std::shared_ptr<ShaderAsset>> m_CompiledOverrides;
};
