#pragma once

#include "Assets/ShaderAsset.h"
#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/ShaderCacheService.h"

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ShaderHandle {
    std::shared_ptr<GpuShader> shader;
    uint64_t version = 0;
};

enum class ShaderPrewarmStatus { Ready, Pending, Failed };

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
    bool PrewarmCacheArtifacts(const std::vector<std::string>& shaderAssetPaths);
    ShaderPrewarmStatus PrewarmCacheArtifactsAsync(const std::vector<std::string>& shaderAssetPaths);

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
    struct PendingPrewarm {
        std::vector<std::string> paths;
        ShaderBackend backend = ShaderBackend::D3D11;
        uint64_t generation = 0;
        std::chrono::steady_clock::time_point start;
        std::shared_ptr<ShaderCacheBatchCancellation> cancellation;
        std::future<std::vector<ShaderCacheResult>> results;
    };
    std::shared_ptr<GpuShader> CompileRecord(const ShaderRecord& rec);
    std::string MakeKey(const ShaderAsset& asset, ShaderPass pass, const VertexElement* layout, uint32_t layoutCount,
                        bool compute) const;
    std::shared_ptr<ShaderHandle> GetOrCreateInternal(const std::string&, ShaderPass, const VertexElement*, uint32_t,
                                                      bool);
    std::vector<ShaderCacheRequest> BuildPrewarmRequests(const std::vector<std::string>& paths,
                                                         std::vector<std::string>& pendingPaths, ShaderBackend& backend,
                                                         bool* hasCachedFailures = nullptr) const;
    bool StorePrewarmResults(const std::vector<std::string>& pendingPaths, ShaderBackend backend,
                             std::vector<ShaderCacheResult> results, std::chrono::steady_clock::time_point start);
    void DrainPendingPrewarm(bool storeResults);

    IRHIDevice* m_Device = nullptr;
    ShaderCacheMode m_CacheMode = ShaderCacheMode::EditorOnDemandCompile;
    std::vector<ShaderRecord> m_Records;
    std::unordered_map<std::string, size_t> m_KeyToIndex;
    std::unordered_map<std::string, std::shared_ptr<ShaderAsset>> m_CompiledOverrides;
    std::unordered_map<std::string, ShaderCacheResult> m_PrewarmedArtifacts;
    std::unique_ptr<PendingPrewarm> m_PendingPrewarm;
    uint64_t m_PrewarmGeneration = 0;
};
